
/*
	This example shows blocking usage of Ftp class
*/

#include <config.h>
#include "ftpclass.h"
#include "ResMgr.h"
#include "SignalHook.h"
#include <unistd.h>

int main()
{
   ResMgr::ClassInit();
   SignalHook::ClassInit();

   Ftp f;
   f.Connect("ftp.yars.free.net",0);
   f.Open("/pub/software/unix/net/ftp/client/lftp",f.LONG_LIST);
   for(;;)
   {
      SMTask::Schedule();
      SMTask::Block();

      char buf[1024];
      int res=f.Read(buf,sizeof(buf));
      if(res<0)
      {
	 if(res==f.DO_AGAIN)
	    continue;
	 fprintf(stderr,"Error: %s\n",f.StrError(res));
	 return 1;
      }
      if(res==0) // eof
      {
	 f.Close();
	 return 0;
      }
      write(1,buf,res);
   }
}
