#include <config.h>
#include <assert.h>

#include "GetFileInfo.h"
#include "misc.h"
#include "LsCache.h"

/* Get information about a path (dir or file).  If _dir is a file, get
 * information about that file only.  If it's a directory, get information
 * about files in it.  If _showdir is true, act like ls -d: get information
 * about the directory itself.
 *
 * To find out if a path is a directory, attempt to chdir into it.  If it
 * succeeds it's a path, otherwise it's a directory (or there was an error).
 * Do this by setting the verify argument to Chdir() to true.
 *
 * If the cache knows the file type of _dir, avoid changing directories if
 * possible, so cached listings don't touch the connection at all.
 *
 * We still need to Chdir() if we're operating out of cache (that's how you
 * look up cache entries).  However, we don't really want to change the
 * directory of the session (ie. send a CWD if we're FTP), so set verify to
 * false if we're operating out of cache.
 *
 * Note: it's possible to know the type of a path (ie. its parent is cached)
 * but not its contents.  This can lead to some inconsistencies, but only in
 * fairly contrived situations (ie. "cls dir/", then change a directory in
 * dir/ to a file).  It's not possible to fix completely, and a partial fix
 * would cause other problems, so it's not worth bothering with.
 */

GetFileInfo::GetFileInfo(FileAccess *a, const char *_dir, bool _showdir)
   : ListInfo(a,0)
{
   dir=xstrdup(_dir? _dir:"", 16);

   showdir=_showdir;
   state=INITIAL;
   tried_dir=tried_file=false;
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
	    tried_file = true; /* it's a dir */
	    from_cache = true;
	    break;
	 }
      }

      assert(!tried_dir || !tried_file); /* always do at least one */

   case CHANGE_DIR:
   {
      if(tried_dir && tried_file) {
	 /* We tried both; no luck.  Fail. */
	 SetError(saved_error_text);
	 state=DONE;
	 return MOVED;
      }

      const char *cd_path;
      if(!tried_dir && (tried_file || !showdir))
      {
	 /* First, try to treat the path as a directory. */
	 tried_dir=true;
	 cd_path = dir;
	 path_to_prefix=xstrdup(dir);
	 was_directory=true;
      }
      else if(!tried_file)
      {
	 tried_file=true;

	 /* Chdir to the parent directory of the path: */
	 session->Chdir(dir, false);
	 cd_path = "..";

	 xfree(path_to_prefix);
	 path_to_prefix=dirname_alloc(dir);
	 was_directory=false;
      }

      /* See top comments for logic here: */
      session->Chdir(cd_path, !from_cache);
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
	 state=CHANGE_DIR;
	 return MOVED;
      }
      session->Close();

      if(!was_directory)
      {
	 /* We now (hopefully) have the home directory path.  Find out
	  * the real name of the path (we may have something like "..".) */

	 char *pwd = alloca_strdup(session->GetCwd());

	 session->SetCwd(origdir);
	 session->Chdir(dir, false);

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

      /* Get a listing: */
      li=session->MakeListInfo();
      if(follow_symlinks) li->FollowSymlinks();
      li->UseCache(use_cache);
      li->NoNeed(FileInfo::ALL_INFO); /* clear need */
      li->Need(need);
      SetExclude(exclude_prefix, rxc_exclude, rxc_include);
      state=GETTING_LIST;
      m=MOVED;

   case GETTING_LIST:
      if(li->Error()) {
	 SetError(li->ErrorText());
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
	 if(verify_fn[strlen(verify_fn)-1] == '/')
	    verify_fn[strlen(verify_fn)-1] = 0;

	 /* Find the file with our filename: */
	 const FileInfo *file = result->FindByName(verify_fn);

	 if(!file) {
	    delete result; result=0;
	    tried_file=true;
	    state=CHANGE_DIR;
	    return MOVED;
	 }

	 /* If we're not listing directories as files, and the file is a
	  * directory, we should have been able to Chdir into it to begin
	  * with.  We probably got Access Denied.  Fail. */
	 if(!showdir && (file->defined&file->TYPE) && file->filetype==FileInfo::DIRECTORY) {
	    delete result; result=0;
	    SetError(saved_error_text);
	    goto done;
	 }

	 FileSet *newresult=new FileSet();
	 FileInfo *copy = new FileInfo(*file);

	 newresult->Add(copy);
	 delete result;
	 result=newresult;
      }

done:
   case DONE:
      if(!done)
      {
	 if(showdir && result->get_fnum())
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

	 if(prepend_path)
	    result->PrependPath(path_to_prefix);
	 done=true;
	 session->SetCwd(origdir);
	 m=MOVED;
      }
      return m;
   }

   abort();
}

const char *GetFileInfo::Status()
{
   if(li && !li->Done()) return li->Status();

   return session->CurrentStatus();
}
