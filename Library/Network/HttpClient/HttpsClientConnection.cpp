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
 * \author Marko Stijak <mstijak@gmail.com>
 * 
 *
 */

#if defined(WIN32) || defined(WIN64)
#   pragma warning( push )
#   pragma warning( disable : 4996 )
#endif

#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>

#if defined(WIN32) || defined(WIN64)
#   pragma warning( pop )
#endif

#include <string>

using boost::iostreams::bidirectional;
using boost::iostreams::device;

#include "../NetInitialisation.h"

#include "HttpsClientConnection.h"
#include "Url.h"

namespace palo {

namespace {

static std::string sslErrorString()
{
	char buf[256];
	unsigned long e = ERR_get_error();
	if (e == 0) {
		return std::string("unknown SSL error");
	}
	ERR_error_string_n(e, buf, sizeof(buf));
	return std::string(buf);
}

} // namespace

class SslDevice : public device<bidirectional> {
	BIO* bio;
public:
	SslDevice(BIO* bio) :
		bio(bio)
	{
	}
	;

	std::streamsize read(char *s, std::streamsize n)
	{
		return BIO_read(bio, s, (UINT)n);
	}
	;

	std::streamsize write(const char *s, std::streamsize n)
	{
		return BIO_write(bio, s, (UINT)n);
	}

	void close(std::ios_base::openmode mode)
	{

	}

	bool is_open() const
	{
		return true;
	}
	;
};

HttpsClientConnection::HttpsClientConnection(const std::string& hostname, unsigned int port) :
	m_Hostname(hostname),
	m_Bio(NULL),
	m_Ssl(NULL),
	m_Port(port),
	m_RequestCount(0),
	m_hasValidCertificate(false)
{
	m_Bio = BIO_new_ssl_connect((SSL_CTX*)NetInitialisation::instance().getSslContext());
	if (m_Bio == NULL) {
		throw SslException(std::string("SSL: cannot allocate connect BIO (").append(sslErrorString()).append(")"));
	}

	if (BIO_get_ssl(m_Bio, &m_Ssl) != 1 || m_Ssl == NULL) {
		BIO_free_all(m_Bio);
		m_Bio = NULL;
		throw SslException(std::string("SSL: BIO_get_ssl failed (").append(sslErrorString()).append(")"));
	}

	SSL_set_mode(m_Ssl, SSL_MODE_AUTO_RETRY);
	SSL_set_tlsext_host_name(m_Ssl, hostname.c_str());

	const std::string hostport = hostname + ":" + lexicalConversion(std::string, unsigned int, port);
	BIO_set_conn_hostname(m_Bio, hostport.c_str());

	if (BIO_do_connect(m_Bio) <= 0) {
		BIO_free_all(m_Bio);
		m_Bio = NULL;
		m_Ssl = NULL;
		throw SslException(std::string("SSL connection failed (TCP/TLS) (").append(sslErrorString()).append(")"));
	}

	m_hasValidCertificate = (SSL_get_verify_result(m_Ssl) == X509_V_OK);
	if (m_hasValidCertificate) {
		X509* cert = SSL_get1_peer_certificate(m_Ssl);
		if (cert == NULL) {
			m_hasValidCertificate = false;
		} else {
			if (X509_check_host(cert, hostname.c_str(), hostname.length(), 0, NULL) != 1) {
				m_hasValidCertificate = false;
			}
			X509_free(cert);
		}
	}
}

HttpsClientConnection::~HttpsClientConnection()
{
	if (m_Bio != NULL) {
		BIO_free_all(m_Bio);
		m_Bio = NULL;
	}
	m_Ssl = NULL;
}

void HttpsClientConnection::reset()
{
	if (m_Ssl != NULL) {
		SSL_shutdown(m_Ssl);
	}
}

const std::string& HttpsClientConnection::getHostname() const
{
	return m_Hostname;
}

bool HttpsClientConnection::hasValidCertificate() const
{
	return m_hasValidCertificate;
}

unsigned int HttpsClientConnection::getPort() const
{
	return m_Port;
}

void HttpsClientConnection::incrementRequestCount()
{
	m_RequestCount++;
}

unsigned int HttpsClientConnection::getRequestCount() const
{
	return m_RequestCount;
}

boost::shared_ptr<std::iostream> HttpsClientConnection::getStream()
{
	return boost::shared_ptr<std::iostream>(new boost::iostreams::stream<SslDevice>(SslDevice(m_Bio)));
}
} /* palo */
