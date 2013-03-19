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

#ifndef BUFFER_STD_H
#define BUFFER_STD_H

#include "Job.h"
#include "buffer.h"

class IOBuffer_STDOUT : public IOBuffer
{
   Job *master;
   int Put_LL(const char *buf,int size);

public:
   IOBuffer_STDOUT(Job *m) : IOBuffer(PUT) { master=m; }
};

#endif //BUFFER_STD_H
