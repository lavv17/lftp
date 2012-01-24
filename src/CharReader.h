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

#ifndef CHARREADER_H
#define CHARREADER_H

#include "SMTask.h"

// CharReader fetches a single character from given file descriptor.
class CharReader : public SMTask
{
   int	 fd;
   int	 ch;

   int	 Do();

public:
   enum { NOCHAR=-2, EOFCHAR=-1 };

   int	 GetChar() { return ch; };

   CharReader(int new_fd)
   {
      fd=new_fd;
      ch=NOCHAR;
   }
};

#endif//CHARREADER_H
