/*
 * plural word form chooser for i18n
 *
 * Copyright (c) 1998,2016 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This file is in public domain.
 */

/*
 * This file provides a function to transform a string with all plural forms
 * of a word to a string with concrete form depending on a number.
 * It uses a rule encoded in special string.
 */

/* TODO:
 *   allow to select number of argument
 */

#include <config.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "plural.h"

static int choose_plural_form(const char *rule,int num)
{
   int res=0;
   int match=1;
   int n=num;
   char c;
   int arg,arg_len;

   for(;;)
   {
      switch(c=*rule)
      {
      case '=':
      case '!':
      case '>':
      case '<':
      case '%':
	 if(sscanf(rule+1,"%d%n",&arg,&arg_len)<1)
	    return -1;
	 rule+=arg_len;
	 if(c=='%')
	    n%=arg;
	 if((c=='=' && !(n==arg))
	 || (c=='!' && !(n!=arg))
	 || (c=='>' && !(n> arg))
	 || (c=='<' && !(n< arg)))
	    match=0;
	 break;

      case '|':
      case ' ':
      case '\0':
	 if(match)
	    return res;
	 if(c=='\0')
	    return res+1;
	 if(c=='|')
	    match=1;
	 if(c==' ') /* next rule */
	 {
	    n=num;
	    res++;
	    match=1;
	 }
	 break;
      }
      rule++;
   }
/*   return res;*/
}

/*
 * The function takes _untranslated_ string with $form1|form2|form3...$
 * inserts, and a list of integer numbers. It uses gettext on the string.
 * Using "translated" rule and the list of numbers it choose appropriate
 * plural forms.
 *
 * If the string or the rule cannot be translated, it uses untranslated
 * string and default (english) rule.
 *
 * Returns pointer to static storage, copy if needed.
 */

const char *plural(const char *format,...)
{
   static char *res=0;
   static size_t res_size=0;

   /* This is the rule for plural form choice (last condition can be omitted) */
   /* Operators: = > < ! | % */
   const char *rule=N_("=1 =0|>1");
   const char *s;
   char *store;
   int index,plural_index;

   const char *new_format=gettext(format);
   const char *new_rule=gettext(rule);

   va_list va;
   long arg;


   va_start(va,format);

   if(new_format!=format && new_rule!=rule)  /* there is translation */
   {
      rule=new_rule;	   /* use "translated" rule */
      format=new_format;   /* and translated format */
   }

   if(res==0)
      res=malloc(res_size=256);
   if(res==0)
      goto va_end_out;

   store=res;

   for(s=format; *s; s++)
   {
#define ALLOCATE \
      if(store-res+1>=res_size)		  \
      {					  \
	 int dif=store-res;		  \
	 res=realloc(res,res_size*=2);	  \
	 if(res==0)			  \
	    goto va_end_out;		  \
	 store=res+dif;			  \
      } /* end ALLOCATE */

      if(*s=='$' && s[1])
      {
	 s++;
	 if(*s=='$')
	    goto plain;

	 /* check options */
	 if(*s=='#')
	 {
	    s++;
	    if(*s=='l')	/* long */
	    {
	       s++;
	       if(*s=='l') /* long long */
	       {
		  s++;
		  arg=va_arg(va,long long)%1000000;
	       }
	       else
		  arg=va_arg(va,long);
	    }
	    else
	    {
	       arg=va_arg(va,int);
	    }
	    if(*s=='#') /* end of options */
	       s++;
	 }
	 else
	 {
	    arg=va_arg(va,int);
	 }

	 if(arg<0)
	    arg=-arg;

	 plural_index=choose_plural_form(rule,arg);

	 index=0;
	 while(index!=plural_index)
	 {
	    /* skip plural form */
	    while(*s!='$' && *s!='|' && *s)
	       s++;
	    if(*s==0)
	       goto out;
	    if(*s=='$')
	       break;
	    s++;
	    index++;
	 }
	 if(index==plural_index)
	 {
	    /* insert the form */
	    while(*s!='$' && *s!='|' && *s)
	    {
	       ALLOCATE;
	       *store++=*s++;
	    }
	    while(*s!='$' && *s)
	       s++;
	    if(*s==0)
	       goto out;
	 }
      }
      else
      {
      plain:
	 ALLOCATE;
	 *store++=*s;
      }
   }
out:
   ALLOCATE;
   *store=0;
va_end_out:
   va_end(va);
   return res;
}
