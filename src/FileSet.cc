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
#include "FileSet.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include "misc.h"
#include "ResMgr.h"

void  FileInfo::Merge(const FileInfo& f)
{
   if(strcmp(name,f.name))
      return;
// int sim=defined&f.defined;
   int dif=(~defined)&f.defined;
   if(dif&MODE)
      SetMode(f.mode);
   if(dif&DATE)
      SetDate(f.date);
   if(dif&DATE_UNPREC && !(defined&DATE))
      SetDateUnprec(f.date);
   if(dif&TYPE)
      SetType(f.filetype);
   if(dif&SYMLINK)
      SetSymlink(f.symlink);
}

void FileInfo::SetName(const char *n)
{
   if(n==name)
      return;
   xfree(name);
   name=xstrdup(n);
   defined|=NAME;
}


void FileSet::Add(FileInfo *fi)
{
   if(!(fi->defined & fi->NAME))
   {
      delete fi;
      return;
   }
   for(int j=0; j<fnum; j++)
   {
      if(!strcmp(files[j]->name,fi->name))
      {
	 files[j]->Merge(*fi);
	 delete fi;
	 return;
      }
   }
   files=(FileInfo**)xrealloc(files,(++fnum)*sizeof(*files));
   files[fnum-1]=fi;
}

void FileSet::Sub(int i)
{
   if(i>=fnum)
      abort();
   delete files[i];
   memmove(files+i,files+i+1,(--fnum-i)*sizeof(*files));
   if(ind>i)
      ind--;
}

void FileSet::Merge(const FileSet *set)
{
   int i,j;
   for(i=0; i<set->fnum; i++)
   {
      for(j=0; j<fnum; j++)
      {
      	 if(!strcmp(files[j]->name,set->files[i]->name))
	 {
	    files[j]->Merge(*(set->files[i]));
	    break;
	 }
      }
      if(j==fnum)
      {
	 Add(new FileInfo(*set->files[i]));
      }
   }
}

void FileSet::Merge(char **list)
{
   if(list==0)
      return;

   int j;
   for( ; *list; list++)
   {
      for(j=0; j<fnum; j++)
      {
      	 if(!strcmp(files[j]->name,*list))
	    break;
      }
      if(j==fnum)
      {
	 FileInfo *fi=new FileInfo();
	 fi->SetName(*list);
	 Add(fi);
      }
   }
}

FileSet::FileSet(FileSet const *set)
{
   ind=set->ind;
   fnum=set->fnum;
   if(fnum==0)
      files=0;
   else
      files=(FileInfo**)xmalloc(fnum*sizeof(*files));
   for(int i=0; i<fnum; i++)
      files[i]=new FileInfo(*(set->files[i]));
}

void FileSet::Empty()
{
   for(int i=0; i<fnum; i++)
      delete files[i];
   xfree(files);
   files=0;
   fnum=0;
   ind=0;
}

FileSet::~FileSet()
{
   Empty();
}

void FileSet::SubtractSame(const FileSet *set,bool only_newer,
      const TimeInterval *prec,const TimeInterval *loose_prec,int ignore)
{
   for(int i=0; i<fnum; i++)
   {
      FileInfo *f=set->FindByName(files[i]->name);
      if(f && files[i]->SameAs(f,only_newer,prec,loose_prec,ignore))
	 Sub(i--);
   }
}

void FileSet::SubtractAny(const FileSet *set)
{
   for(int i=0; i<fnum; i++)
      if(set->FindByName(files[i]->name))
	 Sub(i--);
}

void FileSet::SubtractNotIn(const FileSet *set)
{
   for(int i=0; i<fnum; i++)
      if(!set->FindByName(files[i]->name))
	 Sub(i--);
}

void FileSet::SubtractOlderThan(time_t t)
{
   for(int i=0; i<fnum; i++)
   {
      if(files[i]->defined&FileInfo::TYPE
      && files[i]->filetype!=FileInfo::NORMAL)
	 continue;
      if(files[i]->OlderThan(t))
      {
	 Sub(i);
	 i--;
      }
   }
}

void FileSet::ExcludeDots()
{
   for(int i=0; i<fnum; i++)
   {
      if(!strcmp(files[i]->name,".") || !strcmp(files[i]->name,".."))
      {
	 Sub(i);
	 i--;
      }
   }
}

bool  FileInfo::SameAs(const FileInfo *fi,bool only_newer,
	 const TimeInterval *prec,const TimeInterval *loose_prec,int ignore)
{
   if(defined&NAME && fi->defined&NAME)
      if(strcmp(name,fi->name))
	 return false;
   if(defined&TYPE && fi->defined&TYPE)
      if(filetype!=fi->filetype)
	 return false;

   if((defined&TYPE && filetype==DIRECTORY)
   || (fi->defined&TYPE && fi->filetype==DIRECTORY))
      return false;  // can't guarantee directory is the same (recursively)

   if(defined&SYMLINK_DEF && fi->defined&SYMLINK_DEF)
      return (strcmp(symlink,fi->symlink)==0);

   if(defined&(DATE|DATE_UNPREC) && fi->defined&(DATE|DATE_UNPREC)
   && !(ignore&DATE))
   {
      time_t p;
      bool inf;
      if((defined&DATE_UNPREC) || (fi->defined&DATE_UNPREC))
      {
	 p=loose_prec->Seconds();
	 inf=loose_prec->IsInfty();
      }
      else
      {
	 p=prec->Seconds();
	 inf=prec->IsInfty();
      }
      if(only_newer && date<fi->date)
	    return true;
      if(!inf && abs((long)date-(long)(fi->date))>p)
	 return false;
   }

   if(defined&SIZE && fi->defined&SIZE && !(ignore&SIZE))
      if(size!=fi->size)
	 return false;

   return true;
}

bool  FileInfo::OlderThan(time_t t)
{
   return ((defined&(DATE|DATE_UNPREC)) && date<t);
}

void FileSet::Count(int *d,int *f,int *s,int *o)
{
   for(int i=0; i<fnum; i++)
   {
      if(!(files[i]->defined&FileInfo::TYPE))
      {
	 if(o) (*o)++;
      }
      else switch(files[i]->filetype)
      {
      case(FileInfo::DIRECTORY):
	 if(d) (*d)++; break;
      case(FileInfo::NORMAL):
	 if(f) (*f)++; break;
      case(FileInfo::SYMLINK):
	 if(s) (*s)++; break;
      }
   }
}

FileInfo *FileSet::FindByName(const char *name) const
{
   for(int i=0; i<fnum; i++)
   {
      if(!strcmp(files[i]->name,name))
	 return files[i];
   }
   return 0;
}

void  FileSet::Exclude(const char *prefix,regex_t *exclude,regex_t *include)
{
   for(int i=0; i<fnum; i++)
   {
      const char *name=dir_file(prefix,files[i]->name);
      if(!(include && regexec(include,name,0,0,0)==0)
       && ((exclude && regexec(exclude,name,0,0,0)==0)
	   || (include && !exclude)))
      {
	 Sub(i);
	 i--;
      }
   }
}


// *** Manipulations with set of local files

#if 0
void FileSet::LocalRemove(const char *dir)
{
   FileInfo *file;
   for(int i=0; i<fnum; i++)
   {
      file=files[i];
      if(file->defined & (file->DATE|file->DATE_UNPREC))
      {
	 const char *local_name=dir_file(dir,file->name);

	 if(!(file->defined & file->TYPE)
	 || file->filetype==file->DIRECTORY)
	 {
	    int res=rmdir(local_name);
	    if(res==0)
	       continue;
	    res=remove(local_name);
	    if(res==0)
	       continue;
	    truncate_file_tree(local_name);
	    continue;
	 }
	 remove(local_name);
      }
   }
}
#endif

void FileSet::LocalUtime(const char *dir,bool only_dirs)
{
   FileInfo *file;
   for(int i=0; i<fnum; i++)
   {
      file=files[i];
      if(file->defined & (file->DATE|file->DATE_UNPREC))
      {
	 if(!(file->defined & file->TYPE))
	    continue;
	 if(file->filetype==file->SYMLINK)
	    continue;
	 if(only_dirs && file->filetype!=file->DIRECTORY)
	    continue;

	 const char *local_name=dir_file(dir,file->name);
	 struct utimbuf ut;
	 struct stat st;
	 ut.actime=ut.modtime=file->date;

	 if(stat(local_name,&st)!=-1 && st.st_mtime!=file->date)
	    utime(local_name,&ut);
      }
   }
}
void FileSet::LocalChmod(const char *dir,mode_t mask)
{
   FileInfo *file;
   for(int i=0; i<fnum; i++)
   {
      file=files[i];
      if(file->defined & file->MODE)
      {
	 if(file->defined & file->TYPE
	 && file->filetype==file->SYMLINK)
	    continue;

	 const char *local_name=dir_file(dir,file->name);

	 struct stat st;
	 mode_t new_mode=file->mode&~mask;

	 if(stat(local_name,&st)!=-1 && st.st_mode!=new_mode)
	    chmod(local_name,new_mode);
      }
   }
}

FileInfo *FileSet::curr()
{
   if(ind<fnum)
      return files[ind];
   return 0;
}
FileInfo *FileSet::next()
{
   if(ind<fnum)
   {
      ind++;
      return curr();
   }
   return 0;
}

static int name_compare(const void *a,const void *b)
{
   FileInfo *pa=*(FileInfo*const*)a;
   FileInfo *pb=*(FileInfo*const*)b;
   return strcmp(pa->name,pb->name);
}

void FileSet::SortByName()
{
   qsort(files,fnum,sizeof(*files),name_compare);
}

void FileInfo::Init()
{
   name=NULL;
   defined=0;
   symlink=NULL;
   data=0;
}
FileInfo::FileInfo(const FileInfo &fi)
{
   Init();
   name=xstrdup(fi.name);
   symlink=xstrdup(fi.symlink);
   defined=fi.defined;
   filetype=fi.filetype;
   mode=fi.mode;
   date=fi.date;
   size=fi.size;
}
FileInfo::~FileInfo()
{
   xfree(name);
   xfree(symlink);
   xfree(data);
}
