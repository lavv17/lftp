#include <config.h>
#include <errno.h>
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
   dir=xstrdup(_dir? _dir:"");
   showdir=_showdir;
   state=INITIAL;
   tried_dir=tried_file=false;
   result=0;
   realdir=0;
   li=0;
   from_cache=0;
   saved_error_text=0;

   origdir=xstrdup(session->GetCwd());
}

GetFileInfo::~GetFileInfo()
{
   Delete(li);
   xfree(saved_error_text);
   xfree(dir);
   xfree(realdir);
   xfree(origdir);
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

      /* if we're not showing directories, try to skip tests we don't need */
      if(use_cache && !showdir) switch(LsCache::IsDirectory(session,dir))
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

      assert(!tried_dir || !tried_file); /* always do at least one */

   case CHANGE_DIR:
      if(tried_dir && tried_file) {
	 /* We tried both; no luck.  Fail. */
	 SetError(saved_error_text);
	 state=DONE;

	 return MOVED;
      }

      if(!tried_dir)
      {
	 /* First, try to treat the path as a directory. */
	 tried_dir=true;
	 realdir = xstrdup(dir);
      }
      else if(!tried_file)
      {
	 tried_file=true;

	 xfree(realdir);
	 realdir = xstrdup(dir);
	 /* If the path ends with a slash, and we're showing directories, remove it. */
	 if(*realdir && realdir[strlen(realdir)-1] == '/')
	    realdir[strlen(realdir)-1] = 0;

	 char *slash = strrchr(realdir, '/');
	 if(!slash)
	    realdir=xstrdup(""); /* file with no path */
	 else
	    *slash=0;
      }

      /* See top comments for logic here: */
      session->Chdir(realdir, !from_cache);
      state=CHANGING_DIR;
      m=MOVED;

   case CHANGING_DIR:
      res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;
      session->Close();

      if(res<0)
      {
	 /* Failed.  Save the error, then go back and try to CD again.
	  * Only save the first error, so error texts contain the full
	  * path. */
	 if(!saved_error_text)
	    saved_error_text = xstrdup(session->StrError(res));
	 state=CHANGE_DIR;
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

      /* If this was a listing of the dirname: */
      if(strcmp(realdir, dir)) {
	 char *filename = xstrdup(basename_ptr(dir));
	 if(filename[strlen(filename)-1] == '/')
	    filename[strlen(filename)-1] = 0;

	 /* Find the file with our filename: */
	 FileInfo *file = result->FindByName(filename);
	 xfree(filename);

	 if(!file) {
	    /* The file doesn't exist; fail. */
	    char *buf = xasprintf("%s: %s", dir, strerror(ENOENT));
	    SetError(buf);
	    xfree(buf);
	    delete result; result=0;
	    goto done;
	 }

	 /* If we're not listing directories as files, and the file is a
	  * directory, we should have been able to Chdir into it to begin
	  * with.  We probably got Access Denied.  Fail. */
	 if(!showdir && (file->defined&file->TYPE) && file->filetype==FileInfo::DIRECTORY) {
	    char *buf = xasprintf("%s: %s", dir, strerror(EACCES));
	    SetError(buf);
	    xfree(buf);
	    delete result; result=0;
	    goto done;
	 }

	 FileSet *newresult=new FileSet();
	 newresult->Add(new FileInfo(*file));
	 delete result;
	 result=newresult;
      }

      result->PrependPath(realdir);

done:
   case DONE:
      if(!done)
      {
	 done=true;
	 session->Chdir(origdir, false);
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
