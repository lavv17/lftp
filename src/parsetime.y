/* This file is taken from at-3.1.7 for lftp */
/* It is covered by GNU GPL, see COPYING */
%{
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "parsetime-i.h"

#define YYDEBUG 1

time_t currtime;
struct tm exectm;
static int isgmt;
static int time_only;

int add_date(int number, int period);
%}

%union {
	char *	  	charval;
	int		intval;
}

%token  <charval> INT
%token  NOW
%token  AM PM
%token  NOON MIDNIGHT TEATIME
%token  SUN MON TUE WED THU FRI SAT
%token  TODAY TOMORROW
%token  NEXT
%token  MINUTE HOUR DAY WEEK MONTH YEAR
%token  JAN FEB MAR APR MAY JUN JUL AUG SEP OCT NOV DEC
%token  <charval> WORD

%type <intval> inc_period
%type <intval> inc_number
%type <intval> day_of_week

%start timespec
%%
timespec        : time
		    {
			time_only = 1;
		    }
                | time date
                | time increment
                | time date increment
		| time decrement
		| time date decrement
                | nowspec
                ;

nowspec         : now
                | now increment
		| now decrement
                ;

now		: NOW
		;

time            : hr24clock_hr_min
                | hr24clock_hr_min timezone_name
                | hr24clock_hour time_sep minute
                | hr24clock_hour time_sep minute timezone_name
                | hr24clock_hour am_pm
                | hr24clock_hour am_pm timezone_name
                | hr24clock_hour time_sep minute am_pm
                | hr24clock_hour time_sep minute am_pm timezone_name
                | NOON
		    {
			exectm.tm_hour = 12;
			exectm.tm_min = 0;
		    }
                | MIDNIGHT
		    {
			exectm.tm_hour = 0;
			exectm.tm_min = 0;
			add_date(1, DAY);
		    }
		| TEATIME
		    {
			exectm.tm_hour = 16;
			exectm.tm_min = 0;
		    }
                ;

date            : month_name day_number
                | month_name day_number ',' year_number
                | day_of_week
		   {
		       add_date ((7 + $1 - exectm.tm_wday) %7, DAY);
		   }
                | TODAY
                | TOMORROW
		   {
			add_date(1, DAY);
		   }
		| year_number '-' month_number '-' day_number
		| day_number '.' month_number '.' year_number
		| day_number '.' month_number
		| day_number month_name
		| day_number month_name year_number
		| month_number '/' day_number '/' year_number
                ;

increment       : '+' inc_number inc_period
		    {
		        add_date($2, $3);
		    }
                | NEXT inc_period
		    {
			add_date(1, $2);
		    }
		| NEXT day_of_week
		    {
			add_date ((7 + $2 - exectm.tm_wday) %7, DAY);
		    }
                ;

decrement	: '-' inc_number inc_period
		    {
			add_date(-$2, $3);
		    }
		;

inc_period      : MINUTE { $$ = MINUTE ; }
                | HOUR	 { $$ = HOUR ; }
                | DAY	 { $$ = DAY ; }
                | WEEK   { $$ = WEEK ; }
                | MONTH  { $$ = MONTH ; }
                | YEAR   { $$ = YEAR ; }
                ;

hr24clock_hr_min: INT
		    {
			exectm.tm_min = -1;
			exectm.tm_hour = -1;
			if (strlen($1) == 4) {
			    sscanf($1, "%2d %2d", &exectm.tm_hour,
				&exectm.tm_min);
			}
			else {
			    sscanf($1, "%d", &exectm.tm_hour);
			    exectm.tm_min = 0;
			}
			free($1);

			if (exectm.tm_min > 60 || exectm.tm_min < 0) {
			    yyerror("Problem in minutes specification");
			    YYERROR;
			}
			if (exectm.tm_hour > 24 || exectm.tm_hour < 0) {
			    yyerror("Problem in minutes specification");
			    YYERROR;
		        }
		    }
		;

timezone_name	: WORD
		    {
			if (strcasecmp($1,"utc") == 0) {
			    isgmt = 1;
			}
			else {
			    yyerror("Only UTC timezone is supported");
			    YYERROR;
			}
			free($1);
		    }
		;

hr24clock_hour	: hr24clock_hr_min
		;

minute		: INT
                    {
			if (sscanf($1, "%d", &exectm.tm_min) != 1) {
			    yyerror("Error in minute");
			    YYERROR;
		        }
			free($1);
		    }
		;

am_pm		: AM
		| PM
		    {
			if (exectm.tm_hour > 12) {
			    yyerror("Hour too large for PM");
			    YYERROR;
			}
			else if (exectm.tm_hour < 12) {
			    exectm.tm_hour +=12;
			}
		    }
		;


month_name	: JAN { exectm.tm_mon = 0; }
		| FEB { exectm.tm_mon = 1; }
		| MAR { exectm.tm_mon = 2; }
		| APR { exectm.tm_mon = 3; }
		| MAY { exectm.tm_mon = 4; }
		| JUN { exectm.tm_mon = 5; }
		| JUL { exectm.tm_mon = 6; }
		| AUG { exectm.tm_mon = 7; }
		| SEP { exectm.tm_mon = 8; }
		| OCT { exectm.tm_mon = 9; }
		| NOV { exectm.tm_mon =10; }
		| DEC { exectm.tm_mon =11; }
		;

month_number	: INT
		    {
			{
			    int mnum = -1;
			    sscanf($1, "%d", &mnum);

			    if (mnum < 1 || mnum > 12) {
				yyerror("Error in month number");
				YYERROR;
			    }
			    exectm.tm_mon = mnum -1;
			    free($1);
			}
		    }
day_number	: INT
                     {
			exectm.tm_mday = -1;
			sscanf($1, "%d", &exectm.tm_mday);
			if (exectm.tm_mday < 0 || exectm.tm_mday > 31)
			{
			    yyerror("Error in day of month");
			    YYERROR;
			}
			free($1);
		     }
		;

year_number	: INT
		    {
			{
			    int ynum;

			    if ( sscanf($1, "%d", &ynum) != 1) {
				yyerror("Error in year");
				YYERROR;
			    }
			    if (ynum < 70) {
				ynum += 100;
			    }
			    else if (ynum > 1900) {
				ynum -= 1900;
			    }

			    exectm.tm_year = ynum ;
			    free($1);
			}
		    }
		;


day_of_week	: SUN { $$ = 0; }
		| MON { $$ = 1; }
		| TUE { $$ = 2; }
		| WED { $$ = 3; }
		| THU { $$ = 4; }
		| FRI { $$ = 5; }
		| SAT { $$ = 6; }
		;

inc_number	: INT
		    {
			if (sscanf($1, "%d", &$$) != 1) {
			    yyerror("Unknown increment");
			    YYERROR;
		        }
		        free($1);
		    }
		;

time_sep	: ':'
		| '\''
		| '.'
		| 'h'
		| ','
		;

%%


time_t parsetime(int, char **);

time_t
parsetime(int argc, char **argv)
{
    time_t exectime;

    my_argv = argv;
    currtime = time(NULL);
    exectm = *localtime(&currtime);
    exectm.tm_sec = 0;
    exectm.tm_isdst = -1;
    time_only = 0;
    if (yyparse() == 0) {
	exectime = mktime(&exectm);
	if (isgmt) {
	    exectime += timezone;
	    if (daylight) {
		exectime -= 3600;
	    }
	}
	if (time_only && (currtime > exectime)) {
	    exectime += 24*3600;
	}
        return exectime;
    }
    else {
	return 0;
    }
}

#ifdef TEST_PARSER
int
main(int argc, char **argv)
{
    time_t res;
    res = parsetime(argc-1, &argv[1]);
    if (res > 0) {
	printf("%s",ctime(&res));
    }
    else {
	printf("Ooops...\n");
    }
    return 0;
}

#endif
int yyerror(char *s)
{
    if (last_token == NULL)
	last_token = "(empty)";
    fprintf(stderr,"%s. Last token seen: %s\n",s, last_token);
    return 0;
}

void
add_seconds(struct tm *tm, long numsec)
{
    time_t timeval;
    timeval = mktime(tm);
    timeval += numsec;
    *tm = *localtime(&timeval);
}

int
add_date(int number, int period)
{
    switch(period) {
    case MINUTE:
	add_seconds(&exectm , 60l*number);
	break;

    case HOUR:
	add_seconds(&exectm, 3600l * number);
	break;

    case DAY:
	add_seconds(&exectm, 24*3600l * number);
	break;

    case WEEK:
	add_seconds(&exectm, 7*24*3600l*number);
	break;

    case MONTH:
	{
	    int newmonth = exectm.tm_mon + number;
	    number = 0;
	    while (newmonth < 0) {
		newmonth += 12;
		number --;
	    }
	    exectm.tm_mon = newmonth % 12;
	    number += newmonth / 12 ;
	}
	if (number == 0) {
	    break;
	}
	/* fall through */

    case YEAR:
	exectm.tm_year += number;
	break;

    default:
	yyerror("Internal parser error");
	fprintf(stderr,"Unexpected case %d\n", period);
	abort();
    }
    return 0;
}
