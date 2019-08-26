/*
	This example shows blocking usage of Ftp class
*/

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

   ResMgr::Set("ftp:use-stat-for-list",0,"true");
   ResMgr::Set("ftp:sync-mode",0,"false");

   FileAccess *f=FileAccess::New("ftp","mirror.yandex.ru");
   if(!f)
   {
      fprintf(stderr,"ftp: unknown protocol, cannot create ftp session\n");
      return 1;
   }
   f->Open("/",f->LONG_LIST);
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
