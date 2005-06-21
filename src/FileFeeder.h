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

#ifndef FILEFEEDER_H
#define FILEFEEDER_H

#include "CmdExec.h"

class FileFeeder : public CmdFeeder
{
   FDStream *in;
   enum { buffer_size=0x1000 };
   char buffer[buffer_size];
   FgData *fg_data;
public:
   const char *NextCmd(CmdExec *exec,const char *prompt);
   FileFeeder(FDStream *in)
   {
      this->in=in;
      fg_data=0;
   }
   virtual ~FileFeeder()
   {
      delete fg_data;
      delete in;
   }
   void Fg() { if(fg_data) fg_data->Fg(); }
   void Bg() { if(fg_data) fg_data->Bg(); }
};

#endif//FILEFEEDER_H
