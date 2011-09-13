/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2011 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id: lftp_pty.h,v 1.2 2008/11/27 05:56:38 lav Exp $ */

#ifndef LFTP_PTY_H
#define LFTP_PTY_H

CDECL_BEGIN

/* opens a pseudo-tty, returns slave tty name if successful */
const char *open_pty(int *ptyfd, int *ttyfd);

CDECL_END

#endif
