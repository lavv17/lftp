/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef LFTP_RL_H
#define LFTP_RL_H

CDECL_BEGIN

void lftp_readline_init (void);
int  lftp_history_expand (const char *, char **);
char *lftp_readline (const char *prompt);
void lftp_add_history_nodups(const char *cmd);
void lftp_rl_clear(void);
void lftp_rl_redisplay_maybe(void);

CDECL_END

#endif /* LFTP_RL_H */
