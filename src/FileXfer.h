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

#ifndef FILEXFER_H
#define FILEXFER_H

#include "XferJob.h"
#include "Filter.h"

class FileXfer : public XferJob
{
protected:
   FDStream *local;

   bool cont;

   ArgV	 *args;

   char *saved_cwd;

public:
   FileXfer(FileAccess *s,ArgV *args,bool cont=false); // takes pairs of file names in args
   virtual ~FileXfer();

   void AddFile(char *f1,char *f2)
   {
      args->Append(f1);
      args->Append(f2);
   }
};

#endif /* FILEXFER_H */
