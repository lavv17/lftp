#include <config.h>
#include <unistd.h>
#include "FileAccess.h"
#include "log.h"

char *program_name;

int main(int argc,char **argv)
{
   program_name=argv[0];

   Log::global->SetOutput(2,false);
   Log::global->SetLevel(5);
   Log::global->Enable();
   Log::global->ShowNothing();

   FileAccess *f=FileAccess::New("http","ftp.yar.ru");
   if(!f)
   {
      fprintf(stderr,"http: unknown protocol, cannot create http session\n");
      return 1;
   }
   f->Open("/pub/source/lftp/",f->RETRIEVE);
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
      write(1,buf.Get(),res);
   }
   SMTask::Delete(f);
   return 0;
}
