/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#ifndef RESOLVER_H
#define RESOLVER_H

#include "ProcWait.h"
#include <netinet/in.h>
#include <netdb.h>

class Resolver : public SMTask
{
   char *hostname;
   int port;

   int pipe_to_child[2];
   ProcWait *w;
   int timeout;

   time_t start_time;

   struct sockaddr_in sa;

   char *err_msg;
   bool done;

   void  MakeErrMsg(const char *f);
   void	 DoGethostbyname();

public:
   int	 Do();
   bool	 Done() { return done; }
   bool	 Error() { return err_msg!=0; }
   const char *ErrorMsg() { return err_msg; }
   struct sockaddr_in *Result() { return &sa; }
   void  GetResult(void *m) { memcpy(m,&sa,sizeof(sa)); }

   Resolver(const char *h,int port);
   ~Resolver();

   void Reconfig();
};

#endif // RESOLVER_H
