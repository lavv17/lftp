/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef DIRCOLORS_H
#define DIRCOLORS_H

#include "SMTask.h"
#include "keyvalue.h"

class FileInfo;

class DirColors : public SMTask, public KeyValueDB
{
   static DirColors *instance;

   static const char * const resource="color:dir-colors";
   void Parse(const char *);

public:
   int Do() { return STALL; }
   void Reconfig(const char *name);

   DirColors();

   const char *GetColor(const FileInfo *);

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
