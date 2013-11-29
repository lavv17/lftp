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

#ifndef DIRCOLORS_H
#define DIRCOLORS_H

#include "SMTask.h"
#include "keyvalue.h"
#include "buffer.h"

class FileInfo;

class DirColors : public ResClient, public KeyValueDB
{
   static DirColors *instance;

   static const char resource[];
   void Parse(const char *);

public:
   void Reconfig(const char *name);

   DirColors();

   const char *GetColor(const FileInfo *);
   const char *GetColor(const char *,int);
   void PutColored(const Ref<Buffer>& buf,const char *name,int type);
   void PutReset(const Ref<Buffer>& buf);

   static DirColors *GetInstance()
      {
	 if(!instance)
	    instance=new DirColors();
	 return instance;
      }
   static void DeleteInstance()
      {
	 delete instance;
	 instance=0;
      }
};
#endif
