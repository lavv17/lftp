/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>

#ifdef USE_SSL
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include "lftp_ssl.h"
#include "xmalloc.h"
#include "ResMgr.h"
#include "log.h"

static int lftp_ssl_verify_callback(int ok,X509_STORE_CTX *ctx);
static int lftp_ssl_verify_crl(X509_STORE_CTX *ctx);

SSL_CTX *ssl_ctx;
X509_STORE *crl_store;

static char file[256];

static void lftp_ssl_write_rnd()
{
   RAND_write_file(file);
}

void lftp_ssl_init()
{
   static bool inited=false;
   if(inited) return;
   inited=true;

#ifdef WINDOWS
   RAND_screen();
#endif

   RAND_file_name(file,sizeof(file));
   if(RAND_egd(file)>0)
      return;

   if(RAND_load_file(file,-1) && RAND_status()!=0)
      atexit(lftp_ssl_write_rnd);
}

void lftp_ssl_ctx_init()
{
   if(ssl_ctx) return;

#if SSLEAY_VERSION_NUMBER < 0x0800
   ssl_ctx=SSL_CTX_new();
   X509_set_default_verify_paths(ssl_ctx->cert);
#else
   SSLeay_add_ssl_algorithms();
   ssl_ctx=SSL_CTX_new(SSLv23_client_method());
   SSL_CTX_set_options(ssl_ctx, SSL_OP_ALL);
   SSL_CTX_set_verify(ssl_ctx,SSL_VERIFY_PEER,lftp_ssl_verify_callback);

   const char *ca_file=ResMgr::Query("ssl:ca-file",0);
   const char *ca_path=ResMgr::Query("ssl:ca-path",0);
   if(ca_file && !*ca_file)
      ca_file=0;
   if(ca_path && !*ca_path)
      ca_path=0;
   if(ca_file || ca_path)
   {
      if(!SSL_CTX_load_verify_locations(ssl_ctx,ca_file,ca_path))
      {
	 fprintf(stderr,"WARNING: SSL_CTX_load_verify_locations(%s,%s) failed\n",
	    ca_file?ca_file:"NULL",
	    ca_path?ca_path:"NULL");
	 SSL_CTX_set_default_verify_paths(ssl_ctx);
      }
   }
   else
   {
      SSL_CTX_set_default_verify_paths(ssl_ctx);
   }

   const char *crl_file=ResMgr::Query("ssl:crl-file",0);
   const char *crl_path=ResMgr::Query("ssl:crl-path",0);
   if(crl_file && !*crl_file)
      crl_file=0;
   if(crl_path && !*crl_path)
      crl_path=0;
   if(crl_file || crl_path)
   {
      crl_store=X509_STORE_new();
      if(!X509_STORE_load_locations(crl_store,crl_file,crl_path))
      {
	 fprintf(stderr,"WARNING: X509_STORE_load_locations(%s,%s) failed\n",
	    crl_file?crl_file:"NULL",
	    crl_path?crl_path:"NULL");
      }
   }
#endif /* SSLEAY_VERSION_NUMBER < 0x0800 */
}

SSL *lftp_ssl_new(int fd,const char *host)
{
   lftp_ssl_init();
   lftp_ssl_ctx_init();
   SSL *ssl=SSL_new(ssl_ctx);
   SSL_set_fd(ssl,fd);
   SSL_ctrl(ssl,SSL_CTRL_MODE,SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,0);

   const char *key_file =ResMgr::Query("ssl:key-file",host);
   const char *cert_file=ResMgr::Query("ssl:cert-file",host);
   if(key_file && !*key_file)
      key_file=0;
   if(cert_file && !*cert_file)
      cert_file=0;

   if(cert_file)
   {
      if(!key_file)
	 key_file=cert_file;
      if(SSL_use_certificate_file(ssl,cert_file,SSL_FILETYPE_PEM)<=0)
      {
	 // FIXME
      }
      if(SSL_use_PrivateKey_file(ssl,key_file,SSL_FILETYPE_PEM)<=0)
      {
	 // FIXME
      }
      if(!SSL_check_private_key(ssl))
      {
	 // FIXME
      }
   }

   return ssl;
}

static int certificate_verify_error;

const char *lftp_ssl_strerror(const char *s)
{
   SSL_load_error_strings();
   int error=ERR_get_error();
   const char *ssl_error=0;
   if(ERR_GET_LIB(error)==ERR_LIB_SSL
   && ERR_GET_REASON(error)==SSL_R_CERTIFICATE_VERIFY_FAILED)
      ssl_error=X509_verify_cert_error_string(certificate_verify_error);
   else if(ERR_GET_LIB(error)==ERR_LIB_SSL)
      ssl_error=ERR_reason_error_string(error);
   else
      ssl_error=ERR_error_string(error,NULL);
   if(!ssl_error)
      ssl_error="error";
   static char *buffer;
   static int buffer_alloc;
   int need=xstrlen(s)+2+xstrlen(ssl_error)+1;
   if(buffer_alloc<need)
      buffer=(char*)xrealloc(buffer,buffer_alloc=need);
   if(s)
   {
      strcpy(buffer,s);
      strcat(buffer,": ");
      strcat(buffer,ssl_error);
   }
   else
      strcpy(buffer,ssl_error);
   return buffer;
}

/* This one is (very much!) based on work by Ralf S. Engelschall <rse@engelschall.com>.
 * Comments by Ralf. */
int lftp_ssl_verify_crl(X509_STORE_CTX *ctx)
{
    X509_OBJECT obj;
    X509_NAME *subject;
    X509_NAME *issuer;
    X509 *xs;
    X509_CRL *crl;
    X509_REVOKED *revoked;
    X509_STORE_CTX store_ctx;
    long serial;
    int i, n, rc;
    char *cp;

    /*
     * Unless a revocation store for CRLs was created we
     * cannot do any CRL-based verification, of course.
     */
    if (!crl_store)
        return 1;

    /*
     * Determine certificate ingredients in advance
     */
    xs      = X509_STORE_CTX_get_current_cert(ctx);
    subject = X509_get_subject_name(xs);
    issuer  = X509_get_issuer_name(xs);

    /*
     * OpenSSL provides the general mechanism to deal with CRLs but does not
     * use them automatically when verifying certificates, so we do it
     * explicitly here. We will check the CRL for the currently checked
     * certificate, if there is such a CRL in the store.
     *
     * We come through this procedure for each certificate in the certificate
     * chain, starting with the root-CA's certificate. At each step we've to
     * both verify the signature on the CRL (to make sure it's a valid CRL)
     * and it's revocation list (to make sure the current certificate isn't
     * revoked).  But because to check the signature on the CRL we need the
     * public key of the issuing CA certificate (which was already processed
     * one round before), we've a little problem. But we can both solve it and
     * at the same time optimize the processing by using the following
     * verification scheme (idea and code snippets borrowed from the GLOBUS
     * project):
     *
     * 1. We'll check the signature of a CRL in each step when we find a CRL
     *    through the _subject_ name of the current certificate. This CRL
     *    itself will be needed the first time in the next round, of course.
     *    But we do the signature processing one round before this where the
     *    public key of the CA is available.
     *
     * 2. We'll check the revocation list of a CRL in each step when
     *    we find a CRL through the _issuer_ name of the current certificate.
     *    This CRLs signature was then already verified one round before.
     *
     * This verification scheme allows a CA to revoke its own certificate as
     * well, of course.
     */

    /*
     * Try to retrieve a CRL corresponding to the _subject_ of
     * the current certificate in order to verify it's integrity.
     */
    memset((char *)&obj, 0, sizeof(obj));
    X509_STORE_CTX_init(&store_ctx, crl_store, NULL, NULL);
    rc = X509_STORE_get_by_subject(&store_ctx, X509_LU_CRL, subject, &obj);
    X509_STORE_CTX_cleanup(&store_ctx);
    crl = obj.data.crl;
    if (rc > 0 && crl != NULL) {
        /*
         * Verify the signature on this CRL
         */
        if (X509_CRL_verify(crl, X509_get_pubkey(xs)) <= 0) {
            Log::global->Format(0,"Invalid signature on CRL!\n");
            X509_STORE_CTX_set_error(ctx, X509_V_ERR_CRL_SIGNATURE_FAILURE);
            X509_OBJECT_free_contents(&obj);
            return 0;
        }

        /*
         * Check date of CRL to make sure it's not expired
         */
        i = X509_cmp_current_time(X509_CRL_get_nextUpdate(crl));
        if (i == 0) {
            Log::global->Format(0,"Found CRL has invalid nextUpdate field.\n");
            X509_STORE_CTX_set_error(ctx, X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD);
            X509_OBJECT_free_contents(&obj);
            return 0;
        }
        if (i < 0) {
            Log::global->Format(0,"Found CRL is expired - revoking all certificates until you get updated CRL.\n");
            X509_STORE_CTX_set_error(ctx, X509_V_ERR_CRL_HAS_EXPIRED);
            X509_OBJECT_free_contents(&obj);
            return 0;
        }
        X509_OBJECT_free_contents(&obj);
    }

    /*
     * Try to retrieve a CRL corresponding to the _issuer_ of
     * the current certificate in order to check for revocation.
     */
    memset((char *)&obj, 0, sizeof(obj));
    X509_STORE_CTX_init(&store_ctx, crl_store, NULL, NULL);
    rc = X509_STORE_get_by_subject(&store_ctx, X509_LU_CRL, issuer, &obj);
    X509_STORE_CTX_cleanup(&store_ctx);
    crl = obj.data.crl;
    if (rc > 0 && crl != NULL) {
        /*
         * Check if the current certificate is revoked by this CRL
         */
        n = sk_X509_REVOKED_num(X509_CRL_get_REVOKED(crl));
        for (i = 0; i < n; i++) {
            revoked = sk_X509_REVOKED_value(X509_CRL_get_REVOKED(crl), i);
            if (ASN1_INTEGER_cmp(revoked->serialNumber, X509_get_serialNumber(xs)) == 0) {
                serial = ASN1_INTEGER_get(revoked->serialNumber);
                cp = X509_NAME_oneline(issuer, NULL, 0);
                Log::global->Format(0,
		    "Certificate with serial %ld (0x%lX) revoked per CRL from issuer %s\n",
                        serial, serial, cp ? cp : "(ERROR)");
                free(cp);

                X509_STORE_CTX_set_error(ctx, X509_V_ERR_CERT_REVOKED);
                X509_OBJECT_free_contents(&obj);
                return 0;
            }
        }
        X509_OBJECT_free_contents(&obj);
    }
    return 1;
}

static const char *host;
int lftp_ssl_connect(SSL *ssl,const char *h)
{
   host=h;
   int res=SSL_connect(ssl);
   host=0;
   return res;
}

static int lftp_ssl_verify_callback(int ok,X509_STORE_CTX *ctx)
{
   static X509 *prev_cert=0;
   X509 *cert=X509_STORE_CTX_get_current_cert(ctx);

   if(cert!=prev_cert)
   {
      int depth          = X509_STORE_CTX_get_error_depth(ctx);
      X509_NAME *subject = X509_get_subject_name(cert);
      X509_NAME *issuer  = X509_get_issuer_name(cert);
      char *subject_line = X509_NAME_oneline(subject, NULL, 0);
      char *issuer_line  = X509_NAME_oneline(issuer, NULL, 0);
      Log::global->Format(3,"Certificate depth: %d; subject: %s; issuer: %s\n",
			  depth,subject_line,issuer_line);
      free(subject_line);
      free(issuer_line);
   }

   if(ok && !lftp_ssl_verify_crl(ctx))
      ok=0;

   int error=X509_STORE_CTX_get_error(ctx);

   bool verify=ResMgr::QueryBool("ssl:verify-certificate",host);

   if(!ok)
   {
      Log::global->Format(0,"%s: Certificate verification: %s\n",
			  verify?"ERROR":"WARNING",
			  X509_verify_cert_error_string(error));
   }

   if(!verify)
      ok=1;

   if(!ok)
      certificate_verify_error=error;

   prev_cert=cert;
   return ok;
}

#endif
