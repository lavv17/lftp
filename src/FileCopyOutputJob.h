/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
};

#endif
