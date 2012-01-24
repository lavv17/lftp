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

/* All indexes in this function start at 0; -1 is used contextually to
 * indicate the last job or moving to the end of the list. */
#include <config.h>
#include <unistd.h>
#include <assert.h>
#include <fnmatch.h>
#include <stddef.h>

#include "QueueFeeder.h"
#include "plural.h"
#include "misc.h"

const char *QueueFeeder::NextCmd(CmdExec *exec, const char *)
{
   if(jobs == NULL) return NULL;

   /* denext the first job */
   QueueJob *job = grab_job(0);

   buffer.truncate(0);

   if(xstrcmp(cur_pwd, job->pwd)) {
      buffer.append("cd ").append_quoted(job->pwd).append("; ");
      cur_pwd.set(job->pwd);
   }

   if(xstrcmp(cur_lpwd, job->lpwd)) {
      buffer.append("lcd ").append_quoted(job->lpwd).append("; ");
      cur_lpwd.set(job->lpwd);
   }

   buffer.append(job->cmd.get()).append('\n');
   delete job;
   return buffer;
}

void QueueFeeder::QueueCmd(const char *cmd, const char *pwd, const char *lpwd, int pos, int v)
{
   QueueJob *job = new QueueJob;
   job->cmd.set(cmd);
   job->pwd.set(pwd);
   job->lpwd.set(lpwd);

   /* we never want a newline at the end: */
   if(last_char(job->cmd) == '\n')
      job->cmd.truncate(strlen(job->cmd)-1);

   insert_jobs(job, jobs, lastjob, pos != -1? get_job(pos): NULL);
   PrintJobs(job, v, _("Added job$|s$"));
}

int QueueFeeder::JobCount(const QueueJob *j)
{
   int job_count=0;
   for(; j; j=j->next)
      job_count++;
   return job_count;
}

/* verbose:
 * 0, quiet
 * 1, interactive
 * 2, verbose (print changes of pwd and lpwd)
 * PrintRequeue, output to requeue
 */
xstring& QueueFeeder::FormatJobs(xstring& s,const QueueJob *job, int v, const char *plur) const
{
   if(v < 1)
      return s;

   const char *pwd = 0, *lpwd = 0;
   if(v == PrintRequeue)
   {
      for(const QueueJob *j = job; j; j=j->next)
      {
	 if(xstrcmp(pwd, job->pwd))
	 {
	    s.append("cd ").append_quoted(job->pwd).append(" &\n");
	    pwd = job->pwd;
	 }

	 if(xstrcmp(lpwd, job->lpwd))
	 {
	    s.append("lcd ").append_quoted(job->lpwd).append(" &\n");
	    lpwd = job->lpwd;
	 }

	 s.append("queue ").append_quoted(job->cmd).append('\n');
      }
      return s;
   }

   int job_count=JobCount(job);
   if(job_count>1)
      s.appendf("%s:\n", plural(plur,job_count));

   pwd = cur_pwd;
   lpwd = cur_lpwd;

   int n = 1;
   for(const QueueJob *j = job; j; j=j->next)
   {
      /* Print pwd/lpwd changes when v >= 2.  (This only happens when there's
       * more than one.) */
      if(xstrcmp(pwd, job->pwd))
      {
	 if(v > 2) {
	    s.append("\tcd ").append_quoted(job->pwd).append('\n');
	 }
	 pwd = job->pwd;
      }

      if(xstrcmp(lpwd, job->lpwd))
      {
	 if(v > 2) {
	    s.append("\tlcd ").append_quoted(job->lpwd).append('\n');
	 }
	 lpwd = job->lpwd;
      }

      if(job_count==1)
	 s.appendf("%s: ", plural(plur,job_count));
      else
	 s.appendf("\t%2d. ",n++);

      s.append(j->cmd.get()).append('\n');
   }
   return s;
}
void QueueFeeder::PrintJobs(const QueueJob *job, int v, const char *plur) const
{
   xstring buf("");
   FormatJobs(buf,job,v,plur);
   printf("%s",buf.get());
}

bool QueueFeeder::DelJob(int from, int v)
{
   QueueJob *job = grab_job(from);
   if(!job)
   {
      if(v > 0)
      {
	 if(from == -1 || !jobs)
	    printf(_("No queued jobs.\n"));
	 else
	    printf(_("No queued job #%i.\n"), from+1);
      }
      return false;
   }

   PrintJobs(job, v, _("Deleted job$|s$"));

   FreeList(job);
   return true;
}

bool QueueFeeder::DelJob(const char *cmd, int v)
{
   QueueJob *job = grab_job(cmd);
   if(!job)
   {
      if(v > 0)
      {
	 if(!jobs)
	    printf(_("No queued jobs.\n"));
	 else
	    printf(_("No queued jobs match \"%s\".\n"), cmd);
      }
      return false;
   }

   PrintJobs(job, v, _("Deleted job$|s$"));

   FreeList(job);
   return true;
}

/* When moving, grab the insertion pointer *before* pulling out things to
 * move, since doing so will change offsets.  (Note that "to == -1" means
 * "move to the end", not "before the last entry".)
 */
bool QueueFeeder::MoveJob(int from, int to, int v)
{
   /* Safety: make sure we don't try to move an item before itself. */
   if(from == to) return false;

   QueueJob *before = to != -1? get_job(to): NULL;

   QueueJob *job = grab_job(from);
   if(job == NULL) return false;

   PrintJobs(job, v, _("Moved job$|s$"));
   assert(job != before);

   insert_jobs(job, jobs, lastjob, before);
   return true;
}

bool QueueFeeder::MoveJob(const char *cmd, int to, int v)
{
   QueueJob *before = to != -1? get_job(to): NULL;

   /* Mild hack: we need to make sure the "before" job isn't one that's
    * going to be moved, so move it upward until it isn't. */
   while(before && !fnmatch(cmd, before->cmd,FNM_CASEFOLD)) before=before->next;

   QueueJob *job = grab_job(cmd);
   if(job == NULL) return false;

   PrintJobs(job, v, _("Moved job$|s$"));

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
      QueueJob *match = get_next_match(cmd, j);
      if(!match) break;
      j = match->next;

      /* matches */
      unlink_job(match);
      insert_jobs(match, head, tail, NULL);
   }

   return head;
}

QueueFeeder::QueueJob *QueueFeeder::get_next_match(const char *cmd, QueueJob *j)
{
   while(j) {
      if(!fnmatch(cmd, j->cmd,FNM_CASEFOLD))
	 return j;

      j = j->next;
   }
   return 0;
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
}


xstring& QueueFeeder::FormatStatus(xstring& s,int v,const char *prefix) const
{
   if(jobs == NULL)
      return s;

   if(v == PrintRequeue)
      return FormatJobs(s, jobs, v, "");

   s.append(prefix).append(_("Commands queued:")).append('\n');

   int n = 1;

   const char *pwd = cur_pwd, *lpwd = cur_lpwd;
   for(const QueueJob *job = jobs; job; job = job->next) {
      if(v<2 && n>4 && job->next)
      {
	 s.appendf("%s%2d. ...\n",prefix,n);
	 break;
      }
      /* Print pwd/lpwd changes when v >= 2. */
      if(v >= 2 && (xstrcmp(pwd, job->pwd)))
	 s.appendf("%s    cd %s\n",prefix,job->pwd.get());
      if(v >= 2 && (xstrcmp(lpwd, job->lpwd)))
	 s.appendf("%s    lcd %s\n",prefix,job->lpwd.get());
      pwd = job->pwd;
      lpwd = job->lpwd;

      s.appendf("%s%2d. %s\n",prefix,n++,job->cmd.get());
   }
   return s;
}
