/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#include <config.h>
#include <fcntl.h>
#include "CmdExec.h"
#include "alias.h"
#include "xmalloc.h"
#include "xalloca.h"
#include "xstring.h"

CmdExec::parse_result CmdExec::parse_one_cmd()
{
   int	 in_quotes;

#define quotable(ch) (ch && (strchr("\"\\",ch) \
		             || (!in_quotes && strchr(" \t>|;&",ch))))

   char *line=next_cmd;
   static char *nextarg=0;
   char *endarg;
   const char *alias=0;
   char *old_next_cmd=next_cmd;

   if(args)
      args->Empty();
   else
      args=new ArgV;

   xfree(cmd);
   cmd=0;

   if(output)
   {
      delete output;
      output=0;
   }

   char redir_type=0;
   char *redir_file=0;
   background=0;

   if(line==0 || *line==0)
   {
      // empty command
      return PARSE_OK;
   }

   if(line[0]=='&' && line[1]=='&')
   {
      condition=COND_AND;
      line+=2;
   }
   else if(line[0]=='|' && line[1]=='|')
   {
      condition=COND_OR;
      line+=2;
   }
   else
   {
      condition=COND_ANY;
   }

   // loop for all arguments
   for(;;)
   {
      // skip leading whitespace
      while(*line==' ' || *line=='\t')
	 line++;

      if(line[0]=='\\' && line[1]=='\n')
      {
	 line+=2;
	 continue;   // and continue skipping space
      }

      if(*line==0 || *line=='\n'
      || *line=='|' || *line=='>' || *line==';' || *line=='&')
	 break;

      nextarg=(char*)xrealloc(nextarg,strlen(line)+1);

      // endarg points just beyond the last char of arg
      endarg=nextarg;

      if(args->count()==0 && *line=='#')
      {
	 // comment -- skip and return
	 while(*line!='\n' && *line)
	    line++;
	 next_cmd=line;
	 if(*next_cmd=='\n')
	    next_cmd++;
	 alias_field-=next_cmd-old_next_cmd;
	 return PARSE_OK;
      }

      if(args->count()==0 && *line=='!')
      {
	 // shell command -- it ends only with '\n'
	 args->Append("!");
	 line++;
	 while(*line==' ' || *line=='\t')
	    line++;
	 while(*line!='\n' && *line)
	 {
	    if(*line=='\\' && line[1]=='\n')
	    {
	       line+=2;
	       continue;
	    }
	    *endarg++=*line++;
	 }
	 if(*line==0)
	    return PARSE_AGAIN;
	 next_cmd=line;
	 if(*next_cmd=='\n')
	    next_cmd++;
	 *endarg=0;
	 if(*nextarg)
	    args->Append(nextarg);
	 alias_field-=next_cmd-old_next_cmd;
	 return PARSE_OK;
      }

      if(args->count()==0 && *line=='(')
      {
	 line++;
	 args->Append("(");

	 int level=1;
	 in_quotes=0;
	 for(;;)
	 {
	    if(*line==0)
	       return PARSE_AGAIN;
	    if(*line=='\\' && line[1] && (strchr("\"\\",line[1])
			         || (level==1 && line[1]==')')))
	    {
	       *endarg++=*line++;
	    }
	    else
	    {
	       if(!in_quotes)
	       {
		  if(*line==')')
		  {
		     if(--level==0)
			break;
		  }
		  else if(*line=='(')
		     level++;
	       }
	       if(*line=='"')
		  in_quotes=!in_quotes;
	    }
	    *endarg++=*line++;
	 }
	 *endarg=0;
	 args->Append(nextarg);
	 line++;  // skip )
	 while(*line==' ' || *line=='\t')
	    line++;
	 goto cmd_end;
      }

      if(args->count()==0 && *line=='?')
      {
	 line++;
	 args->Append("?");
	 continue;
      }

      // loop for one argument
      in_quotes=0;
      for(;;)
      {
	 if(*line=='\\' && line[1]=='\n')
	 {
	    line+=2;
	    continue;
	 }
	 if(*line=='\\' && quotable(line[1]))
	 {
	    line++;
	 }
	 else
	 {
	    if(*line==0 || *line=='\n'
	    || (!in_quotes && (*line==' ' || *line=='\t'
		     || *line=='>' || *line=='|' || *line==';' || *line=='&')))
	       break;
	    if(*line=='"')
	    {
	       in_quotes=!in_quotes;
	       line++;
	       continue;
	    }
	 }
	 *endarg++=*line++;
      }
      if(*line==0)
	 return PARSE_AGAIN;  // normal commands finish with \n or ;
      *endarg=0;
      if(args->count()==0)
      {
	 alias=Alias::Find(nextarg);
      	 if(alias)
	 {
	    int alias_len=strlen(alias);
	    if(alias_field<(int)(line-next_cmd))
	    {
	       // the case of previous alias ending before end of new one
	       free_used_aliases();
	       old_next_cmd=next_cmd;
	    }
	    if(!TouchedAlias::IsTouched(alias,used_aliases))
	    {
	       alias_field-=next_cmd-old_next_cmd;
	       alias_field-=line-next_cmd;
	       if(alias_field<0)
		  alias_field=0;
	       used_aliases=new TouchedAlias(alias,used_aliases);
	       if(line-cmd_buf < alias_len)
	       {
		  int offs=line-cmd_buf;
		  int line_len=strlen(line);
		  cmd_buf=(char*)xrealloc(cmd_buf,line_len+1+alias_len);
		  memmove(cmd_buf+alias_len,cmd_buf+offs,line_len+1);
		  line=cmd_buf;
	       }
	       else
	       {
		  line-=alias_len;
	       }
	       memcpy(line,alias,alias_len);
	       old_next_cmd=next_cmd=line;
	       alias_field+=alias_len;
	       continue;
	    }
	 }
      }
      args->Append(nextarg);
   }

   if(*line==0)
      return PARSE_AGAIN;

   if((line[0]=='&' && line[1]=='&')
   || (line[0]=='|' && line[1]=='|'))
   {
      next_cmd=line;
      alias_field-=next_cmd-old_next_cmd;
      return PARSE_OK;
   }

   if(*line=='>' || *line=='|')
   {
      redir_type=*line;
      line++;
      if(*line=='>')
      {
	 // '>>' means append
	 redir_type='+';
	 line++;
      }

      // skip leading whitespace
      while(*line==' ' || *line=='\t')
	 line++;

      if(*line==0 || *line=='\n' || *line==';' || *line=='&')
      {
	 if(redir_type=='|')
	    eprintf(_("parse: missing filter command\n"));
	 else
	    eprintf(_("parse: missing redirection filename\n"));

	 if(*line==';' || *line=='&' || *line=='\n')
	    next_cmd=line+1;
	 else
	    next_cmd=line;
	 alias_field-=next_cmd-old_next_cmd;
	 return PARSE_ERR;
      }

      redir_file=endarg=nextarg;

      in_quotes=0;
      for(;;)
      {
	 if(*line=='\\' && line[1]=='\n')
	 {
	    line+=2;
	    continue;
	 }
	 if(*line=='\\' && quotable(line[1]))
	    line++;
	 else
	 {
	    // filename can end with a space, filter command can't.
	    if(*line==0 || *line=='\n' || (!in_quotes
		  && ((redir_type!='|' && (*line==' '||*line=='\t'))
		      || *line==';' || *line=='&')))
	       break;
	    if(*line=='"')
	    {
	       in_quotes=!in_quotes;
	       line++;
	       continue;
	    }
	 }
	 *endarg++=*line++;
      }
      *endarg=0;
      // skip spaces
      while(*line==' ' || *line=='\t')
	 line++;
   }

cmd_end:
   if((line[0]=='&' && line[1]=='&')
   || (line[0]=='|' && line[1]=='|'))
   {
      next_cmd=line;
   }
   else if(*line==';' || *line=='&' || *line=='\n')
   {
      next_cmd=line+1;
      if(*line=='&')
	 background=1;
   }
   else
   {
      next_cmd=line;
   }
   alias_field-=next_cmd-old_next_cmd;

   switch(redir_type)
   {
   case('|'):
      output=new OutputFilter(redir_file);
      break;
   case('>'):
      output=new FileStream(redir_file,O_WRONLY|O_TRUNC|O_CREAT);
      break;
   case('+'):
      output=new FileStream(redir_file,O_WRONLY|O_APPEND|O_CREAT);
      break;
   }

   return PARSE_OK;

#undef quotable
}
