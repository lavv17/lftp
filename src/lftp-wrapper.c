/***************************************************************************
 *                                                                         *
 *   Copyright (c) 2005 SUSE LINUX Products GmbH, Nuernberg, Germany       *
 *   Authors: Pavel Nemec  <pnemec@suse.cz>                                *
 *            Petr Ostadal <postadal@suse.cz>                              *
 *                                                                         *
 *   wrapper for ftp                                                       *
 *  this wraper should provide ftp format of comands for lftp              *
 *  only noninteractive mode is implemented                                *
 *                                                                         *
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <err.h>
#include <ctype.h>

static void help(void);
static int strsuftoi(const char *arg);
static void not_implemented_ignoring(char *what);
static void add_to_e_script(char *cmd);	/* at to end */
static void add_to_e_scriptB(char *cmd);	/* at to begin */
static char *collect_strings(int argc, char **argv);
char *get_file(char *url_with_file);
char *get_url(char *url_with_file);
int haveFile(char *text);
void printPortWarningMessage(char *port);
void printPortErrorMessage(char *url, int port);

int isFile(char *url);
int isFTP(char *url);
int isHTML(char *url);
int isHost(char *url);
int locateChar(char *haystack, char needle);
int locateCharR(char *haystack, char needle);
char *convertClasicToFtp(char *url);
int isWithUsername(char *url);
int isValidPort(char *port);
int isPortInUrl(char *url);
void removeAnonymousUser(char *url);


#define BUFSIZE 4096
#define FTP_PORT 21

#define INSTALLED_COMPAT_MODE_MODULE 1
#define SSL_NOT_ALLOW 1

#define ANONYMOUS_PREFIX "anonymous@"

#ifdef INSTALLED_COMPAT_MODE_MODULE
# define LFTP_OPEN "lftp-open"
# define LFTP_COMPAT_MODE_OPEN "open"
#else				/* INSTALLED_COMPAT_MODE_MODULE */
# define LFTP_OPEN "open"
# define LFTP_COMPAT_MODE_OPEN "open"
#endif				/* INSTALLED_COMPAT_MODE_MODULE */

static char *binname = NULL;
static int compat_mode_warning = 1;
static char e_script[BUFSIZE + 1];
static char *HTML_URL = "http://";
static char *FTP_URL = "ftp://";
static char *FILE_URL = "file://";
static unsigned int MAX_PORT = 65535;
static char *std_out = "/dev/stdout";


int main(int argc, char *argv[])
{
    int ch;
    int notimplemented;
    int anonymous = 0; /* 0 not force anonymous login, 1 force anonymous login*/
    int debug = 0;
    int verbose = 0;		/* -1=off 0 = NA 1 =on */
    int force_cache_reload = 0;
    int autologin = 1;
    char *ftp_port = NULL;
    int ftp_port_i=FTP_PORT;
    int retry_wait = -1;
    char *end;
    int restart_auto_fetching = 0;	/* 0 no, 1-yes */
    char *upload_path = NULL;
    char *new_argv0 = "lftp";
    char **new_argv;
    int iarg = 0;
    char buf[BUFSIZE + 1];
    int rate_get = 0, rate_put = 0;
    int rate_get_incr = -1, rate_put_incr = -1;
    char *anonymous_pass = NULL;
    char *env;
    int passive_mode = -1;
    int active_fallback = -1;
    int interactive = 0;

    /*
     * if 0 using mput/mget, if 1 * using * put/get to supress * local
     * file * completiotion 
     */
    int globing = 0;
    int showEXE = 0;

    /*
     * if 0 no redirection, if 1 redirection to file or to /dev/stdout 
     */
    int redirect_output = 0;
    char *output_file = NULL;

    /*
     * both in lftp and lukemftp is default mode passive, 0 means acitve 
     */
    passive_mode = 1;

    /*
     * terminal 
     */
    int is_tty = 0;
    int i;


    e_script[0] = '\0';

    if (!(new_argv = (char **) malloc(sizeof(char *) * (argc + 100))))
	return -1;

    binname = strrchr(argv[0], '/');
    if (binname == NULL)
	binname = argv[0];
    else
	binname++;

    if (strcmp(binname, "pftp") == 0) {
	passive_mode = 1;
	active_fallback = 0;
    } else if (strcmp(binname, "gate-ftp") == 0)
	not_implemented_ignoring("gate_mode");

    /*
     * environment compatibility 
     */
    anonymous_pass = getenv("FTPANONPASS");
    /*
     * verbosity
     */
    is_tty = isatty(1);
    if (is_tty == 0)
	verbose = -1;

    if ((env = getenv("FTPMODE")) != NULL) {
	if (strcasecmp(env, "passive") == 0) {
	    passive_mode = 1;
	    active_fallback = 0;
	} else if (strcasecmp(env, "active") == 0) {
	    passive_mode = 0;
	    active_fallback = 0;
	} else if (strcasecmp(env, "gate") == 0) {
	    not_implemented_ignoring
		("enviroment variable FTPSERVERPORT=gate");
	} else if (strcasecmp(env, "auto") == 0) {
	    not_implemented_ignoring
		("enviroment variable FTPSERVERPORT=auto");
	} else
	    warnx("unknown $FTPMODE '%s'; using defaults", env);
    }
    while ((ch = getopt(argc, argv, "Aadefhgino:pP:r:RtT:u:vVsx")) != -1) {
	notimplemented = 0;
	switch (ch) {
	case 'A':
	    /*
	     * Force active mode ftp. 
	     */
	    passive_mode = 0;
	    break;
	case 'g':
	    /*
	     * Disables file name globbing. 
	     */
	    globing = 1;
	    break;
	case 'i':
	    /*
	     * Turns off interactive prompting during multiple file
	     * transfers. 
	     */
	case 't':
	    /*
	     * Enables packet tracing 
	     */
	    break;
	case 'v':
	    verbose = 1;	/* on */
	    break;
	case 'V':
	    /*
	     * Disable verbose and progress, overriding the default of
	     * enabled when output is to a terminal. 
	     */
	    verbose = -1;	/* off */
	    break;
	case 'a':
	    anonymous = 1;
	    break;
	case 'd':
	    /*
	     * Enables debugging. 
	     */
	    debug++;
	    break;
	case 'f':
	    /*
	     * Forces a cache reload for transfers that go through the FTP 
	     * or HTTP proxies. 
	     */
	    force_cache_reload = 1;
	    break;
	case 'h':
	    help();
	    exit(1);

	case 'n':
	    /*
	     * Restrains auto-login upon initial connection 
	     */
	    autologin = 0;
	    break;
	case 'o':
	    /*
	     * When auto-fetching files, save the contents in output 
	     */
	    redirect_output = 1;
	    output_file = optarg;
	    if (strcmp(output_file, "-") == 0)
		output_file = std_out;

	    break;
	case 'p':
	    /*
	     * Enable passive mode operation 
	     */
	    /*
	     * deprecated by lukemftp, passive mode is used by default 
	     */
	    passive_mode = 1;
	    break;
	case 'P':
	    /*
	     * Sets the port number to port 
	     */
	    ftp_port = optarg;
	    int tmp_port = isValidPort(ftp_port);
	    if(tmp_port)ftp_port_i=tmp_port;
	    break;
	case 'r':
	    retry_wait = strtol(optarg, &end, 10);

	    if (retry_wait < 1 || *end != '\0')
		errx(1, "bad '-r' retry value: %s", optarg);
	    break;
	case 'R':
	    /*
	     * Restart all non-proxied auto-fetches. 
	     */
	    restart_auto_fetching = 1;
	    break;
	case 'T':
	    /*
	     * Set the maximum transfer rate for direction to maximum
	     * bytes/second 
	     */
	    {
		int targc = 0;
		char *targv[5], *oac, *c;
		int dir = 0, max = 0, incr = -1;

		/*
		 * look for `dir,max[,incr]' 
		 */
		if (!(oac = strdup(optarg)))
		    return -1;

		while ((c = strsep(&oac, ",")) != NULL) {
		    if (*c == '\0') {
			warnx("bad throttle value: %s", optarg);
			help();
			/*
			 * NOTREACHED 
			 */
		    }
		    targv[targc++] = c;
		    if (targc >= 4)
			break;
		}

		if (targc > 3 || targc < 2)
		    help();

#define	RATE_GET	1
#define	RATE_PUT	2
#define	RATE_ALL	(RATE_GET | RATE_PUT)

		if (strcasecmp(targv[0], "all") == 0)
		    dir = RATE_ALL;
		else if (strcasecmp(targv[0], "get") == 0)
		    dir = RATE_GET;
		else if (strcasecmp(targv[0], "put") == 0)
		    dir = RATE_PUT;
		else
		    help();

		if (targc >= 2)
		    if ((max = strsuftoi(targv[1])) < 0)
			help();

		if (targc == 3)
		    if ((incr = strsuftoi(targv[1])) < 0)
			help();


		if (dir & RATE_GET) {
		    rate_get = max;
		    rate_get_incr = incr;
		}

		if (dir & RATE_PUT) {
		    rate_put = max;
		    rate_put_incr = incr;
		}

		free(oac);
	    }
	    break;
	case 'u':
	    /*
	     * Upload files on the command line to url where url is one of 
	     * the ftp URL types as supported by auto-fetch 
	     */
	    upload_path = strdup(optarg);
	    break;
	case 's':
	    /*
	     * don't display warning message about compatibility mode 
	     */
	    compat_mode_warning = 0;
	    break;
	case 'x':
	    showEXE = 1;	/* show exec string */
	    break;
	default:
	    printf("unknown character %c \n", ch);
	    continue;
	}
    }
    argc -= optind;
    argv += optind;

    if (compat_mode_warning && (verbose != -1))
	fprintf(stderr,
		"Wrapper for lftp to simulate compatibility with lukemftp\n");

    if (getenv("FTPPROMPT"))
	not_implemented_ignoring("enviroment variable FTPPROMPT");

    if (getenv("FTPRPROMPT"))
	not_implemented_ignoring("enviroment variable FTPRPROMPT");

    if (getenv("FTPSERVER"))
	not_implemented_ignoring("enviroment variable FTPSERVER");

    if (getenv("FTPSERVERPORT"))
	not_implemented_ignoring("enviroment variable FTPSERVERPORT");

    new_argv[iarg++] = strdup(new_argv0);

    if (ftp_port) {
	new_argv[iarg++] = "-p";
	new_argv[iarg++] = strdup(ftp_port);
    }
    switch (verbose) {
    case 1:
	debug += 3;
	break;
    case 0:
	debug = 3;
	break;
    case -1:
    default:
	debug = 0;
    }

    if (debug)
	snprintf(buf, BUFSIZE, "debug %d", debug);
    else
	snprintf(buf, BUFSIZE, "debug off");

    buf[BUFSIZE] = '\0';
    add_to_e_script(buf);

    /*
     * lukemftp does not support ssl, neither wraper will 
     */
    add_to_e_script("set ftp:ssl-allow false");
    
    add_to_e_script("set  net:max-retries 1");
    
    if (anonymous_pass) {
	snprintf(buf, BUFSIZE, "set ftp:anon-pass %s", anonymous_pass);
	buf[BUFSIZE - 1] = '\0';
	add_to_e_script(buf);
    }

    if (passive_mode != -1) {
	snprintf(buf, BUFSIZE, "set ftp:passive-mode %s",
		 passive_mode ? "on" : "off");
	buf[BUFSIZE] = '\0';
	add_to_e_script(buf);
    }

    if (retry_wait != -1) {
	snprintf(buf, BUFSIZE,
		 "set net:reconnect-interval-base %d;"
		 "set net:reconnect-interval-multiplier 1;"
		 "set net:reconnect-interval-max %d", retry_wait,
		 retry_wait);
	buf[BUFSIZE] = '\0';
	add_to_e_script(buf);
    }

    if (rate_put || rate_get) {

	if (rate_get_incr != -1 || rate_put_incr != -1)
	    not_implemented_ignoring("incremention in -T");

	snprintf(buf, BUFSIZE,
		 "set net:limit-total-rate %d:%d", rate_get, rate_put);

	buf[BUFSIZE] = '\0';
	add_to_e_script(buf);
    }
    if (!passive_mode) {
	snprintf(buf, BUFSIZE, "set ftp:passive-mode=true");
    }
    if (upload_path) {
	snprintf(buf, BUFSIZE, LFTP_OPEN " %s", upload_path);
	buf[BUFSIZE] = '\0';
	add_to_e_script(buf);
	interactive = 1;
	if (argc > 1) {
	    if (globing) {
		int i;
		for (i = 0; i < argc; i++) {
		    snprintf(buf, BUFSIZE, "put %s", argv[i]);
		    add_to_e_script(buf);
		}
		buf[0] = '\0';	/* not to add it again */
	    } else {
		char *buf2 = collect_strings(argc, argv);
		snprintf(buf, BUFSIZE, "mput %s", buf2);
		free(buf2);
	    }
	} else {
	    if (argc == 1)
		snprintf(buf, BUFSIZE, "%s %s", globing ? "put" : "mput",
			 argv[0]);
	    else
		help();		/* nothing to upload */
	}

	buf[BUFSIZE] = '\0';
	add_to_e_script(buf);
    } else if (argc > 0) {	/* another arguments left */
	char get[100];
	char open[100];

	/*
	 * get command 
	 */
	if (redirect_output) {
	    snprintf(get, 100,
		     (restart_auto_fetching ? "get1 -c -o %s" :
		      "get1 -o %s"), output_file);
	} else {
	    snprintf(get, 100, globing ?
		     (restart_auto_fetching ? "get -c" : "get") :
		     (restart_auto_fetching ? "mget -c" : "mget"));
	}

	for (i = 0; i < argc; i++) {
	    char *buf2 = argv[i];
	    char *buf_file = NULL;
	    char *buf_url = NULL;
	    int is = haveFile(buf2);

/**	    if (ftp_port == NULL)
		snprintf(open, 100,
			 (anonymous
			  || isWithUsername(buf2) ? LFTP_OPEN :
			  LFTP_COMPAT_MODE_OPEN));
	    else
		snprintf(open, 100, "%s -p %s",
			 (anonymous
			  || isWithUsername(buf2) ? LFTP_OPEN :
			  LFTP_COMPAT_MODE_OPEN), ftp_port);**/

	    int ishtml,isftp,isfile,ishost,isuser; //flags for url
	    int tmp_port;
	    ishtml=isHTML(buf2);
	    isfile=isFile(buf2);
	    ishost=isHost(buf2);
	    isftp=isFTP(buf2);
	    isuser=isWithUsername(buf2);
	    if (is) {
		 snprintf(open, 100,isuser ? LFTP_OPEN :
			  LFTP_COMPAT_MODE_OPEN);
		/* url to download file with */
		if (isfile) {	
		    /* file download is simple, only mget is needed */
		    snprintf(buf, BUFSIZE, "%s %s", get, buf2);
		    add_to_e_script(buf);
		    interactive = 1;
		    continue;
		}
		if( ishtml){ /*similar behaving like ftp but diferent port */
			if(ftp_port_i==FTP_PORT)/*easy case, no port hacking */
				snprintf(buf, BUFSIZE, "%s %s", get, buf2);
			else{   /* complicated one */
				tmp_port=isPortInUrl(buf2);
				if(tmp_port==-1) {
					printPortErrorMessage(buf2,-1); /* will exit */
					continue;
				}
				if(tmp_port)ftp_port_i=tmp_port;
				buf_file = get_file(buf2);
				buf_url = get_url(buf2);
				snprintf(buf, BUFSIZE, "%s %s:%d%s",
					 get, buf_url, ftp_port_i,
					 buf_file);
				add_to_e_script(buf);
			}
			snprintf(buf, BUFSIZE, "%s %s", get, buf2);
			add_to_e_script(buf);
			interactive = 1;
			continue;
		}   
		if (ishost) {
			buf2 = convertClasicToFtp(buf2);
			snprintf(open, 100, LFTP_COMPAT_MODE_OPEN);
			isftp=1;
			}
		if (isftp) {	
			/* ftp download is litle bit more complicated */
			buf_file = get_file(buf2);
			buf_url = get_url(buf2);
			tmp_port= isPortInUrl(buf2);/* no need to test error message, it is done in get_url() */
			if(tmp_port)ftp_port_i=tmp_port;
			snprintf(open, 100, LFTP_OPEN); 
			snprintf(buf, BUFSIZE, "%s %s:%d; %s %s",
					 open, buf_url, ftp_port_i, get,
					 buf_file);
			add_to_e_script(buf);
			interactive = 1;
		} else
			printf
			    ("url open/download handling not implemented with %s\n",buf2);
		interactive = 1;
	    } else {		/* no file at the end of url, so just open 
				 * ftp */
		if (isftp){
			anonymous = 1;  
		} else {	
		     /* [[user@]host [port]] */
		    if (i < argc - 1) {	/* some arguments left */
			ftp_port_i = isValidPort(argv[1 + i]);
		    }
		}
		
		/*if (!strncmp
		    (buf2, ANONYMOUS_PREFIX, strlen(ANONYMOUS_PREFIX)))
		    anonymous_offset = strlen(ANONYMOUS_PREFIX);*/
		snprintf(open, 100,(anonymous || isuser) ? LFTP_OPEN :
			  LFTP_COMPAT_MODE_OPEN);     
		buf_file = get_file(buf2); /* directory */ 
		buf_url = get_url(buf2);
		tmp_port= isPortInUrl(buf2);/* no need to test error message, it is done in get_url() */
		if(tmp_port)ftp_port_i=tmp_port;
		snprintf(buf, BUFSIZE, "%s %s:%d%s", open, buf_url, ftp_port_i, (buf_file==NULL ? "":buf_file));
		add_to_e_script(buf);
		interactive = 0;
		argv += argc;	/* delete comands */
		argc = 0;
		break;		/* from FOR loop, because ftp is not able
				 * to download some file and then open
				 * url at the end */
	    }
	    /*
	     * if (!autologin) printf("autologin is not implemented
	     * yet\n"); else { snprintf(buf, BUFSIZE,"open %s", argv[0]);
	     * buf[BUFSIZE] = '\0'; add_to_e_script(buf); } 
	     */
	}
    }

    if (e_script != NULL && strlen(e_script) > 0) {
	new_argv[iarg++] = "-e";
	if (interactive) {
	    add_to_e_script("exit");
	}
	new_argv[iarg++] = e_script;
    }
#ifdef INSTALLED_COMPAT_MODE_MODULE
    add_to_e_scriptB("module compat-mode");
#endif				/* INSTALLED_COMPAT_MODE_MODULE */
    new_argv[iarg++] = NULL;
    if (showEXE) {
	printf("EXE:");
	for (i = 0; new_argv[i]; i++)
	    printf(" %s", new_argv[i]);
	printf("\n");
    }
    execvp(new_argv0, new_argv);
    return 0;
}

/**
  * check if text is in format: (see man ftp) 
  *   ftp://
  *   http://
  *   file:///
  *   host:path
  * @param char * text -expecting string end with '\n' 
  * if yes return 1
  * if no return 0
  **/
int haveFile(char *text)
{
    int end = -1;
    int i = 0, j = 0, k = 0;
    char *tmp = NULL;
    tmp = text;
    /* if is '/' on end, it is directory */ 
    i = locateCharR(tmp, '/');
    if(i==strlen(text)) return 0;
	    
    if (text == NULL)
	return 0;

	/**
	  * Handle this
	  * [http://[user[:password]@]host[:port]/path]
	  **/
    if (isHTML(text)) {
	tmp += strlen(HTML_URL);
	end = strlen(tmp);
	if (strncmp(tmp + end - 1, "/", 1) == 0)
	    return 0;		/* is directory ! */
	i = locateCharR(tmp, '/');
	if (i != end - 1 && i != -1)
	    return 1;		/* is there any character after /? */
	else
	    return 0;
    }

	/**
	  * Handle this
	  * ftp://[user[:password]@]host[:port]/path[/]]
	  **/
    if (isFTP(text)) {
	tmp += strlen(FTP_URL);
	end = strlen(tmp);
	if (strncmp(tmp + end - 1, "/", 1) == 0)
	    return 0;		/* is directory ! */
	/*
	 * is there any character after /? 
	 */
	i = locateCharR(tmp, '/');
	return (i != end - 1 && i != -1);
    }

	/**
	  * Handle this
	  * [file:///path]
	  **/
    if (isFile(text)) {
	tmp += strlen(FILE_URL);
	end = strlen(tmp);
	if (strncmp(tmp + end - 1, "/", 1) == 0)
	    return 0;		/* is directory ! */
	/*
	 * is there any character after /? 
	 */
	i = locateCharR(tmp, '/');
	return (i != end - 1 && i != -1);
    }

	/**
	  * now it could be only host thing
	  * [user@]host:[path][/]
	  * [user@][::1]:[path][/]  //ipv6 adress is in bracket!
	  **/

    end = strlen(tmp);
    if (strncmp(tmp + end - 1, "/", 1) == 0)
	return 0;		/* is directory ! */
    i = locateChar(tmp, '[');
    j = locateChar(tmp, ':');	/* this will find first characters */
    k = locateChar(tmp, '@');
    if (i < k) {		/* there is [ character in name of user */
	i = locateChar(tmp + k, '[');	/* find another one */
    }
    if (i != -1) {		/* ipv6! */
	i = locateChar(tmp + i, ']');	/* find end of adress */
	j = locateChar(tmp + i, ':');	/* find colum after end bracket */
    }

    return j != end - 1 && j != -1 ? 1 : 0;
}

/**
  * clasic format is
  *  [user@]host:[path][/]
  * will change to
  * ftp://[user@]host/[path][/]
  * it will alocate new memory but not free url !
  * return  char * newurl
  **/
char *convertClasicToFtp(char *url)
{
    char *change;
    char *newurl = NULL;
    if (url == NULL)
	return url;
    if ((newurl =
	 malloc((strlen(url) + strlen(FTP_URL)) * sizeof(char))) == NULL) {
	printf("convertclasicToFtp, cannot alocate memory\n");
	exit(1);
    }
    sprintf(newurl, "%s%s", FTP_URL, url);
    change = strstr(newurl + strlen(FTP_URL), ":");
    change[0] = '/';
    return newurl;
}

/**
  * find file (or directory) at the end of url
  * return *char file
  * or null on erorr
  * it can handle those formats 
  * ftp://[user[:password]@]host[:port]/path[/]
  * http://[user[:password]@]host[:port]/path
  **/
char *get_file(char *url_with_file)
{
    char *reti;
    char *tmp;
    int len, file_pos;
    if (url_with_file == NULL)
	return 0;
    tmp = url_with_file;
    if (isFTP(url_with_file))
	tmp += strlen(FTP_URL);
    if (isHTML(url_with_file))
	tmp += strlen(HTML_URL);
    len = strlen(tmp);
    file_pos = locateChar(tmp, '/');
    if (file_pos == -1)		/* no file */
	return NULL;

    if ((reti = malloc((len - file_pos) * sizeof(char))) == NULL) {
	printf
	    ("function get_file, cannot allocate memmory, returning null\n");
	return NULL;
    }
    strncpy(reti, tmp + file_pos, len - file_pos);
    return reti;
}

/**
  * find url int the url with file, url will be striped by ending '/'
  * return *char = url;
  * or null on erorr
  * it can handle those formats 
  * file:///path
  * [user@]host:[path][/]
  * [user@]host
  * ftp://[user[:password]@]host[:port]/path[/]
  * http://[user[:password]@]host[:port]/path
  **/
char *get_url(char *url_with_file)
{
    char *reti;
    char *tmp;
    int len, url_end_pos=-1;
    if (url_with_file == NULL)
	return 0;
    tmp = url_with_file;
    int isftp = 0, ishtml = 0,ishost=0;
    if (isFTP(url_with_file)) {
	tmp += strlen(FTP_URL);
	isftp = 1;
    }
    if (isHTML(url_with_file)) {
	ishtml = 1;
	tmp += strlen(HTML_URL);
    }
    ishost=isHost(url_with_file);
    url_end_pos = locateChar(tmp,':'); 
    if(url_end_pos>locateChar(tmp,'/')) /* localhost/:dir/   case */
	    url_end_pos = locateChar(tmp,'/');
    if(url_end_pos==-1)
	    url_end_pos = locateChar(tmp,'/');
    if (url_end_pos == -1){		
	/* no file,  whole url_with_file is valid url or user@url */
	removeAnonymousUser(url_with_file);
	return url_with_file; 
    }
    else {
	len = strlen(tmp);
	int tmpi = strlen(url_with_file)-len; /* offset */ 
	tmpi += url_end_pos;
	if ((reti =
	     malloc(1+tmpi * sizeof(char))) == NULL) {
	    printf
		("function get_file, cannot allocate memmory, returning null\n");
	    return NULL;
	}
	strncpy(reti, url_with_file,
		tmpi);
	*(reti+tmpi+1)='\0';
	/* anonymous hack - remove user anonymous, who cause troubles */
	removeAnonymousUser(reti);
	/*if (*(reti + len - 1) == '/' || *(reti + len - 1) == ':')
	    *(reti + len - 1) = '\0';*/
	return reti;
    }
}
/**
  * this will check for ANONYMOUS_PREFIX and remove it from url
  **/
void removeAnonymousUser(char *url){
	char *user = strstr(url,ANONYMOUS_PREFIX);
	int len,url_end_pos;
	if(user){
		len = strlen(ANONYMOUS_PREFIX);
		url_end_pos=strlen(user);
		strncpy(user, (user+len),url_end_pos-len);
		*(user+url_end_pos-len)='\0';
	}
	return;
}

/**
  * return 1 if url begin with FTP_URL string
  * return 0 otherwise
  **/
int isFTP(char *url)
{
    return !strncmp(url, FTP_URL, strlen(FTP_URL));
}

/**
  * return true if url begin with HTML_URL string
  * return  otherwise
  **/
int isHTML(char *url)
{
    return !strncmp(url, HTML_URL, strlen(HTML_URL));
}

/**
  * return number of port if char represent valid port
  * return FTP_PORT otherwise (pftp behaving)
  * return 0 if there is some raly ugly port (pftp crash on such port)
  **/
int isValidPort(char *port)
{
    int reti = -1, i;
    char c;
    if (port == NULL || strlen(port) >= 6){
	    printPortWarningMessage(port);
	    return FTP_PORT;
    }
    for (i = 0; i < strlen(port) && reti == -1; i++) {
	c = *(port + i);
	if (isdigit((int) c))
	    continue;
	else
	    reti = 0;
    }
    if (reti != -1) {		/* strange digit found */
	return 0;
    }
    reti = atoi(port);
    if(reti > 0 && reti < MAX_PORT){
	return reti;
    }
    printPortWarningMessage(port);
    return FTP_PORT;
}

/**
  * return 1 if url is in format  [user@]host[:port][path][/]
  * return 0 otherwise
  * it call isFTP is HTML isFile !!
  * @todo - not ipv6 [::1] ready
  **/
int isHost(char *url)
{
    if (url == NULL || isFile(url) || isHTML(url) || isFTP(url))
	return 0;
    int at = locateChar(url, '@');
    if (at == -1)
	at = 0;
    int colon = locateChar(url + at, ':');
    if(colon==-1)
	    colon=locateChar(url + at, '/');
    if(colon==-1)
	    colon=strlen(url);
    return at>colon? 0 : 1;
}
/**
  * return true if user is included
  * */
int isWithUsername(char *url)
{
    int i1, i2, i3, i4;
    if (isFTP(url))
	url += strlen(FTP_URL);

    i1 = locateChar(url, '@');
    i2 = locateChar(url, ':');
    i3 = locateChar(url, '/');
    i4 = locateChar(url, '[');

    return (i1 != -1 &&
	    (i2 == -1 || i1 < i2) &&
	    (i3 == -1 || i1 < i3) && 
	    (i4 == -1 || i1 < i4));
}

/**
  * return 1 if url begin with FILE_URL string
  * return 0 otherwise
  **/
int isFile(char *url)
{
    return !strncmp(url, FILE_URL, strlen(FILE_URL));
}

/**
  * return position of char in string, start from begin of @haystack
  * return -1 if char is not found in string
  **/
int locateChar(char *haystack, char needle)
{
    int reti = -1, size, i;
    if (!haystack || !needle)
	return reti;
    size = strlen(haystack);
    for (i = 0; i < size && reti == -1; i++) {
	if (strncmp(haystack + i, &needle, 1) == 0) {
	    reti = i;
	}
    }
    return reti;
}

/**
  * return position of char in string, start from end of @haystack
  * return -1 if char is not found in string
  **/
int locateCharR(char *haystack, char needle)
{
    int reti = -1, size, i;
    if (!haystack || !needle)
	return reti;
    size = strlen(haystack);
    for (i = size-1; i >= 0 && reti == -1; i--) {
	if (strncmp(haystack + i, &needle, 1) == 0) {
	    reti = i;
	}
    }
    return reti;
}
static void help(void)
{
    fprintf(stderr,
	    /*
	     * "usage: %s [-AadefginpRtvV] [-o outfile] [-P port] [-r
	     * retry]\n" 
	     */
	    "usage: %s [-adefnpR] [-o outfile] [-P port] [-r retry]\n"
	    "           [-T dir,max[,inc][[user@]host [port]]] [host:path[/]]\n"
	    "           [file:///file] [ftp://[user[:pass]@]host[:port]/path[/]]\n"
	    "           [http://[user[:pass]@]host[:port]/path] [...]\n"
	    "       %s -u url file [...]\n", binname, binname);
    exit(1);
}

/**
 * Convert the string `arg' to an int, which may have an optional SI suffix
 * (`b', `k', `m', `g'). Returns the number for success, -1 otherwise.
 */
static int strsuftoi(const char *arg)
{
    char *c;
    long val;

    if (!isdigit((unsigned char) arg[0]))
	return -1;

    val = strtol(arg, &c, 10);
    if (c != NULL) {
	if (c[0] != '\0' && c[1] != '\0')
	    return -1;
	switch (tolower((unsigned char) c[0])) {
	case '\0':
	case 'b':
	    break;
	case 'k':
	    val <<= 10;
	    break;
	case 'm':
	    val <<= 20;
	    break;
	case 'g':
	    val <<= 30;
	    break;
	default:
	    return (-1);
	}
    }
    return val < 0 || val > INT_MAX ? -1 : val;
}

static void not_implemented_ignoring(char *what)
{
    if (compat_mode_warning)
	fprintf(stderr, "Does not implemented: '%s' (ignoring)\n", what);
}

/**
  * this method will add string in char *cmd to END of *e_script, it will also add ';' char to the end
  **/
static void add_to_e_script(char *cmd)
{
    if (!cmd || strlen(cmd) == 0)
	return;

    if (strlen(cmd) >= (BUFSIZE - strlen(e_script) - 2)) {
	warnx("Command line for lftp is too long.");
	exit(1);
    }
    strcat(e_script, cmd);
    strcat(e_script, ";");
}

/**
  * this method will add string in char *cmd to BEGIN of *e_script, it will also add ';' char to the end
  **/
static void add_to_e_scriptB(char *cmd)
{
    char tmp[BUFSIZE];
    char *p = tmp;
    int i;
    for (i = 0; i < BUFSIZE; i++)
	tmp[i] = '\0';
    if (!cmd || strlen(cmd) == 0)
	return;

    if (strlen(cmd) >= (BUFSIZE - strlen(e_script) - 2)) {
	warnx("Command line for lftp is too long.");
	exit(1);
    }
    strcpy(p, e_script);
    strcpy(e_script, cmd);
    strcat(e_script, ";");
    strcat(e_script, p);
}

static char *collect_strings(int argc, char **argv)
{
    char *buf;
    int i, len = 1;
    len = argc;			/* for space */

    for (i = 0; i < argc; i++)
	len += strlen(argv[i]);

    if (!(buf = malloc(len))) {
	warnx("Command line for lftp is too long.");
	exit(1);
    }
    buf[len - 1] = '\0';

    for (i = 0; i < argc; i++) {
	strcat(buf, " ");
	strcat(buf, argv[i]);
    }
    return buf;
}

/*
 * Returns TRUE if address is a IPv6 is given. 
 */
int is_ipv6_address(const char *addr)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_INET6;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;
    if (getaddrinfo(addr, "0", &hints, &res) != 0)
	return 0;

    freeaddrinfo(res);
    return 1;
}
/**
  * url - url which could contain user, password, ftp/http/local url
  * Return:
  * number of port in url if there is some port in url
  * 0  if there is no port in url
  * -1 if url is in format when port should be present and no valid port number 
  * 	is found 
  **/
int isPortInUrl(char *url){
	char *port,*tmp;
	int position,i;
	tmp=url;
	if (isFTP(url))
		tmp += strlen(FTP_URL);
	if (isHTML(url))
		tmp += strlen(HTML_URL);
	i=locateChar(tmp,'@');
	if(i!=-1)
		i=0;
	position=locateChar(tmp+i,':');
	i=locateChar(tmp+i,'/');
	if(position==-1 || position>i )
		return 0; /* no port */
	port = tmp+position+1;
	position = isValidPort(port);
	if(position==0)
		printPortErrorMessage(url,position);
	return position;
}
/**
  * this method will print standard warning error about wrong port and url
  * this method will exit wrapper !!
  **/
void printPortErrorMessage(char *url, int port){
	fprintf(stderr,"Unknown port `%d' in URL %s\n", port, url);
	fprintf(stderr,"Invalid URL `%s'\n", url);
	exit(1);
}
/**
  * this method will print standard warning message about wrong port
  **/
void printPortWarningMessage(char *port){
	fprintf(stderr,"Unknown port `%s', using port %d\n",
				    port, FTP_PORT);
}
