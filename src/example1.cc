/*
	This example shows blocking usage of Ftp class
*/

#include <config.h>
#include <unistd.h>
#include "FileAccess.h"

char *program_name;

int main(int argc,char **argv)
{
   program_name=argv[0];

   FileAccess *f=FileAccess::New("ftp","ftp.yar.ru");
   if(!f)
   {
      fprintf(stderr,"ftp: unknown protocol, cannot create ftp session\n");
      return 1;
   }
   f->Open("/pub/source/lftp",f->LONG_LIST);
   Buffer buf;
   for(;;)
   {
      SMTask::Schedule();

      int res=f->Read(&buf,1024);
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
      buf.SpaceAdd(res);
      write(1,buf.Get(),res);
      buf.Skip(res);
   }
   SMTask::Delete(f);
   return 0;
}
