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

#include "FindJob.h"
#include "misc.h"

#define top (*stack[stack_ptr])

int FindJob::Do()
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
   err:
      errors++;
      Up();
      return MOVED;

   pre_INFO:
      li=session->MakeListInfo();
      if(li==0)
      {
	 //FIXME
	 abort();
      }
      li->Need(FileInfo::NAME|FileInfo::TYPE);
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
	 goto err;
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
      if(top.fset->curr()==0)
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
      depth_done=false;

      pres=ProcessFile(top.path,top.fset->curr());
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
	 return m;
      case(PRF_OK):
	 break;
      }
   post_WAIT:
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

void FindJob::Up()
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

void FindJob::Push(FileSet *fset)
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

FindJob::place::place(const char *p,FileSet *f)
{
   path=xstrdup(p);
   fset=f;
}
FindJob::place::~place()
{
   xfree(path);
   if(fset) delete fset;
}

void FindJob::Down(const char *p)
{
   dir=p;
   state=INIT;
}

FindJob::prf_res FindJob::ProcessFile(const char *d,const FileInfo *f)
{
   return PRF_OK;
}

void FindJob::Init()
{
   op="find";
   start_dir=0;
   dir=0;
   errors=0;
   li=0;

   stack_allocated=0;
   stack_ptr=-1;
   stack=0;

   show_sl=true;

   depth_first=false; // useful for rm -r
   depth_done=false;

   state=INIT;
}

FindJob::FindJob(FileAccess *s,const char *d)
   : SessionJob(s)
{
   Init();
   start_dir=xstrdup(dir_file(session->GetCwd(),d));
   Down(start_dir);
}

FindJob::~FindJob()
{
   while(stack_ptr>=0)
      Up();
   xfree(stack);
   xfree(start_dir);
   if(li) delete li;
}

void FindJob::ShowRunStatus(StatusLine *sl)
{
   if(!show_sl)
      return;

   char *path=0;
   switch(state)
   {
   case CD:
      sl->Show("cd `%s' [%s]",dir,session->CurrentStatus());
      break;
   case INFO:
      if(stack_ptr>=0)
	 path=top.path;
      sl->Show("getting listing for `%s' [%s]",dir_file(path,dir),li->Status());
      break;
   default:
      sl->Show("");
      break;
   }
}

// FindJob_List implementation
// find files and write list to a stream
FindJob::prf_res FindJob_List::ProcessFile(const char *d,const FileInfo *fi)
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
      return PRF_WAIT;
   buf->Put(dir_file(d,fi->name));
   buf->Put("\n");
   return FindJob::ProcessFile(d,fi);
}

FindJob_List::FindJob_List(FileAccess *s,const char *d,FDStream *o)
   : FindJob(s,d)
{
   show_sl = !o->usesfd(1);
   buf=new FileOutputBuffer(o);
}

FindJob_List::~FindJob_List()
{
   delete buf;
}
