/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FILESET_H
#define FILESET_H

#include <sys/types.h>
#include "xarray.h"

#undef TYPE

class TimeInterval;
class Range;

#define NO_SIZE	     ((off_t)-1L)
#define NO_SIZE_YET  ((off_t)-2L)
#define NO_DATE	     ((time_t)-1L)
#define NO_DATE_YET  ((time_t)-2L)

struct FileTimestamp
{
   time_t ts;
   int ts_prec;
   FileTimestamp() : ts(NO_DATE_YET), ts_prec(0) {}
   void set(time_t ts1,int ts1_prec) { ts=ts1; ts_prec=ts1_prec; }
   bool is_set() { return ts!=NO_DATE && ts!=NO_DATE_YET; }
   operator time_t() const { return ts; }
   time_t operator=(time_t t) { set(t,0); return t; }
};

class FileInfo
{
   void def(unsigned m) { defined|=m; need&=~m; }
public:
   xstring  name;
   xstring  longname;
   xstring_c symlink;
   xstring_c uri;
   mode_t   mode;
   FileTimestamp date;
   off_t    size;
   xstring  data;
   const char *user, *group;
   int      nlinks;

   enum	 type
   {
      UNKNOWN=0,
      DIRECTORY,
      SYMLINK,
      NORMAL,
      REDIRECT,
   };
   type	 filetype;

   enum defined_bits
   {
      NAME=001,MODE=002,DATE=004,TYPE=010,SYMLINK_DEF=020,
      SIZE=0100,USER=0200,GROUP=0400,NLINKS=01000,

      IGNORE_SIZE_IF_OLDER=02000, // for ignore mask
      IGNORE_DATE_IF_OLDER=04000, // for ignore mask

      ALL_INFO=NAME|MODE|DATE|TYPE|SYMLINK_DEF|SIZE|USER|GROUP|NLINKS
   };
   unsigned defined;
   unsigned need;

   int rank;

   void Init();
   FileInfo() { Init(); }
   FileInfo(const FileInfo &fi);
   FileInfo(const char *n) { Init(); SetName(n); }
   FileInfo(const xstring& n) { Init(); SetName(n); }
   ~FileInfo();

   void SetName(const char *n) { name.set(n); def(NAME); }
   void SetName(const xstring& n) { name.set(n); def(NAME); }
   void SetUser(const char *n);
   void SetGroup(const char *n);
   void LocalFile(const char *name, bool follow_symlinks);
   static FileInfo *parse_ls_line(const char *line,int line_len,const char *tz);
   static FileInfo *parse_ls_line(const char *line,const char *tz) { return parse_ls_line(line,strlen(line),tz); }

   void SetMode(mode_t m) { mode=m; def(MODE); }
   void SetDate(time_t t,int prec) { date.set(t,prec); def(DATE); }
   void SetType(type t) { filetype=t; def(TYPE); }
   void SetSymlink(const char *s) { symlink.set(s); filetype=SYMLINK; def(TYPE|SYMLINK_DEF); }
   void SetRedirect(const char *s) { symlink.set(s); filetype=REDIRECT; def(TYPE|SYMLINK_DEF); }
   const char *GetRedirect() const { return symlink; }
   void	SetSize(off_t s) { size=s; def(SIZE); }
   void	SetNlink(int n) { nlinks=n; def(NLINKS); }

   void	 Merge(const FileInfo&);
   void  MergeInfo(const FileInfo& f,unsigned mask);

   bool	 SameAs(const FileInfo *,int ignore) const;
   bool	 OlderThan(time_t t) const;
   bool	 NewerThan(time_t t) const;
   bool	 NotOlderThan(time_t t) const;
   bool	 NotNewerThan(time_t t) const;
   bool  SizeOutside(const Range *r) const;
   bool	 TypeIs(type t) const { return (defined&TYPE) && filetype==t; }

   void	 SetAssociatedData(const void *d,int len) { data.nset((const char*)d,len); }
   const void *GetAssociatedData() const { return data; }

   void SetRank(int r) { rank=r; }
   int GetRank() const { return rank; }
   void MakeLongName();
   void SetLongName(const char *s) { longname.set(s); }
   const char *GetLongName() { if(!longname) MakeLongName(); return longname; }

   operator const char *() const { return name; }

   bool Has(unsigned m) const { return defined&m; }
   bool HasAny(unsigned m) const { return defined&m; }
   bool HasAll(unsigned m) const { return (defined&m)==m; }

   void Need(unsigned m) { need|=m; }
   void NoNeed(unsigned m) { need&=~m; }
};

class PatternSet;

class FileSet
{
public:
   enum sort_e { BYNAME, BYSIZE, DIRSFIRST, BYRANK, BYDATE, BYNAME_FLAT };

private:
   RefArray<FileInfo> files;

   /* indexes when sort != NAME: */
   xarray<int> sorted;
   sort_e sort_mode;

   int	 ind;

   void	 Sub(int);
   FileInfo *Borrow(int);

   void add_before(int pos,FileInfo *fi);
   void assert_sorted() const;

public:
   FileSet();
   FileSet(const FileSet *s);
   ~FileSet();

   void Empty();

   int	 get_fnum() const { return files.count(); }
   int	 count() const { return files.count(); }
   int	 curr_index() const { return ind; }
   int	 curr_pct() const { return count()==0 ? 100 : ind*100/count(); }

   void	 Add(FileInfo *);
   void	 Merge(const FileSet *);
   void	 Merge_insert(const FileSet *set);
   void	 SubtractSame(const FileSet *,int ignore);
   void	 SubtractAny(const FileSet *);
   void  SubtractTimeCmp(bool (FileInfo::*cmp)(time_t) const,time_t);
   void  SubtractOlderThan(time_t t) { SubtractTimeCmp(&FileInfo::OlderThan,t); }
   void  SubtractNewerThan(time_t t) { SubtractTimeCmp(&FileInfo::NewerThan,t); }
   void  SubtractNotOlderThan(time_t t) { SubtractTimeCmp(&FileInfo::NotOlderThan,t); }
   void  SubtractNotNewerThan(time_t t) { SubtractTimeCmp(&FileInfo::NotNewerThan,t); }
   void  SubtractSizeOutside(const Range *r);
   void  SubtractDirs();
   void  SubtractNotDirs();
   void  SubtractNotIn(const FileSet *);
   void  SubtractSameType(const FileSet *);
   void  SubtractDirs(const FileSet *);
   void  SubtractNotOlderDirs(const FileSet *);
   void  SubtractCurr();
   bool  SubtractByName(const char *name);
   void  Sort(sort_e newsort, bool casefold=false, bool reverse=false);
   void  Unsort();
   void	 SortByPatternList(const char *list_c);
   void	 ReverseSort();
   void	 UnsortFlat();

   void	 Exclude(const char *prefix,const PatternSet *x,FileSet *fsx=0);
   void	 ExcludeDots();
   void	 ExcludeCompound();
   void	 ExcludeUnaccessible(const char *user=0);

   void	 rewind() { ind=0; }
   FileInfo *curr();
   FileInfo *next();
   FileInfo *borrow_curr() { return Borrow(ind--); }

   void	 LocalRemove(const char *dir);
   void	 LocalUtime(const char *dir,bool only_dirs=false,bool flat=false);
   void	 LocalChmod(const char *dir,mode_t mask=0,bool flat=false);
   void	 LocalChown(const char *dir,bool flat=false);

   void Count(int *d,int *f,int *s,int *o) const;
   void CountBytes(long long *b) const;

   int FindGEIndByName(const char *name) const;
   FileInfo *FindByName(const char *name) const;

   void  SetSize(const char *name,off_t size)
   {
      FileInfo *f=FindByName(name);
      if(f)
	 f->SetSize(size);
   }
   void  SetDate(const char *name,time_t date,int prec)
   {
      FileInfo *f=FindByName(name);
      if(f)
	 f->SetDate(date,prec);
   }

   /* add a path to all files */
   void PrependPath(const char *path);

   /* get all defined_bits used by this fileset */
   int Have() const;

   FileInfo * operator[](int i) const;

   size_t EstimateMemory() const;
   void Dump(const char *tag) const;
};

#endif // FILESET_H
