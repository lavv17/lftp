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

#include <errno.h>
#include <unistd.h>
#include "xstring.h"
#include "QuoteJob.h"

int   QuoteJob::Done()
{
   return local==0;
}

int   QuoteJob::Do()
{
   RateDrain();

   int m=STALL;

   if(Done())
      return STALL;

   if(got_eof && in_buffer==0)
   {
      if(local->Done())
      {
	 delete local;
	 local=0;
	 return MOVED;
      }
   }

   int res;

   if(!got_eof)
   {
      res=TryRead(session);
      if(res<0 && res!=Ftp::DO_AGAIN)
      {
	 local->remove_if_empty();
	 got_eof=true; in_buffer=0;
	 return MOVED;
      }
      else if(res>=0)
	 m=MOVED;
   }

   res=TryWrite(local);
   if(res<0)
   {
      got_eof=true; in_buffer=0;
      return MOVED;
   }
   if(res>0)
      m=MOVED;

   return m;
}
