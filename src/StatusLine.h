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

#ifndef STATUSLINE_H
#define STATUSLINE_H

#include "SMTask.h"
#include "Timer.h"
#include "StringSet.h"

class StatusLine : public SMTask
{
   int fd;
   StringSet shown;
   bool	not_term;
   Timer update_timer;
   StringSet to_be_shown;
   char def_title[0x800];
   bool update_delayed;
   void update(const char *const *,int);
   void update(const char *s) { update(&s,1); }
   int LastWidth;
   int LastHeight;
   bool next_update_title_only;
   static const char *to_status_line, *from_status_line, *prev_line;

protected:
   void WriteTitle(const char *s, int fd) const;

public:
   int GetWidth();
   int GetHeight();
   int GetWidthDelayed() const { return LastWidth; }
   int GetHeightDelayed() const { return LastHeight; }
   void NextUpdateTitleOnly() { next_update_title_only=true; }
   void DefaultTitle(const char *s);
   void ShowN(const char *const* newstr,int n);
   void Show(const char *f,...) PRINTF_LIKE(2,3);
   void Show(const StringSet &s) { ShowN(s.Set(),s.Count()); }
   void Show(const StringSet *s) { Show(*s); }
   void WriteLine(const char *f,...) PRINTF_LIKE(2,3);
   void Clear(bool title_also=true);
   bool CanShowNow() { return update_timer.Stopped(); }

   int getfd() const { return fd; }

   StatusLine(int new_fd);
   ~StatusLine();

   int Do();
};

#endif // STATUSLINE_H
