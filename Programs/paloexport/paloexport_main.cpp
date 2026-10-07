/*
 * paloexport — export structure d'un cube Palo (JSON) via l'API HTTP existante.
 *
 * Exemple :
 *   paloexport -u admin -h 127.0.0.1 -P 7777 -p secret --database dwh --cube ventes
 *
 * Stratégie d’import depuis ce JSON : Programs/paloexport/README.txt (cahier des charges).
 */

#include "paloexport_client.hpp"

#include <getopt.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string json_escape(std::string_view s) {
	std::string o;
	o.reserve(s.size() + 8);
	for (unsigned char c : s) {
		switch (c) {
			case '"': o += "\\\""; break;
			case '\\': o += "\\\\"; break;
			case '\b': o += "\\b"; break;
			case '\f': o += "\\f"; break;
			case '\n': o += "\\n"; break;
			case '\r': o += "\\r"; break;
			case '\t': o += "\\t"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					o += buf;
				} else {
					o += static_cast<char>(c);
				}
		}
	}
	return o;
}

std::string json_string(const std::string &s) {
	return std::string("\"") + json_escape(s) + "\"";
}

void print_usage(const char *argv0) {
	std::cerr
	    << "Usage: " << argv0
	    << " -h HOST -u USER -p PASSWORD --database NAME --cube NAME [options]\n"
	       "Options:\n"
	       "  -h, --host HOST      Serveur Palo (défaut: 127.0.0.1)\n"
	       "  -P, --port PORT      Port HTTP (défaut: 7777)\n"
	       "  -u, --user USER      Identifiant\n"
	       "  -p, --password PASS  Mot de passe (clair ; hash MD5 côté client comme l'API)\n"
	       "  -D, --database NAME  Nom de la base\n"
	       "  -C, --cube NAME      Nom du cube\n"
	       "  -v, --verbose        Journal sur stderr : URL, puis chaque réponse API (HTTP + corps texte Palo)\n"
	       "      --timeout SEC    Délai max lecture/écriture socket (défaut: 120)\n"
	       "      --https          Utiliser https:// (défaut: http)\n"
	       "      --with-data      Exporter aussi les valeurs de cellules (cube principal + cubes d’attributs,\n"
	       "                       via /cell/export ; chemins en noms d’éléments)\n"
	       "      --data-block-size N   Taille max de bloc /cell/export (défaut: 10000)\n"
	       "      --help           Aide\n"
	       "\n"
	       "Sortie : JSON (paloexport_version 6 ou 7 avec --with-data) : structure ; optionnellement\n"
	       "cellules « base » non vides (base_only=1, skip_empty=1, valeurs stockées sans règles).\n"
	       "Import (spécification) : README.txt dans ce dossier.\n"
	       "Les messages d'erreur et le mode verbose vont sur la sortie d'erreur.\n";
}

struct Config {
	std::string host = "127.0.0.1";
	std::string port = "7777";
	std::string user;
	std::string password;
	std::string database_name;
	std::string cube_name;
	bool use_https = false;
	bool verbose = false;
	int timeout_sec = 120;
	bool with_data = false;
	int data_block_size = 10000;
};

std::string find_database_id(paloexport::PaloClient &c, const std::string &name) {
	std::string text = c.get("/server/databases", {});
	auto rows = paloexport::parse_palo_csv_body(text);
	for (const auto &row : rows) {
		if (row.size() < 2) continue;
		if (row[1] == name) return row[0];
	}
	throw paloexport::PaloHttpError("base de données introuvable: " + name);
}

std::string find_cube_id(paloexport::PaloClient &c, const std::string &database_id, const std::string &name) {
	std::map<std::string, std::string> p;
	p["database"] = database_id;
	std::string text = c.get("/database/cubes", p);
	auto rows = paloexport::parse_palo_csv_body(text);
	for (const auto &row : rows) {
		if (row.size() < 2) continue;
		if (row[1] == name) return row[0];
	}
	throw paloexport::PaloHttpError("cube introuvable: " + name);
}

struct DimensionInfo {
	std::string name;
	/** Type Palo dimension : 0=normal, 1=system, 2=attribute, 3=user info, 4=system id (cf. API). */
	std::string type;
	/** Si dimension normale avec attributs : ID de la dimension d’attributs (col. 7). */
	std::string attributes_dimension_id;
	/** ID du cube d’attributs (col. 8). */
	std::string attributes_cube_id;
	/** Si type==2 (dimension d’attributs) : ID de la dimension normale associée (col. 7). */
	std::string associated_normal_dimension_id;
};

/** dimension_id -> métadonnées (/database/dimensions : colonnes 7–8 = attributs). */
static std::map<std::string, DimensionInfo> load_dimension_infos(paloexport::PaloClient &c,
								 const std::string &database_id) {
	std::map<std::string, std::string> qp;
	qp["database"] = database_id;
	qp["show_normal"] = "1";
	qp["show_attribute"] = "1";
	qp["show_system"] = "1";
	qp["show_info"] = "1";
	qp["show_gputype"] = "1";
	std::string text = c.get("/database/dimensions", qp);
	auto rows = paloexport::parse_palo_csv_body(text);
	std::map<std::string, DimensionInfo> out;
	for (const auto &row : rows) {
		if (row.size() < 11) continue;
		DimensionInfo di;
		di.name = row[1];
		di.type = row[6];
		if (di.type == "2") {
			di.associated_normal_dimension_id = row[7];
		} else {
			di.attributes_dimension_id = row[7];
			di.attributes_cube_id = row[8];
		}
		out[row[0]] = std::move(di);
	}
	return out;
}

/** cube_id -> name (cubes normaux et d’attributs). */
static std::map<std::string, std::string> load_cube_name_map(paloexport::PaloClient &c,
							     const std::string &database_id) {
	std::map<std::string, std::string> qp;
	qp["database"] = database_id;
	qp["show_normal"] = "1";
	qp["show_attribute"] = "1";
	qp["show_system"] = "1";
	qp["show_info"] = "1";
	qp["show_gputype"] = "1";
	std::string text = c.get("/database/cubes", qp);
	auto rows = paloexport::parse_palo_csv_body(text);
	std::map<std::string, std::string> out;
	for (const auto &row : rows) {
		if (row.size() < 2) continue;
		out[row[0]] = row[1];
	}
	return out;
}

/** Découpe `children` Palo (ids séparés par des virgules). */
static std::vector<std::string> split_comma_ids(const std::string &s) {
	std::vector<std::string> out;
	std::string cur;
	for (unsigned char uc : s) {
		char c = static_cast<char>(uc);
		if (c == ',') {
			while (!cur.empty() && (cur[0] == ' ' || cur[0] == '\t')) cur.erase(0, 1);
			while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
			if (!cur.empty()) out.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	while (!cur.empty() && (cur[0] == ' ' || cur[0] == '\t')) cur.erase(0, 1);
	while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
	if (!cur.empty()) out.push_back(cur);
	return out;
}

/** Convertit une liste d’IDs Palo (CSV) en tableau JSON de noms d’éléments. */
static void write_json_name_array(std::ostream &out, const std::string &ids_csv,
				  const std::map<std::string, std::string> &id_to_name) {
	out << "[";
	if (!ids_csv.empty()) {
		const std::vector<std::string> ids = split_comma_ids(ids_csv);
		for (size_t i = 0; i < ids.size(); ++i) {
			if (i) out << ", ";
			auto it = id_to_name.find(ids[i]);
			const std::string &nm = (it != id_to_name.end()) ? it->second : std::string("?");
			out << json_string(nm);
		}
	}
	out << "]";
}

/** Contenu d’un tableau `elements` (hiérarchie, parents/enfants en noms). */
static void write_elements_array_json(std::ostream &out, const std::vector<paloexport::PaloCsvRow> &el_rows,
				      const std::string &brace_indent, const std::string &field_indent) {
	std::map<std::string, std::string> id_to_name;
	for (const auto &row : el_rows) {
		if (row.size() >= 2) id_to_name[row[0]] = row[1];
	}
	bool first_el = true;
	for (size_t r = 0; r < el_rows.size(); ++r) {
		const auto &row = el_rows[r];
		if (row.size() < 10) continue;
		if (!first_el) out << ",\n";
		first_el = false;
		out << brace_indent << "{\n";
		out << field_indent << "\"name\": " << json_string(row[1]) << ",\n";
		out << field_indent << "\"position\": " << json_string(row[2]) << ",\n";
		out << field_indent << "\"level\": " << json_string(row[3]) << ",\n";
		out << field_indent << "\"indent\": " << json_string(row[4]) << ",\n";
		out << field_indent << "\"depth\": " << json_string(row[5]) << ",\n";
		out << field_indent << "\"type\": " << json_string(row[6]) << ",\n";
		out << field_indent << "\"number_parents\": " << json_string(row[7]) << ",\n";
		out << field_indent << "\"parents\": ";
		write_json_name_array(out, row[8], id_to_name);
		out << ",\n";
		out << field_indent << "\"number_children\": " << json_string(row[9]) << ",\n";
		out << field_indent << "\"children\": ";
		write_json_name_array(out, row[10], id_to_name);
		if (row.size() > 11) out << ",\n" << field_indent << "\"weights\": " << json_string(row[11]);
		else out << ",\n" << field_indent << "\"weights\": \"\"";
		out << "\n" << brace_indent << "}";
	}
}

/** id élément → nom pour une dimension (lignes /dimension/elements). */
static std::map<std::string, std::string> build_element_id_to_name_map(const std::vector<paloexport::PaloCsvRow> &el_rows) {
	std::map<std::string, std::string> m;
	for (const auto &row : el_rows) {
		if (row.size() >= 2) m[row[0]] = row[1];
	}
	return m;
}

/** Ordre des dimensions d’un cube (ids) depuis /cube/info. */
static std::vector<std::string> get_cube_dimension_ids(paloexport::PaloClient &c, const std::string &db_id,
						       const std::string &cube_id) {
	std::map<std::string, std::string> info_p;
	info_p["database"] = db_id;
	info_p["cube"] = cube_id;
	c.clear_database_token();
	c.clear_dimension_token();
	std::string info_text = c.get("/cube/info", info_p);
	auto info_rows = paloexport::parse_palo_csv_body(info_text);
	if (info_rows.empty()) throw paloexport::PaloHttpError("/cube/info: réponse vide");
	const auto &ci = info_rows[0];
	std::string dim_ids_csv = ci.size() > 3 ? ci[3] : "";
	std::vector<std::string> out;
	if (!dim_ids_csv.empty()) {
		std::istringstream iss(dim_ids_csv);
		std::string part;
		while (std::getline(iss, part, ',')) {
			while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) part.erase(0, 1);
			while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
			if (!part.empty()) out.push_back(part);
		}
	}
	return out;
}

static std::string join_comma_ids(const std::vector<std::string> &ids) {
	std::ostringstream o;
	for (size_t i = 0; i < ids.size(); ++i) {
		if (i) o << ',';
		o << ids[i];
	}
	return o.str();
}

static void path_ids_to_names(const std::vector<std::string> &cube_dim_ids, const std::vector<std::string> &elem_ids,
			      const std::map<std::string, std::map<std::string, std::string>> &dim_elem_maps,
			      std::vector<std::string> &out_names) {
	out_names.clear();
	if (cube_dim_ids.size() != elem_ids.size()) {
		out_names = elem_ids;
		return;
	}
	for (size_t i = 0; i < cube_dim_ids.size(); ++i) {
		const std::string &did = cube_dim_ids[i];
		const std::string &eid = elem_ids[i];
		auto dit = dim_elem_maps.find(did);
		if (dit == dim_elem_maps.end()) {
			out_names.push_back(eid);
			continue;
		}
		auto it = dit->second.find(eid);
		if (it == dit->second.end()) out_names.push_back(eid);
		else out_names.push_back(it->second);
	}
}

/** Une ligne de données /cell/export (pas la ligne de progression). Retourne faux si ligne d’erreur type 99 ou invalide. */
static bool parse_cell_export_data_row(const paloexport::PaloCsvRow &f, int &type_out, bool &exists_out,
				       std::string &value_out, std::vector<std::string> &path_ids_out) {
	if (f.size() < 2) return false;
	try {
		type_out = std::stoi(f[0]);
	} catch (...) {
		return false;
	}
	if (type_out == 99) return false;
	exists_out = (f[1] == "1");
	value_out.clear();
	if (exists_out && f.size() > 2) value_out = f[2];
	if (f.size() > 3 && !f[3].empty()) path_ids_out = split_comma_ids(f[3]);
	else path_ids_out.clear();
	return true;
}

/** Dernière ligne du corps : deux entiers (progression / 1000). */
static bool parse_cell_export_progress_line(const std::string &line, int &a, int &b) {
	auto rows = paloexport::parse_palo_csv_body(line + "\n");
	if (rows.empty() || rows[0].size() < 2) return false;
	try {
		a = std::stoi(rows[0][0]);
		b = std::stoi(rows[0][1]);
	} catch (...) {
		return false;
	}
	return true;
}

/**
 * Écrit dans `out` le tableau JSON de cellules pour un cube (chemins en noms).
 * Utilise /cell/export par blocs (reprise avec path=).
 */
static void write_cube_cells_json_array(std::ostream &out, paloexport::PaloClient &c, const Config &cfg,
					const std::string &db_id, const std::string &cube_id,
					const std::vector<std::string> &cube_dim_ids,
					const std::map<std::string, std::map<std::string, std::string>> &dim_elem_maps) {
	const int block = cfg.data_block_size > 0 ? cfg.data_block_size : 10000;
	std::string path_continue;
	bool first_cell = true;
	out << "[\n";

	for (;;) {
		std::map<std::string, std::string> p;
		p["database"] = db_id;
		p["cube"] = cube_id;
		p["blocksize"] = std::to_string(block);
		p["skip_empty"] = "1";
		p["base_only"] = "1";
		p["use_rules"] = "0";
		if (!path_continue.empty()) p["path"] = path_continue;

		c.clear_database_token();
		c.clear_dimension_token();
		std::string text = c.get("/cell/export", p);

		std::vector<std::string> lines;
		{
			std::istringstream iss(text);
			std::string ln;
			while (std::getline(iss, ln)) {
				if (!ln.empty() && ln.back() == '\r') ln.pop_back();
				if (!ln.empty()) lines.push_back(std::move(ln));
			}
		}
		if (lines.empty()) break;

		std::string progress_line = std::move(lines.back());
		lines.pop_back();

		std::string last_path_for_continue;

		for (const std::string &line : lines) {
			auto fields = paloexport::parse_palo_csv_body(line + "\n");
			if (fields.empty()) continue;
			const auto &f = fields[0];
			int t = 0;
			bool ex = false;
			std::string val;
			std::vector<std::string> pids;
			if (!parse_cell_export_data_row(f, t, ex, val, pids)) continue;
			if (cube_dim_ids.size() != pids.size()) continue;

			std::vector<std::string> pnames;
			path_ids_to_names(cube_dim_ids, pids, dim_elem_maps, pnames);
			last_path_for_continue = join_comma_ids(pids);

			if (!first_cell) out << ",\n";
			first_cell = false;
			out << "            {\n";
			out << "              \"path\": [";
			for (size_t i = 0; i < pnames.size(); ++i) {
				if (i) out << ", ";
				out << json_string(pnames[i]);
			}
			out << "],\n";
			out << "              \"type\": " << json_string(std::to_string(t)) << ",\n";
			out << "              \"exists\": " << (ex ? "true" : "false") << ",\n";
			out << "              \"value\": " << json_string(ex ? val : std::string()) << "\n";
			out << "            }";
		}

		int prog = 0, pmax = 0;
		if (!parse_cell_export_progress_line(progress_line, prog, pmax)) break;
		if (pmax != 1000) break;
		if (prog >= 1000) break;
		if (last_path_for_continue.empty()) {
			if (prog < 1000 && cfg.verbose) {
				std::cerr << "[paloexport] Avertissement : export cellules interrompu (reprise impossible).\n";
			}
			break;
		}
		path_continue = last_path_for_continue;
	}

	out << "\n          ]";
}

void dump_cube_json(paloexport::PaloClient &c, const Config &cfg, std::ostream &out) {
	if (cfg.verbose) {
		std::cerr << "[paloexport] Export structure : base «" << cfg.database_name << "», cube «" << cfg.cube_name
			  << "»\n";
	}
	const std::string db_id = find_database_id(c, cfg.database_name);
	if (cfg.verbose) {
		std::cerr << "[paloexport] Base résolue : id=" << db_id << "\n";
	}
	const std::string cube_id = find_cube_id(c, db_id, cfg.cube_name);
	if (cfg.verbose) {
		std::cerr << "[paloexport] Cube résolu : id=" << cube_id << "\n";
	}

	std::map<std::string, std::string> info_p;
	info_p["database"] = db_id;
	info_p["cube"] = cube_id;
	std::string info_text = c.get("/cube/info", info_p);
	auto info_rows = paloexport::parse_palo_csv_body(info_text);
	if (info_rows.empty()) throw paloexport::PaloHttpError("/cube/info: réponse vide");

	const auto &ci = info_rows[0];
	// cube, name, n_dim, dimensions, n_cells, filled, status, type, ...
	std::string dim_ids_csv = ci.size() > 3 ? ci[3] : "";

	if (cfg.verbose) {
		std::cerr << "[paloexport] Chargement des métadonnées dimensions (/database/dimensions)…\n";
	}
	std::map<std::string, DimensionInfo> dim_infos = load_dimension_infos(c, db_id);
	if (cfg.verbose) {
		std::cerr << "[paloexport] Chargement des noms de cubes (/database/cubes)…\n";
	}
	std::map<std::string, std::string> cube_name_map = load_cube_name_map(c, db_id);

	std::vector<std::string> axis_dim_ids;
	if (!dim_ids_csv.empty()) {
		std::istringstream iss(dim_ids_csv);
		std::string part;
		while (std::getline(iss, part, ',')) {
			while (!part.empty() && (part[0] == ' ' || part[0] == '\t')) part.erase(0, 1);
			while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.pop_back();
			if (!part.empty()) axis_dim_ids.push_back(part);
		}
	}

	/** Pour export données : id dimension → (id élément → nom). */
	std::map<std::string, std::map<std::string, std::string>> dim_elem_maps;
	std::vector<std::pair<std::string, std::string>> attr_cubes_for_data;
	std::set<std::string> attr_cube_ids_seen_data;

	out << "{\n";
	out << "  \"paloexport_version\": " << (cfg.with_data ? "7" : "6") << ",\n";
	out << "  \"generator\": \"paloexport\",\n";
	out << "  \"server\": " << json_string(cfg.host + ":" + cfg.port) << ",\n";
	out << "  \"database\": { \"name\": " << json_string(cfg.database_name) << " },\n";

	out << "  \"cube\": {\n";
	out << "    \"name\": " << json_string(cfg.cube_name) << ",\n";
	out << "    \"number_dimensions\": " << json_string(ci.size() > 2 ? ci[2] : "") << ",\n";
	out << "    \"dimensions_in_order\": [";
	for (size_t i = 0; i < axis_dim_ids.size(); ++i) {
		if (i) out << ", ";
		const std::string &did = axis_dim_ids[i];
		auto dit = dim_infos.find(did);
		const std::string dnm = (dit != dim_infos.end()) ? dit->second.name : std::string("?");
		out << json_string(dnm);
	}
	out << "],\n";
	out << "    \"type\": " << json_string(ci.size() > 7 ? ci[7] : "") << ",\n";
	out << "    \"status\": " << json_string(ci.size() > 6 ? ci[6] : "") << "\n";
	out << "  },\n";

	out << "  \"dimensions\": [\n";

	bool first_dim = true;
	std::vector<std::string> attr_dim_ids_extra;
	std::set<std::string> attr_dim_seen;
	for (size_t di = 0; di < axis_dim_ids.size(); ++di) {
		const std::string &dim_id = axis_dim_ids[di];
		auto it = dim_infos.find(dim_id);
		const std::string dim_name = (it != dim_infos.end()) ? it->second.name : "?";
		const std::string dim_type = (it != dim_infos.end()) ? it->second.type : "";

		if (cfg.verbose) {
			std::cerr << "[paloexport] Dimension " << (di + 1) << "/" << axis_dim_ids.size() << " «" << dim_name
				  << "» (éléments)…\n";
		}

		std::map<std::string, std::string> ep;
		ep["database"] = db_id;
		ep["dimension"] = dim_id;
		c.clear_dimension_token();
		std::string el_text = c.get("/dimension/elements", ep);
		auto el_rows = paloexport::parse_palo_csv_body(el_text);
		dim_elem_maps[dim_id] = build_element_id_to_name_map(el_rows);

		if (!first_dim) out << ",\n";
		first_dim = false;
		out << "    {\n";
		out << "      \"name\": " << json_string(dim_name) << ",\n";
		out << "      \"type\": " << json_string(dim_type) << ",\n";
		if (it != dim_infos.end() && !it->second.attributes_dimension_id.empty()) {
			const std::string &attr_dim_id = it->second.attributes_dimension_id;
			std::string adn = "?";
			auto an = dim_infos.find(attr_dim_id);
			if (an != dim_infos.end()) adn = an->second.name;
			std::string acn;
			if (!it->second.attributes_cube_id.empty()) {
				auto cn = cube_name_map.find(it->second.attributes_cube_id);
				if (cn != cube_name_map.end()) acn = cn->second;
			}
			out << "      \"attributes_dimension\": " << json_string(adn) << ",\n";
			out << "      \"attributes_cube\": " << json_string(acn) << ",\n";
			if (!attr_dim_id.empty() && !attr_dim_seen.count(attr_dim_id)) {
				attr_dim_seen.insert(attr_dim_id);
				attr_dim_ids_extra.push_back(attr_dim_id);
			}
			if (cfg.with_data && !it->second.attributes_cube_id.empty() &&
			    !attr_cube_ids_seen_data.count(it->second.attributes_cube_id)) {
				auto cn = cube_name_map.find(it->second.attributes_cube_id);
				const std::string acn =
				    (cn != cube_name_map.end()) ? cn->second : std::string("?");
				attr_cube_ids_seen_data.insert(it->second.attributes_cube_id);
				attr_cubes_for_data.emplace_back(it->second.attributes_cube_id, acn);
			}
		}
		out << "      \"elements\": [\n";
		write_elements_array_json(out, el_rows, "        ", "          ");
		out << "\n      ]\n";
		out << "    }";
	}

	for (const std::string &attr_dim_id : attr_dim_ids_extra) {
		auto dit = dim_infos.find(attr_dim_id);
		const std::string adim_name = (dit != dim_infos.end()) ? dit->second.name : "?";
		const std::string adim_type = (dit != dim_infos.end()) ? dit->second.type : "2";
		std::string assoc_norm;
		if (dit != dim_infos.end() && !dit->second.associated_normal_dimension_id.empty()) {
			auto nd = dim_infos.find(dit->second.associated_normal_dimension_id);
			if (nd != dim_infos.end()) assoc_norm = nd->second.name;
		}
		if (cfg.verbose) {
			std::cerr << "[paloexport] Dimension d’attributs «" << adim_name << "» (type " << adim_type
				  << ", éléments)…\n";
		}
		std::map<std::string, std::string> ap;
		ap["database"] = db_id;
		ap["dimension"] = attr_dim_id;
		c.clear_dimension_token();
		std::string attr_el_text = c.get("/dimension/elements", ap);
		auto attr_el_rows = paloexport::parse_palo_csv_body(attr_el_text);
		dim_elem_maps[attr_dim_id] = build_element_id_to_name_map(attr_el_rows);

		out << ",\n    {\n";
		out << "      \"name\": " << json_string(adim_name) << ",\n";
		out << "      \"type\": " << json_string(adim_type) << ",\n";
		if (!assoc_norm.empty()) {
			out << "      \"associated_normal_dimension\": " << json_string(assoc_norm) << ",\n";
		}
		out << "      \"elements\": [\n";
		write_elements_array_json(out, attr_el_rows, "        ", "          ");
		out << "\n      ]\n";
		out << "    }";
	}
	out << "\n  ],\n";

	if (cfg.with_data) {
		if (cfg.verbose) {
			std::cerr << "[paloexport] Export des cellules (/cell/export) — bloc max " << cfg.data_block_size
				  << "…\n";
		}
		out << "  \"cell_data\": {\n";
		bool first_cube_key = true;
		auto emit_one_cube_data = [&](const std::string &display_name, const std::string &cid,
					      const std::vector<std::string> &dim_order) {
			if (!first_cube_key) out << ",\n";
			first_cube_key = false;
			out << "    " << json_string(display_name) << ": ";
			write_cube_cells_json_array(out, c, cfg, db_id, cid, dim_order, dim_elem_maps);
		};
		emit_one_cube_data(cfg.cube_name, cube_id, axis_dim_ids);
		for (const auto &ac : attr_cubes_for_data) {
			const std::string &acid = ac.first;
			const std::string &acname = ac.second;
			try {
				std::vector<std::string> adim_order = get_cube_dimension_ids(c, db_id, acid);
				emit_one_cube_data(acname, acid, adim_order);
			} catch (const std::exception &e) {
				if (cfg.verbose) {
					std::cerr << "[paloexport] Avertissement : pas d’export données pour le cube «"
						  << acname << "» : " << e.what() << "\n";
				}
			}
		}
		out << "\n  },\n";
	}

	std::map<std::string, std::string> rp;
	rp["database"] = db_id;
	rp["cube"] = cube_id;
	rp["use_identifier"] = "0";
	if (cfg.verbose) {
		std::cerr << "[paloexport] Règles du cube (/cube/rules)…\n";
	}
	std::string rules_text = c.get("/cube/rules", rp);
	auto rule_rows = paloexport::parse_palo_csv_body(rules_text);

	out << "  \"rules\": [\n";
	for (size_t r = 0; r < rule_rows.size(); ++r) {
		const auto &row = rule_rows[r];
		if (row.size() < 2) continue;
		if (r) out << ",\n";
		out << "    {\n";
		out << "      \"rule_string\": " << json_string(row[1]);
		if (row.size() > 2) out << ",\n      \"external_identifier\": " << json_string(row[2]);
		if (row.size() > 3) out << ",\n      \"comment\": " << json_string(row[3]);
		if (row.size() > 4) out << ",\n      \"timestamp\": " << json_string(row[4]);
		if (row.size() > 5) out << ",\n      \"active\": " << json_string(row[5]);
		if (row.size() > 6) out << ",\n      \"position\": " << json_string(row[6]);
		out << "\n    }";
	}
	out << "\n  ]\n";
	out << "}\n";
}

} // namespace

int main(int argc, char **argv) {
	Config cfg;

	static struct option long_opts[] = {{"host", required_argument, nullptr, 'h'},
					    {"port", required_argument, nullptr, 'P'},
					    {"user", required_argument, nullptr, 'u'},
					    {"password", required_argument, nullptr, 'p'},
					    {"database", required_argument, nullptr, 'D'},
					    {"cube", required_argument, nullptr, 'C'},
					    {"https", no_argument, nullptr, 1},
					    {"verbose", no_argument, nullptr, 'v'},
					    {"timeout", required_argument, nullptr, 3},
					    {"with-data", no_argument, nullptr, 4},
					    {"data-block-size", required_argument, nullptr, 5},
					    {"help", no_argument, nullptr, 2},
					    {nullptr, 0, nullptr, 0}};

	int c;
	while ((c = getopt_long(argc, argv, "h:P:u:p:D:C:v", long_opts, nullptr)) != -1) {
		switch (c) {
			case 'h': cfg.host = optarg; break;
			case 'P': cfg.port = optarg; break;
			case 'u': cfg.user = optarg; break;
			case 'p': cfg.password = optarg; break;
			case 'D': cfg.database_name = optarg; break;
			case 'C': cfg.cube_name = optarg; break;
			case 'v': cfg.verbose = true; break;
			case 3: {
				int t = std::atoi(optarg);
				cfg.timeout_sec = t > 0 ? t : 120;
				break;
			}
			case 4: cfg.with_data = true; break;
			case 5: {
				int b = std::atoi(optarg);
				cfg.data_block_size = b > 0 ? b : 10000;
				break;
			}
			case 1: cfg.use_https = true; break;
			case 2: print_usage(argv[0]); return 0;
			default: print_usage(argv[0]); return 2;
		}
	}

	for (int i = optind; i < argc; ++i) {
		std::cerr << "Argument inattendu: " << argv[i] << "\n";
		print_usage(argv[0]);
		return 2;
	}

	if (cfg.user.empty() || cfg.password.empty() || cfg.database_name.empty() || cfg.cube_name.empty()) {
		std::cerr << "Options obligatoires: -u -p --database --cube\n";
		print_usage(argv[0]);
		return 2;
	}

	std::string scheme = cfg.use_https ? "https" : "http";
	std::string base = scheme + "://" + cfg.host + ":" + cfg.port;

	try {
		paloexport::PaloClient client(base);
		client.set_verbose(cfg.verbose);
		client.set_io_timeout(cfg.timeout_sec);
		if (cfg.verbose) {
			std::cerr << "[paloexport] Connexion à " << base << " (utilisateur «" << cfg.user << "», timeout "
				  << cfg.timeout_sec << " s)\n";
		}
		client.login(cfg.user, cfg.password);
		dump_cube_json(client, cfg, std::cout);
		if (cfg.verbose) {
			std::cerr << "[paloexport] Export JSON terminé.\n";
		}
	} catch (const paloexport::PaloHttpError &e) {
		std::cerr << "paloexport: " << e.what() << "\n";
		return 1;
	} catch (const std::exception &e) {
		std::cerr << "paloexport: " << e.what() << "\n";
		return 1;
	}

	return 0;
}
