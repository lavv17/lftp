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

#ifndef FG_H
#define FG_H

#include <sys/types.h>


#if 0
/* Assume Posix termio if we have the header and function */
#if HAVE_TERMIOS_H && HAVE_TCGETATTR
#ifndef TERMIOS
#define TERMIOS 1
#endif
#include <termios.h>
#define TTY struct termios

#else /* !HAVE_TERMIOS_H */

#if HAVE_TERMIO_H
#ifndef TERMIOS
#define TERMIOS 1
#endif
#include <termio.h>
#define TTY struct termio
#define TCSANOW TCSETA
#define TCSADRAIN TCSETAW
#define TCSAFLUSH TCSETAF
#define tcsetattr(fd, cmd, arg) ioctl(fd, cmd, arg)
#define tcgetattr(fd, arg) ioctl(fd, TCGETA, arg)
#define cfgetospeed(t) ((t)->c_cflag & CBAUD)
#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2
#define tcflush(fd, arg) ioctl(fd, TCFLSH, arg)

#else /* !HAVE_TERMIO_H */

#undef TERMIOS
#include <sgtty.h>
#include <sys/ioctl.h>
#define TTY struct sgttyb

#endif /* HAVE_TERMIO_H */

#endif /* HAVE_TERMIOS_H */

#ifdef TERMIOS
#define GET_TTY(fd, buf) tcgetattr(fd, buf)
#define SET_TTY(fd, buf) tcsetattr(fd, TCSADRAIN, buf)
#else
#define GET_TTY(fd, buf) gtty(fd, buf)
#define SET_TTY(fd, buf) stty(fd, buf)
#endif

#endif // 0

class FgData
{
   pid_t pg;
   pid_t old_pgrp;
#if 0 // not done yet
   TTY tc;
   TTY old_tc;
#endif
public:
   FgData(pid_t npg,bool fg);
   ~FgData();
   void Fg(),Bg();
   void cont();
};

#endif//FG_H
