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

#ifndef DISPCOLUMNS_H
#define DISPCOLUMNS_H

#include "OutputJob.h"
#include "xarray.h"

class datum
{
   int ws, curwidth;

public:
   /* Each entry has a series of strings; each string has a color. */
   StringSet names;
   StringSet colors;
   datum() : ws(0), curwidth(0) {}

   void append(const char *name, const char *color);

   // get the total display width
   int width() const { return curwidth; }

   // print with or without color
   void print(const JobRef<OutputJob>& o, bool color, int skip,
	   const char *color_pref, const char *color_suf, const char *color_reset) const;

   /* count leading whitespace in the first name only. */
   int whitespace() const { return ws; }
};

class ColumnOutput {
   void get_print_info(unsigned width, xarray<int> &col_arr, xarray<int> &ws_arr, int &cols) const;

public:
   enum mode_t { VERT };

   void append();
   void add(const char *name, const char *color);
   void addf(const char *fmt, const char *color, ...);

   void SetWidth(unsigned width);
   void SetColor(bool color);

   void print(const JobRef<OutputJob>& o, unsigned width, bool color) const;
   void set_mode(mode_t m) { mode = m; }

private:
   RefArray<datum> lst;

   mode_t mode;
};

#endif
