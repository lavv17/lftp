/* This file is taken from at-3.1.7 for lftp */
/* It is covered by GNU GPL, see COPYING */
%{
#include <config.h>
#include <string.h>
#include <time.h>
#include "parsetime.h"
#include "parsetime-i.h"

char *last_token = NULL;
char **my_argv;

#define COPY_TOK do { last_token = strdup(yytext); } while(0)

#define COPY_VAL do { yylval.charval = strdup(yytext); } while(0)

#define YY_INPUT(buf,result,max_size) \
    do { \
	if (*my_argv == NULL) { \
	    result = YY_NULL ;\
	    buf[0] = 0; \
	} \
	else { \
	    if (**my_argv == '\0') { \
		buf[0] = ' ' ; \
		my_argv ++; \
	    } \
	    else { \
		buf[0] = **my_argv; \
		(*my_argv) ++; \
	    } \
	result = 1; \
	} \
    } while(0)
%}

WORD		[a-z][a-z0-9]+
%%

now		{ COPY_TOK ; return NOW; }
am		{ COPY_TOK ; return AM; }
pm		{ COPY_TOK ; return PM; }
noon		{ COPY_TOK ; return NOON; }
midnight	{ COPY_TOK ; return MIDNIGHT; }
teatime		{ COPY_TOK ; return TEATIME; }
sun(day)?	{ COPY_TOK ; return SUN; }
mon(day)?	{ COPY_TOK ; return MON; }
tue(sday)?	{ COPY_TOK ; return TUE; }
wed(nesday)?	{ COPY_TOK ; return WED; }
thu(rsday)?	{ COPY_TOK ; return THU; }
fri(day)?	{ COPY_TOK ; return FRI; }
sat(urday)?	{ COPY_TOK ; return SAT; }
today		{ COPY_TOK ; return TODAY; }
tomorrow	{ COPY_TOK ; return TOMORROW; }
next		{ COPY_TOK ; return NEXT; }
min		{ COPY_TOK ; return MINUTE; }
minute(s)?	{ COPY_TOK ; return MINUTE; }
hour(s)?	{ COPY_TOK ; return HOUR; }
day(s)?		{ COPY_TOK ; return DAY; }
week(s)?	{ COPY_TOK ; return WEEK; }
month(s)?	{ COPY_TOK ; return MONTH; }
jan(uary)?	{ COPY_TOK ; return JAN; }
feb(uary)?	{ COPY_TOK ; return FEB; }
mar(ch)?	{ COPY_TOK ; return MAR; }
apr(il)?	{ COPY_TOK ; return APR; }
may		{ COPY_TOK ; return MAY; }
jun(e)?		{ COPY_TOK ; return JUN; }
jul(y)?		{ COPY_TOK ; return JUL; }
aug(ust)?	{ COPY_TOK ; return AUG; }
sep(tember)?	{ COPY_TOK ; return SEP; }
oct(ober)?	{ COPY_TOK ; return OCT; }
nov(ember)?	{ COPY_TOK ; return NOV; }
dec(ember)?	{ COPY_TOK ; return DEC; }
[0-9]+		{ COPY_TOK ; COPY_VAL; return INT; }
[ \t\n]		;
{WORD}		{ COPY_TOK ; COPY_VAL; return WORD; }
.		{ COPY_TOK ; return yytext[0]; }

%%

#ifdef NEED_YYWRAP
int yywrap()
{
    return 1;
}
#endif
