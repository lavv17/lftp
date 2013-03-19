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

#ifndef QUEUEFEEDER_H
#define QUEUEFEEDER_H

#include "CmdExec.h"

class QueueFeeder : public CmdFeeder
{
   struct QueueJob {
      xstring_c cmd;
      xstring_c pwd;
      xstring_c lpwd;

      QueueJob *next, *prev;
      QueueJob(): next(0), prev(0) {}
   } *jobs, *lastjob;
   xstring_c cur_pwd;
   xstring_c cur_lpwd;

   xstring buffer;

   /* remove the given job from the list */
   void unlink_job(QueueJob *job);

   /* get the n'th job */
   QueueJob *get_job(int n);

   /* get the n'th job, removed from the list: */
   QueueJob *grab_job(int n);

   /* get all jobs (linked and removed from the list)
    * that match the cmd: */
   QueueJob *grab_job(const char *cmd);

   /* get the next job in j that matches cmd (including j) */
   static QueueJob *get_next_match(const char *cmd, QueueJob *j);
   void PrintJobs(const QueueJob *job, int v, const char *plur) const;
   xstring& FormatJobs(xstring& s,const QueueJob *job, int v, const char *plur) const;

   void insert_jobs(QueueJob *job,
		   QueueJob *&lst_head, QueueJob *&lst_tail,
		   QueueJob *before);

   void FreeList(QueueJob *j);

public:
   const char *NextCmd(CmdExec *exec,const char *prompt);

   /* Add a command to the queue at a given position; a 0 position inserts at the end. */
   void QueueCmd(const char *cmd, const char *pwd, const char *lpwd, int pos = 0, int verbose = 0);

   /* delete jobs (by index or wildcard expr) */
   bool DelJob(int from, int v = 0);
   bool DelJob(const char *cmd, int v = 0);

   /* move one or more jobs (by index or wildcard expr). */
   bool MoveJob(int from, int to, int v = 0);
   bool MoveJob(const char *cmd, int to, int v = 0);

   static int JobCount(const QueueJob *);
   int JobCount() const { return JobCount(jobs); }

   enum { PrintRequeue = 9999 };
   xstring& FormatStatus(xstring&,int v,const char *prefix="\t") const;

   QueueFeeder(const char *pwd, const char *lpwd):
      jobs(0), lastjob(0), cur_pwd(pwd), cur_lpwd(lpwd) {}
   virtual ~QueueFeeder();
};

#endif//QUEUEFEEDER_H
