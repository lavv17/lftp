/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

class FtpSplitList : public Glob
{
   enum state_t
      {
	 INITIAL,
	 GETTING_DATA,
	 DONE
      };
   state_t state;

   FileAccess::open_mode mode;

   int	 inbuf;
   char	 *buf;
   char	 *ptr;
   bool	 from_cache;

   FileAccess  *f;

   void Init(FileAccess *session,FA::open_mode n_mode);

public:
   int	 Do();
   const char *Status();

   FtpSplitList(FileAccess *session,
	    FileAccess::open_mode n_mode=FileAccess::LIST);
   virtual ~FtpSplitList();
};

#endif
