#include <config.h>
#include <unistd.h>
#include "CmdExec.h"

int main()
{
   CmdExec exec(0,0);
   exec.FeedCmd("open ftp://ftp.yar.ru/pub/source/lftp/\n");
   exec.FeedCmd("cls -l\n");
   exec.WaitDone();
   return exec.ExitCode();
}
