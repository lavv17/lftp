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

#ifndef RMJOB_H
#define RMJOB_H

#include "TreatFileJob.h"

class rmJob : public TreatFileJob
{
   void TreatCurrent();

protected:
   FA::open_mode mode;

public:
   void	 SayFinal();

   rmJob(FileAccess *session,ArgV *a);
};

class rmdirJob : public rmJob
{
public:
   rmdirJob(FileAccess *session,ArgV *a) : rmJob(session,a)
   {
      mode=FA::REMOVE_DIR;
   }
};

#endif // RMJOB_H
