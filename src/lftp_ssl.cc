/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>

#if USE_SSL
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "lftp_ssl.h"
#include "xmalloc.h"
#include "ResMgr.h"
#include "log.h"
#include "misc.h"
#include "network.h"
#include "buffer.h"
extern "C" {
#include "c-ctype.h"
#include "quotearg.h"
#include "quote.h"
}

lftp_ssl_base::lftp_ssl_base(int fd1,handshake_mode_t m,const char *h)
   : hostname(h)
{
   fd=fd1;
   handshake_done=false;
   handshake_mode=m;
   fatal=false;
   cert_error=false;
}
void lftp_ssl_base::set_error(const char *s1,const char *s2)
{
   if(s2)
      error.vset(s1,": ",s2,NULL);
   else
      error.set(s1);
}
void lftp_ssl_base::set_cert_error(const char *s)
{
   bool verify=ResMgr::QueryBool("ssl:verify-certificate",hostname);
   const char *const warn=verify?"ERROR":"WARNING";
   Log::global->Format(0,"%s: Certificate verification: %s\n",warn,s);
   if(verify && !error)
   {
      set_error("Certificate verification",s);
      fatal=true;
      cert_error=true;
   }
}

#if USE_GNUTLS

/* Helper functions to load a certificate and key
 * files into memory. They use mmap for simplicity.
 */
static gnutls_datum_t mmap_file(const char *file)
{
    int fd;
    gnutls_datum_t mmaped_file = { NULL, 0 };
    struct stat stat_st;
    void *ptr;

    fd = open(file, 0);
    if (fd == -1)
	return mmaped_file;

    fstat(fd, &stat_st);

    if ((ptr =
	 mmap(NULL, stat_st.st_size, PROT_READ, MAP_SHARED, fd,
	      0)) == MAP_FAILED)
    {
      close(fd);
	return mmaped_file;
    }
   close(fd);

    mmaped_file.data = (unsigned char*)ptr;
    mmaped_file.size = stat_st.st_size;

    return mmaped_file;
}

static void munmap_file(gnutls_datum_t data)
{
    munmap(data.data, data.size);
}

#if LFTP_LIBGNUTLS_VERSION_CODE < 0x010201
#define gnutls_x509_crt_list_import lftp_gnutls_x509_crt_list_import
#define GNUTLS_X509_CRT_LIST_IMPORT_FAIL_IF_EXCEED 1
static
int gnutls_x509_crt_list_import(gnutls_x509_crt_t *certs, unsigned int* cert_max,
    const gnutls_datum_t * data, gnutls_x509_crt_fmt_t format, unsigned int flags);
#endif

void lftp_ssl_gnutls_instance::LoadCA()
{
   // free CA first
   for(unsigned i=0; i<ca_list_size; i++)
      gnutls_x509_crt_deinit(ca_list[i]);
   xfree(ca_list);
   ca_list=0;
   ca_list_size=0;

   const char *ca_file=ResMgr::Query("ssl:ca-file",0);
   if(!ca_file || !ca_file[0])
      return;

   gnutls_datum_t ca_pem=mmap_file(ca_file);
   if(!ca_pem.data)
   {
      Log::global->Format(0,"%s: %s\n",ca_file,strerror(errno));
      return;
   }

   ca_list_size=64;
   ca_list=(gnutls_x509_crt_t*)xmalloc(ca_list_size*sizeof(gnutls_x509_crl_t));
   int res=gnutls_x509_crt_list_import(ca_list,&ca_list_size,&ca_pem,GNUTLS_X509_FMT_PEM,GNUTLS_X509_CRT_LIST_IMPORT_FAIL_IF_EXCEED);
   if(res==GNUTLS_E_SHORT_MEMORY_BUFFER)
   {
      ca_list=(gnutls_x509_crt_t*)xrealloc(ca_list,ca_list_size*sizeof(gnutls_x509_crl_t));
      res=gnutls_x509_crt_list_import(ca_list,&ca_list_size,&ca_pem,GNUTLS_X509_FMT_PEM,0);
   }
   if(res<0)
   {
      Log::global->Format(0,"gnutls_x509_crt_list_import: %s\n",gnutls_strerror(res));
      xfree(ca_list);
      ca_list=0;
      ca_list_size=0;
   }

   munmap_file(ca_pem);
}
void lftp_ssl_gnutls_instance::LoadCRL()
{
   // free CRL first
   for(unsigned i=0; i<crl_list_size; i++)
      gnutls_x509_crl_deinit(crl_list[i]);
   xfree(crl_list);
   crl_list=0;
   crl_list_size=0;

   const char *crl_file=ResMgr::Query("ssl:crl-file",0);
   if(!crl_file || !crl_file[0])
      return;

   gnutls_datum_t crl_pem=mmap_file(crl_file);
   if(!crl_pem.data)
   {
      Log::global->Format(0,"%s: %s\n",crl_file,strerror(errno));
      return;
   }
   crl_list_size=1;
   crl_list=(gnutls_x509_crl_t*)xmalloc(crl_list_size*sizeof(gnutls_x509_crl_t));
   int res=gnutls_x509_crl_import(crl_list[0],&crl_pem,GNUTLS_X509_FMT_PEM);
   if(res<0)
   {
      Log::global->Format(0,"gnutls_x509_crl_import: %s\n",gnutls_strerror(res));
      xfree(crl_list);
      crl_list=0;
      crl_list_size=0;
   }

   munmap_file(crl_pem);
}
void lftp_ssl_gnutls_instance::Reconfig(const char *name)
{
   if(!name || !strcmp(name,"ssl:ca-file"))
      LoadCA();
   if(!name || !strcmp(name,"ssl:crl-file"))
      LoadCRL();
}

static const char *lftp_ssl_find_ca_file()
{
   // a few possible locations of ca-bundle.crt
   static const char *const ca_file_location[]={
      "/etc/pki/tls/certs/ca-bundle.crt",
      "/etc/certs/ca-bundle.crt",
      "/usr/share/ssl/certs/ca-bundle.crt",
      "/etc/ssl/certs/ca-certificates.crt",
      "/usr/local/ssl/certs/ca-bundle.crt",
      "/etc/apache/ssl.crt/ca-bundle.crt",
      "/usr/share/curl/curl-ca-bundle.crt",
      0};
   for(int i=0; ca_file_location[i]; i++)
   {
      if(access(ca_file_location[i], R_OK)==0)
	 return ca_file_location[i];
   }
   return 0;
}

static void lftp_ssl_gnutls_log_func(int level, const char *msg)
{
   if(!strncmp(msg,"ASSERT",6)
   || !strncmp(msg,"READ",4)
   || !strncmp(msg,"WRITE",5))
      level+=10;
   Log::global->Format(9+level,"GNUTLS: %s",msg);
}

lftp_ssl_gnutls_instance::lftp_ssl_gnutls_instance()
{
   ca_list=0;
   ca_list_size=0;
   crl_list=0;
   crl_list_size=0;

   Suspend();
   gnutls_global_init();
   gnutls_global_set_log_function(lftp_ssl_gnutls_log_func);
   gnutls_global_set_log_level(9);

   const char *ca_file=ResMgr::Query("ssl:ca-file",0);
   if(!ca_file || !ca_file[0])
      ResMgr::Set("ssl:ca-file",0,lftp_ssl_find_ca_file());

   Reconfig(0);
}
lftp_ssl_gnutls_instance::~lftp_ssl_gnutls_instance()
{
   gnutls_global_deinit();
}


lftp_ssl_gnutls_instance *lftp_ssl_gnutls::instance;

void lftp_ssl_gnutls::global_init()
{
   if(!instance)
      instance=new lftp_ssl_gnutls_instance();
}
void lftp_ssl_gnutls::global_deinit()
{
   SMTask::Delete(instance);
   instance=0;
}
lftp_ssl_gnutls::lftp_ssl_gnutls(int fd1,handshake_mode_t m,const char *h)
   : lftp_ssl_base(fd1,m,h)
{
   global_init();

   cred=0;

   gnutls_init(&session,(m==CLIENT?GNUTLS_CLIENT:GNUTLS_SERVER));
   gnutls_set_default_priority(session);

   gnutls_transport_set_ptr(session,(gnutls_transport_ptr_t)fd);

   const char *priority=ResMgr::Query("ssl:priority", 0);
   if(priority)
      gnutls_priority_set_direct(session, priority, 0);
   else
   {
      // hack for some ftp servers
      const char *auth=ResMgr::Query("ftp:ssl-auth", hostname);
      if(auth && !strncmp(auth, "SSL", 3))
         gnutls_priority_set_direct(session, "NORMAL:+SSL3.0:-TLS1.0:-TLS1.1:-TLS1.2",0);
   }

   if(h && ResMgr::QueryBool("ssl:use-sni",h)) {
      if(gnutls_server_name_set(session, GNUTLS_NAME_DNS, h, xstrlen(h)) < 0)
	 fprintf(stderr,"WARNING: failed to configure server name indication (SNI) TLS extension\n");
   }
}
void lftp_ssl_gnutls::load_keys()
{
   gnutls_certificate_allocate_credentials(&cred);
   int res;
#if 0
   const char *ca_file=ResMgr::Query("ssl:ca-file",hostname);
   const char *ca_file0=ResMgr::Query("ssl:ca-file",0);
   if(ca_file && ca_file[0] && xstrcmp(ca_file,ca_file0))
   {
      res=gnutls_certificate_set_x509_trust_file(cred,ca_file,GNUTLS_X509_FMT_PEM);
      if(res<0)
	 Log::global->Format(0,"gnutls_certificate_set_x509_trust_file(%s): %s\n",ca_file,gnutls_strerror(res));
   }
   const char *crl_file=ResMgr::Query("ssl:crl-file",hostname);
   const char *crl_file0=ResMgr::Query("ssl:crl-file",0);
   if(crl_file && crl_file[0] && xstrcmp(crl_file,crl_file0))
   {
      res=gnutls_certificate_set_x509_crl_file(cred,crl_file,GNUTLS_X509_FMT_PEM);
      if(res<0)
	 Log::global->Format(0,"gnutls_certificate_set_x509_crl_file(%s): %s\n",crl_file,gnutls_strerror(res));
   }
#endif
   const char *key_file =ResMgr::Query("ssl:key-file",hostname);
   const char *cert_file=ResMgr::Query("ssl:cert-file",hostname);
   if(key_file && key_file[0] && cert_file && cert_file[0])
   {
      res=gnutls_certificate_set_x509_key_file(cred,cert_file,key_file,GNUTLS_X509_FMT_PEM);
      if(res<0)
	 Log::global->Format(0,"gnutls_certificate_set_x509_key_file(%s,%s): %s\n",cert_file,key_file,gnutls_strerror(res));
   }
   gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, cred);
}
void lftp_ssl_gnutls::shutdown()
{
   if(handshake_done)
      gnutls_bye(session,GNUTLS_SHUT_RDWR);  // FIXME - E_AGAIN
}
lftp_ssl_gnutls::~lftp_ssl_gnutls()
{
   if(cred)
      gnutls_certificate_free_credentials(cred);
   gnutls_deinit(session);
   session=0;
}

/* This function will try to verify the peer's certificate chain, and
 * also check if the hostname matches, and the activation, expiration dates.
 */
void lftp_ssl_gnutls::verify_certificate_chain(const gnutls_datum_t *cert_chain,int cert_chain_length)
{
   int i;
   gnutls_x509_crt_t *cert=(gnutls_x509_crt_t*)alloca(cert_chain_length*sizeof(gnutls_x509_crt_t));

   /* Import all the certificates in the chain to
    * native certificate format.
    */
   for (i = 0; i < cert_chain_length; i++)
   {
      gnutls_x509_crt_init(&cert[i]);
      gnutls_x509_crt_import(cert[i],&cert_chain[i],GNUTLS_X509_FMT_DER);
   }

   /* Now verify the certificates against their issuers
    * in the chain.
    */
   for (i = 1; i < cert_chain_length; i++)
      verify_cert2(cert[i - 1], cert[i]);

    /* Here we must verify the last certificate in the chain against
     * our trusted CA list.
     */
   verify_last_cert(cert[cert_chain_length - 1]);

   /* Check if the name in the first certificate matches our destination!
    */
   bool check_hostname = ResMgr::QueryBool("ssl:check-hostname", hostname);
   if(check_hostname) {
      if(!gnutls_x509_crt_check_hostname(cert[0], hostname))
	 set_cert_error(xstring::format("certificate common name doesn't match requested host name %s",quote(hostname)));
   } else {
      Log::global->Format(0, "WARNING: Certificate verification: hostname checking disabled\n");
   }

   for (i = 0; i < cert_chain_length; i++)
      gnutls_x509_crt_deinit(cert[i]);
}


/* Verifies a certificate against an other certificate
 * which is supposed to be it's issuer. Also checks the
 * crl_list if the certificate is revoked.
 */
void lftp_ssl_gnutls::verify_cert2(gnutls_x509_crt_t crt,gnutls_x509_crt_t issuer)
{
   int ret;
   time_t now = SMTask::now;
   size_t name_size;
   char name[256];

   /* Print information about the certificates to
    * be checked.
    */
   name_size = sizeof(name);
   gnutls_x509_crt_get_dn(crt, name, &name_size);

   Log::global->Format(9, "Certificate: %s\n", name);

   name_size = sizeof(name);
   gnutls_x509_crt_get_issuer_dn(crt, name, &name_size);

   Log::global->Format(9, " Issued by:        %s\n", name);

   /* Get the DN of the issuer cert.
    */
   name_size = sizeof(name);
   gnutls_x509_crt_get_dn(issuer, name, &name_size);

   Log::global->Format(9, " Checking against: %s\n", name);

   /* Do the actual verification.
    */
   unsigned crt_status=0;
   unsigned issuer_status=0;
   gnutls_x509_crt_verify(crt, &issuer, 1, 0, &crt_status);
   if(crt_status&GNUTLS_CERT_SIGNER_NOT_CA)
   {
      // recheck the issuer certificate against CA
      gnutls_x509_crt_verify(issuer, instance->ca_list, instance->ca_list_size, 0, &issuer_status);
      if(issuer_status==0)
	 crt_status&=~GNUTLS_CERT_SIGNER_NOT_CA;
      if(crt_status==GNUTLS_CERT_INVALID)
	 crt_status=0;
   }
   if (crt_status & GNUTLS_CERT_INVALID)
   {
      char msg[256];
      strcpy(msg,"Not trusted");
      if(crt_status & GNUTLS_CERT_SIGNER_NOT_FOUND)
	 strcat(msg,": no issuer was found");
      if(crt_status & GNUTLS_CERT_SIGNER_NOT_CA)
	 strcat(msg,": issuer is not a CA");
      set_cert_error(msg);
   }
   else
      Log::global->Format(9, "  Trusted\n");


    /* Now check the expiration dates.
     */
    if (gnutls_x509_crt_get_activation_time(crt) > now)
	set_cert_error("Not yet activated");

    if (gnutls_x509_crt_get_expiration_time(crt) < now)
	set_cert_error("Expired");

    /* Check if the certificate is revoked.
     */
    ret = gnutls_x509_crt_check_revocation(crt, instance->crl_list, instance->crl_list_size);
    if (ret == 1) {		/* revoked */
	set_cert_error("Revoked");
    }
}


/* Verifies a certificate against the trusted CA list.
 * Also checks the crl_list if the certificate is revoked.
 */
void lftp_ssl_gnutls::verify_last_cert(gnutls_x509_crt_t crt)
{
   unsigned int crt_status;
   int ret;
   time_t now = SMTask::now;
   size_t name_size;
   char name[256];

   /* Print information about the certificates to
    * be checked.
    */
   name_size = sizeof(name);
   gnutls_x509_crt_get_dn(crt, name, &name_size);

   Log::global->Format(9, "Certificate: %s\n", name);

   name_size = sizeof(name);
   gnutls_x509_crt_get_issuer_dn(crt, name, &name_size);

   Log::global->Format(9, " Issued by: %s\n", name);

   /* Do the actual verification.
    */
   gnutls_x509_crt_verify(crt, instance->ca_list, instance->ca_list_size, GNUTLS_VERIFY_ALLOW_X509_V1_CA_CRT, &crt_status);

   if (crt_status & GNUTLS_CERT_INVALID)
   {
      char msg[256];
      strcpy(msg,"Not trusted");
      if (crt_status & GNUTLS_CERT_SIGNER_NOT_CA)
	 strcat(msg,": Issuer is not a CA");
      set_cert_error(msg);
   }
   else
      Log::global->Format(9, "  Trusted\n");


   /* Now check the expiration dates.
    */
   if(gnutls_x509_crt_get_activation_time(crt) > now)
      set_cert_error("Not yet activated");

   if(gnutls_x509_crt_get_expiration_time(crt) < now)
      set_cert_error("Expired");

   /* Check if the certificate is revoked.
    */
   ret = gnutls_x509_crt_check_revocation(crt, instance->crl_list, instance->crl_list_size);
   if (ret == 1) {		/* revoked */
      set_cert_error("Revoked");
   }
}

bool lftp_ssl_gnutls::check_fatal(int res)
{
   if(!gnutls_error_is_fatal(res))
      return false;
   if((res==GNUTLS_E_UNEXPECTED_PACKET_LENGTH
       || res==GNUTLS_E_PUSH_ERROR || res==GNUTLS_E_PULL_ERROR
       || res==GNUTLS_E_DECRYPTION_FAILED)
   && (!errno || temporary_network_error(errno)))
      return false;
   return true;
}

int lftp_ssl_gnutls::do_handshake()
{
   if(handshake_done)
      return DONE;
   errno=0;
   int res=gnutls_handshake(session);
   if(res<0)
   {
      if(res==GNUTLS_E_AGAIN || res==GNUTLS_E_INTERRUPTED)
	 return RETRY;
      else // error
      {
	 fatal=check_fatal(res);
	 set_error("gnutls_handshake",gnutls_strerror(res));
	 return ERROR;
      }
   }
   handshake_done=true;
   SMTask::current->Timeout(0);

   if(gnutls_certificate_type_get(session)!=GNUTLS_CRT_X509)
   {
      set_cert_error("Unsupported certificate type");
      return DONE; // FIXME: handle openpgp as well
   }

   unsigned cert_list_size=0;
   const gnutls_datum_t *cert_list=gnutls_certificate_get_peers(session,&cert_list_size);
   if(cert_list==NULL || cert_list_size==0)
      set_cert_error("No certificate was found!");
   else
      verify_certificate_chain(cert_list,cert_list_size);

   return DONE;
}

#ifndef GNUTLS_E_PREMATURE_TERMINATION // for gnutls < 3.0
# define GNUTLS_E_PREMATURE_TERMINATION GNUTLS_E_UNEXPECTED_PACKET_LENGTH
#endif

int lftp_ssl_gnutls::read(char *buf,int size)
{
   if(error)
      return ERROR;
   int res=do_handshake();
   if(res!=DONE)
      return res;
   errno=0;
   res=gnutls_record_recv(session,buf,size);
   if(res<0)
   {
      if(res==GNUTLS_E_AGAIN || res==GNUTLS_E_INTERRUPTED)
	 return RETRY;
      else if(res==GNUTLS_E_UNEXPECTED_PACKET_LENGTH || res==GNUTLS_E_PREMATURE_TERMINATION)
      {
	 Log::global->Format(7,"gnutls_record_recv: %s Assuming EOF.\n",gnutls_strerror(res));
	 return 0;
      }
      else // error
      {
	 fatal=check_fatal(res);
	 set_error("gnutls_record_recv",gnutls_strerror(res));
	 return ERROR;
      }
   }
   return res;
}
int lftp_ssl_gnutls::write(const char *buf,int size)
{
   if(error)
      return ERROR;
   int res=do_handshake();
   if(res!=DONE)
      return res;
   if(size==0)
      return 0;
   errno=0;
   res=gnutls_record_send(session,buf,size);
   if(res<0)
   {
      if(res==GNUTLS_E_AGAIN || res==GNUTLS_E_INTERRUPTED)
	 return RETRY;
      else // error
      {
	 fatal=check_fatal(res);
	 set_error("gnutls_record_send",gnutls_strerror(res));
	 return ERROR;
      }
   }
   return res;
}
bool lftp_ssl_gnutls::want_in()
{
   return gnutls_record_get_direction(session)==0;
}
bool lftp_ssl_gnutls::want_out()
{
   return gnutls_record_get_direction(session)==1;
}
void lftp_ssl_gnutls::copy_sid(const lftp_ssl_gnutls *o)
{
   size_t session_data_size;
   void *session_data;
   int res=gnutls_session_get_data(o->session,NULL,&session_data_size);
   if(res!=GNUTLS_E_SUCCESS && res!=GNUTLS_E_SHORT_MEMORY_BUFFER)
      return;
   session_data=xmalloc(session_data_size);
   if(gnutls_session_get_data(o->session,session_data,&session_data_size)!=GNUTLS_E_SUCCESS)
      return;
   gnutls_session_set_data(session,session_data,session_data_size);
}

#if LFTP_LIBGNUTLS_VERSION_CODE < 0x010201
#define PEM_CERT_SEP2 "-----BEGIN X509 CERTIFICATE"
#define PEM_CERT_SEP "-----BEGIN CERTIFICATE"
#define CLEAR_CERTS \
    for(j=0;j<count;j++) gnutls_x509_crt_deinit( certs[j])
/**
  * gnutls_x509_crt_list_import - This function will import a PEM encoded certificate list
  * @certs: The structures to store the parsed certificate. Must not be initialized.
  * @cert_max: Initially must hold the maximum number of certs. It will be updated with the number of certs available.
  * @data: The PEM encoded certificate.
  * @format: One of DER or PEM. Only PEM is supported for now.
  * @flags: must be zero or an OR'd sequence of gnutls_certificate_import_flags.
  *
  * This function will convert the given PEM encoded certificate list
  * to the native gnutls_x509_crt_t format. The output will be stored in @certs.
  * They will be automatically initialized.
  *
  * If the Certificate is PEM encoded it should have a header of "X509 CERTIFICATE", or
  * "CERTIFICATE".
  *
  * Returns the number of certificates read or a negative error value.
  *
  */
static
int gnutls_x509_crt_list_import(gnutls_x509_crt_t *certs, unsigned int* cert_max,
    const gnutls_datum_t * data, gnutls_x509_crt_fmt_t format, unsigned int flags)
{
    int size;
    const char *ptr;
    gnutls_datum_t tmp;
    int ret, nocopy=0;
    unsigned int count=0,j;

    /* move to the certificate
     */
    ptr = (const char *)memmem(data->data, data->size,
		 PEM_CERT_SEP, sizeof(PEM_CERT_SEP) - 1);
    if (ptr == NULL)
	ptr = (const char *)memmem(data->data, data->size,
		     PEM_CERT_SEP2, sizeof(PEM_CERT_SEP2) - 1);

    if (ptr == NULL) {
	return GNUTLS_E_BASE64_DECODING_ERROR;
    }
    size = data->size - (ptr - (char*)data->data);

    count = 0;

    do {
        if (count >= *cert_max) {
            if (!(flags & GNUTLS_X509_CRT_LIST_IMPORT_FAIL_IF_EXCEED))
                break;
            else
                nocopy = 1;
        }

	if (!nocopy) {
	    ret = gnutls_x509_crt_init( &certs[count]);
	    if (ret < 0) {
                goto error;
            }

	    tmp.data = (unsigned char*)ptr;
	    tmp.size = size;

	    ret = gnutls_x509_crt_import( certs[count], &tmp, GNUTLS_X509_FMT_PEM);
	    if (ret < 0) {
                goto error;
            }
        }

	/* now we move ptr after the pem header
	 */
	ptr++;
	/* find the next certificate (if any)
	 */
	size = data->size - (ptr - (char*)data->data);

	if (size > 0) {
	    const char *ptr2;

	    ptr2 =
		(const char *)memmem(ptr, size, PEM_CERT_SEP, sizeof(PEM_CERT_SEP) - 1);
	    if (ptr2 == NULL)
		ptr2 = (const char *)memmem(ptr, size, PEM_CERT_SEP2,
			      sizeof(PEM_CERT_SEP2) - 1);

	    ptr = ptr2;
	} else
	    ptr = NULL;

	count++;
    } while (ptr != NULL);

    *cert_max = count;

    if (nocopy==0)
        return count;
    else
        return GNUTLS_E_SHORT_MEMORY_BUFFER;

error:
    CLEAR_CERTS;
    return ret;
}
#endif

/*=============================== OpenSSL ====================================*/
#elif USE_OPENSSL
//static int lftp_ssl_passwd_callback(char *buf,int size,int rwflag,void *userdata);

lftp_ssl_openssl_instance *lftp_ssl_openssl::instance;

static char file[256];
static void lftp_ssl_write_rnd()
{
   RAND_write_file(file);
}

void lftp_ssl_openssl::global_init()
{
   if(!instance)
      instance=new lftp_ssl_openssl_instance();
}
void lftp_ssl_openssl::global_deinit()
{
   delete instance;
   instance=0;
}

#ifndef SSL_OP_NO_TICKET
# define SSL_OP_NO_TICKET 0
#endif

lftp_ssl_openssl_instance::lftp_ssl_openssl_instance()
{
   crl_store=0;
   ssl_ctx=0;

#ifdef WINDOWS
   RAND_screen();
#endif

   RAND_file_name(file,sizeof(file));

   if(RAND_load_file(file,-1) && RAND_status()!=0)
      atexit(lftp_ssl_write_rnd);

#if SSLEAY_VERSION_NUMBER < 0x0800
   ssl_ctx=SSL_CTX_new();
   X509_set_default_verify_paths(ssl_ctx->cert);
#else
   SSLeay_add_ssl_algorithms();
   ssl_ctx=SSL_CTX_new(SSLv23_client_method());
   long options=SSL_OP_ALL|SSL_OP_NO_TICKET|SSL_OP_NO_SSLv2;
   const char *priority=ResMgr::Query("ssl:priority", 0);
   if(priority)
   {
      const char *ptr;
      if((ptr=strstr(priority,"-SSL3.0")) && (ptr==priority || ptr[-1]==':') && (ptr[7]==':' || ptr[7]=='\0'))
         options|=SSL_OP_NO_SSLv3;
      if((ptr=strstr(priority,"-TLS1.0")) && (ptr==priority || ptr[-1]==':') && (ptr[7]==':' || ptr[7]=='\0'))
         options|=SSL_OP_NO_TLSv1;
      if((ptr=strstr(priority,"-TLS1.1")) && (ptr==priority || ptr[-1]==':') && (ptr[7]==':' || ptr[7]=='\0'))
         options|=SSL_OP_NO_TLSv1_1;
      if((ptr=strstr(priority,"-TLS1.2")) && (ptr==priority || ptr[-1]==':') && (ptr[7]==':' || ptr[7]=='\0'))
         options|=SSL_OP_NO_TLSv1_2;
   }
   SSL_CTX_set_options(ssl_ctx, options);
   SSL_CTX_set_cipher_list(ssl_ctx, "ALL:!aNULL:!eNULL:!SSLv2:!LOW:!EXP:!MD5:@STRENGTH");
   SSL_CTX_set_verify(ssl_ctx,SSL_VERIFY_PEER,lftp_ssl_openssl::verify_callback);
//    SSL_CTX_set_default_passwd_cb(ssl_ctx,lftp_ssl_passwd_callback);

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
lftp_ssl_openssl_instance::~lftp_ssl_openssl_instance()
{
   SSL_CTX_free(ssl_ctx);
   X509_STORE_free(crl_store);
}

lftp_ssl_openssl::lftp_ssl_openssl(int fd1,handshake_mode_t m,const char *h)
   : lftp_ssl_base(fd1,m,h)
{
   if(!instance)
      global_init();

   ssl=SSL_new(instance->ssl_ctx);
   SSL_set_fd(ssl,fd);
   SSL_ctrl(ssl,SSL_CTRL_MODE,SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,0);

   if(h && ResMgr::QueryBool("ssl:use-sni",h)) {
      if(!SSL_set_tlsext_host_name(ssl, h))
	 fprintf(stderr,"WARNING: failed to configure server name indication (SNI) TLS extension\n");
   }
}
void lftp_ssl_openssl::load_keys()
{
   const char *key_file =ResMgr::Query("ssl:key-file",hostname);
   const char *cert_file=ResMgr::Query("ssl:cert-file",hostname);
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
}
void lftp_ssl_openssl::shutdown()
{
   if(handshake_done)
      SSL_shutdown(ssl);
}
lftp_ssl_openssl::~lftp_ssl_openssl()
{
   SSL_free(ssl);
   ssl=0;
}

static const char *verify_callback_host;
static int lftp_ssl_connect(SSL *ssl,const char *h)
{
   verify_callback_host=h;
   int res=SSL_connect(ssl);
   verify_callback_host=0;
   return res;
}
bool lftp_ssl_openssl::check_fatal(int res)
{
   return !(SSL_get_error(ssl,res)==SSL_ERROR_SYSCALL
	    && (ERR_get_error()==0 || temporary_network_error(errno)));
}

int lftp_ssl_openssl::do_handshake()
{
   if(handshake_done)
      return DONE;
   if(handshake_mode==SERVER)
   {
      // FIXME: SSL_accept
      return RETRY;
   }
   errno=0;
   int res=lftp_ssl_connect(ssl,hostname);
   if(res<=0)
   {
      if(BIO_sock_should_retry(res))
	 return RETRY;
      else if (SSL_want_x509_lookup(ssl))
	 return RETRY;
      else // error
      {
	 fatal=check_fatal(res);
	 set_error("SSL_connect",strerror());
	 return ERROR;
      }
   }
   handshake_done=true;
   check_certificate();
   SMTask::current->Timeout(0);
   return DONE;
}
int lftp_ssl_openssl::read(char *buf,int size)
{
   if(error)
      return ERROR;
   int res=do_handshake();
   if(res!=DONE)
      return res;
   errno=0;
   res=SSL_read(ssl,buf,size);
   if(res<0)
   {
      if(BIO_sock_should_retry(res))
	 return RETRY;
      else if (SSL_want_x509_lookup(ssl))
	 return RETRY;
      else // error
      {
	 fatal=check_fatal(res);
	 set_error("SSL_read",strerror());
	 return ERROR;
      }
   }
   return res;
}
int lftp_ssl_openssl::write(const char *buf,int size)
{
   if(error)
      return ERROR;
   int res=do_handshake();
   if(res!=DONE)
      return res;
   if(size==0)
      return 0;
   errno=0;
   res=SSL_write(ssl,buf,size);
   if(res<0)
   {
      if(BIO_sock_should_retry(res))
	 return RETRY;
      else if (SSL_want_x509_lookup(ssl))
	 return RETRY;
      else // error
      {
	 fatal=check_fatal(res);
	 set_error("SSL_write",strerror());
	 return ERROR;
      }
   }
   return res;
}
bool lftp_ssl_openssl::want_in()
{
   return SSL_want_read(ssl);
}
bool lftp_ssl_openssl::want_out()
{
   return SSL_want_write(ssl);
}
void lftp_ssl_openssl::copy_sid(const lftp_ssl_openssl *o)
{
   SSL_copy_session_id(ssl,o->ssl);
}

static int certificate_verify_error;

const char *lftp_ssl_openssl::strerror()
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
   return ssl_error;
}

/* This one is (very much!) based on work by Ralf S. Engelschall <rse@engelschall.com>.
 * Comments by Ralf. */
int lftp_ssl_openssl::verify_crl(X509_STORE_CTX *ctx)
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
    if (!instance->crl_store)
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
    X509_STORE_CTX_init(&store_ctx, instance->crl_store, NULL, NULL);
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
    X509_STORE_CTX_init(&store_ctx, instance->crl_store, NULL, NULL);
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

static bool convert_from_utf8(char *str,int len)
{
   DirectedBuffer translate(DirectedBuffer::GET);
   translate.SetTranslation("UTF-8",false);
   translate.PutTranslated(str,len);
   const char *str1,*str2;
   int len1,len2;
   translate.Get(&str1,&len1);
   if(len1>len)
      return false;  // no room to store expanded string

   // be safe and try to convert back to UTF-8
   DirectedBuffer translate_back(DirectedBuffer::PUT);
   translate_back.SetTranslation("UTF-8",false);
   translate_back.PutTranslated(str1,len1);
   translate_back.Get(&str2,&len2);
   if(len2!=len || memcmp(str2,str,len))
      return false;  // conversion error

   memcpy(str,str1,len1);
   str[len1]=0;
   return true;
}

/* begin curl-7.21.3 code */
/* Copyright (c) 1998 - 2010, Daniel Stenberg, <daniel@haxx.se> */
#define Curl_raw_toupper c_toupper
#define Curl_raw_equal !strcmp
#define Curl_inet_pton inet_pton
#if INET6
# define ENABLE_IPV6 1
#endif
/*
 * Match a hostname against a wildcard pattern.
 * E.g.
 *  "foo.host.com" matches "*.host.com".
 *
 * We are a bit more liberal than RFC2818 describes in that we
 * accept multiple "*" in pattern (similar to what some other browsers do).
 * E.g.
 *  "abc.def.domain.com" should strickly not match "*.domain.com", but we
 *  don't consider "." to be important in CERT checking.
 */
#define HOST_NOMATCH 0
#define HOST_MATCH   1

static int hostmatch(const char *hostname, const char *pattern)
{
  for(;;) {
    char c = *pattern++;

    if(c == '\0')
      return (*hostname ? HOST_NOMATCH : HOST_MATCH);

    if(c == '*') {
      c = *pattern;
      if(c == '\0')      /* "*\0" matches anything remaining */
        return HOST_MATCH;

      while(*hostname) {
        /* The only recursive function in libcurl! */
        if(hostmatch(hostname++,pattern) == HOST_MATCH)
          return HOST_MATCH;
      }
      break;
    }

    if(Curl_raw_toupper(c) != Curl_raw_toupper(*hostname++))
      break;
  }
  return HOST_NOMATCH;
}

static int
cert_hostcheck(const char *match_pattern, const char *hostname)
{
  if(!match_pattern || !*match_pattern ||
      !hostname || !*hostname) /* sanity check */
    return 0;

  if(Curl_raw_equal(hostname, match_pattern)) /* trivial case */
    return 1;

  if(hostmatch(hostname,match_pattern) == HOST_MATCH)
    return 1;
  return 0;
}

/* Quote from RFC2818 section 3.1 "Server Identity"

   If a subjectAltName extension of type dNSName is present, that MUST
   be used as the identity. Otherwise, the (most specific) Common Name
   field in the Subject field of the certificate MUST be used. Although
   the use of the Common Name is existing practice, it is deprecated and
   Certification Authorities are encouraged to use the dNSName instead.

   Matching is performed using the matching rules specified by
   [RFC2459].  If more than one identity of a given type is present in
   the certificate (e.g., more than one dNSName name, a match in any one
   of the set is considered acceptable.) Names may contain the wildcard
   character * which is considered to match any single domain name
   component or component fragment. E.g., *.a.com matches foo.a.com but
   not bar.foo.a.com. f*.com matches foo.com but not bar.com.

   In some cases, the URI is specified as an IP address rather than a
   hostname. In this case, the iPAddress subjectAltName must be present
   in the certificate and must exactly match the IP in the URI.

*/
void lftp_ssl_openssl::check_certificate()
{
  X509 *server_cert = SSL_get_peer_certificate (ssl);
  if (!server_cert)
    {
      set_cert_error(xstring::format(_("No certificate presented by %s.\n"),
                 quotearg_style (escape_quoting_style, hostname)));
      return;
    }

  bool check_hostname = ResMgr::QueryBool("ssl:check-hostname", hostname);
  if(!check_hostname) {
    Log::global->Format(0, "WARNING: Certificate verification: hostname checking disabled\n");
    return;
  }

  int matched = -1; /* -1 is no alternative match yet, 1 means match and 0
                       means mismatch */
  int target = GEN_DNS; /* target type, GEN_DNS or GEN_IPADD */
  size_t addrlen = 0;
  STACK_OF(GENERAL_NAME) *altnames;
#ifdef ENABLE_IPV6
  struct in6_addr addr;
#else
  struct in_addr addr;
#endif

  sockaddr_u fd_addr;
  socklen_t fd_addr_len = sizeof(fd_addr);
  getsockname(fd,&fd_addr.sa,&fd_addr_len);

#ifdef ENABLE_IPV6
  if(fd_addr.sa.sa_family==AF_INET6 &&
     Curl_inet_pton(AF_INET6, hostname, &addr)) {
    target = GEN_IPADD;
    addrlen = sizeof(struct in6_addr);
  }
  else
#endif
    if(Curl_inet_pton(AF_INET, hostname, &addr)) {
      target = GEN_IPADD;
      addrlen = sizeof(struct in_addr);
    }

  /* get a "list" of alternative names */
  altnames = (STACK_OF(GENERAL_NAME)*)X509_get_ext_d2i(server_cert, NID_subject_alt_name, NULL, NULL);

  if(altnames) {
    int numalts;
    int i;

    /* get amount of alternatives, RFC2459 claims there MUST be at least
       one, but we don't depend on it... */
    numalts = sk_GENERAL_NAME_num(altnames);

    /* loop through all alternatives while none has matched */
    for (i=0; (i<numalts) && (matched != 1); i++) {
      /* get a handle to alternative name number i */
      const GENERAL_NAME *check = sk_GENERAL_NAME_value(altnames, i);

      /* only check alternatives of the same type the target is */
      if(check->type == target) {
        /* get data and length */
        const char *altptr = (char *)ASN1_STRING_data(check->d.ia5);
        size_t altlen = (size_t) ASN1_STRING_length(check->d.ia5);

        switch(target) {
        case GEN_DNS: /* name/pattern comparison */
          /* The OpenSSL man page explicitly says: "In general it cannot be
             assumed that the data returned by ASN1_STRING_data() is null
             terminated or does not contain embedded nulls." But also that
             "The actual format of the data will depend on the actual string
             type itself: for example for and IA5String the data will be ASCII"

             Gisle researched the OpenSSL sources:
             "I checked the 0.9.6 and 0.9.8 sources before my patch and
             it always 0-terminates an IA5String."
          */
          if((altlen == strlen(altptr)) &&
             /* if this isn't true, there was an embedded zero in the name
                string and we cannot match it. */
             cert_hostcheck(altptr, hostname))
            matched = 1;
          else
            matched = 0;
          break;

        case GEN_IPADD: /* IP address comparison */
          /* compare alternative IP address if the data chunk is the same size
             our server IP address is */
          if((altlen == addrlen) && !memcmp(altptr, &addr, altlen))
            matched = 1;
          else
            matched = 0;
          break;
        }
      }
    }
    GENERAL_NAMES_free(altnames);
  }

  if(matched == 1)
    /* an alternative name matched the server hostname */
    Log::global->Format(9, "Certificate verification: subjectAltName: %s matched\n", quote(hostname));
  else if(matched == 0) {
    /* an alternative name field existed, but didn't match and then
       we MUST fail */
    set_cert_error(xstring::format("subjectAltName does not match %s", quote(hostname)));
  }
  else {
    /* we have to look to the last occurence of a commonName in the
       distinguished one to get the most significant one. */
    int j,i=-1 ;

/* The following is done because of a bug in 0.9.6b */

    unsigned char *nulstr = (unsigned char *)"";
    unsigned char *peer_CN = nulstr;

    X509_NAME *name = X509_get_subject_name(server_cert) ;
    if(name)
      while((j = X509_NAME_get_index_by_NID(name, NID_commonName, i))>=0)
        i=j;

    /* we have the name entry and we will now convert this to a string
       that we can use for comparison. Doing this we support BMPstring,
       UTF8 etc. */

    if(i>=0) {
      ASN1_STRING *tmp = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name,i));

      /* In OpenSSL 0.9.7d and earlier, ASN1_STRING_to_UTF8 fails if the input
         is already UTF-8 encoded. We check for this case and copy the raw
         string manually to avoid the problem. This code can be made
         conditional in the future when OpenSSL has been fixed. Work-around
         brought by Alexis S. L. Carvalho. */
      if(tmp) {
        if(ASN1_STRING_type(tmp) == V_ASN1_UTF8STRING) {
          j = ASN1_STRING_length(tmp);
          if(j >= 0) {
            peer_CN = (unsigned char*)OPENSSL_malloc(j+1);
            if(peer_CN) {
              memcpy(peer_CN, ASN1_STRING_data(tmp), j);
              peer_CN[j] = '\0';
            }
          }
        }
        else /* not a UTF8 name */
          j = ASN1_STRING_to_UTF8(&peer_CN, tmp);

        if(peer_CN && ((int)strlen((char *)peer_CN) != j)) {
          /* there was a terminating zero before the end of string, this
             cannot match and we return failure! */
          set_cert_error("illegal cert name field (contains NUL character)");
        }
      }
    }

    if(peer_CN == nulstr)
       peer_CN = NULL;
    else {
      /* convert peer_CN from UTF8 */
      if(!convert_from_utf8((char*)peer_CN, strlen((char*)peer_CN)))
	 set_cert_error("invalid cert name field (cannot convert from UTF8)");
    }

    if(cert_error)
      /* error already detected, pass through */
      ;
    else if(!peer_CN) {
      set_cert_error("unable to obtain common name from peer certificate");
    }
    else if(!cert_hostcheck((const char *)peer_CN, hostname)) {
        set_cert_error(xstring::format("certificate subject name %s does not match "
              "target host name %s", quote_n(0,(const char *)peer_CN), quote_n(1,hostname)));
    }
    else {
      Log::global->Format(9, "Certificate verification: common name: %s matched\n", quote((char*)peer_CN));
    }
    if(peer_CN)
      OPENSSL_free(peer_CN);
  }
}
/* end curl code */

/* begin wget-1.12 code */
#if 0
#define ASTERISK_EXCLUDES_DOT   /* mandated by rfc2818 */

/* Return true is STRING (case-insensitively) matches PATTERN, false
   otherwise.  The recognized wildcard character is "*", which matches
   any character in STRING except ".".  Any number of the "*" wildcard
   may be present in the pattern.

   This is used to match of hosts as indicated in rfc2818: "Names may
   contain the wildcard character * which is considered to match any
   single domain name component or component fragment. E.g., *.a.com
   matches foo.a.com but not bar.foo.a.com. f*.com matches foo.com but
   not bar.com [or foo.bar.com]."

   If the pattern contain no wildcards, pattern_match(a, b) is
   equivalent to !strcasecmp(a, b).  */

static bool
pattern_match (const char *pattern, const char *string)
{
  const char *p = pattern, *n = string;
  char c;
  for (; (c = c_tolower (*p++)) != '\0'; n++)
    if (c == '*')
      {
        for (c = c_tolower (*p); c == '*'; c = c_tolower (*++p))
          ;
        for (; *n != '\0'; n++)
          if (c_tolower (*n) == c && pattern_match (p, n))
            return true;
#ifdef ASTERISK_EXCLUDES_DOT
          else if (*n == '.')
            return false;
#endif
        return c == '\0';
      }
    else
      {
        if (c != c_tolower (*n))
          return false;
      }
  return *n == '\0';
}

/* Verify the validity of the certificate presented by the server.
   Also check that the "common name" of the server, as presented by
   its certificate, corresponds to HOST.  (HOST typically comes from
   the URL and is what the user thinks he's connecting to.)

   This assumes that ssl_connect_wget has successfully finished, i.e. that
   the SSL handshake has been performed and that FD is connected to an
   SSL handle.

   If opt.check_cert is true (the default), this returns 1 if the
   certificate is valid, 0 otherwise.  If opt.check_cert is 0, the
   function always returns 1, but should still be called because it
   warns the user about any problems with the certificate.  */

void lftp_ssl_openssl::check_certificate_wget ()
{
  X509 *cert;
  char common_name[256];

  cert = SSL_get_peer_certificate (ssl);
  if (!cert)
    {
      set_cert_error(xstring::format(_("No certificate presented by %s.\n"),
                 quotearg_style (escape_quoting_style, hostname)));
      return;
    }

  /* LAV: certificate validity is already checked in callback. */

  /* Check that HOST matches the common name in the certificate.
     #### The following remains to be done:

     - It should use dNSName/ipAddress subjectAltName extensions if
       available; according to rfc2818: "If a subjectAltName extension
       of type dNSName is present, that MUST be used as the identity."

     - When matching against common names, it should loop over all
       common names and choose the most specific one, i.e. the last
       one, not the first one, which the current code picks.

     - Ensure that ASN1 strings from the certificate are encoded as
       UTF-8 which can be meaningfully compared to HOST.  */

  X509_NAME *xname = X509_get_subject_name(cert);
  common_name[0] = '\0';
  X509_NAME_get_text_by_NID (xname, NID_commonName, common_name,
                             sizeof (common_name));

  if (!pattern_match (common_name, hostname))
    {
      set_cert_error(xstring::format(_("certificate common name %s doesn't match requested host name %s"),
                 quote_n (0, common_name), quote_n (1, hostname)));
    }
  else
    {
      /* We now determine the length of the ASN1 string. If it differs from
       * common_name's length, then there is a \0 before the string terminates.
       * This can be an instance of a null-prefix attack.
       *
       * https://www.blackhat.com/html/bh-usa-09/bh-usa-09-archives.html#Marlinspike
       * */

      int i = -1, j;
      X509_NAME_ENTRY *xentry;
      ASN1_STRING *sdata;

      if (xname) {
        for (;;)
          {
            j = X509_NAME_get_index_by_NID (xname, NID_commonName, i);
            if (j == -1) break;
            i = j;
          }
      }

      xentry = X509_NAME_get_entry(xname,i);
      sdata = X509_NAME_ENTRY_get_data(xentry);
      if (strlen (common_name) != (size_t)ASN1_STRING_length (sdata))
          set_cert_error(_("certificate common name is invalid (contains a NUL character)"));
    }

  X509_free (cert);
}
#endif
/* end wget code */

int lftp_ssl_openssl::verify_callback(int ok,X509_STORE_CTX *ctx)
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

   if(ok && !verify_crl(ctx))
      ok=0;

   int error=X509_STORE_CTX_get_error(ctx);

   bool verify=ResMgr::QueryBool("ssl:verify-certificate",verify_callback_host);

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
#endif // USE_OPENSSL

#endif // USE_SSL
