/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <fcntl.h>
#include "CmdExec.h"
#include "alias.h"
#include "xstring.h"

bool CmdExec::quotable(char ch,char in_quotes)
{
   if(!ch)
      return false;
   if(ch=='\\' || ch=='!' || ch==in_quotes)
      return true;
   if(in_quotes)
      return false;
   if(strchr("\"' \t>|;&",ch))
      return true;
   return false;
}

CmdExec::parse_result CmdExec::parse_one_cmd()
{
   char	 in_quotes;
   const char *line=cmd_buf.Get();
   const char *line_begin=line;
   static xstring nextarg;
   const char *alias=0;

   if(args)
      args->Empty();
   else
      args=new ArgV;

   output=0;
   char redir_type=0;
   background=0;

   if(!*line)
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
      while(is_space(*line))
	 line++;

      if(line[0]=='\\' && line[1]=='\n')
      {
	 line+=2;
	 continue;   // and continue skipping space
      }

      if(line[0]=='\r' && line[1]=='\n')
	 line++;

      if(*line==0)
	 return PARSE_AGAIN;

      if(*line=='\n'
      || *line=='|' || *line=='>' || *line==';' || *line=='&')
	 break;

      nextarg.truncate(0);

      if(args->count()==0 && *line=='#')
      {
	 // comment -- skip and return
	 while(*line!='\n' && *line)
	    line++;
	 if(*line=='\n')
	    line++;
	 else
	    return PARSE_AGAIN;
	 skip_cmd(line-line_begin);
	 return PARSE_OK;
      }

      if(args->count()==0 && *line=='!')
      {
	 // shell command -- it ends only with '\n'
	 args->Append("!");
	 line++;
	 while(is_space(*line))
	    line++;
	 while(*line!='\n' && *line)
	 {
	    if(*line=='\\' && line[1]=='\n')
	    {
	       line+=2;
	       continue;
	    }
	    nextarg.append(*line++);
	 }
	 if(*line==0)
	    return PARSE_AGAIN;
	 if(*line=='\n')
	    line++;
	 skip_cmd(line-line_begin);
	 if(nextarg.length()>0)
	    args->Append(nextarg);
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
	       nextarg.append(*line++);
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
	       if(in_quotes && *line==in_quotes)
		  in_quotes=0;
	       else if(!in_quotes && is_quote(*line))
		  in_quotes=*line;
	    }
	    nextarg.append(*line++);
	 }
	 args->Append(nextarg);
	 line++;  // skip )
	 while(is_space(*line))
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
	 if(line[0]=='\r' && line[1]=='\n')
	    line++;
	 if(*line=='\\' && quotable(line[1],in_quotes))
	 {
	    line++;
	 }
	 else
	 {
	    if(*line==0)
	       return PARSE_AGAIN;
	    if(*line=='\n'
	    || (!in_quotes && (is_space(*line)
		     || *line=='>' || *line=='|' || *line==';' || *line=='&')))
	       break;
	    if(!in_quotes && is_quote(*line))
	    {
	       in_quotes=*line;
	       line++;
	       continue;
	    }
	    if(in_quotes && *line==in_quotes)
	    {
	       in_quotes=0;
	       line++;
	       continue;
	    }
	 }
	 nextarg.append(*line++);
      }
      if(*line==0)
	 return PARSE_AGAIN;  // normal commands finish with \n or ;

      // check if the first arg is an alias, expand it accordingly.
      if(args->count()==0)
      {
	 alias=Alias::Find(nextarg);
      	 if(alias)
	 {
	    int alias_len=strlen(alias);
	    /* Check if the previous alias ends before the end of new one.
	     * So the new alias does not expand entirely from previous
	     * aliases and we can repeat the expansion from the very beginning. */
	    if(alias_field<(int)(line-line_begin))
	       free_used_aliases();
	    if(!TouchedAlias::IsTouched(alias,used_aliases))
	    {
	       skip_cmd(line-line_begin);

	       used_aliases=new TouchedAlias(alias,used_aliases);

	       cmd_buf.Prepend(alias);
	       alias_field+=alias_len;
	       line=line_begin=cmd_buf.Get();
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
      skip_cmd(line-line_begin);
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
      while(is_space(*line))
	 line++;

      if(*line==0)
	 return PARSE_AGAIN;

      if(*line=='\n' || *line==';' || *line=='&')
      {
	 if(redir_type=='|')
	    eprintf(_("parse: missing filter command\n"));
	 else
	    eprintf(_("parse: missing redirection filename\n"));

	 if(*line==';' || *line=='&' || *line=='\n')
	    line++;

	 skip_cmd(line-line_begin);
	 return PARSE_ERR;
      }

      nextarg.truncate(0);

      in_quotes=0;
      for(;;)
      {
	 if(*line=='\\' && line[1]=='\n')
	 {
	    line+=2;
	    continue;
	 }
	 if(*line=='\\' && quotable(line[1],in_quotes))
	    line++;
	 else
	 {
	    if(*line==0)
	       return PARSE_AGAIN;
	    // filename can end with a space, filter command can't.
	    if(*line=='\n' || (!in_quotes
		  && ((redir_type!='|' && is_space(*line))
		      || *line==';' || *line=='&')))
	       break;
	    if(!in_quotes && is_quote(*line))
	    {
	       in_quotes=*line;
	       line++;
	       continue;
	    }
	    if(in_quotes && *line==in_quotes)
	    {
	       in_quotes=0;
	       line++;
	       continue;
	    }
	 }
	 nextarg.append(*line++);
      }
      // skip spaces
      while(is_space(*line))
	 line++;
   }

cmd_end:
   if((line[0]=='&' && line[1]=='&')
   || (line[0]=='|' && line[1]=='|'))
      ;
   else if(*line==';' || *line=='&' || *line=='\n')
   {
      if(*line=='&')
	 background=1;
      line++;
   }
   skip_cmd(line-line_begin);

   switch(redir_type)
   {
   case('|'):
      output=new OutputFilter(nextarg);
      break;
   case('>'):
      output=new FileStream(nextarg,O_WRONLY|O_TRUNC|O_CREAT);
      break;
   case('+'):
      output=new FileStream(nextarg,O_WRONLY|O_APPEND|O_CREAT);
      break;
   }

   return PARSE_OK;
}
