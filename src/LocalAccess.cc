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
/* this is used in mirror command to build local files list */

#include <config.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <glob.h>
#include <utime.h>

#include "LocalAccess.h"
#include "xmalloc.h"
#include "xalloca.h"
#include "xstring.h"
#include "misc.h"
#include "log.h"

FileAccess *LocalAccess::New() { return new LocalAccess(); }

void LocalAccess::ClassInit()
{
   // register the class
   Register("file",LocalAccess::New);
}

void LocalAccess::Init()
{
   done=false;
   error_code=OK;
   stream=0;
   xfree(home);
   home=xstrdup(getenv("HOME"));
   xfree(hostname);
   hostname=xstrdup("localhost");
}

LocalAccess::LocalAccess() : FileAccess()
{
   Init();
   xfree(cwd);
   cwd=xgetcwd();
}
LocalAccess::LocalAccess(const LocalAccess *o) : FileAccess(o)
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
   xfree(error);
   const char *err=strerror(errno);
   error=(char*)xmalloc(xstrlen(file)+xstrlen(file1)+strlen(err)+20);
   if(mode==RENAME)
      sprintf(error,"rename(%s, %s): %s",file,file1,err);
   else
      sprintf(error,"%s: %s",file,err);
   Log::global->Format(0,"**** %s\n",error);
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
   if(Error() || done)
      return STALL;
   int m=STALL;
   if(mode!=CLOSED)
      ExpandTildeInCWD();
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
	 // FIXME: shell-quote file name
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
	 DebugPrint("---- ",cmd,5);
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
	    Fatal(stream->error_text);
	    return MOVED;
	 }
	 TimeoutS(1);
	 return m;
      }
      stream->Kill(SIGCONT);
      Block(stream->getfd(),POLLIN);
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
   case(REMOVE): {
      const char *f=dir_file(cwd,file);
      Log::global->Format(5,"---- remove(%s)\n",f);
      if(remove(f)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      done=true;
      return MOVED;
   }
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
	    SetError(NO_FILE,stream->error_text);
	    return MOVED;
	 }
	 TimeoutS(1);
	 return m;
      }
      stream->Kill(SIGCONT);
      if(opt_size || opt_date)
      {
	 struct stat st;
	 if(fstat(stream->getfd(),&st)==-1)
	 {
	    if(opt_size) *opt_size=-1L;
	    if(opt_date) *opt_date=(time_t)-1;
	 }
	 else
	 {
	    if(opt_size) *opt_size=st.st_size;
	    if(opt_date) *opt_date=st.st_mtime;
	 }
	 opt_size=0;
	 opt_date=0;
      }
      Block(stream->getfd(),(mode==STORE?POLLOUT:POLLIN));
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
      else
      {
	 f->size=-1L;
	 f->time=(time_t)-1;
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
      if(ascii || lseek(fd,pos,SEEK_SET)==-1)
	 real_pos=0;
      else
	 real_pos=pos;
   }
   stream->Kill(SIGCONT);
read_again:
   int res;

#ifndef NATIVE_CRLF
   if(ascii)
      res=read(fd,buf,size/2);
   else
#endif
      res=read(fd,buf,size);

   if(res<0)
   {
      if(errno==EINTR || errno==EAGAIN)
	 return DO_AGAIN;
      saved_errno=errno;
      return SEE_ERRNO;
   }
   if(res==0)
      return res; // eof

#ifndef NATIVE_CRLF
   if(ascii)
   {
      char *p=(char*)buf;
      for(int i=res; i>0; i--)
      {
	 if(*p=='\n')
	 {
	    memmove(p+1,p,i);
	    *p++='\r';
	    res++;
	 }
	 p++;
      }
   }
#endif

   real_pos+=res;
   if(real_pos<=pos)
      goto read_again;
   long shift;
   if((shift=pos+res-real_pos)>0)
   {
      memmove(buf,(char*)buf+shift,size-shift);
      res-=shift;
   }
   pos+=res;
   return(res);
}

int LocalAccess::Write(const void *vbuf,int len)
{
   const char *buf=(const char *)vbuf;
   if(error_code<0)
      return error_code;
   if(stream==0)
      return DO_AGAIN;
   int fd=stream->getfd();
   if(fd==-1)
      return DO_AGAIN;
   if(real_pos==-1)
   {
      if(ascii || lseek(fd,pos,SEEK_SET)==-1)
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

   int skip_cr=0;

#ifndef NATIVE_CRLF
   if(ascii)
   {
      // find where line ends.
      const char *cr=buf;
      for(;;)
      {
	 cr=(const char *)memchr(cr,'\r',len-(cr-buf));
	 if(!cr)
	    break;
	 if(cr-buf<len-1 && cr[1]=='\n')
	 {
	    skip_cr=1;
	    len=cr-buf;
	    break;
	 }
	 if(cr-buf==len-1)
	 {
	    if(len==1)	   // last CR in stream will be lost. (FIX?)
	       skip_cr=1;
	    len--;
	    break;
	 }
	 cr++;
      }
   }
#endif	 // NATIVE_CRLF

   if(len==0)
   {
      pos=(real_pos+=skip_cr);
      return skip_cr;
   }

   int res=write(fd,buf,len);
   if(res<0)
   {
      if(errno==EINTR || errno==EAGAIN)
	 return DO_AGAIN;
      saved_errno=errno;
      return SEE_ERRNO;
   }

   if(res==len)
      res+=skip_cr;
   pos=(real_pos+=res);
   return res;
}

int LocalAccess::StoreStatus()
{
   if(stream)
   {
      delete stream;
      stream=0;
      if(entity_date!=(time_t)-1)
      {
	 static struct utimbuf ut;
	 ut.actime=ut.modtime=entity_date;
	 utime(dir_file(cwd,file),&ut);
      }
   }

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
   xfree(file);
   file=xstrdup(pattern);
   ExpandTildeInCWD();
   return new LocalGlob(cwd,file);
}

LocalGlob::LocalGlob(const char *c,const char *pattern)
   : Glob(pattern)
{
   cwd=c;
}
int LocalGlob::Do()
{
   if(done)
      return STALL;

   glob_t g;
   char *oldcwd=xgetcwd();
   if(!oldcwd)
   {
      SetError("cannot get current directory");
      return MOVED;
   }
   if(chdir(cwd)==-1)
   {
      const char *se=strerror(errno);
      char *err=(char*)alloca(strlen(cwd)+strlen(se)+20);
      sprintf(err,"chdir(%s): %s",cwd,se);
      SetError(err);
      xfree(oldcwd);
      return MOVED;
   }

   glob(pattern, 0, NULL, &g);

   for(unsigned i=0; i<g.gl_pathc; i++)
   {
      struct stat st;
      if(stat(g.gl_pathv[i],&st)!=-1)
      {
	 if(dirs_only && !S_ISDIR(st.st_mode))
	    continue;
	 if(files_only && !S_ISREG(st.st_mode))
	    continue;
      }
      add(g.gl_pathv[i]);
   }
   globfree(&g);

   if(chdir(oldcwd)==-1)
      fprintf(stderr,"chdir(%s): %s",oldcwd,strerror(errno));
   xfree(oldcwd);

   done=true;
   return MOVED;
}

DirList *LocalAccess::MakeDirList(ArgV *a)
{
   return new LocalDirList(a,cwd);
}

#include "ArgV.h"
LocalDirList::LocalDirList(ArgV *a,const char *cwd)
   : DirList(a)
{
   fg_data=0;
   a->setarg(0,"ls");
   a->insarg(1,"-l");
   InputFilter *f=new InputFilter(a);
   f->SetCwd(cwd);
   Delete(buf);
   buf=new FileInputBuffer(f);
}
int LocalDirList::Do()
{
   if(done)
      return STALL;

   if(buf->Eof())
   {
      done=true;
      return MOVED;
   }

   if(buf->Error())
   {
      SetError(buf->ErrorText());
      return MOVED;
   }
   if(!fg_data)
      fg_data=buf->GetFgData(false);
   return STALL;
}
LocalDirList::~LocalDirList()
{
   if(fg_data)
      delete fg_data;
}

#ifdef MODULE
CDECL void module_init()
{
   LocalAccess::ClassInit();
}
#endif
