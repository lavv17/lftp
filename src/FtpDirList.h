/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FTPDIRLIST_H
#define FTPDIRLIST_H

class FtpDirList : public DirList
{
   SMTaskRef<IOBuffer> ubuf;
   const xstring_ca pattern;

   bool TryEPLF(const char *line,int len);
   bool TryMLSD(const char *line,int len);
   bool TryColor(const char *line,int len);

   void FormatGeneric(class FileInfo *);

public:
   FtpDirList(FileAccess *s,ArgV *a)
      : DirList(s,a), pattern(args->Combine(1)) {}
   const char *Status();
   int Do();

   void SuspendInternal();
   void ResumeInternal();
};

#endif//FTPDIRLIST_H
