/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifdef USE_SSL
# include <openssl/ssl.h>
# include <openssl/err.h>

SSL *lftp_ssl_new(int fd,const char *host=0);
const char *lftp_ssl_strerror(const char *s);
int lftp_ssl_connect(SSL *,const char *host=0);

#endif//USE_SSL

#endif//LFTP_SSL_H
