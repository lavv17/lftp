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

SSL_CTX *ssl_ctx;

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
   SSL_CTX_set_default_verify_paths(ssl_ctx);
#endif /* SSLEAY_VERSION_NUMBER < 0x0800 */
}

SSL *lftp_ssl_new(int fd)
{
   lftp_ssl_init();
   lftp_ssl_ctx_init();
   SSL *ssl=SSL_new(ssl_ctx);
   SSL_set_fd(ssl,fd);
   SSL_ctrl(ssl,SSL_CTRL_MODE,SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,0);
   return ssl;
}

const char *lftp_ssl_strerror(const char *s)
{
   SSL_load_error_strings();
   const char *ssl_error=ERR_error_string(ERR_get_error(),NULL);
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

#endif
