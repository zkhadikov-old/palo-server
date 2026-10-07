/*
 * Client HTTP(S) Palo — sockets + OpenSSL (pas de libcurl).
 */

#include "paloexport_client.hpp"

#include <openssl/md5.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <netdb.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace paloexport {

namespace {

std::string to_lower(std::string s) {
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

/** Masque password=… dans une URL pour les logs. */
std::string sanitize_url_for_log(const std::string &url) {
	std::string out = url;
	const std::string key = "password=";
	size_t p = 0;
	while ((p = out.find(key, p)) != std::string::npos) {
		size_t val_start = p + key.size();
		size_t val_end = out.find('&', val_start);
		if (val_end == std::string::npos) val_end = out.size();
		out.replace(val_start, val_end - val_start, "(md5)");
		p = val_start + 5;
	}
	return out;
}

/** Encodage query form (espace -> %20 pour compatibilité stricte). */
std::string url_encode_query(const std::string &s) {
	static const char *hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(s.size() + 8);
	for (unsigned char c : s) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xf];
		}
	}
	return out;
}

struct ParsedUrl {
	std::string scheme;
	std::string host;
	std::string port;
	std::string path_query;
};

bool parse_http_url(const std::string &url, ParsedUrl &out) {
	out = {};
	if (url.rfind("http://", 0) == 0) {
		out.scheme = "http";
	} else if (url.rfind("https://", 0) == 0) {
		out.scheme = "https";
	} else {
		return false;
	}
	size_t start = out.scheme == "http" ? 7 : 8;
	size_t path_start = url.find('/', start);
	if (path_start == std::string::npos) {
		std::string hostport = url.substr(start);
		out.path_query = "/";
		size_t colon = hostport.rfind(':');
		if (colon != std::string::npos && hostport.find(':') == colon) {
			out.host = hostport.substr(0, colon);
			out.port = hostport.substr(colon + 1);
		} else {
			out.host = hostport;
			out.port = out.scheme == "https" ? "443" : "80";
		}
		return true;
	}
	std::string hostport = url.substr(start, path_start - start);
	out.path_query = url.substr(path_start);
	size_t colon = hostport.rfind(':');
	if (colon != std::string::npos && hostport.find(':') == colon) {
		out.host = hostport.substr(0, colon);
		out.port = hostport.substr(colon + 1);
	} else {
		out.host = hostport;
		out.port = out.scheme == "https" ? "443" : "80";
	}
	return true;
}

static void set_socket_timeouts(int fd, int sec) {
	if (sec <= 0 || fd < 0) return;
	struct timeval tv {};
	tv.tv_sec = sec;
	tv.tv_usec = 0;
	(void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	(void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int tcp_connect(const std::string &host, const std::string &port) {
	struct addrinfo hints {};
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = AF_UNSPEC;
	struct addrinfo *res = nullptr;
	int gai = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
	if (gai != 0 || !res) {
		throw PaloHttpError(std::string("getaddrinfo: ") + gai_strerror(gai));
	}
	int fd = -1;
	for (struct addrinfo *p = res; p; p = p->ai_next) {
		fd = static_cast<int>(socket(p->ai_family, p->ai_socktype, p->ai_protocol));
		if (fd < 0) continue;
		if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
		::close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) throw PaloHttpError("connexion TCP échouée vers " + host + ":" + port);
	return fd;
}

/** Première ligne : "HTTP/1.x 200" → 200 */
static long parse_http_status_from_headers(const std::string &headers_block) {
	std::istringstream hl(headers_block);
	std::string status_line;
	if (!std::getline(hl, status_line)) return 0;
	if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();
	size_t sp = status_line.find(' ');
	if (sp == std::string::npos) return 0;
	size_t sp2 = status_line.find(' ', sp + 1);
	try {
		return std::stol(status_line.substr(sp + 1, sp2 != std::string::npos ? sp2 - sp - 1 : std::string::npos));
	} catch (...) {
		return 0;
	}
}

/** Limite taille des messages d’erreur / verbose (évite d’inonder le terminal). */
static std::string truncate_for_log(const std::string &s, size_t max_bytes) {
	if (s.size() <= max_bytes) return s;
	return s.substr(0, max_bytes) + "\n… [tronqué, " + std::to_string(s.size()) + " octets au total]";
}

bool header_value_icase(const std::string &headers, const char *name, std::string &value_out) {
	std::istringstream in(headers);
	std::string line;
	const std::string name_l = to_lower(name);
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		auto colon = line.find(':');
		if (colon == std::string::npos) continue;
		if (to_lower(line.substr(0, colon)) != name_l) continue;
		value_out = line.substr(colon + 1);
		while (!value_out.empty() && (value_out[0] == ' ' || value_out[0] == '\t')) value_out.erase(0, 1);
		return true;
	}
	return false;
}

/** GET : réponse complète → headers (sans ligne vide finale) + corps. */
void http_get_raw(const ParsedUrl &pu, const std::string &extra_headers, std::string &out_headers_block,
		  std::string &out_body, const PaloClient *client, int timeout_sec) {
	auto tr = [client](const std::string &s) {
		if (client) client->trace(s);
	};

	const std::string req_path = pu.path_query.empty() ? "/" : pu.path_query;
	std::ostringstream req;
	req << "GET " << req_path << " HTTP/1.1\r\n";
	req << "Host: " << pu.host;
	if ((pu.scheme == "http" && pu.port != "80") || (pu.scheme == "https" && pu.port != "443")) {
		req << ":" << pu.port;
	}
	req << "\r\n";
	req << "Connection: close\r\n";
	req << "User-Agent: paloexport\r\n";
	if (!extra_headers.empty()) req << extra_headers;
	req << "\r\n";
	const std::string req_str = req.str();

	tr("TCP : connexion à " + pu.host + ":" + pu.port + " …");
	int fd = tcp_connect(pu.host, pu.port);
	tr("TCP : connecté.");
	set_socket_timeouts(fd, timeout_sec);

	SSL_CTX *ctx = nullptr;
	SSL *ssl = nullptr;
	if (pu.scheme == "https") {
		static bool ssl_inited = false;
		if (!ssl_inited) {
			SSL_library_init();
			SSL_load_error_strings();
			OpenSSL_add_all_algorithms();
			ssl_inited = true;
		}
		ctx = SSL_CTX_new(TLS_client_method());
		if (!ctx) {
			::close(fd);
			throw PaloHttpError("SSL_CTX_new failed");
		}
		ssl = SSL_new(ctx);
		if (!ssl) {
			SSL_CTX_free(ctx);
			::close(fd);
			throw PaloHttpError("SSL_new failed");
		}
		SSL_set_tlsext_host_name(ssl, pu.host.c_str());
		SSL_set_fd(ssl, fd);
		if (SSL_connect(ssl) != 1) {
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			::close(fd);
			throw PaloHttpError("SSL_connect failed");
		}
		tr("TLS : connecté, envoi de la requête (" + std::to_string(req_str.size()) + " octets)…");
		int nw = SSL_write(ssl, req_str.data(), static_cast<int>(req_str.size()));
		if (nw != static_cast<int>(req_str.size())) {
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			::close(fd);
			throw PaloHttpError("SSL_write incomplet");
		}
	} else {
		tr("Envoi de la requête HTTP (" + std::to_string(req_str.size()) + " octets)…");
		ssize_t nw = ::send(fd, req_str.data(), req_str.size(), 0);
		if (nw != static_cast<ssize_t>(req_str.size())) {
			::close(fd);
			throw PaloHttpError("send incomplet");
		}
	}

	tr("Lecture de la réponse (timeout " + std::to_string(timeout_sec) + " s)…");
	std::string buf;
	char tmp[8192];
	const std::string scheme_hint = pu.scheme == "https" ? "HTTPS" : "HTTP";

	/*
	 * Ne pas attendre la fermeture TCP : en HTTP/1.1 le serveur peut garder la connexion
	 * (keep-alive). On s’arrête dès que Content-Length octets de corps sont reçus.
	 */
	bool have_complete_by_length = false;
	out_headers_block.clear();
	out_body.clear();

	while (true) {
		int n = ssl ? SSL_read(ssl, tmp, sizeof(tmp)) : static_cast<int>(::recv(fd, tmp, sizeof(tmp), 0));
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				if (ssl) {
					SSL_free(ssl);
					SSL_CTX_free(ctx);
				}
				::close(fd);
				throw PaloHttpError(
				    "délai dépassé en lecture (" + std::to_string(timeout_sec) +
				    " s). Aucune donnée reçue — Palo tourne-t-il ? Port " + pu.port +
				    " en " + scheme_hint + " ? (sinon essayez --https ou -P autre port)");
			}
			if (ssl) {
				int err = SSL_get_error(ssl, n);
				SSL_free(ssl);
				SSL_CTX_free(ctx);
				ssl = nullptr;
				ctx = nullptr;
				::close(fd);
				throw PaloHttpError("SSL_read a échoué (code OpenSSL " + std::to_string(err) + ")");
			}
			::close(fd);
			throw PaloHttpError(std::string("recv: ") + std::strerror(errno));
		}
		if (n == 0) break;
		buf.append(tmp, static_cast<size_t>(n));

		size_t sep = buf.find("\r\n\r\n");
		if (sep == std::string::npos) continue;

		std::string hdrs = buf.substr(0, sep);
		std::string cl_s;
		long content_length = -1;
		if (header_value_icase(hdrs, "Content-Length", cl_s)) {
			try {
				content_length = std::stol(cl_s);
			} catch (...) {
			}
		}
		const size_t body_off = sep + 4;
		const size_t body_have = buf.size() - body_off;
		if (content_length >= 0 && body_have >= static_cast<size_t>(content_length)) {
			out_headers_block = std::move(hdrs);
			out_body = buf.substr(body_off, static_cast<size_t>(content_length));
			have_complete_by_length = true;
			tr("Réponse complète (Content-Length=" + std::to_string(content_length) + "), " +
			   std::to_string(buf.size()) + " octets reçus.");
			break;
		}
	}

	if (!have_complete_by_length) {
		size_t sep = buf.find("\r\n\r\n");
		if (sep == std::string::npos) {
			throw PaloHttpError(
			    std::string("réponse HTTP invalide (pas de fin d’en-têtes). Octets reçus : ") +
			    std::to_string(buf.size()) + "\n--- brut reçu (début) ---\n" + truncate_for_log(buf, 8000));
		}
		out_headers_block = buf.substr(0, sep);
		out_body = buf.substr(sep + 4);
		tr("Réponse reçue jusqu’à fermeture TCP (" + std::to_string(buf.size()) + " octets).");
	}

	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
		SSL_CTX_free(ctx);
	}
	::close(fd);

	long http_status = parse_http_status_from_headers(out_headers_block);

	if (http_status < 200 || http_status >= 300) {
		int code = 0;
		std::string msg;
		parse_palo_error(out_body, code, msg);
		std::string err = msg + " (HTTP " + std::to_string(http_status) + ")";
		err += "\n--- corps brut renvoyé par l’API ---\n";
		err += truncate_for_log(out_body, 16384);
		throw PaloHttpError(err);
	}
}

} // namespace

std::vector<PaloCsvRow> parse_palo_csv_body(const std::string &text) {
	std::vector<PaloCsvRow> rows;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || (line.size() == 1 && line[0] == '\r')) continue;
		if (!line.empty() && line.back() == '\r') line.pop_back();

		PaloCsvRow fields;
		size_t i = 0;
		const size_t len = line.size();
		while (i < len) {
			if (line[i] == '"') {
				++i;
				std::string s;
				while (i < len) {
					if (i + 1 < len && line[i] == '"' && line[i + 1] == '"') {
						s += '"';
						i += 2;
						continue;
					}
					if (line[i] == '"') {
						++i;
						break;
					}
					s += line[i];
					++i;
				}
				fields.push_back(std::move(s));
				if (i < len && line[i] == ';') ++i;
				continue;
			}
			size_t start = i;
			while (i < len && line[i] != ';') ++i;
			fields.emplace_back(line.substr(start, i - start));
			if (i < len && line[i] == ';') ++i;
		}
		rows.push_back(std::move(fields));
	}
	return rows;
}

void parse_palo_error(const std::string &text, int &out_code, std::string &out_message) {
	auto rows = parse_palo_csv_body(text);
	if (rows.empty()) {
		out_code = -1;
		out_message = text.empty() ? "erreur inconnue" : text;
		return;
	}
	const auto &f = rows[0];
	out_code = -1;
	if (!f.empty()) {
		try {
			out_code = std::stoi(f[0]);
		} catch (...) {
		}
	}
	out_message = f.size() > 2 ? f[2] : (f.size() > 1 ? f[1] : text);
}

std::string md5_hex_password(const std::string &utf8_password) {
	unsigned char digest[MD5_DIGEST_LENGTH];
	MD5(reinterpret_cast<const unsigned char *>(utf8_password.data()), utf8_password.size(), digest);
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');
	for (unsigned char b : digest) oss << std::setw(2) << static_cast<int>(b);
	return oss.str();
}

PaloClient::PaloClient(std::string base_url) : base_url_(std::move(base_url)) {
	while (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
}

void PaloClient::trace(const std::string &line) const {
	if (!verbose_) return;
	std::cerr << "[paloexport] " << line << '\n';
}

void PaloClient::trace_api_body(long http_status, const std::string &body) const {
	if (!verbose_) return;
	std::cerr << "[paloexport] ← HTTP " << http_status << " — corps API (" << body.size() << " octets)\n";
	std::cerr << truncate_for_log(body, 12288) << "\n";
}

void PaloClient::login(const std::string &user, const std::string &password_plain) {
	sid_.clear();
	token_sv_.clear();
	token_db_.clear();
	token_dim_.clear();
	token_cube_.clear();
	token_cc_.clear();

	trace("Connexion /server/login (mot de passe transmis en MD5, voir ligne GET ci-dessous)…");
	std::map<std::string, std::string> p;
	p["user"] = user;
	p["password"] = md5_hex_password(password_plain);
	const std::string url = build_url("/server/login", p);
	const std::string raw = request_get(url);
	auto rows = parse_palo_csv_body(raw);
	if (rows.empty()) throw PaloHttpError("réponse /server/login vide");
	sid_ = rows[0][0];
	if (verbose_) {
		std::string sid_show = sid_.size() > 16 ? sid_.substr(0, 16) + "…" : sid_;
		trace("Session établie, sid=" + sid_show);
	}
}

std::string PaloClient::build_url(const std::string &path, const std::map<std::string, std::string> &params) const {
	std::string q;
	if (!sid_.empty()) q = "sid=" + url_encode_query(sid_);
	for (const auto &kv : params) {
		if (!q.empty()) q += '&';
		q += kv.first;
		q += '=';
		q += url_encode_query(kv.second);
	}

	std::string u = base_url_;
	if (!path.empty() && path[0] != '/') u += '/';
	u += path;
	if (!q.empty()) {
		u += '?';
		u += q;
	}
	return u;
}

void PaloClient::merge_response_headers(const std::string &headers_block) {
	std::istringstream in(headers_block);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		auto colon = line.find(':');
		if (colon == std::string::npos) continue;
		std::string name = to_lower(line.substr(0, colon));
		std::string value = line.substr(colon + 1);
		while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
		if (name == "x-palo-sv") token_sv_ = value;
		else if (name == "x-palo-db") token_db_ = value;
		else if (name == "x-palo-dim") token_dim_ = value;
		else if (name == "x-palo-cb") token_cube_ = value;
		else if (name == "x-palo-cc") token_cc_ = value;
	}
}

std::string PaloClient::request_get(const std::string &url) {
	ParsedUrl pu;
	if (!parse_http_url(url, pu)) throw PaloHttpError("URL invalide (http:// ou https:// attendu)");

	trace(std::string("GET ") + sanitize_url_for_log(url));

	std::ostringstream eh;
	if (!token_sv_.empty()) eh << "X-PALO-SV: " << token_sv_ << "\r\n";
	if (!token_db_.empty()) eh << "X-PALO-DB: " << token_db_ << "\r\n";
	if (!token_dim_.empty()) eh << "X-PALO-DIM: " << token_dim_ << "\r\n";
	if (!token_cube_.empty()) eh << "X-PALO-CB: " << token_cube_ << "\r\n";
	if (!token_cc_.empty()) eh << "X-PALO-CC: " << token_cc_ << "\r\n";

	std::string headers_block;
	std::string body;
	http_get_raw(pu, eh.str(), headers_block, body, this, io_timeout_sec_);
	merge_response_headers(headers_block);
	long st = parse_http_status_from_headers(headers_block);
	trace_api_body(st, body);
	return body;
}

std::string PaloClient::get(const std::string &path, const std::map<std::string, std::string> &params) {
	const std::string url = build_url(path, params);
	return request_get(url);
}

} // namespace paloexport
