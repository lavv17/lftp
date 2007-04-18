/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FILECOPYPEEROUTPUTJOB_H
#define FILECOPYPEEROUTPUTJOB_H

#include "OutputJob.h"

class FileCopyPeerOutputJob : public FileCopyPeer
{
   const JobRef<OutputJob>& o;
   int Put_LL(const char *buf,int len);

public:
   FileCopyPeerOutputJob(const JobRef<OutputJob>& o);

   int Do();
   void Fg();
   void Bg();

   const char *GetDescriptionForLog()
      {
	 return "[pipe to other job]";
      }
};

#endif
