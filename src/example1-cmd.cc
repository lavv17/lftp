#include <config.h>
#include <unistd.h>
#include "CmdExec.h"

char *program_name;

int main(int argc,char **argv)
{
   program_name=argv[0];
   CmdExec exec(0,0);
   exec.FeedCmd("open ftp://ftp.yar.ru/pub/source/lftp/\n");
   exec.FeedCmd("cls -l\n");
   exec.WaitDone();
   return exec.ExitCode();
}
