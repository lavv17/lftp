#ifndef FILESETOUTPUT_H
#define FILESETOUTPUT_H

#include "FileSet.h"
#include "buffer.h"
#include "keyvalue.h"
#include "FileCopy.h"
#include "GetFileInfo.h"

class FileSetOutput {
   const char *FileInfoColor(const FileInfo &fi, KeyValueDB &col) const;
   const char *FileInfoSuffix(const FileInfo &fi) const;

public:
   bool classify; // add / (dir) @ (link)
   // TODO: extra-optional * for exec? maybe not, some servers stick +x on everything

   int width; // width to output, 0 to force one column
   bool color;

   enum { NONE=0, PERMS = 0x1, SIZE = 0x2, DATE = 0x4, LINKS = 0x8,
	  USER = 0x10, GROUP = 0x20, NLINKS = 0x40,

	  ALL=PERMS|SIZE|DATE|LINKS|USER|GROUP|NLINKS
   };
   int mode;

   char *pat;

   bool basenames;
   bool showdots;
   bool quiet;
   bool patterns_casefold;
   bool sort_casefold;
   bool sort_dirs_first;
   bool size_filesonly;
   bool single_column;
   bool list_directories;
   int output_block_size;

   FileSet::sort_e sort;
   FileSetOutput(): classify(0), width(0), color(false), mode(NONE),
      pat(NULL), basenames(false), showdots(false),
      quiet(false), patterns_casefold(false), sort_casefold(false),
      sort_dirs_first(false), size_filesonly(false), single_column(false),
      list_directories(false), output_block_size(0), sort(FileSet::BYNAME) { }
   ~FileSetOutput() { xfree(pat); }
   FileSetOutput(const FileSetOutput &cp);
   const FileSetOutput &FileSetOutput::operator = (const FileSetOutput &cp);

   void long_list();
   void config(FDStream *fd);
   const char *parse_argv(ArgV *a);
   static const char *ValidateArgv(char **s);

   void print(FileSet &fs, Buffer *o) const;
};

/* Job interface to FileSetOutput */
class FileCopyPeerCLS : public FileCopyPeer
{
   FileAccess *session;

   FileSetOutput fso;
   FileSet *f;
   ListInfo *list_info;

   ArgV *args;

   bool quiet;

   /* element in args we're currently processing */
   int num;

   char *dir;
   char *mask;

protected:
   ~FileCopyPeerCLS();

public:
   FileCopyPeerCLS(FA *s, ArgV *a, const FileSetOutput &_opts);
   int Do();
   const char *GetStatus();
   void Quiet() { quiet = 1; }
};


#endif
