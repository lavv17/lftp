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

#ifndef FTPGLOB_H
#define FTPGLOB_H

#include "FileAccess.h"

class FtpGlob : public Glob
{
   enum state_t
      {
	 INITIAL,
	 GETTING_DATA,
	 DONE
      };
   state_t state;

   FileAccess::open_mode mode;
   char	 *dir;

   int	 inbuf;
   char	 *buf;
   char	 *ptr;
   bool	 from_cache;
   bool	 use_long_list;

   FileAccess  *f;

   int	 flags;
   enum { RESTRICT_SLASHES=1,RESTRICT_PATH=2,NO_CHANGE=4 };
   int	 extra_slashes;

   void Init(FileAccess *session,FA::open_mode n_mode);

public:
   int	 Do();
   const char *Status();

   FtpGlob(FileAccess *session,const char *n_pattern,
	    FileAccess::open_mode n_mode=FileAccess::LIST);
   virtual ~FtpGlob();


   void	 SetSlashFilter(int num)
   {
      extra_slashes=num;
      flags|=RESTRICT_SLASHES;
   }
   void RestrictPath() { flags|=RESTRICT_PATH; }
/*   void NoCache() { use_cache=false; }*/
   void NoCheck() { flags&=~(RESTRICT_SLASHES|RESTRICT_PATH); }
   void NoChange() { flags|=NO_CHANGE; }
   void NoLongList() { use_long_list=false; }
};

#endif
