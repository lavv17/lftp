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

#ifndef LSJOB_H
#define LSJOB_H

#include "XferJob.h"
#include "Filter.h"

class LsJob : public XferJob
{
protected:
   char	 *arg;
   ArgV  *args;
   FDStream *local;

   void NextFile();

   char	 *cache_buffer;
/*   char	 *cache_buffer_ptr;*/
   int   cache_buffer_size;
   bool	 from_cache;

   int	 mode;

   DirList *dl;

public:
   int	 Do();
   int	 Done();

   LsJob(FileAccess *s,FDStream *g,ArgV *args,int mode=FA::LONG_LIST);
   ~LsJob();

   void	 NoCache();
};

#endif /* LSJOB_H */
