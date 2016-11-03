#include <config.h>
#include <unistd.h>
#include "FileAccess.h"
#include "log.h"

char *program_name;

int main(int argc,char **argv)
{
   program_name=argv[0];

   Log::global=new Log("debug");
   ResMgr::Set("log:level",0,"5");
   ResMgr::Set("log:enabled",0,"true");

   FileAccess *f=FileAccess::New("http","lftp.tech");
   if(!f)
   {
      fprintf(stderr,"http: unknown protocol, cannot create http session\n");
      return 1;
   }
   f->Open("/ftp/",f->RETRIEVE);
   Buffer buf;
   for(;;)
   {
      SMTask::Schedule();

      int res=f->Read(&buf,sizeof(buf));
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
      buf.SpaceAdd(res);
      write(1,buf.Get(),res);
      buf.Skip(res);
   }
   SMTask::Delete(f);
   return 0;
}
