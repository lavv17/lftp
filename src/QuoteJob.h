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

#ifndef QUOTEJOB_H
#define QUOTEJOB_H

#include "XferJob.h"
#include "Filter.h"
#include "xmalloc.h"

class QuoteJob : public XferJob
{
   FDStream *local;
   char *quote_cmd;
public:
   QuoteJob(FileAccess *f,char *op,char *cmd,FDStream *o) : XferJob(f)
   {
      this->op=op;
      curr=cmd;
      quote_cmd=cmd;
      local=o;
      print_run_status=!local->usesfd(1);
      session->Open(quote_cmd,FA::QUOTE_CMD);
   }
   ~QuoteJob()
   {
      if(local)
	 delete local;
      if(quote_cmd)
	 free(quote_cmd);
   }
   int	 Do();
   int	 Done();
};

#endif//QUOTEJOB_H
