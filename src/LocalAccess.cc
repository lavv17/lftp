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

/* $Id$ */

/* this is not very useful, just a proof of concept */

#include <config.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <glob.h>

#include "LocalAccess.h"
#include "xmalloc.h"
#include "xalloca.h"
#include "xstring.h"
#include "misc.h"

void LocalAccess::ClassInit()
{
   // register the class
   Register("file",LocalAccess::New);
}

void LocalAccess::Init()
{
   done=true;
   error_code=OK;
   stream=0;
   xfree(home);
   home=xstrdup(getenv("HOME"));
}

LocalAccess::LocalAccess() : FileAccess()
{
   Init();
}
LocalAccess::LocalAccess(const LocalAccess *o) : FileAccess((const FileAccess *)o)
{
   Init();
}
LocalAccess::~LocalAccess()
{
   if(stream)
      delete stream;
}

void LocalAccess::errno_handle()
{
   xfree(last_error_resp);
   const char *err=strerror(errno);
   last_error_resp=(char*)xmalloc(xstrlen(file)+xstrlen(file1)+strlen(err)+20);
   if(mode==RENAME)
      sprintf(last_error_resp,"rename(%s, %s): %s",file,file1,err);
   else
      sprintf(last_error_resp,"%s: %s",file,err);
}

int LocalAccess::Done()
{
   if(error_code<0)
      return error_code;
   if(done)
      return OK;
   switch((open_mode)mode)
   {
   case(CLOSED):
   case(CONNECT_VERIFY):
      return OK;
   default:
      return IN_PROGRESS;
   }
}

int LocalAccess::Do()
{
   if(error_code<0 || done)
      return STALL;
   int m=STALL;
   switch((open_mode)mode)
   {
   case(CLOSED):
      return m;
   case(LIST):
   case(LONG_LIST):
   case(QUOTE_CMD):
      if(stream==0)
      {
	 char *cmd=(char*)alloca(10+xstrlen(file));
	 if(mode==LIST)
	 {
	    if(file && file[0])
	       sprintf(cmd,"ls %s",file);
	    else
	       strcpy(cmd,"ls");
	 }
	 else if(mode==LONG_LIST)
	 {
	    if(file && file[0])
	       sprintf(cmd,"ls -l %s",file);
	    else
	       strcpy(cmd,"ls -la");
	 }
	 else// if(mode==QUOTE_CMD)
	    strcpy(cmd,file);
	 DebugPrint("---- ",cmd);
	 InputFilter *f_stream=new InputFilter(cmd);
	 f_stream->SetCwd(cwd);
	 stream=f_stream;
	 real_pos=0;
	 m=MOVED;
      }
      if(stream->getfd()==-1)
      {
	 if(stream->error())
	 {
	    error_code=FATAL;
	    xfree(last_error_resp);
	    last_error_resp=xstrdup(stream->error_text);
	    return MOVED;
	 }
	 block+=TimeOut(1000);
	 return m;
      }
      stream->Kill(SIGCONT);
      block+=PollVec(stream->getfd(),POLLIN);
      return m;
   case(CHANGE_DIR):
      if(access(file,X_OK)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      else
      {
	 xfree(cwd);
	 cwd=xstrdup(file);
      }
      done=true;
      return MOVED;
   case(REMOVE):
      if(remove(dir_file(cwd,file))==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      done=true;
      return MOVED;
   case(REMOVE_DIR):
      if(rmdir(dir_file(cwd,file))==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      done=true;
      return MOVED;
   case(RENAME):
   {
      char *cwd_file1=xstrdup(dir_file(cwd,file1));
      if(rename(dir_file(cwd,file),cwd_file1)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      xfree(cwd_file1);
      done=true;
      return MOVED;
   }
   case(MAKE_DIR):
      if(mkdir(dir_file(cwd,file),0755)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      done=true;
      return MOVED;
   case(CHANGE_MODE):
      if(chmod(dir_file(cwd,file),chmod_mode)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      done=true;
      return MOVED;

   case(RETRIEVE):
   case(STORE):
      if(stream==0)
      {
	 int o_mode=O_RDONLY;
	 if(mode==STORE)
	 {
	    o_mode=O_WRONLY|O_CREAT;
	    if(pos==0)
	       o_mode|=O_TRUNC;
	 }
	 stream=new FileStream(dir_file(cwd,file),o_mode);
	 real_pos=-1;
	 m=MOVED;
      }
      if(stream->getfd()==-1)
      {
	 if(stream->error())
	 {
	    error_code=NO_FILE;
	    xfree(last_error_resp);
	    last_error_resp=xstrdup(stream->error_text);
	    return MOVED;
	 }
	 block+=TimeOut(1000);
	 return m;
      }
      stream->Kill(SIGCONT);
      block+=PollVec(stream->getfd(),(mode==STORE?POLLOUT:POLLIN));
      return m;

   case(CONNECT_VERIFY):
      done=true;
      return MOVED;

   case(ARRAY_INFO):
      fill_array_info();
      done=true;
      return MOVED;
   }
   return m;
}

void LocalAccess::fill_array_info()
{
   for(int i=0; i<array_cnt; i++)
   {
      fileinfo *f = &array_for_info[i];
      struct stat st;
      if(stat(dir_file(cwd,f->file),&st)!=-1)
      {
	 f->size=st.st_size;
	 f->time=st.st_mtime;
      }
   }
}

int LocalAccess::Read(void *buf,int size)
{
   if(error_code<0)
      return error_code;
   if(stream==0)
      return DO_AGAIN;
   int fd=stream->getfd();
   if(fd==-1)
      return DO_AGAIN;
   if(real_pos==-1)
   {
      if(lseek(fd,pos,SEEK_SET)==-1)
	 real_pos=0;
      else
	 real_pos=pos;
   }
   stream->Kill(SIGCONT);
read_again:
   int res=Poll(fd,POLLIN);
   if(res==-1 || (res&(POLLIN|POLLNVAL)))
   {
      int res=read(fd,buf,size);
      if(res<0)
      {
	 saved_errno=errno;
	 return SEE_ERRNO;
      }
      if(res==0)
	 return res;
      real_pos+=res;
      if(real_pos<=pos)
	 goto read_again;
      int shift;
      if((shift=pos+res-real_pos)>0)
      {
	 memmove(buf,(char*)buf+shift,size-shift);
	 res-=shift;
      }
      pos+=res;
      return(res);
   }
   if(res)
   {
      // looks like eof
      return 0;
   }
   return DO_AGAIN;
}

int LocalAccess::Write(const void *buf,int size)
{
   if(error_code<0)
      return error_code;
   if(stream==0)
      return DO_AGAIN;
   int fd=stream->getfd();
   if(fd==-1)
      return DO_AGAIN;
   if(real_pos==-1)
   {
      if(lseek(fd,pos,SEEK_SET)==-1)
	 real_pos=0;
      else
	 real_pos=pos;
      if(real_pos<pos)
      {
	 error_code=STORE_FAILED;
	 return error_code;
      }
   }
   stream->Kill(SIGCONT);
   int res=Poll(fd,POLLOUT);
   if(res==-1 || (res&(POLLOUT|POLLNVAL)))
   {
      int res=write(fd,buf,size);
      if(res>=0)
      {
	 pos=(real_pos+=res);
	 return res;
      }
      saved_errno=errno;
      return SEE_ERRNO;
   }

   return 0;
}

int LocalAccess::StoreStatus()
{
   if(error_code<0)
      return error_code;

   return OK;
}

void LocalAccess::Close()
{
   done=false;
   error_code=OK;
   if(stream)
   {
      delete stream;
      stream=0;
   }
   FileAccess::Close();
}

bool LocalAccess::SameLocationAs(FileAccess *fa)
{
   if(!SameProtoAs(fa))
      return false;
   LocalAccess *o=(LocalAccess*)fa;

   if(xstrcmp(home,o->home))
      return false;

   return !xstrcmp(cwd,o->cwd);
}

ListInfo *LocalAccess::MakeListInfo()
{
   return new LocalListInfo(cwd);
}


#ifndef S_ISLNK
# define S_ISLNK(mode) (S_IFLNK==(mode&S_IFMT))
#endif

int LocalListInfo::Do()
{
   if(done)
      return STALL;

   DIR *d=opendir(dir);
   struct dirent *f;

   if(d==0)
   {
      const char *err=strerror(errno);
      char *mem=(char*)alloca(strlen(err)+strlen(dir)+3);
      sprintf(mem,"%s: %s",dir,err);
      SetError(mem);
      return MOVED;
   }

   result=new FileSet;
   for(;;)
   {
      f=readdir(d);
      if(f==0)
	 break;
      FileInfo *fi=new FileInfo();
      fi->SetName(f->d_name);
      result->Add(fi);
   }
   closedir(d);

   result->Exclude(path,rxc_exclude,rxc_include);

   result->rewind();
   for(FileInfo *file=result->curr(); file!=0; file=result->next())
   {
      const char *name=dir_file(dir,file->name);

      struct stat st;
      if(lstat(name,&st)==-1)
	 continue;

check_again:
      FileInfo::type t;
      if(S_ISDIR(st.st_mode))
	 t=FileInfo::DIRECTORY;
      else if(S_ISREG(st.st_mode))
	 t=FileInfo::NORMAL;
#ifdef HAVE_LSTAT
      else if(S_ISLNK(st.st_mode))
      {
	 if(follow_symlinks)
	 {
	    if(stat(name,&st)!=-1)
	       goto check_again;
	    // dangling symlink, don't follow it.
	 }
	 t=FileInfo::SYMLINK;
      }
#endif
      else
	 continue;   // ignore other type files

      file->SetSize(st.st_size);
      file->SetDate(st.st_mtime);
      file->SetMode(st.st_mode&07777);
      file->SetType(t);

#ifdef HAVE_LSTAT
      if(t==file->SYMLINK)
      {
	 char *buf=(char*)alloca(st.st_size+1);
	 int res=readlink(name,buf,st.st_size);
	 if(res!=-1)
	 {
	    buf[res]=0;
	    file->SetSymlink(buf);
      	 }
      }
#endif /* HAVE_LSTAT */
   }

   done=true;
   return MOVED;
}

Glob *LocalAccess::MakeGlob(const char *pattern)
{
   return new LocalGlob(cwd,pattern);
}

LocalGlob::LocalGlob(const char *cwd,const char *pattern)
   : Glob(pattern)
{
   glob_t g;
   char *oldcwd=xgetcwd();
   if(!oldcwd)
   {
      SetError("cannot get current directory");
      return;
   }
   if(chdir(cwd)==-1)
   {
      const char *se=strerror(errno);
      char *err=(char*)alloca(strlen(cwd)+strlen(se)+20);
      sprintf(err,"chdir(%s): %s",cwd,se);
      SetError(err);
      xfree(oldcwd);
      return;
   }

   glob(pattern, 0, NULL, &g);

   if(chdir(oldcwd)==-1)
      fprintf(stderr,"chdir(%s): %s",oldcwd,strerror(errno));
   xfree(oldcwd);
   for(unsigned i=0; i<g.gl_pathc; i++)
      add(g.gl_pathv[i],strlen(g.gl_pathv[i]));
   globfree(&g);
}
int LocalGlob::Do()
{
   if(!done)
   {
      done=true;
      return MOVED;
   }
   return STALL;
}

#ifdef MODULE
CDECL void module_init()
{
   LocalAccess::ClassInit();
}
#endif
