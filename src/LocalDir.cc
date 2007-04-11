/*
 * lftp and utils
 *
 * Copyright (c) 2002 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "LocalDir.h"
#include "misc.h"
#include "xmalloc.h"

#ifndef O_DIRECTORY
# define O_DIRECTORY 0
#endif

void LocalDirectory::SetFromCWD()
{
   Unset();
   fd=open(".",O_RDONLY|O_DIRECTORY);
   name.set_allocated(xgetcwd());
}

const char *LocalDirectory::Chdir()
{
#ifdef HAVE_FCHDIR
   if(fd!=-1)
   {
      int res=fchdir(fd);
      if(res==-1)
	 return strerror(errno);
      return 0;
   }
#endif
   if(name)
   {
      int res=chdir(name);
      if(res==-1)
	 return strerror(errno);
      return 0;
   }
   return "Directory location is undefined";
}

const char *LocalDirectory::GetName()
{
   return name;
}

void LocalDirectory::Unset()
{
   if(fd!=-1)
      close(fd);
   fd=-1;
   name.set(0);
}

LocalDirectory::LocalDirectory()
{
   fd=-1;
}

LocalDirectory::LocalDirectory(const LocalDirectory *o)
{
   fd=-1;
   if(o->fd!=-1)
      fd=dup(o->fd);
   name.set(o->name);
}

LocalDirectory *LocalDirectory::Clone() const
{
   return new LocalDirectory(this);
}

LocalDirectory::~LocalDirectory()
{
   Unset();
}
