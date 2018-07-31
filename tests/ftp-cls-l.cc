#include <config.h>
#include <unistd.h>
#include "CmdExec.h"

char *program_name;

int main(int argc,char **argv)
{
   program_name=argv[0];
   JobRef<CmdExec> exec(new CmdExec(0,0));
   exec->FeedCmd("open ftp://ftp.redhat.com/redhat/\n");
   exec->FeedCmd("cls -l\n");
   exec->WaitDone();
   return exec->ExitCode();
}
