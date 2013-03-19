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

#include <config.h>
#include "Error.h"

Error::Error() : code(0), fatal(false) {}
Error::Error(int c,const char *s,bool f) : text(s), code(c), fatal(f) {}
Error::~Error() {}

void Error::Set(int c,const char *s,bool f)
{
   text.set(s);
   code=c;
   fatal=f;
}

Error *Error::Fatal(const char *s,int c)
{
   return new Error(c,s,true);
}
