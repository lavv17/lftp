/*
 * poll on select implementation
 *
 * Copyright (c) 1996 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <poll.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <assert.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

int poll(struct pollfd *pfd,unsigned nfd,int timeout)
{
	fd_set	in,out,pri;
	struct timeval tv;
	int	n=0;
	int	i;

	FD_ZERO(&in);
	FD_ZERO(&out);
	FD_ZERO(&pri);

	for(i=0; i<nfd; i++)
	{
		assert(pfd[i].fd>=0);
		if(pfd[i].events&POLLIN)
			FD_SET(pfd[i].fd,&in);
		if(pfd[i].events&POLLOUT)
			FD_SET(pfd[i].fd,&out);
		if(pfd[i].events&POLLPRI)
			FD_SET(pfd[i].fd,&pri);
		if(pfd[i].fd>=n)
			n++;
	}
	tv.tv_sec=timeout/1000;
	tv.tv_usec=(timeout%1000)*1000;

	n=select(FD_SETSIZE,&in,&out,&pri,timeout==-1?NULL:&tv);

	if(n==-1)
		return(-1);

	for(i=0; i<nfd; i++)
	{
		pfd[i].revents=0;
		if(FD_ISSET(pfd[i].fd,&in))
			pfd[i].revents|=POLLIN;
		if(FD_ISSET(pfd[i].fd,&out))
			pfd[i].revents|=POLLOUT;
		if(FD_ISSET(pfd[i].fd,&pri))
			pfd[i].revents|=POLLPRI;
	}

	return(n);
}
