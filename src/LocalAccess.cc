/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <utime.h>
#include <pwd.h>

#include "LocalAccess.h"
#include "xstring.h"
#include "misc.h"
#include "log.h"
#include "LocalDir.h"

CDECL_BEGIN
#include <glob.h>
CDECL_END

#define FILES_AT_ONCE_STAT 64
#define FILES_AT_ONCE_READDIR 256

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
   home.Set(getenv("HOME"));
   hostname.set("localhost");
   struct passwd *p=getpwuid(getuid());
   if(p)
      user.set(p->pw_name);
}

LocalAccess::LocalAccess() : FileAccess()
{
   Init();
   xstring_ca c(xgetcwd());
   cwd.Set(c?c.get():".");
   LogNote(10,"local cwd is `%s'",cwd.path.get());
}
LocalAccess::LocalAccess(const LocalAccess *o) : FileAccess(o)
{
   Init();
}

void LocalAccess::errno_handle()
{
   saved_errno=errno;
   const char *err=strerror(saved_errno);
   if(mode==RENAME)
      error.vset("rename(",file.get(),", ",file1.get(),"): ",err,NULL);
   else
      error.vset(file.get(),": ",err,NULL);
   if(saved_errno!=EEXIST)
      LogError(0,"%s",error.get());
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
	 const char *cmd=0;
	 // FIXME: shell-quote file name
	 if(mode==LIST)
	 {
	    if(file && file[0])
	       cmd=xstring::cat("ls ",shell_encode(file).get(),NULL);
	    else
	       cmd="ls";
	 }
	 else if(mode==LONG_LIST)
	 {
	    if(file && file[0])
	       cmd=xstring::cat("ls -l",shell_encode(file).get(),NULL);
	    else
	       cmd="ls -la";
	 }
	 else// if(mode==QUOTE_CMD)
	    cmd=file;
	 LogNote(5,"running `%s'",cmd);
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
      return m;
   case(CHANGE_DIR):
   {
      LocalDirectory old_cwd;
      old_cwd.SetFromCWD();
      const char *err=old_cwd.Chdir();
      if(err)
      {
	 SetError(NO_FILE,err);
	 return MOVED;
      }
      if(chdir(file)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      else
      {
	 cwd.Set(file);
	 old_cwd.Chdir();
      }
      done=true;
      return MOVED;
   }
   case(REMOVE):
   case(REMOVE_DIR): {
      const char *f=dir_file(cwd,file);
      LogNote(5,"remove(%s)",f);
      if((mode==REMOVE?remove:rmdir)(f)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      done=true;
      return MOVED;
   }
   case(RENAME):
   case(LINK):
   case(SYMLINK):
   {
      const char *cwd_file1=dir_file(cwd,file1);
      int (*fn)(const char *f1,const char *f2)=(
	 mode==RENAME ? rename :
	 mode==LINK ? link :
	 /*mode==SYMLINK?*/ symlink
      );
      if(fn(mode==SYMLINK?file.get():dir_file(cwd,file),cwd_file1)==-1)
      {
	 errno_handle();
	 error_code=NO_FILE;
      }
      done=true;
      return MOVED;
   }
   case(MAKE_DIR):
      if(mkdir_p)
      {
	 const char *sl=strchr(file,'/');
	 while(sl)
	 {
	    if(sl>file)
	       mkdir(dir_file(cwd,xstring::get_tmp(file,sl-file)),0775);
	    sl=strchr(sl+1,'/');
	 }
      }
      if(mkdir(dir_file(cwd,file),0775)==-1)
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
	    if(opt_size) *opt_size=NO_SIZE;
	    if(opt_date) *opt_date=NO_DATE;
	 }
	 else
	 {
	    if(opt_size)
	       *opt_size=(S_ISREG(st.st_mode)?st.st_size:NO_SIZE);
	    if(opt_date)
	       *opt_date=st.st_mtime;
	 }
	 opt_size=0;
	 opt_date=0;
      }
      return m;

   case(CONNECT_VERIFY):
      done=true;
      return MOVED;

   case(ARRAY_INFO):
      fill_array_info();
      done=true;
      return MOVED;
   case MP_LIST:
      SetError(NOT_SUPP);
      return MOVED;
   }
   return m;
}

void LocalAccess::fill_array_info()
{
   for(FileInfo *fi=fileset_for_info->curr(); fi; fi=fileset_for_info->next())
      fi->LocalFile(fi->name,(fi->filetype!=fi->SYMLINK));
}

int LocalAccess::Read(Buffer *buf0,int size)
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

   char *buf=buf0->GetSpace(size);
#ifndef NATIVE_CRLF
   if(ascii)
      res=read(fd,buf,size/2);
   else
#endif
      res=read(fd,buf,size);

   if(res<0)
   {
      saved_errno=errno;
      if(E_RETRY(saved_errno))
      {
	 Block(stream->getfd(),POLLIN);
	 return DO_AGAIN;
      }
      if(stream->NonFatalError(saved_errno))
	 return DO_AGAIN;
      return SEE_ERRNO;
   }
   stream->clear_status();
   if(res==0)
      return res; // eof

#ifndef NATIVE_CRLF
   if(ascii)
   {
      char *p=buf;
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
   off_t shift=pos+res-real_pos;
   if(shift>0)
   {
      memmove(buf,buf+shift,size-shift);
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
      saved_errno=errno;
      if(E_RETRY(saved_errno))
      {
	 Block(stream->getfd(),POLLOUT);
	 return DO_AGAIN;
      }
      if(stream->NonFatalError(saved_errno))
      {
	 // in case of full disk, check file correctness.
	 if(saved_errno==ENOSPC)
	 {
	    struct stat st;
	    if(fstat(fd,&st)!=-1)
	    {
	       if(st.st_size<pos)
	       {
		  // workaround solaris nfs bug. It can lose data if disk is full.
		  pos=real_pos=st.st_size;
		  return DO_AGAIN;
	       }
	    }
	 }
	 return DO_AGAIN;
      }
      return SEE_ERRNO;
   }
   stream->clear_status();

   if(res==len)
      res+=skip_cr;
   pos=(real_pos+=res);
   return res;
}

int LocalAccess::StoreStatus()
{
   if(mode!=STORE)
      return OK;
   if(!stream)
      return IN_PROGRESS;
   if(stream->getfd()==-1)
   {
      if(stream->error())
	 SetError(NO_FILE,stream->error_text);
   }
   stream=0;
   if(error_code==OK && entity_date!=NO_DATE)
   {
      static struct utimbuf ut;
      ut.actime=ut.modtime=entity_date;
      utime(dir_file(cwd,file),&ut);
   }

   if(error_code<0)
      return error_code;

   return OK;
}

void LocalAccess::Close()
{
   done=false;
   error_code=OK;
   stream=0;
   FileAccess::Close();
}

const char *LocalAccess::CurrentStatus()
{
   if(stream && stream->status)
      return stream->status;
   return "";
}

bool LocalAccess::SameLocationAs(const FileAccess *fa) const
{
   if(!SameProtoAs(fa))
      return false;
   LocalAccess *o=(LocalAccess*)fa;

   if(xstrcmp(home,o->home))
      return false;

   return !xstrcmp(cwd,o->cwd);
}

class LocalListInfo : public ListInfo
{
   DIR *dir;
public:
   LocalListInfo(FileAccess *s,const char *d) : ListInfo(s,d), dir(0) {}
   ~LocalListInfo() { if(dir) closedir(dir); }
   const char *Status();
   int Do();
};

ListInfo *LocalAccess::MakeListInfo(const char *path)
{
   return new LocalListInfo(this,path);
}

int LocalListInfo::Do()
{
   int m=STALL;

   if(done)
      return STALL;

   if(!dir && !result)
   {
      const char *path=session->GetCwd();
      dir=opendir(path);
      if(!dir)
      {
	 SetError(xstring::format("%s: %s",path,strerror(errno)));
	 return MOVED;
      }
      m=MOVED;
   }
   if(dir)
   {
      if(!result)
	 result=new FileSet;
      int count=FILES_AT_ONCE_READDIR;
      for(;;)
      {
	 struct dirent *f=readdir(dir);
	 if(f==0)
	    break;
	 const char *name=f->d_name;
	 if(name[0]=='~')
	    name=dir_file(".",name);
	 result->Add(new FileInfo(name));
	 if(!--count)
	    return MOVED;	// let other tasks run
      }
      closedir(dir);
      dir=0;
      result->rewind();
      m=MOVED;
   }
   if(!dir && result)
   {
      int count=FILES_AT_ONCE_STAT;
      const char *path=session->GetCwd();
      for(FileInfo *file=result->curr(); file!=0; file=result->next())
      {
	 const char *name=dir_file(path,file->name);
	 file->LocalFile(name, follow_symlinks);
	 if(!(file->defined&file->TYPE))
	    result->SubtractCurr();
	 if(!--count)
	    return MOVED;	// let other tasks run
      }
      result->Exclude(exclude_prefix,exclude,excluded.get_non_const());
      done=true;
      m=MOVED;
   }
   return m;
}
const char *LocalListInfo::Status()
{
   if(done)
      return "";
   if(dir && result)
      return xstring::format("%s (%d)",_("Getting directory contents"),result->count());
   if(result && result->count())
      return xstring::format("%s (%d%%)",_("Getting files information"),result->curr_pct());
   return "";
}

#include "FileGlob.h"
class LocalGlob : public Glob
{
   const char *cwd;
public:
   LocalGlob(const char *cwd,const char *pattern);
   const char *Status() { return "Reading directory"; }
   int Do();
};
Glob *LocalAccess::MakeGlob(const char *pattern)
{
   file.set(pattern);
   ExpandTildeInCWD();
   return new LocalGlob(cwd,file);
}

LocalGlob::LocalGlob(const char *c,const char *pattern)
   : Glob(0,pattern)
{
   cwd=c;
}
int LocalGlob::Do()
{
   if(done)
      return STALL;

   glob_t g;
   LocalDirectory oldcwd;
   oldcwd.SetFromCWD();
   // check if we can return.
   if(oldcwd.Chdir())
   {
      SetError(_("cannot get current directory"));
      return MOVED;
   }
   if(chdir(cwd)==-1)
   {
      SetError(xstring::format("chdir(%s): %s",cwd,strerror(errno)));
      return MOVED;
   }

   glob(pattern, 0, NULL, &g);

   for(unsigned i=0; i<g.gl_pathc; i++)
   {
      struct stat st;
      FileInfo info(g.gl_pathv[i]);
      if(stat(g.gl_pathv[i],&st)!=-1)
      {
	 if(dirs_only && !S_ISDIR(st.st_mode))
	    continue;
	 if(files_only && !S_ISREG(st.st_mode))
	    continue;
	 if(S_ISDIR(st.st_mode))
	    info.SetType(info.DIRECTORY);
	 else if(S_ISREG(st.st_mode))
	    info.SetType(info.NORMAL);
      }
      add(&info);
   }
   globfree(&g);

   const char *err=oldcwd.Chdir();
   const char *name=oldcwd.GetName();
   if(err)
      fprintf(stderr,"chdir(%s): %s",name?name:"?",err);

   done=true;
   return MOVED;
}

class LocalDirList : public DirList
{
   SMTaskRef<IOBuffer> ubuf;
   Ref<FgData> fg_data;
public:
   LocalDirList(ArgV *a,const char *cwd);
   const char *Status() { return ""; }
   int Do();
};

DirList *LocalAccess::MakeDirList(ArgV *a)
{
   return new LocalDirList(a,cwd);
}

#include "ArgV.h"
LocalDirList::LocalDirList(ArgV *a,const char *cwd)
   : DirList(0,0)
{
   a->setarg(0,"ls");
   a->insarg(1,"-l");
   InputFilter *f=new InputFilter(a); // a is consumed.
   f->SetCwd(cwd);
   ubuf=new IOBufferFDStream(f,IOBuffer::GET);
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

   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      return MOVED;
   }
   if(!fg_data)
      fg_data=ubuf->GetFgData(false);
   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      buf->PutEOF();
      return MOVED;
   }
   if(len==0)
      return STALL;
   buf->Put(b,len);
   ubuf->Skip(len);
   return MOVED;
}

#include "modconfig.h"
#ifdef MODULE_PROTO_FILE
CDECL void module_init()
{
   LocalAccess::ClassInit();
}
#endif
