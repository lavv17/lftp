#include <config.h>
#include "Http.h"
#include "log.h"
#include "ResMgr.h"
#include "SignalHook.h"
#include <unistd.h>

int main()
{
   ResMgr::ClassInit();
   SignalHook::ClassInit();
   FileAccess::ClassInit();

   Log::global->SetOutput(2,false);
   Log::global->SetLevel(5);
   Log::global->Enable();

   Http *f=new Http;
   f->Connect("ftp.yars.free.net",0);
   f->Open("/pub/software/",f->RETRIEVE);
   for(;;)
   {
      SMTask::Schedule();

      char buf[1024];
      int res=f->Read(buf,sizeof(buf));
      if(res<0)
      {
	 if(res==f->DO_AGAIN)
	 {
	    SMTask::Block();
	    continue;
	 }
	 fprintf(stderr,"Error: %s\n",f->StrError(res));
	 return 1;
      }
      if(res==0) // eof
      {
	 f->Close();
	 return 0;
      }
      write(1,buf,res);
   }
   SMTask::Delete(f);
   return 0;
}
