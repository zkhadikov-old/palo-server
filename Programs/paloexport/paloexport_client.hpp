/*
 * Client HTTP minimal pour l'API Palo (CSV) — utilisé par paloexport.
 */
#ifndef PALOEXPORT_CLIENT_HPP
#define PALOEXPORT_CLIENT_HPP

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace paloexport {

struct PaloHttpError : public std::runtime_error {
	explicit PaloHttpError(const std::string &m) : std::runtime_error(m) {}
};

/** Une ligne du corps CSV Palo (champs déjà découpés). */
using PaloCsvRow = std::vector<std::string>;

/** Parse le corps d'une réponse Palo (lignes non vides). */
std::vector<PaloCsvRow> parse_palo_csv_body(const std::string &text);

/** Première ligne d'erreur Palo : code;…;message */
void parse_palo_error(const std::string &text, int &out_code, std::string &out_message);

/** MD5 du mot de passe en hexadécimal (32 caractères), comme /server/login. */
std::string md5_hex_password(const std::string &utf8_password);

/**
 * Client : session sid + en-têtes X-PALO-* renvoyés par le serveur (comportement type navigateur).
 */
class PaloClient {
public:
	explicit PaloClient(std::string base_url);

	/** Si vrai, journalise chaque requête sur stderr (URL sans mot de passe en clair). */
	void set_verbose(bool v) { verbose_ = v; }

	/** Délai max lecture/écriture socket (secondes), défaut 120. Évite un blocage infini si le serveur ne répond pas. */
	void set_io_timeout(int sec) { io_timeout_sec_ = sec > 0 ? sec : 120; }
	int io_timeout_sec() const { return io_timeout_sec_; }

	/** Ligne de trace verbose (no-op si verbose désactivé). */
	void trace(const std::string &line) const;

	/** Connexion ; lève PaloHttpError si échec. */
	void login(const std::string &user, const std::string &password_plain);

	/** GET chemin commençant par / ; params sans sid (ajouté automatiquement). */
	std::string get(const std::string &path, const std::map<std::string, std::string> &params = {});

	/**
	 * Oublie le jeton dimension courant avant un GET sur une autre dimension.
	 * Les en-têtes X-PALO-DIM sont par dimension ; un seul champ côté client ne suffit pas
	 * pour en enchaîner plusieurs sans erreur « dimension token outdated ».
	 */
	void clear_dimension_token() { token_dim_.clear(); }

	/**
	 * Oublie le jeton base (X-PALO-DB). À utiliser après des écritures qui invalident le jeton
	 * sans le renvoyer dans la réponse (ex. création de dimension) — sinon « database token outdated ».
	 */
	void clear_database_token() { token_db_.clear(); }

	const std::string &sid() const { return sid_; }

private:
	std::string base_url_;
	bool verbose_ = false;
	int io_timeout_sec_ = 120;
	std::string sid_;
	std::string token_sv_;
	std::string token_db_;
	std::string token_dim_;
	std::string token_cube_;
	std::string token_cc_;

	void merge_response_headers(const std::string &headers_block);
	std::string build_url(const std::string &path, const std::map<std::string, std::string> &params) const;
	std::string request_get(const std::string &url);
	/** Verbose : affiche statut HTTP + corps API (texte Palo). */
	void trace_api_body(long http_status, const std::string &body) const;
};

} // namespace paloexport

#endif
