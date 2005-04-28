/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef LFTP_SSL_H
#define LFTP_SSL_H

#if USE_SSL
# if USE_GNUTLS
#  include <gnutls/gnutls.h>
# elif USE_OPENSSL
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <openssl/rand.h>
#endif

class lftp_ssl
{
#if USE_GNUTLS
   gnutls_session_t session;
#elif USE_OPENSSL
   SSL *ssl;
   int do_handshake();
   bool check_fatal(int res);
#endif

public:
   int fd;
   char *hostname;
   enum handshake_mode_t { CLIENT, SERVER } handshake_mode;
   bool fatal;

   lftp_ssl(int fd,handshake_mode_t m,const char *host=0);
   ~lftp_ssl();
   const char *strerror(const char *s);

   enum code { RETRY=-2, ERROR=-1, DONE=0 };
   int read(char *buf,int size);
   int write(const char *buf,int size);
   bool want_in();
   bool want_out();
   void copy_sid(const lftp_ssl *);
};

#endif//USE_SSL

#endif//LFTP_SSL_H
