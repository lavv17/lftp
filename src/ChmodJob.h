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

#ifndef CHMODJOB_H
#define CHMODJOB_H

#include "TreatFileJob.h"
#include "FileSet.h"
CDECL_BEGIN
#include "modechange.h"
CDECL_END

class ChmodJob : public TreatFileJob
{
public:
   enum verbosity { V_NONE, V_CHANGES, V_ALL };

private:
   void TreatCurrent(const char *d,const FileInfo *fi);
   void CurrentFinished(const char *d,const FileInfo *fi);

   void Init();
   void Report(const char *d,const FileInfo *fi, bool success);
   bool RelativeMode(const mode_change *m) const;

   verbosity verbose;
   mode_change *m;
   int simple_mode;
   int GetMode(const FileInfo *fi) const;

public:
   /* if you use this constructor, also set a mode with SetMode() */
   ChmodJob(FileAccess *s,ArgV *a);
   /* simple "chmod 123" interface: */
   ChmodJob(FileAccess *s,int m,ArgV *a);
   ~ChmodJob();

   void SetVerbosity(verbosity v);
   void SetMode(mode_change *newm);
   void Recurse();
};

#endif//CHMODJOB_H
