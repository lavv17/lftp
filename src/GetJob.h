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

#ifndef GETJOB_H
#define GETJOB_H

#include "FileXfer.h"

class GetJob : public FileXfer
{
protected:
   void	 NextFile();

   time_t set_file_time;
   time_t file_time;
   bool delete_files:1;
   bool	deleting:1;
   bool made_backup:1;

   void RemoveBackupFile();

public:
   int	 Do();

   void SetTime(time_t t) { set_file_time=t; }
   void DeleteFiles() { delete_files=true; }

   GetJob(FileAccess *s,ArgV *args,bool cont=false)
      : FileXfer(s,args,cont)
   {
      set_file_time=(time_t)-1;
      file_time=(time_t)-2;
      delete_files=deleting=made_backup=false;
   }
};

#endif /* GETJOB_H */
