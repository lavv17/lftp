/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef DISPCOLUMNS_H
#define DISPCOLUMNS_H

#include "OutputJob.h"

struct datum {
   /* Each entry has a series of strings; each string has a color. */
   char **names, **colors;
   int num;
   datum();
   ~datum();

   void append(const char *name, const char *color);

   // get the total display width
   int width() const;

   // print with or without color
   void print(OutputJob *o, bool color, int skip,
	   const char *color_pref, const char *color_suf, const char *color_reset) const;

   /* count leading whitespace in the first name only. */
   int whitespace() const;

private:
   int ws, curwidth;
};

class ColumnOutput {
   void get_print_info(unsigned width, int *&col_arr, int *&ws_arr, int &cols) const;

public:
   enum mode_t { VERT };
   ColumnOutput();
   ~ColumnOutput();

   void append();
   void add(const char *name, const char *color);
   void addf(const char *fmt, const char *color, ...);

   void SetWidth(unsigned width);
   void SetColor(bool color);

   void print(OutputJob *o, unsigned width, bool color) const;
   void set_mode(mode_t m) { mode = m; }

private:
   datum **lst;
   int lst_cnt, lst_alloc;

   mode_t mode;
};

#endif
