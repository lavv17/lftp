#include <config.h>
#include <errno.h>
#include <assert.h>

#include "GetFileInfo.h"
#include "misc.h"

GetFileInfo::GetFileInfo(FileAccess *a, const char *_dir, bool _showdir)
{
   session=a;
   dir=xstrdup(_dir? _dir:"");
   showdir=_showdir;
   state=CHANGE_DIR;
   tried_dir=tried_file=false;
   result=0;
   realdir=0;
   li=0;

   origdir=xstrdup(session->GetCwd());

   if(_showdir) tried_dir = true;

   /* if it ends with a slash and we're not showing directories,
    * don't try it as a file at all */
   if(!_showdir && *dir && dir[strlen(dir)-1] == '/')
      tried_file = true;

   assert(!tried_dir || !tried_file); /* always do at least one */
   saved_error_text=0;
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

   if(Done())
      return STALL;

   switch(state)
   {
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
	 /* No need to save the initial directory, since we only chdir()
	  * twice when the first fails (hence leaving our cwd intact). */
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

      session->Chdir(realdir, true);
      state=CHANGING_DIR;
      return MOVED;

   case CHANGING_DIR:
      res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return STALL;
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
      li->NoNeed(FileInfo::ALL_INFO); /* clear need */
      li->Need(need);
      SetExclude(path, rxc_exclude, rxc_include);
      state=GETTING_LIST;
      return MOVED;

   case GETTING_LIST:
      if(li->Error()) {
	 SetError(li->ErrorText());
	 return MOVED;
      }

      if(!li->Done())
	 return STALL;

      state=DONE;

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
	    return MOVED;
	 }

	 /* If we're not listing directories as files, and the file is a
	  * directory, we should have been able to Chdir into it to begin
	  * with.  We probably got Access Denied.  Fail. */
	 if(!showdir && file->filetype==FileInfo::DIRECTORY) {
	    char *buf = xasprintf("%s: %s", dir, strerror(EACCES));
	    SetError(buf);
	    xfree(buf);
	    delete result; result=0;
	    return MOVED;
	 }

	 FileSet *newresult=new FileSet();
	 newresult->Add(new FileInfo(*file));
	 delete result;
	 result=newresult;
      }

      result->PrependPath(realdir);

      return MOVED;

   case DONE:
      if(done)
	 return STALL;

      done=true;
      session->Chdir(origdir, false);
      return MOVED;
   }

   abort();
}

const char *GetFileInfo::Status()
{
   if(li && !li->Done()) return li->Status();

   return session->CurrentStatus();
}
