/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>

#include "FindJob.h"
#include "CmdExec.h"
#include "misc.h"

#define top (*stack[stack_ptr])

int FinderJob::Do()
{
   int m=STALL;
   int res;
   prf_res pres;

   switch(state)
   {
   case INIT:
      // nothing to do - just fall to CD

      {
	 const char *cd_dir=dir;
	 if(stack_ptr>=0)
	    cd_dir=dir_file(dir_file(start_dir,top.path),dir);
	 session->Chdir(cd_dir,false);
      }
      state=CD;
      m=MOVED;
   case CD:
      res=session->Done();
      if(res==session->IN_PROGRESS)
	 return m;
      if(res==session->OK)
      {
	 session->Close();
	 goto pre_INFO;
      }
      // cd error
      eprintf("%s: cd %s: %s\n",op,dir,session->StrError(res));
      errors++;
      depth_done=true;
      state=LOOP;
      return MOVED;

   pre_INFO:
      li=session->MakeListInfo();
      if(li==0)
      {
	 //FIXME
	 abort();
      }
      li->Need(FileInfo::NAME|FileInfo::TYPE);
      if(use_cache)
	 li->UseCache();
      state=INFO;
      m=MOVED;
   case INFO:
      if(!li->Done())
	 return m;
      if(li->Error())
      {
	 eprintf("%s: %s\n",op,li->ErrorText());
	 delete li;
	 li=0;
	 errors++;
	 depth_done=true;
	 state=LOOP;
	 return MOVED;
      }
      Push(li->GetResult());
      delete li;
      li=0;
      goto pre_LOOP;

   pre_LOOP:
      top.fset->rewind();
      state=LOOP;
      m=MOVED;
   case LOOP:
      if(stack_ptr==-1 || top.fset->curr()==0)
      {
	 Up();
	 return MOVED;
      }
      // at this point either is true:
      // 1. we just process another file (!depth_done)
      // 2. we just returned from a subdir (depth_done)
      if(depth_first && !depth_done)
      {
	 FileInfo *f=top.fset->curr();
	 if((f->defined&f->TYPE) && f->filetype==f->DIRECTORY)
	 {
	    Down(f->name);
	    return MOVED;
	 }
      }
      pres=ProcessFile(top.path,top.fset->curr());

      if(pres==PRF_LATER)
	 return m;

      depth_done=false;

      switch(pres)
      {
      case(PRF_FATAL):
	 errors++;
	 state=DONE;
	 m=MOVED;
	 return m;
      case(PRF_ERR):
	 errors++;
	 break;
      case(PRF_WAIT):
	 state=WAIT;
	 return MOVED;
      case(PRF_OK):
	 break;
      case(PRF_LATER):
	 abort();
      }
   post_WAIT:
      if(stack_ptr==-1)
	 return m;
      if(!depth_first)
      {
	 FileInfo *f=top.fset->curr();
	 if((f->defined&f->TYPE) && f->filetype==f->DIRECTORY)
	 {
	    top.fset->next();
	    Down(f->name);
	    return MOVED;
	 }
      }
      top.fset->next();
      return MOVED;

   case WAIT:
      if(!waiting->Done())
	 return m;
      delete waiting;
      waiting=0;
      state=LOOP;
      m=MOVED;
      goto post_WAIT;

   case DONE:
      return m;
   }
   return m;
}

void FinderJob::Up()
{
   if(stack_ptr==-1)
   {
   done:
      state=DONE;
      Finish();
      return;
   }
   delete stack[stack_ptr--];
   if(stack_ptr==-1)
      goto done;
   depth_done=true;
   state=LOOP;
}

void FinderJob::Push(FileSet *fset)
{
   const char *old_path=0;
   if(stack_ptr>=0)
      old_path=top.path;

   stack_ptr++;
   if(stack_allocated<=stack_ptr)
   {
      stack_allocated=stack_ptr+8;
      stack=(place**)xrealloc(stack,sizeof(*stack)*stack_allocated);
   }

   const char *new_path="";
   if(old_path) // the first path will be empty
      new_path=dir_file(old_path,dir);

   fset->ExcludeDots(); // don't need . and ..
   stack[stack_ptr]=new place(new_path,fset);
}

FinderJob::place::place(const char *p,FileSet *f)
{
   path=xstrdup(p);
   fset=f;
}
FinderJob::place::~place()
{
   xfree(path);
   if(fset) delete fset;
}

void FinderJob::Down(const char *p)
{
   dir=p;
   state=INIT;
}

FinderJob::prf_res FinderJob::ProcessFile(const char *d,const FileInfo *f)
{
   return PRF_OK;
}

void FinderJob::Init()
{
   op="find";
   start_dir=0;
   init_dir=0;
   dir=0;
   errors=0;
   li=0;

   stack_allocated=0;
   stack_ptr=-1;
   stack=0;

   show_sl=true;

   depth_first=false; // useful for rm -r
   depth_done=false;

   use_cache=true;

   state=INIT;
}

FinderJob::FinderJob(FileAccess *s,const char *d)
   : SessionJob(s)
{
   Init();
   init_dir=xstrdup(session->GetCwd());
   NextDir(d);
}

void FinderJob::NextDir(const char *d)
{
   session->Chdir(init_dir,false); // no verification
   xfree(start_dir);
   start_dir=xstrdup(dir_file(session->GetCwd(),d));
   Down(start_dir);
}

FinderJob::~FinderJob()
{
   while(stack_ptr>=0)
      Up();
   xfree(stack);
   xfree(start_dir);
   xfree(init_dir);
   if(li) delete li;
}

void FinderJob::ShowRunStatus(StatusLine *sl)
{
   if(!show_sl)
      return;

   char *path=0;
   switch(state)
   {
   case INFO:
      if(stack_ptr>=0)
	 path=top.path;
      sl->Show("%s: %s [%s]",dir_file(path,dir),li->Status(),
			   session->CurrentStatus());
      break;
   case WAIT:
      waiting->ShowRunStatus(sl);
      break;
   default:
      sl->Clear();
      break;
   }
}

void FinderJob::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);

   char *path=0;
   switch(state)
   {
   case INFO:
      if(stack_ptr>=0)
	 path=top.path;
      printf("\t%s: %s [%s]\n",dir_file(path,dir),li->Status(),
			   session->CurrentStatus());
      break;
   case WAIT:
      break;
   default:
      break;
   }
}

// FinderJob_List implementation
// find files and write list to a stream
FinderJob::prf_res FinderJob_List::ProcessFile(const char *d,const FileInfo *fi)
{
   if(buf->Broken())
      return PRF_FATAL;
   if(buf->Error())
   {
      eprintf("%s: %s\n",op,buf->ErrorText());
      return PRF_FATAL;
   }
   if(fg_data==0)
      fg_data=buf->GetFgData(fg);
   if(buf->Size()>0x10000)
      return PRF_LATER;
   buf->Put(dir_file(d,fi->name));
   if((fi->defined&fi->TYPE) && fi->filetype==fi->DIRECTORY)
      buf->Put("/");
   buf->Put("\n");
   return FinderJob::ProcessFile(d,fi);
}

FinderJob_List::FinderJob_List(FileAccess *s,const char *d,FDStream *o)
   : FinderJob(s,d)
{
   show_sl = !o->usesfd(1);
   buf=new FileOutputBuffer(o);
}

FinderJob_List::~FinderJob_List()
{
   delete buf;
}


// FinderJob_Cmd implementation
// process directory tree
#define super FinderJob

FinderJob::prf_res FinderJob_Cmd::ProcessFile(const char *d,const FileInfo *f)
{
#define ISDIR  ((f->defined&f->TYPE) && f->filetype==f->DIRECTORY)
#define ISLINK ((f->defined&f->TYPE) && f->filetype==f->SYMLINK)

   FileAccess *s=session->Clone();
   s->Chdir(dir_file(start_dir,d),false);

   CmdExec *exec=new CmdExec(s);
   exec->parent=this;
   exec->SetCWD(saved_cwd);

   char *file=f->name;

   switch(cmd)
   {
   case RM:
      if(ISDIR)
      {
	 exec->FeedCmd("rmdir -- ");
	 exec->FeedQuoted(file);
	 exec->FeedCmd("\n");
      }
      else
      {
	 exec->FeedCmd("rm -- ");
	 exec->FeedQuoted(file);
	 exec->FeedCmd("\n");
      }
      break;
   case GET:
      if(ISDIR)
      {
// 	 mkdir(dir_file(saved_cwd,
      }
      else if(ISLINK)
      {
      }
      else
      {
      }
      break;
   }
   waiting=exec;
   return PRF_WAIT;

#undef ISDIR
#undef ISLINK
}

FinderJob_Cmd::FinderJob_Cmd(FileAccess *s,ArgV *a,cmd_t c)
   : FinderJob(s,a->getcurr())
{
   cmd=c;
   args=a;
   use_cache=false;
   if(cmd==RM)
      depth_first=true;
   saved_cwd=xgetcwd();
   removing_last=false;
}
FinderJob_Cmd::~FinderJob_Cmd()
{
   xfree(saved_cwd);
   delete args;
   if(waiting)
   {
      if(waiting->waiting)
	 waiting->waiting->jobno=-1; // prevent reparenting
      delete waiting;
   }
}

void FinderJob_Cmd::Finish()
{
   if(cmd==RM)
   {
      if(removing_last)
	 removing_last=false;
      else
      {
	 /* remove the specified directory at the last */
	 session->Chdir(init_dir,false); // to leave the directory
	 CmdExec *exec=new CmdExec(session->Clone());
	 exec->parent=this;
	 exec->FeedCmd("rmdir -- ");
	 exec->FeedQuoted(start_dir);
	 exec->FeedCmd("\n");
	 waiting=exec;
	 removing_last=true;
	 state=WAIT; // it will wait for termination, try to process
		     // next file and call Finish() again
	 return;
      }
   }
   char *d=args->getnext();
   if(!d)
      return;
   FinderJob::NextDir(d);
}

int FinderJob_Cmd::Done()
{
   return FinderJob::Done() && args->getcurr()==0;
}
