/*
 * lftp and utils
 *
 * Copyright (c) 1996-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#ifndef STATUSLINE_H
#define STATUSLINE_H

#include "SMTask.h"
#include "Timer.h"

class StatusLine : public SMTask
{
   int fd;
   char shown[0x800];
   bool	not_term;
   Timer update_timer;
   char to_be_shown[0x800];
   char def_title[0x800];
   bool update_delayed;
   void update(char *);
   int LastWidth;
   bool next_update_title_only;

protected:
   ~StatusLine();
   void WriteTitle(const char *s, int fd) const;

public:
   int GetWidth();
   int GetWidthDelayed() const { return LastWidth; }
   void NextUpdateTitleOnly() { next_update_title_only=true; }
   void DefaultTitle(const char *s);
   void Show(const char *f,...) PRINTF_LIKE(2,3);
   void WriteLine(const char *f,...) PRINTF_LIKE(2,3);
   void Clear(bool title_also=true);

   int getfd() const { return fd; }

   StatusLine(int new_fd);

   int Do();
};

#endif // STATUSLINE_H
