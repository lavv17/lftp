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

#include <config.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "FileXfer.h"

FileXfer::~FileXfer()
{
   XferJob::NextFile(0);
   if(local)
      delete local;
   if(args)
      delete args;
   if(saved_cwd)
      free(saved_cwd);
};

FileXfer::FileXfer(FileAccess *new_session,ArgV *new_args,bool cont) : XferJob(new_session)
{
   local=0;
   this->cont=cont;
   args=new_args;
   if(args && args->count()>=1)
   {
      op=args->getarg(0);
      args->rewind();
   }
   saved_cwd=getcwd(0,1024);
}
