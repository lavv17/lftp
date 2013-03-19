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
#include <signal.h>
#include <unistd.h>
#include "trio.h"
#include "fg.h"

void FgData::cont()
{
   if(!pg)
      return;
   if(kill(-pg,SIGCONT)==-1)
      kill(pg,SIGCONT);
}

FgData::FgData(pid_t npg,bool fg)
{
   old_pgrp=0;
   pg=npg;
   if(!pg)
      return;
   if(fg)
      Fg();
   else
      cont();
}

FgData::~FgData()
{
   if(old_pgrp)
      Bg();
}

void FgData::Fg()
{
   if(!pg)
      return;
   pid_t tc_grp=tcgetpgrp(0);
   if(tc_grp==-1 || tc_grp==getpgrp())
   {
      old_pgrp=getpgrp();
#ifdef FG_DEBUG
      printf("fg: tcsetpgrp(%d)\n",(int)pg);
#endif
      tcsetpgrp(0,pg);
   }
   cont();
}

void FgData::Bg()
{
   if(old_pgrp)
   {
#ifdef FG_DEBUG
      printf("bg: tcsetpgrp(%d)\n",(int)old_pgrp);
#endif
      tcsetpgrp(0,old_pgrp);
      old_pgrp=0;
   }
}
