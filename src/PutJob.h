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

#ifndef PUTJOB_H
#define PUTJOB_H

#include "FileXfer.h"

class PutJob : public FileXfer
{
   FileAccess::fileinfo info;

protected:
   void	 NextFile();

   long	 remote_size;

   bool delete_files;

public:
   int	 Do();

   PutJob(FileAccess *s,ArgV *args,bool cont=false)
      : FileXfer(s,args,cont)
   {
      remote_size=0;
      delete_files=false;
   }
};

#endif /* PUTJOB_H */
