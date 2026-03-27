#include <config.h>
#include "CmdExec.h"

const char *extractFilename(const char *path) {
	// Calculate memory address
	char *pnt = (char*)path + (strlen(path) * sizeof(char));

	// Go from back to beginning and check for (back)slashes
	for (pnt--; pnt > path; pnt--)
		if (*pnt == '/' || *pnt == '\\')
			return ++pnt;

	return pnt;
}

Job *cmd_edit(CmdExec *parent)
{
   if (parent->args->count() < 2) {
	   printf("Missing filename.\n");
	   return 0;
   }
   const char *filename = parent->args->getarg(1);
   const char *baseFilename = extractFilename(filename);

   // Allocate enough bytes for the new commandline, but without format strings
   char *commandline = (char*)xmalloc(52 + strlen(filename));
   sprintf(commandline, "shell \"/bin/sh -c 'exec ${EDITOR:-vi} /tmp/lftp.%s'\"\n", baseFilename);

   char *getcommandline = (char*)xmalloc(22 + (2 * strlen(filename)));
   sprintf(getcommandline, "get %s -o /tmp/lftp.%s\n", filename, baseFilename);

   char *putcommandline = (char*)xmalloc(22 + (2 * strlen(filename)));
   sprintf(putcommandline, "put /tmp/lftp.%s -o %s\n", baseFilename, filename);

   parent->FeedCmd(getcommandline);
   parent->FeedCmd(commandline);
   parent->FeedCmd(putcommandline);

   return 0;
}

CDECL void module_init(int argc, const char * const *argv) 
{
   CmdExec::RegisterCommand("edit", cmd_edit, "edit <filename>", "Edit the file in your EDITOR\n");
}
