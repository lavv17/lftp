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

#include <config.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "xalloca.h"
#include "url.h"

/*
   URL -> [PROTO://]CONNECT[[:]/PATH]
   CONNECT -> [USER[:PASS]@]HOST[:PORT]
*/

ParsedURL::ParsedURL(const char *url)
{
   memory=(char*)xmalloc(strlen(url)*2+1);
   strcpy(memory,url);

   proto=0;
   host=0;
   user=0;
   pass=0;
   port=0;
   path=0;

   char *base=memory;
   char *scan=base;
   while(isalpha(*scan))
      scan++;
   if(scan[0]==':' && scan[1]=='/' && scan[2]=='/')
   {
      // found protocol
      *scan=0;
      scan+=3;
      proto=base;

      base=scan;
   }
   else if(scan[0]==':' && !strncmp(base,"file:",5))
   {
      // special form for file protocol
      *scan=0;
      scan++;
      proto=base;
      path=scan;
      host="localhost";
      return;
   }

   scan=base;
   while(*scan && *scan!='/')
      scan++; // skip host name, port and user:pass

   if(*scan=='/') // directory
   {
      if(scan-1>=base && scan[-1]==':')
	 scan[-1]=0;
      if(scan[1]!='~')
	 memmove(scan+1,scan,strlen(scan)+1);
      *scan++=0;
      path=scan;
   }

   // try to extract user name/password
   scan=base;
   while(*scan && *scan!='@')
      scan++;
   if(*scan=='@')
   {
      *scan++=0;
      user=base;
      base=scan;

      scan=user;
      while(*scan && *scan!=':')
	 scan++;
      if(*scan==':')
      {
	 *scan++=0;
	 pass=scan;
      }
   }

   // extract host name/password
   scan=base;
   host=base;
   while(*scan && *scan!=':')
      scan++;

   if(*scan==':') // port found
   {
      *scan++=0;
      port=scan;
   }
}
