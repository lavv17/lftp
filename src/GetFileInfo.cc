/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* Get information about a path (dir or file).  If _dir is a file, get
 * information about that file only.  If it's a directory, get information
 * about files in it.  If _showdir is true, act like ls -d: get information
 * about the directory itself.
 *
 * To find out if a path is a directory, attempt to chdir into it.  If it
 * succeeds it's a directory, otherwise it's a file (or there was an error).
 *
 * If the cache knows the file type of _dir, avoid changing directories if
 * possible, so cached listings don't touch the connection at all.
 *
 * List of cases:
 *
 * showdirs off:
 * 1. We CD into the directory.
 *    a. We get a listing.  Success.
 *    b. We fail to get a listing.  Fail.
 * 2. We fail to CD into the path. We fail to CD to the parent. Fail.
 * 3. We fail to CD into the path. We CD to the parent and fail to get a listing.
 *    Do GetInfoArray case.
 * 4. We fail to CD into the path. We CD to the parent and get a listing.
 *    a. The path we were looking for is a directory.  Fail.  (If it's a directory,
 *       we need to get its contents or nothing at all.)
 *    b. The path we were looking for is not a directory.  Success.
 *    c. The path we were looking for isn't there.  Do GetInfoArray case.
 *
 * showdirs on:
 *
 * 1. We CD to the parent and get a listing.  The path we were looking for
 *    is there.  Success.
 * 2. We CD to the parent and fail to get a listing.
 *    a. We CD to the path. (Partial success: we know it exists and is a directory.)
 *    b. We fail to CD to the path. Do GetInfoArray case.
 * 3. We fail to CD to the parent.
 *    a. We CD to the path.  The path is a directory.  Success (don't try to
 *       get more information.)
 *    b. We fail to CD to the path.  Fail.
 * 4. We CD to the parent and fail to get a listing OR
 *    We CD to the parent and the listing does not contain the path.
 *    a. We CD to the path.  The path is a directory.  Success (don't try to
 *       get more information.)
 *    b. We fail to CD to the path.  Do GetInfoArray case.
 *
 * GetInfoArray case:
 * A. We GetInfoArray in the parent, which tells us something.  We
 *    know the path exists, and is not a directory that we have access
 *    to.  Success.
 * B. We GetInfoArray in the parent, which doesn't tell us anything.
 *    We have no evidence the path exists at all. Fail.
 *
 * If we fail from something in cache, we don't know why, so turn cache off
 * and attempt to CD into the path to get an error message.
 *
 * All of these cases can operate out of cache, so be sure to test both, as
 * the code flow is often different.  (GetInfoArray never operates out of cache.)
 */

#include <config.h>
#include <assert.h>

#include "GetFileInfo.h"
#include "misc.h"
#include "LsCache.h"

GetFileInfo::GetFileInfo(FileAccess *a, const char *_dir, bool _showdir)
   : ListInfo(a,0)
{
  dir=xstrdup(_dir? _dir:"");

   showdir=_showdir;
   state=INITIAL;
   tried_dir=tried_file=tried_info=false;
   result=0;
   path_to_prefix=0;
   verify_fn=0;
   li=0;
   from_cache=0;
   saved_error_text=0;
   was_directory=0;
   prepend_path=true;

   origdir=xstrdup(session->GetCwd());
}

GetFileInfo::~GetFileInfo()
{
   session->Close();
   session->SetCwd(origdir);
   Delete(li);
   xfree(saved_error_text);
   xfree(dir);
   xfree(path_to_prefix);
   xfree(origdir);
   xfree(verify_fn);
}

int GetFileInfo::Do()
{
   int res;
   int m=STALL;

   if(Done())
      return m;

   switch(state)
   {
   case INITIAL:
      state=CHANGE_DIR;
      m=MOVED;

      if(use_cache)
      {
	 switch(LsCache::IsDirectory(session,dir))
	 {
	 case 0:
	    tried_dir = true; /* it's a file */
	    from_cache = true;
	    break;
	 case 1:
	    if(!showdir)
	       tried_file = true; /* it's a dir */
	    from_cache = true;
	    break;
	 }
      }

      assert(!tried_dir || !tried_file || !tried_info); /* always do at least one */

   case CHANGE_DIR:
   {
      if(tried_dir && tried_file && tried_info) {
	 /* We tried everything; no luck.  Fail. */
	 if(saved_error_text)
	 {
	    SetError(saved_error_text);
	    state=DONE;
	    return MOVED;
	 }

	 /* We don't have an error message.  We may have done everything
	  * out of cache. */
	 tried_dir=false;  // this will get error message.
	 from_cache=false;
      }

      session->SetCwd(origdir);
      const char *cd_path=0;
      if(!tried_dir && (tried_file || !showdir))
      {
	 /* First, try to treat the path as a directory,
	  * if we are going to show its contents */
	 tried_dir=true;
	 cd_path = dir;
	 xfree(path_to_prefix);
	 path_to_prefix=xstrdup(dir);
	 was_directory=true;
      }
      else if(!tried_file)
      {
	 /* Try to treat the path as a file.  If showdir is true,
	  * this is done first. */
	 tried_file=true;

	 /* Chdir to the parent directory of the path: */
	 session->Chdir(dir, false);
	 cd_path = "..";

	 xfree(path_to_prefix);
	 path_to_prefix=dirname_alloc(dir);
	 was_directory=false;
      }
      else if(!tried_info)
      {
	 tried_info=true;
	 /* This is always done after tried_file or a failed tried_dir,
	  * so we should be in the parent, but let's make sure: */
	 session->Chdir(dir, false);
	 session->Chdir("..", false);

	 xfree(path_to_prefix);
	 path_to_prefix=dirname_alloc(dir);
	 was_directory=false;

	 /* We tried both; no luck. Fall back on ARRAY_INFO. */
	 state=GETTING_INFO_ARRAY;
	 return MOVED;
      }

      /* We still need to Chdir() if we're operating out of cache (that's how you
       * look up cache entries).  However, we don't really want to change the
       * directory of the session (ie. send a CWD if we're FTP), so set verify to
       * false if we're operating out of cache.  */
      bool cd_verify = !from_cache;

      /* We can *not* do this out of cache if 1: dir starts with a ~ and 2: we don't
       * know the home path.
       *
       * Yes we can, usually--we may know the home path in home_auto (FTP), but
       * GetHome won't return that.  We need to do this if we *really* don't know
       * it. */
      /* if(dir[0] == '~' && !session->GetHome())
	 cd_verify = true; */

      session->Chdir(cd_path, cd_verify);
      state=CHANGING_DIR;
      m=MOVED;
   }

   case CHANGING_DIR:
      res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;

      if(res<0)
      {
	 /* Failed.  Save the error, then go back and try to CD again.
	  * Only save the first error, so error texts contain the full
	  * path. */
	 if(!saved_error_text)
	    saved_error_text = xstrdup(session->StrError(res));
	 session->Close();
	 if(res==FA::NO_FILE)
	 {
	    /* If this is a CWD to the parent, and it failed, we
	     * can't do GetInfoArray. */
	    if(!was_directory)
	       tried_info=true;

	    state=CHANGE_DIR;
	    return MOVED;
	 }
	 SetError(saved_error_text);
	 state=DONE;
	 return MOVED;
      }
      session->Close();
      if(!from_cache)
	 LsCache::SetDirectory(session,"",true);

      /* Now that we've connected, we should have the home directory path. Find out
       * the real name of the path.  (We may have something like "~/..".) */
      if(!verify_fn)
      {
	 char *pwd = alloca_strdup(session->GetCwd());

	 session->SetCwd(origdir);
	 session->Chdir(dir, false);

	 xfree(verify_fn);
	 verify_fn = xstrdup(basename_ptr(session->GetCwd()));

	 /* go back */
	 session->SetCwd(pwd);
      }

      /* Special case: looking up "/". Make a phony entry. */
      if(showdir && !strcmp(verify_fn, "/"))
      {
	 FileInfo *fi = new FileInfo(verify_fn);
	 fi->SetType(fi->DIRECTORY);

	 result = new FileSet;
	 result->Add(fi);
	 state=DONE;
	 return MOVED;
      }

      if(was_directory && showdir)
      {
	 /* We could chdir to the dir, but we should not get dir listing.
	  * We got here because either we could not get dir listing of
	  * parent directory or the file name was not found in parent
	  * directory index. */
	 FileInfo *fi = new FileInfo(dir);
	 fi->SetType(fi->DIRECTORY);
	 xfree(path_to_prefix);
	 path_to_prefix=dirname_alloc(dir);

	 result = new FileSet;
	 result->Add(fi);
	 state=DONE;
	 return MOVED;
      }

      /* Get a listing: */
      Delete(li);
      li=session->MakeListInfo();
      if(follow_symlinks) li->FollowSymlinks();
      li->UseCache(use_cache);
      li->NoNeed(FileInfo::ALL_INFO); /* clear need */
      li->Need(need);
      SetExclude(exclude_prefix, exclude);
      state=GETTING_LIST;
      m=MOVED;

   case GETTING_LIST:
      if(li->Error()) {
	 /* If we're listing contents of dirs, and this was listing
	  * a path (as a directory), fail: */
	 if(!showdir && was_directory)
	 {
	    SetError(li->ErrorText());
	    return MOVED;
	 }

	 if(!saved_error_text)
	    saved_error_text = xstrdup(li->ErrorText());

	 /* Otherwise, go on to try the next mode. */
	 state=CHANGE_DIR;
	 return MOVED;
      }

      if(!li->Done())
	 return m;

      state=DONE;
      m=MOVED;

      /* Got the list.  Steal it from the listinfo: */
      result=li->GetResult();
      Delete(li); li=0;

      /* If this was a listing of the basename: */
      if(!was_directory) {
	 int len=strlen(verify_fn);
	 while(len>0 && verify_fn[len-1] == '/')
	    verify_fn[--len] = 0;

	 /* Find the file with our filename: */
	 const FileInfo *file = result->FindByName(verify_fn);

	 if(!file) {
	    /* It doesn't exist, or we have no result (failed). */
	    delete result; result=0;
	    tried_file=true;
	    from_cache=false;
	    state=CHANGE_DIR;
	    return MOVED;
	 }

	 /* If we're not listing directories as files, and the file is a
	  * directory, we should have been able to Chdir into it to begin
	  * with.  We probably got Access Denied.  Fail. */
	 if(!showdir && (file->defined&file->TYPE) && file->filetype==FileInfo::DIRECTORY) {
	    delete result; result=0;
	    if(saved_error_text)
	    {
	       SetError(saved_error_text);
	       goto done;
	    }
	    tried_file=true;
	    from_cache=false;
	    state=CHANGE_DIR;
	    return MOVED;
	 }

	 FileSet *newresult=new FileSet();
	 FileInfo *copy = new FileInfo(*file);

	 newresult->Add(copy);
	 delete result;
	 result=newresult;
      }

      goto done;

   case GETTING_INFO_ARRAY:
      if(session->IsClosed())
      {
	 /* maybe FA::fileinfo should have "need" and a FileInfo?
	  *
	  * Try to get requested information with GetInfoArray.  This
	  * also serves as a last attempt to see if the file exists--we
	  * only get here if everything else thinks the path doesn't exist.
	  */
	 get_info.file=verify_fn;
	 get_info.get_size=need&FileInfo::SIZE;
	 get_info.get_time=need&FileInfo::DATE;

	 /* We need to do at least one. */
	 if(!get_info.get_size && !get_info.get_time)
	    get_info.get_time=true;

	 session->GetInfoArray(&get_info,1);
      }

      res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;

      if(res < 0)
      {
	 if(!saved_error_text)
	    saved_error_text = xstrdup(session->StrError(res));
	 state=CHANGE_DIR;
	 return MOVED;
      }

      session->Close();
      if(get_info.size==NO_SIZE && get_info.time==NO_DATE)
      {
	 /* We didn't get any information.  The file probably doesn't
	  * exist.  Not necessarily: it might have been a directory
	  * that we don't have access to CD into.  Some servers will
	  * refuse to give even an MDTM for directories.  We could
	  * scan the MDTM and/or SIZE responses for "not a plain file"
	  * for some servers (proftpd). */
	 state=CHANGE_DIR;
	 return MOVED;
      }

      {
	 /* We got at least one, so the file exists. Fill in what we know. */
	 FileInfo *fi = new FileInfo(verify_fn);
	 was_directory = false;

	 if(get_info.size!=NO_SIZE)
	 {
	    fi->SetSize(get_info.size);
	    /* We got the size, so it's probably a file.  It could be a link,
	     * though.  (If this is done, then always request size, even if we
	     * don't Need it.) */
	    // fi->SetType(fi->NORMAL);
	 }
	 if(get_info.time!=NO_DATE)
	    fi->SetDate(get_info.time,0);

	 result = new FileSet;
	 result->Add(fi);
      }
      state=DONE;
      return MOVED;

done:
   case DONE:
      if(!done)
      {
	 if(result && showdir && result->get_fnum())
	 {
	    FileInfo *f = (*result)[0];
	    /* Make sure the filename is what was requested (ie ".."). */
	    char *fn = basename_ptr(dir);
	    f->SetName(*fn? fn:".");

	    /* If we're in show_dir mode, was_directory will always be false;
	     * set it to whether the single file is actually a directory or not. */
	    if(f->defined&f->TYPE)
	       was_directory = (f->filetype == f->DIRECTORY);
	 }
	 if(result && prepend_path)
	    result->PrependPath(path_to_prefix);
	 done=true;
	 m=MOVED;
      }
      return m;
   }

   abort();
}

const char *GetFileInfo::Status()
{
   if(Done())
      return "";

   if(li && !li->Done()) return li->Status();

   if(session->IsOpen())
      return session->CurrentStatus();

   return "";
}
