#include <config.h>
#include "CmdExec.h"

Job *cmd_test1(CmdExec *parent)
{
   parent->printf("test1 called with %d arguments\n",parent->args->count());
   parent->exit_code=0;
   return 0;
}

CDECL void module_init(int argc,const char *const *argv)
{
   CmdExec::RegisterCommand("test1",cmd_test1,"test1 [args]","This test command prints the number of arguments\n");
}
