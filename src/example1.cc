/*
	This example shows blocking usage of Ftp class
*/

#include <config.h>
#include <unistd.h>
#include "FileAccess.h"

int main()
{
   FileAccess *f=FileAccess::New("ftp","ftp.yar.ru");
   if(!f)
   {
      fprintf(stderr,"ftp: unknown protocol, cannot create ftp session\n");
      return 1;
   }
   f->Open("/pub/source/lftp",f->LONG_LIST);
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
	 break;
      }
      write(1,buf,res);
   }
   SMTask::Delete(f);
   return 0;
}
