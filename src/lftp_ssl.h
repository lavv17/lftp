/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LFTP_SSL_H
#define LFTP_SSL_H

#if USE_SSL
# if USE_GNUTLS
#  include <gnutls/gnutls.h>
# elif USE_OPENSSL
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <openssl/rand.h>
#  include <openssl/x509v3.h>
#  include <openssl/x509_vfy.h>
# endif

#include "Ref.h"
#include "xstring.h"

class lftp_ssl_base
{
public:
   bool handshake_done;
   int fd;
   xstring_c hostname;
   enum handshake_mode_t { CLIENT, SERVER } handshake_mode;
   xstring error;
   bool fatal;
   bool cert_error;

   lftp_ssl_base(int fd,handshake_mode_t m,const char *host=0);

   enum code { RETRY=-2, ERROR=-1, DONE=0 };

   void set_error(const char *s1,const char *s2);
   void set_cert_error(const char *s,const xstring& fp);
};

#if USE_GNUTLS

#include <gnutls/x509.h>

#if LFTP_LIBGNUTLS_VERSION_CODE < 0x010201
/* Compatibility defintions for old gnutls */
typedef gnutls_session gnutls_session_t;
typedef gnutls_anon_server_credentials gnutls_anon_server_credentials_t;
typedef gnutls_dh_params gnutls_dh_params_t;
typedef gnutls_certificate_credentials gnutls_certificate_credentials_t;
typedef gnutls_transport_ptr gnutls_transport_ptr_t;
typedef gnutls_x509_crt gnutls_x509_crt_t;
typedef gnutls_x509_crl gnutls_x509_crl_t;
typedef gnutls_x509_crt_fmt gnutls_x509_crt_fmt_t;
typedef gnutls_datum gnutls_datum_t;
#endif

#include "ResMgr.h"
class lftp_ssl_gnutls_instance : public ResClient
{
   gnutls_x509_crl_t *crl_list;
   unsigned crl_list_size;
   gnutls_x509_crt_t *ca_list;
   unsigned ca_list_size;
   friend class lftp_ssl_gnutls;

   void LoadCA();
   void LoadCRL();
public:
   lftp_ssl_gnutls_instance();
   ~lftp_ssl_gnutls_instance();
   void Reconfig(const char *);
};
class lftp_ssl_gnutls : public lftp_ssl_base
{
   static Ref<lftp_ssl_gnutls_instance> instance;
   gnutls_session_t session;
   gnutls_certificate_credentials_t cred;
   void verify_certificate_chain(const gnutls_datum_t *cert_chain,int cert_chain_length);
   void verify_cert2(gnutls_x509_crt_t crt,gnutls_x509_crt_t issuer);
   void verify_last_cert(gnutls_x509_crt_t crt);
   int do_handshake();
   bool check_fatal(int res);
   static const xstring& get_fp(gnutls_x509_crt_t crt);
public:
   static void global_init();
   static void global_deinit();

   lftp_ssl_gnutls(int fd,handshake_mode_t m,const char *host=0);
   ~lftp_ssl_gnutls();

   int read(char *buf,int size);
   int write(const char *buf,int size);
   bool want_in();
   bool want_out();
   void copy_sid(const lftp_ssl_gnutls *);
   void load_keys();
   void shutdown();
};
typedef lftp_ssl_gnutls lftp_ssl;
#elif USE_OPENSSL
class lftp_ssl_openssl_instance {
public:
   SSL_CTX *ssl_ctx;
   X509_STORE *crl_store;
   lftp_ssl_openssl_instance();
   ~lftp_ssl_openssl_instance();
};
class lftp_ssl_openssl : public lftp_ssl_base
{
   static Ref<lftp_ssl_openssl_instance> instance;
   SSL *ssl;
   bool check_fatal(int res);
   int do_handshake();
   const char *strerror();
   static const xstring& get_fp(X509 *crt);
public:
   static int verify_crl(X509_STORE_CTX *ctx);
   static int verify_callback(int ok,X509_STORE_CTX *ctx);
   void check_certificate();

   static void global_init();
   static void global_deinit();

   lftp_ssl_openssl(int fd,handshake_mode_t m,const char *host=0);
   ~lftp_ssl_openssl();

   int read(char *buf,int size);
   int write(const char *buf,int size);
   bool want_in();
   bool want_out();
   void copy_sid(const lftp_ssl_openssl *);
   void load_keys();
   void shutdown();
};
typedef lftp_ssl_openssl lftp_ssl;
#endif

#endif//USE_SSL

#endif//LFTP_SSL_H
