/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* All indexes in this function start at 0; -1 is used contextually to
 * indicate the last job or moving to the end of the list. */
#include <config.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

/* Our autoconf test will switch to lib/fnmatch.c if the local fnmatch
 * isn't GNU, so this should be OK. */
#define _GNU_SOURCE
#include <fnmatch.h>

#include "QueueFeeder.h"

const char *QueueFeeder::NextCmd(CmdExec *exec, const char *)
{
   if(jobs == NULL) return NULL;
  
   /* denext the first job */
   QueueJob *job = grab_job(0);

   int bufsize = 1;
   buffer = (char *) xrealloc(buffer, bufsize);
   buffer[0] = 0;
   
   if(cur_pwd == NULL || strcmp(cur_pwd, job->pwd)) {
      bufsize += strlen(job->pwd)*2 + 	/* quote safety */
	         7;			/* 'cd ""; ' */
      buffer = (char *) xrealloc(buffer, bufsize);

      strcat(buffer, "cd \"");
      CmdExec::unquote(buffer+strlen(buffer), job->pwd);
      strcat(buffer, "\"; ");

      xfree(cur_pwd);
      cur_pwd = xstrdup(job->pwd);
   }

   if(cur_lpwd == NULL || strcmp(cur_lpwd, job->lpwd)) {
      bufsize += strlen(job->lpwd)*2 + 	/* quote safety */
	         8;			/* 'lcd ""; ' */
      buffer = (char *) xrealloc(buffer, bufsize);

      strcat(buffer, "lcd \"");
      CmdExec::unquote(buffer+strlen(buffer), job->lpwd);
      strcat(buffer, "\"; ");

      xfree(cur_lpwd);
      cur_lpwd = xstrdup(job->lpwd);
   }

   bufsize += strlen(job->cmd) + 1;
   buffer = (char *) xrealloc(buffer, bufsize);
   strcat(buffer, job->cmd);
   strcat(buffer, "\n");

   delete job;

   return buffer;
}

void QueueFeeder::QueueCmd(const char *cmd, const char *pwd, const char *lpwd, int pos)
{
   QueueJob *job = new QueueJob;
   job->cmd = xstrdup(cmd);
   job->pwd = xstrdup(pwd);
   job->lpwd = xstrdup(lpwd);

   /* we never want a newline at the end: */
   if(job->cmd[strlen(job->cmd)-1] == '\n')
      job->cmd[strlen(job->cmd)-1] = 0;

   insert_jobs(job, jobs, lastjob, pos != -1? get_job(pos): NULL);
}
   
bool QueueFeeder::DelJob(int from)
{
   QueueJob *job = grab_job(from);
   if(!job) return false;
   FreeList(job);
   return true;
}

bool QueueFeeder::DelJob(const char *cmd)
{
   QueueJob *job = grab_job(cmd);
   if(!job) return false;
   FreeList(job);
   return true;
}

/* When moving, grab the insertion pointer *before* pulling out things to
 * move, since doing so will change offsets.  (Note that "to == -1" means
 * "move to the end", not "before the last entry".) 
 */
bool QueueFeeder::MoveJob(int from, int to)
{
   /* Safety: make sure we don't try to move an item before itself. */
   if(from == to) return false;
   
   QueueJob *before = to != -1? get_job(to): NULL;
   
   QueueJob *job = grab_job(from);
   if(job == NULL) return false;

   assert(job != before);

   insert_jobs(job, jobs, lastjob, before);
   return true;
}

bool QueueFeeder::MoveJob(const char *cmd, int to)
{
   QueueJob *before = to != -1? get_job(to): NULL;

   /* Mild hack: we need to make sure the "before" job isn't one that's
    * going to be moved, so move it upward until it isn't. */
   while(before && !fnmatch(cmd, before->cmd,FNM_CASEFOLD)) before=before->next;

   QueueJob *job = grab_job(cmd);
   if(job == NULL) return false;

   insert_jobs(job, jobs, lastjob, before);
   return true;
}

/* remove the given job from the list */
void QueueFeeder::unlink_job(QueueJob *job)
{
   /* update head/tail */
   if(!job->prev) jobs = jobs->next;
   if(!job->next) lastjob = lastjob->prev;

   /* linked list stuff */
   if(job->prev) job->prev->next = job->next;
   if(job->next) job->next->prev = job->prev;
   job->prev = job->next = 0;
}

QueueFeeder::QueueJob *QueueFeeder::get_job(int n)
{
   QueueJob *j;
   if(n == -1) {
      j = lastjob;
   } else {
      j = jobs;
      while(j && n--) j=j->next;
   }

   return j;
}

/* get the n'th job, removed from the list; returns NULL (an empty list)
 * if there aren't that many jobs: */
QueueFeeder::QueueJob *QueueFeeder::grab_job(int n)
{
   QueueJob *j = get_job(n);
   if(j)
      unlink_job(j);
   return j;
}

QueueFeeder::QueueJob *QueueFeeder::grab_job(const char *cmd)
{
   QueueJob *j = jobs, *head = NULL, *tail = NULL;

   while(j) {
      QueueJob *next = j->next;

      if(!fnmatch(cmd, j->cmd,FNM_CASEFOLD))  {
	 /* matches */
	 unlink_job(j);
	 insert_jobs(j, head, tail, NULL);
      }
 
      j = next;
   }

   return head;
}

/* insert a list of jobs before "before", or at the end if before is NULL. 
 * If before is not NULL, it must be contained between lst_head and lst_tail. */
void QueueFeeder::insert_jobs(QueueJob *job,
      			      QueueJob *&lst_head,
      			      QueueJob *&lst_tail,
			      QueueJob *before)
{
   assert(!job->prev); /* this should be an independant, clean list head */

   /* Find the last entry in the new list.  (This is a bit inefficient, as
    * we usually know this somewhere else, but passing around both head
    * and tail pointers of the new job list is too klugy.) */
   QueueJob *tail = job;
   while(tail->next) tail=tail->next;

   if(!before) {
      /* end */
      job->prev = lst_tail;
      tail->next = 0; /* superfluous; here for clarity */
   } else {
      tail->next = before;
      job->prev = before->prev;
   }

   if(job->prev) job->prev->next = job;
   if(tail->next) tail->next->prev = tail;
   if(!tail->next) lst_tail = tail;
   if(!job->prev) lst_head = job;
}

QueueFeeder::QueueJob::~QueueJob()
{
   xfree(cmd);
   xfree(pwd);
   xfree(lpwd);
}

/* Free a list of jobs (forward only; j should be a head pointer.) */
void QueueFeeder::FreeList(QueueJob *j)
{
   while(j) {
      QueueJob *job = j;
      j = j->next;
      delete job;
   }
}

QueueFeeder::~QueueFeeder()
{
   FreeList(jobs);

   xfree(cur_pwd);
   xfree(cur_lpwd);
   xfree(buffer);
}


void QueueFeeder::PrintStatus(int v) const
{
   if(jobs == NULL)
      return;
   
   printf(_("\tCommands queued:\n"));

   int n = 1;

   const char *pwd = cur_pwd, *lpwd = cur_lpwd;
   for(const QueueJob *job = jobs; job; job = job->next) {
      if(v<2 && n>4)
      {
	 printf("\t%2d. ...\n",n);
	 break;
      }
      /* Print pwd/lpwd changes when v >= 2.  Ideally, we should
       * quote these commands, too; but I really don't want to
       * add another 15 lines of code to this function, and this
       * output isn't all that useful for copying and pasting anyway
       * due to formatting.  I'll do it if someone requests it (or
       * we get better strings ...)
       */
      if(v >= 2 && (!cur_pwd || strcmp(cur_pwd, job->pwd)))
	 printf("\t    cd %s\n", job->pwd);
      if(v >= 2 && (!cur_lpwd || strcmp(cur_lpwd, job->lpwd)))
	 printf("\t    lcd %s\n", job->lpwd);
      pwd = job->pwd;
      lpwd = job->lpwd;

      printf("\t%2d. %s\n",n++, job->cmd);
   }
}
