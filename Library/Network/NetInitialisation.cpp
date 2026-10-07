/* 
 *
 * Copyright (C) 2006-2014 Jedox AG
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * \author Florian Schaper <florian.schaper@jedox.com>
 * 
 *
 */

#if defined(WIN32) || defined(WIN64)
#include <WinSock2.h>
#endif

#include <mutex>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "NetInitialisation.h"
#include "../Exceptions/ErrorException.h"

namespace palo {

namespace {

std::mutex g_sslClientInitMutex;

void initOpenSslStrings()
{
	OPENSSL_init_ssl(
		OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
		NULL);
}

} // namespace

NetInitialisation& NetInitialisation::instance()
{
	static NetInitialisation obj;
	return obj;
}

NetInitialisation::NetInitialisation()
{
#if defined(WIN32) || defined(WIN64)
	WSADATA wsaData;
	WSAStartup(0x101, &wsaData);
#endif
	ctx = NULL;
}

void NetInitialisation::initSSL(std::string trustFile)
{
	std::lock_guard<std::mutex> lock(g_sslClientInitMutex);
	if (ctx != NULL) {
		return;
	}

	initOpenSslStrings();

	SSL_CTX* sslctx = SSL_CTX_new(TLS_client_method());
	if (sslctx == NULL) {
		throw ErrorException(ErrorException::ERROR_INTERNAL, "cannot create TLS client context");
	}

	SSL_CTX_set_min_proto_version(sslctx, TLS1_2_VERSION);
	SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, NULL);

	if (!trustFile.empty()) {
		if (!SSL_CTX_load_verify_locations(sslctx, trustFile.c_str(), NULL)) {
			SSL_CTX_free(sslctx);
			throw ErrorException(ErrorException::ERROR_INTERNAL, "bad certificate trust file");
		}
	} else if (!SSL_CTX_set_default_verify_paths(sslctx)) {
		SSL_CTX_free(sslctx);
		throw ErrorException(ErrorException::ERROR_INTERNAL, "cannot load default CA paths");
	}

	ctx = sslctx;
}

void* NetInitialisation::getSslContext()
{
	if (ctx == NULL) {
		initSSL(std::string());
	}
	return ctx;
}

NetInitialisation::~NetInitialisation()
{
	if (ctx != NULL) {
		SSL_CTX_free((SSL_CTX*)ctx);
		ctx = NULL;
	}

#if defined(WIN32) || defined(WIN64)
	WSACleanup();
#endif
}

} /* palo */
