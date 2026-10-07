/*
 * paloimport — rejeu d’un JSON paloexport (structure) vers un serveur Palo via l’API HTTP.
 * Par défaut : lecture du JSON, validation, plan d’action (dry-run). Avec --execute : import réel.
 */

#include "paloexport_client.hpp"

#include <fstream>
#include <getopt.h>

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

void print_usage(const char *argv0) {
	std::cerr
	    << "Usage: " << argv0
	    << " -f FICHIER.json -u USER -p PASSWORD --database NAME [options]\n"
	       "Options:\n"
	       "  -f, --file PATH      Fichier JSON (paloexport_version 6 ou 7)\n"
	       "  -h, --host HOST      Serveur Palo (défaut: 127.0.0.1)\n"
	       "  -P, --port PORT      Port HTTP (défaut: 7777)\n"
	       "  -u, --user USER      Identifiant\n"
	       "  -p, --password PASS  Mot de passe\n"
	       "  -D, --database NAME  Base cible (-D ; la base doit exister pour un JSON « données seules »)\n"
	       "  -v, --verbose        Journal sur stderr\n"
	       "      --timeout SEC    Timeout socket (défaut: 120)\n"
	       "      --https          Utiliser https://\n"
	       "      --dry-run        Plan + vérification cell_data sur le serveur si présent (défaut: oui)\n"
	       "      --execute        Appliquer l’import sur le serveur (HTTP)\n"
	       "      --help           Aide\n"
	       "\n"
	       "Fichier JSON : soit structure complète (dimensions, cube, …), soit uniquement « cell_data »\n"
	       "(noms de cubes → tableaux de cellules) ; dans ce cas connexion au serveur pour valider les chemins.\n";
}

struct Config {
	std::string json_path;
	std::string host = "127.0.0.1";
	std::string port = "7777";
	std::string user;
	std::string password;
	std::string database_name;
	bool use_https = false;
	bool verbose = false;
	int timeout_sec = 120;
	bool dry_run = true;
	bool execute = false;
};

std::string read_file_utf8(const std::string &path) {
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) throw std::runtime_error("impossible de lire le fichier: " + path);
	std::ostringstream ss;
	ss << ifs.rdbuf();
	return ss.str();
}

/** `paloexport_version` (actuel) ou `palodump_version` (fichiers générés par l’ancien outil). */
static int read_schema_version(const nlohmann::json &j) {
	if (j.contains("paloexport_version")) {
		if (!j["paloexport_version"].is_number_integer())
			throw std::runtime_error("champ 'paloexport_version' invalide");
		return j["paloexport_version"].get<int>();
	}
	if (j.contains("palodump_version")) {
		if (!j["palodump_version"].is_number_integer())
			throw std::runtime_error("champ 'palodump_version' invalide");
		return j["palodump_version"].get<int>();
	}
	throw std::runtime_error("champ 'paloexport_version' ou 'palodump_version' (legacy) manquant");
}

static bool json_has_cell_data(const nlohmann::json &j) {
	return j.contains("cell_data") && j["cell_data"].is_object() && !j["cell_data"].empty();
}

static bool json_has_structure(const nlohmann::json &j) {
	return j.contains("dimensions") && j["dimensions"].is_array() && j.contains("cube") && j["cube"].is_object() &&
	       j["cube"].contains("dimensions_in_order") && j["cube"]["dimensions_in_order"].is_array();
}

/** JSON sans dimensions/cube : seulement cell_data (et métadonnées minimales). */
static bool json_is_data_only(const nlohmann::json &j) {
	return json_has_cell_data(j) && !json_has_structure(j);
}

void validate_json(const nlohmann::json &j) {
	const int ver = read_schema_version(j);
	if (ver < 6 || ver > 7) {
		throw std::runtime_error("schéma version " + std::to_string(ver) + " non supporté (attendu 6 ou 7)");
	}
	if (!json_has_structure(j) && !json_has_cell_data(j)) {
		throw std::runtime_error(
		    "JSON invalide : fournir une structure (dimensions, cube, …) et/ou un objet « cell_data » non vide");
	}
	if (json_has_structure(j)) {
		if (!j.contains("database") || !j["database"].is_object()) throw std::runtime_error("'database' manquant");
		if (!j.contains("cube") || !j["cube"].is_object()) throw std::runtime_error("'cube' manquant");
		if (!j.contains("dimensions") || !j["dimensions"].is_array()) throw std::runtime_error("'dimensions' manquant");
		const auto &cube = j["cube"];
		if (!cube.contains("dimensions_in_order") || !cube["dimensions_in_order"].is_array()) {
			throw std::runtime_error("'cube.dimensions_in_order' manquant ou invalide");
		}
	}
}

static std::string json_scalar_string(const nlohmann::json &v) {
	if (v.is_string()) return v.get<std::string>();
	if (v.is_number_integer()) return std::to_string(v.get<int>());
	if (v.is_number_float()) return std::to_string(v.get<double>());
	if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
	throw std::runtime_error("valeur JSON non convertible en chaîne");
}

/** Enfants consolidés : liste de noms séparés par des virgules (API Palo). */
static std::string join_child_names_csv(const nlohmann::json &children_array) {
	if (!children_array.is_array()) throw std::runtime_error("'children' doit être un tableau");
	std::ostringstream o;
	for (size_t i = 0; i < children_array.size(); ++i) {
		if (i) o << ',';
		if (!children_array[i].is_string())
			throw std::runtime_error("nom d’enfant d’élément invalide (chaîne attendue)");
		o << children_array[i].get<std::string>();
	}
	return o.str();
}

struct CellDataVerifyReport {
	bool ran = false;
	std::vector<std::string> lines;
};

void print_plan(const nlohmann::json &j, std::ostream &out, const CellDataVerifyReport *cell_report) {
	out << "=== Plan d’import ===\n";
	out << "Version JSON : " << read_schema_version(j) << "\n";

	if (json_is_data_only(j)) {
		out << "Mode : données seules (pas de structure dans le fichier)\n\n";
		out << "Vérification sur le serveur (chemins vs dimensions des cubes cibles) :\n";
		if (cell_report && cell_report->ran) {
			for (const auto &ln : cell_report->lines) out << ln << "\n";
		} else {
			out << "   (non exécutée)\n";
		}
		out << "\nAvec --execute : écriture des cellules via /cell/replace (splash=1).\n";
		return;
	}

	const auto &cube = j["cube"];
	const auto &dims_in_order = cube["dimensions_in_order"];
	const size_t n_axes = dims_in_order.size();
	const auto &dims = j["dimensions"];
	if (dims.size() < n_axes) {
		throw std::runtime_error("moins d'entrées dans 'dimensions' que dans dimensions_in_order");
	}

	out << "Cube cible (structure) : «" << cube.value("name", "") << "»\n\n";

	out << "1) Dimensions normales (création / éléments d’axe)\n";
	for (size_t i = 0; i < n_axes; ++i) {
		const auto &d = dims[i];
		const std::string dname = d.value("name", "");
		const std::string dtype = d.value("type", "");
		out << "   - Dimension «" << dname << "» (type dimension Palo=" << dtype << ")\n";
		if (d.contains("attributes_dimension")) {
			out << "     (attributs : dim «" << d.value("attributes_dimension", "") << "», cube «"
			    << d.value("attributes_cube", "") << "»)\n";
		}
		const auto &els = d.contains("elements") && d["elements"].is_array() ? d["elements"] : nlohmann::json::array();
		out << "     → passe 1 : créer " << els.size() << " éléments (consolidés vides)\n";
		out << "     → passe 2 : rattacher consolidations (children / weights)\n";
	}

	out << "\n2) Dimensions d’attributs (type 2) — ne pas créer la dimension ; seulement éléments\n";
	for (size_t i = n_axes; i < dims.size(); ++i) {
		const auto &d = dims[i];
		const std::string dtype = d.value("type", "");
		if (dtype != "2") {
			out << "   ! Entrée supplémentaire «" << d.value("name", "") << "» avec type " << dtype
			    << " (attendu 2 pour une dim d’attributs)\n";
			continue;
		}
		out << "   - «" << d.value("name", "") << "» → associée à «" << d.value("associated_normal_dimension", "")
		    << "»\n";
		const auto &els = d.contains("elements") && d["elements"].is_array() ? d["elements"] : nlohmann::json::array();
		out << "     → passes 1 et 2 sur " << els.size() << " éléments\n";
	}

	out << "\n3) Créer le cube avec les dimensions d’axe dans l’ordre : ";
	for (size_t i = 0; i < dims_in_order.size(); ++i) {
		if (i) out << ", ";
		out << "«" << dims_in_order[i].get<std::string>() << "»";
	}
	out << "\n";

	out << "\n4) Règles\n";
	if (j.contains("rules") && j["rules"].is_array()) {
		out << "   - " << j["rules"].size() << " règle(s)\n";
	} else {
		out << "   - aucune\n";
	}
	if (json_has_cell_data(j)) {
		out << "\n5) Données (cell_data)\n";
		if (cell_report && cell_report->ran) {
			for (const auto &ln : cell_report->lines) out << ln << "\n";
			out << "   → avec --execute : écriture après la structure (même session).\n";
		} else {
			out << "   (vérification non exécutée)\n";
		}
	}
	out << "\n(Utilisez --execute pour appliquer ce plan sur le serveur.)\n";
}

/** GET API en oubliant X-PALO-DB : après une écriture, le jeton base change souvent sans être renvoyé. */
static std::string import_get(paloexport::PaloClient &c, const std::string &path,
			      const std::map<std::string, std::string> &params = {}) {
	c.clear_database_token();
	return c.get(path, params);
}

static void prime_database_context(paloexport::PaloClient &c, const std::string &db_name) {
	std::map<std::string, std::string> qp;
	qp["name_database"] = db_name;
	qp["show_normal"] = "1";
	qp["show_attribute"] = "1";
	qp["show_system"] = "1";
	qp["show_info"] = "1";
	qp["show_gputype"] = "1";
	c.clear_dimension_token();
	import_get(c, "/database/dimensions", qp);
}

static bool find_database_id_by_name(paloexport::PaloClient &c, const std::string &name, std::string &out_id) {
	std::string text = import_get(c, "/server/databases", {});
	auto rows = paloexport::parse_palo_csv_body(text);
	for (const auto &row : rows) {
		if (row.size() >= 2 && row[1] == name) {
			out_id = row[0];
			return true;
		}
	}
	return false;
}

/** Noms de dimensions présents dans la base (pour import idempotent). */
static std::set<std::string> list_dimension_names_in_database(paloexport::PaloClient &c, const std::string &db_name) {
	std::map<std::string, std::string> qp;
	qp["name_database"] = db_name;
	qp["show_normal"] = "1";
	qp["show_attribute"] = "1";
	qp["show_system"] = "1";
	qp["show_info"] = "1";
	qp["show_gputype"] = "1";
	c.clear_dimension_token();
	std::string text = import_get(c, "/database/dimensions", qp);
	auto rows = paloexport::parse_palo_csv_body(text);
	std::set<std::string> names;
	for (const auto &row : rows) {
		if (row.size() >= 2) names.insert(row[1]);
	}
	return names;
}

static std::string find_dimension_id_by_name(paloexport::PaloClient &c, const std::string &db_name,
					     const std::string &dim_name) {
	std::map<std::string, std::string> qp;
	qp["name_database"] = db_name;
	qp["show_normal"] = "1";
	qp["show_attribute"] = "1";
	qp["show_system"] = "1";
	qp["show_info"] = "1";
	qp["show_gputype"] = "1";
	c.clear_dimension_token();
	std::string text = import_get(c, "/database/dimensions", qp);
	auto rows = paloexport::parse_palo_csv_body(text);
	for (const auto &row : rows) {
		if (row.size() >= 2 && row[1] == dim_name) return row[0];
	}
	return "";
}

static bool cube_exists_by_name(paloexport::PaloClient &c, const std::string &db_id, const std::string &name) {
	std::map<std::string, std::string> p;
	p["database"] = db_id;
	std::string text = import_get(c, "/database/cubes", p);
	auto rows = paloexport::parse_palo_csv_body(text);
	for (const auto &row : rows) {
		if (row.size() >= 2 && row[1] == name) return true;
	}
	return false;
}

static void ensure_database(paloexport::PaloClient &c, const std::string &db_name, bool verbose) {
	std::string id;
	if (find_database_id_by_name(c, db_name, id)) {
		if (verbose) std::cerr << "paloimport: base «" << db_name << "» existante (id=" << id << ")\n";
		return;
	}
	import_get(c, "/database/create", {{"new_name", db_name}});
	if (verbose) std::cerr << "paloimport: base «" << db_name << "» créée\n";
}

static std::string find_cube_id_by_name(paloexport::PaloClient &c, const std::string &db_id, const std::string &name) {
	std::map<std::string, std::string> p;
	p["database"] = db_id;
	std::string text = import_get(c, "/database/cubes", p);
	auto rows = paloexport::parse_palo_csv_body(text);
	for (const auto &row : rows) {
		if (row.size() < 2) continue;
		if (row[1] == name) return row[0];
	}
	throw std::runtime_error("cube introuvable dans la base : «" + name + "»");
}

static std::vector<std::string> parse_dim_ids_csv(const std::string &dim_ids_csv) {
	std::vector<std::string> out;
	if (dim_ids_csv.empty()) return out;
	std::istringstream iss(dim_ids_csv);
	std::string part;
	while (std::getline(iss, part, ',')) {
		while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) part.erase(0, 1);
		while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
		if (!part.empty()) out.push_back(part);
	}
	return out;
}

static std::vector<std::string> get_cube_dimension_ids(paloexport::PaloClient &c, const std::string &db_id,
						      const std::string &cube_id) {
	std::map<std::string, std::string> info_p;
	info_p["database"] = db_id;
	info_p["cube"] = cube_id;
	c.clear_dimension_token();
	std::string info_text = import_get(c, "/cube/info", info_p);
	auto info_rows = paloexport::parse_palo_csv_body(info_text);
	if (info_rows.empty()) throw std::runtime_error("/cube/info : réponse vide");
	const auto &ci = info_rows[0];
	std::string dim_ids_csv = ci.size() > 3 ? ci[3] : "";
	return parse_dim_ids_csv(dim_ids_csv);
}

static std::map<std::string, std::string> load_dimension_id_to_name(paloexport::PaloClient &c,
								    const std::string &db_name) {
	std::map<std::string, std::string> qp;
	qp["name_database"] = db_name;
	qp["show_normal"] = "1";
	qp["show_attribute"] = "1";
	qp["show_system"] = "1";
	qp["show_info"] = "1";
	qp["show_gputype"] = "1";
	c.clear_dimension_token();
	std::string text = import_get(c, "/database/dimensions", qp);
	auto rows = paloexport::parse_palo_csv_body(text);
	std::map<std::string, std::string> out;
	for (const auto &row : rows) {
		if (row.size() >= 2) out[row[0]] = row[1];
	}
	return out;
}

/** nom d’élément → id (pour une dimension donnée par id). */
static std::map<std::string, std::string> load_element_name_to_id(paloexport::PaloClient &c,
								  const std::string &db_name,
								  const std::string &dim_id) {
	c.clear_dimension_token();
	std::map<std::string, std::string> ep;
	ep["name_database"] = db_name;
	ep["dimension"] = dim_id;
	std::string text = import_get(c, "/dimension/elements", ep);
	auto el_rows = paloexport::parse_palo_csv_body(text);
	std::map<std::string, std::string> out;
	for (const auto &row : el_rows) {
		if (row.size() >= 2) out[row[1]] = row[0];
	}
	return out;
}

static std::string join_path_names_csv(const nlohmann::json &path_arr) {
	if (!path_arr.is_array()) throw std::runtime_error("« path » doit être un tableau");
	std::ostringstream o;
	for (size_t i = 0; i < path_arr.size(); ++i) {
		if (i) o << ',';
		o << json_scalar_string(path_arr[i]);
	}
	return o.str();
}

/**
 * Vérifie que chaque cellule de cell_data pointe vers des éléments existants (noms) pour l’ordre des dimensions du cube.
 * Lève std::runtime_error au premier échec.
 */
static CellDataVerifyReport verify_cell_data_on_server(paloexport::PaloClient &c, const Config &cfg,
						       const nlohmann::json &j) {
	CellDataVerifyReport rep;
	rep.ran = true;
	std::string db_id;
	if (!find_database_id_by_name(c, cfg.database_name, db_id)) {
		throw std::runtime_error("base «" + cfg.database_name + "» introuvable sur le serveur (mode données seules)");
	}
	prime_database_context(c, cfg.database_name);
	const std::map<std::string, std::string> id_to_dimname = load_dimension_id_to_name(c, cfg.database_name);
	const nlohmann::json &cell_data = j["cell_data"];
	size_t total_cells = 0;
	for (auto it = cell_data.begin(); it != cell_data.end(); ++it) {
		const std::string cube_name = it.key();
		if (!it.value().is_array()) {
			throw std::runtime_error("cell_data[«" + cube_name + "»] doit être un tableau");
		}
		const std::string cube_id = find_cube_id_by_name(c, db_id, cube_name);
		const std::vector<std::string> dim_ids_order = get_cube_dimension_ids(c, db_id, cube_id);
		if (dim_ids_order.empty()) {
			throw std::runtime_error("cube «" + cube_name + "» : aucune dimension dans /cube/info");
		}
		std::vector<std::map<std::string, std::string>> elem_maps;
		elem_maps.reserve(dim_ids_order.size());
		std::string dim_order_label;
		for (size_t di = 0; di < dim_ids_order.size(); ++di) {
			const std::string &did = dim_ids_order[di];
			auto nit = id_to_dimname.find(did);
			const std::string dnm = (nit != id_to_dimname.end()) ? nit->second : std::string("?");
			if (di) dim_order_label += ", ";
			dim_order_label += "«" + dnm + "»";
			elem_maps.push_back(load_element_name_to_id(c, cfg.database_name, did));
		}
		size_t idx = 0;
		for (const auto &cell : it.value()) {
			++idx;
			if (!cell.is_object()) throw std::runtime_error("cell_data «" + cube_name + "» #" + std::to_string(idx) + " : objet attendu");
			if (!cell.contains("path")) throw std::runtime_error("cell_data «" + cube_name + "» #" + std::to_string(idx) + " : « path » manquant");
			const auto &path = cell["path"];
			if (!path.is_array() || path.size() != dim_ids_order.size()) {
				throw std::runtime_error("cell_data «" + cube_name + "» #" + std::to_string(idx) +
							 " : « path » doit avoir " + std::to_string(dim_ids_order.size()) +
							 " coordonnée(s) (ordre cube : " + dim_order_label + ")");
			}
			for (size_t k = 0; k < path.size(); ++k) {
				const std::string ename = json_scalar_string(path[k]);
				if (elem_maps[k].find(ename) == elem_maps[k].end()) {
					auto nit = id_to_dimname.find(dim_ids_order[k]);
					const std::string ddisplay = (nit != id_to_dimname.end()) ? nit->second : dim_ids_order[k];
					throw std::runtime_error("cell_data «" + cube_name + "» #" + std::to_string(idx) +
								 " : élément «" + ename + "» inconnu dans la dimension «" + ddisplay +
								 "»");
				}
			}
		}
		total_cells += it.value().size();
		std::ostringstream ln;
		ln << "   - Cube «" << cube_name << "» : " << it.value().size() << " cellule(s) OK (dimensions : "
		   << dim_order_label << ")";
		rep.lines.push_back(ln.str());
	}
	std::ostringstream sum;
	sum << "   Total : " << total_cells << " cellule(s) sur " << cell_data.size() << " cube(x).";
	rep.lines.insert(rep.lines.begin(), sum.str());
	return rep;
}

static void import_cell_data_execute(paloexport::PaloClient &c, const Config &cfg, const nlohmann::json &j) {
	std::string db_id;
	if (!find_database_id_by_name(c, cfg.database_name, db_id)) {
		throw std::runtime_error("base «" + cfg.database_name + "» introuvable");
	}
	prime_database_context(c, cfg.database_name);
	const auto &cell_data = j["cell_data"];
	for (auto it = cell_data.begin(); it != cell_data.end(); ++it) {
		const std::string cube_name = it.key();
		const std::string cube_id = find_cube_id_by_name(c, db_id, cube_name);
		size_t written = 0;
		size_t idx = 0;
		for (const auto &cell : it.value()) {
			++idx;
			if (!cell.is_object()) continue;
			bool exists = true;
			if (cell.contains("exists")) {
				if (cell["exists"].is_boolean()) exists = cell["exists"].get<bool>();
				else if (cell["exists"].is_number()) exists = (cell["exists"].get<int>() != 0);
				else if (cell["exists"].is_string()) exists = (cell["exists"].get<std::string>() == "1");
			}
			if (!exists) continue;
			std::string value_str;
			if (cell.contains("value")) {
				if (cell["value"].is_string()) value_str = cell["value"].get<std::string>();
				else value_str = json_scalar_string(cell["value"]);
			}
			std::map<std::string, std::string> p;
			p["name_database"] = cfg.database_name;
			p["name_cube"] = cube_name;
			p["name_path"] = join_path_names_csv(cell["path"]);
			p["value"] = value_str;
			p["splash"] = "1";
			c.clear_dimension_token();
			import_get(c, "/cell/replace", p);
			++written;
			if (cfg.verbose) std::cerr << "paloimport: cellule «" << cube_name << "» #" << idx << " ← " << value_str << "\n";
		}
		if (cfg.verbose) std::cerr << "paloimport: cube «" << cube_name << "» : " << written << " valeur(s) écrite(s)\n";
	}
}

static std::string element_type_str(const nlohmann::json &el) {
	if (!el.contains("type")) throw std::runtime_error("élément sans champ 'type'");
	return json_scalar_string(el["type"]);
}

/**
 * Deux passes : créer tous les éléments (consolidés sans enfants), puis /element/replace pour les consolidations.
 */
static void import_dimension_elements(paloexport::PaloClient &c, const std::string &db_name,
				      const std::string &dim_name, const nlohmann::json &elements, bool verbose) {
	if (!elements.is_array()) throw std::runtime_error("'elements' doit être un tableau");

	std::set<std::string> existing_el;
	const std::string dim_id = find_dimension_id_by_name(c, db_name, dim_name);
	if (!dim_id.empty()) {
		const auto m = load_element_name_to_id(c, db_name, dim_id);
		for (const auto &kv : m) existing_el.insert(kv.first);
	}

	c.clear_dimension_token();
	for (const auto &el : elements) {
		if (!el.is_object()) throw std::runtime_error("élément de dimension invalide");
		const std::string ename = el.value("name", "");
		if (ename.empty()) throw std::runtime_error("élément sans nom");
		if (existing_el.count(ename)) {
			if (verbose) std::cerr << "paloimport:   élément «" << ename << "» existe déjà — création ignorée\n";
			continue;
		}
		const std::string t = element_type_str(el);
		std::map<std::string, std::string> p;
		p["name_database"] = db_name;
		p["name_dimension"] = dim_name;
		p["new_name"] = ename;
		p["type"] = t;
		if (t == "4") {
			import_get(c, "/element/create", p);
		} else if (t == "1" || t == "2") {
			import_get(c, "/element/create", p);
		} else {
			throw std::runtime_error("type d’élément non géré: " + t + " («" + ename + "»)");
		}
		existing_el.insert(ename);
		if (verbose) std::cerr << "paloimport:   créé élément «" << ename << "» type " << t << "\n";
	}

	for (const auto &el : elements) {
		const std::string ename = el.value("name", "");
		const std::string t = element_type_str(el);
		if (t != "4") continue;
		if (!el.contains("children") || !el["children"].is_array() || el["children"].empty()) continue;

		const std::string names_csv = join_child_names_csv(el["children"]);
		std::map<std::string, std::string> p;
		p["name_database"] = db_name;
		p["name_dimension"] = dim_name;
		p["name_element"] = ename;
		p["type"] = "4";
		p["name_children"] = names_csv;
		if (el.contains("weights")) {
			std::string w;
			if (el["weights"].is_string()) w = el["weights"].get<std::string>();
			else w = json_scalar_string(el["weights"]);
			if (!w.empty()) p["weights"] = w;
		}
		c.clear_dimension_token();
		import_get(c, "/element/replace", p);
		if (verbose) std::cerr << "paloimport:   consolidation «" << ename << "» → enfants rattachés\n";
	}
}

static void execute_import(paloexport::PaloClient &client, const Config &cfg, const nlohmann::json &j) {
	const std::string &db_name = cfg.database_name;
	ensure_database(client, db_name, cfg.verbose);
	prime_database_context(client, db_name);

	std::string db_id;
	if (!find_database_id_by_name(client, db_name, db_id)) {
		throw std::runtime_error("base «" + db_name + "» introuvable après ensure_database");
	}

	std::set<std::string> dim_names_existing = list_dimension_names_in_database(client, db_name);

	const auto &cube = j["cube"];
	const auto &dims_in_order = cube["dimensions_in_order"];
	const size_t n_axes = dims_in_order.size();
	const auto &dims = j["dimensions"];
	const std::string cube_name = cube.value("name", "");

	for (size_t i = 0; i < n_axes; ++i) {
		const auto &d = dims[i];
		const std::string dname = d.value("name", "");
		if (dname.empty()) throw std::runtime_error("dimension d’axe sans nom");
		if (dim_names_existing.count(dname)) {
			if (cfg.verbose) std::cerr << "paloimport: dimension «" << dname << "» existe déjà — réutilisation\n";
		} else {
			client.clear_dimension_token();
			import_get(client, "/dimension/create", {{"name_database", db_name}, {"new_name", dname}, {"type", "0"}});
			dim_names_existing.insert(dname);
			if (cfg.verbose) std::cerr << "paloimport: dimension «" << dname << "» créée\n";
		}

		const nlohmann::json &els =
		    d.contains("elements") && d["elements"].is_array() ? d["elements"] : nlohmann::json::array();
		import_dimension_elements(client, db_name, dname, els, cfg.verbose);
	}

	for (size_t i = n_axes; i < dims.size(); ++i) {
		const auto &d = dims[i];
		const std::string dtype = d.value("type", "");
		if (dtype != "2") {
			throw std::runtime_error("entrée dimensions[" + std::to_string(i) +
						 "] : type " + dtype + " inattendu (attendu 2 après les axes)");
		}
		const std::string dname = d.value("name", "");
		if (dname.empty()) throw std::runtime_error("dimension d’attributs sans nom");
		const nlohmann::json &els =
		    d.contains("elements") && d["elements"].is_array() ? d["elements"] : nlohmann::json::array();
		import_dimension_elements(client, db_name, dname, els, cfg.verbose);
	}

	{
		std::ostringstream nd;
		for (size_t i = 0; i < dims_in_order.size(); ++i) {
			if (i) nd << ',';
			nd << dims_in_order[i].get<std::string>();
		}
		if (cube_exists_by_name(client, db_id, cube_name)) {
			if (cfg.verbose) std::cerr << "paloimport: cube «" << cube_name << "» existe déjà — création ignorée\n";
		} else {
			client.clear_dimension_token();
			import_get(client, "/cube/create",
				   {{"name_database", db_name}, {"new_name", cube_name}, {"name_dimensions", nd.str()}, {"type", "0"}});
			if (cfg.verbose) std::cerr << "paloimport: cube «" << cube_name << "» créé\n";
		}
	}

	if (j.contains("rules") && j["rules"].is_array()) {
		size_t ri = 0;
		for (const auto &r : j["rules"]) {
			if (!r.is_object()) continue;
			std::string def = r.value("rule_string", "");
			if (def.empty()) continue;
			std::map<std::string, std::string> p;
			p["name_database"] = db_name;
			p["name_cube"] = cube_name;
			p["definition"] = def;
			p["use_identifier"] = "0";
			if (r.contains("active")) p["activate"] = json_scalar_string(r["active"]);
			if (r.contains("position")) p["position"] = json_scalar_string(r["position"]);
			if (r.contains("external_identifier")) p["external_identifier"] = json_scalar_string(r["external_identifier"]);
			if (r.contains("comment")) p["comment"] = json_scalar_string(r["comment"]);
			client.clear_dimension_token();
			import_get(client, "/rule/create", p);
			++ri;
			if (cfg.verbose) std::cerr << "paloimport: règle " << ri << " créée\n";
		}
	}

	if (json_has_cell_data(j)) {
		if (cfg.verbose) std::cerr << "paloimport: import des cellules (cell_data)…\n";
		import_cell_data_execute(client, cfg, j);
	}

	std::cerr << "paloimport: import structure terminé — base «" << db_name << "», cube «" << cube_name << "» sur "
		  << cfg.host << ":" << cfg.port << "\n";
}

static void execute_data_only(paloexport::PaloClient &client, const Config &cfg, const nlohmann::json &j) {
	import_cell_data_execute(client, cfg, j);
	std::cerr << "paloimport: import données terminé — base «" << cfg.database_name << "» sur " << cfg.host << ":"
		  << cfg.port << "\n";
}

} // namespace

int main(int argc, char **argv) {
	Config cfg;

	static struct option long_opts[] = {{"host", required_argument, nullptr, 'h'},
					    {"port", required_argument, nullptr, 'P'},
					    {"user", required_argument, nullptr, 'u'},
					    {"password", required_argument, nullptr, 'p'},
					    {"database", required_argument, nullptr, 'D'},
					    {"file", required_argument, nullptr, 'f'},
					    {"https", no_argument, nullptr, 1},
					    {"verbose", no_argument, nullptr, 'v'},
					    {"timeout", required_argument, nullptr, 3},
					    {"dry-run", no_argument, nullptr, 4},
					    {"execute", no_argument, nullptr, 5},
					    {"help", no_argument, nullptr, 2},
					    {nullptr, 0, nullptr, 0}};

	int c;
	while ((c = getopt_long(argc, argv, "h:P:u:p:D:f:v", long_opts, nullptr)) != -1) {
		switch (c) {
			case 'h': cfg.host = optarg; break;
			case 'P': cfg.port = optarg; break;
			case 'u': cfg.user = optarg; break;
			case 'p': cfg.password = optarg; break;
			case 'D': cfg.database_name = optarg; break;
			case 'f': cfg.json_path = optarg; break;
			case 'v': cfg.verbose = true; break;
			case 3: {
				int t = std::atoi(optarg);
				cfg.timeout_sec = t > 0 ? t : 120;
				break;
			}
			case 1: cfg.use_https = true; break;
			case 4: cfg.dry_run = true; cfg.execute = false; break;
			case 5: cfg.execute = true; cfg.dry_run = false; break;
			case 2: print_usage(argv[0]); return 0;
			default: print_usage(argv[0]); return 2;
		}
	}

	for (int i = optind; i < argc; ++i) {
		std::cerr << "Argument inattendu: " << argv[i] << "\n";
		print_usage(argv[0]);
		return 2;
	}

	if (cfg.json_path.empty() || cfg.user.empty() || cfg.password.empty() || cfg.database_name.empty()) {
		std::cerr << "Options obligatoires: -f --file -u -p --database\n";
		print_usage(argv[0]);
		return 2;
	}

	try {
		const std::string raw = read_file_utf8(cfg.json_path);
		nlohmann::json j = nlohmann::json::parse(raw);
		validate_json(j);

		const std::string scheme = cfg.use_https ? "https" : "http";
		const std::string base = scheme + "://" + cfg.host + ":" + cfg.port;
		paloexport::PaloClient client(base);
		client.set_verbose(cfg.verbose);
		client.set_io_timeout(cfg.timeout_sec);

		CellDataVerifyReport cell_rep;
		bool logged_in = false;
		if (json_has_cell_data(j)) {
			if (cfg.verbose) {
				std::cerr << "paloimport: connexion à " << base << " (vérification cell_data, utilisateur «"
					  << cfg.user << "»)\n";
			}
			client.login(cfg.user, cfg.password);
			logged_in = true;
			cell_rep = verify_cell_data_on_server(client, cfg, j);
		}

		print_plan(j, std::cout, json_has_cell_data(j) ? &cell_rep : nullptr);

		if (cfg.execute) {
			if (!logged_in) {
				if (cfg.verbose) {
					std::cerr << "paloimport: connexion à " << base << " (utilisateur «" << cfg.user << "», timeout "
						  << cfg.timeout_sec << " s)\n";
				}
				client.login(cfg.user, cfg.password);
			}
			if (json_is_data_only(j))
				execute_data_only(client, cfg, j);
			else
				execute_import(client, cfg, j);
		}
	} catch (const nlohmann::json::exception &e) {
		std::cerr << "paloimport: JSON: " << e.what() << "\n";
		return 1;
	} catch (const paloexport::PaloHttpError &e) {
		std::cerr << "paloimport: " << e.what() << "\n";
		return 1;
	} catch (const std::exception &e) {
		std::cerr << "paloimport: " << e.what() << "\n";
		return 1;
	}

	return 0;
}
