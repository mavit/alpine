#if !defined(lint) && !defined(DOS)
static char rcsid[] = "$Id: rpload.c 1074 2008-06-04 00:08:43Z hubert@u.washington.edu $";
#endif

/*
 * ========================================================================
 * Copyright 2006-2008 University of Washington
 * Copyright 2013 Eduardo Chappa
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * ========================================================================
 */

#include "headers.h"
#include "radio.h"		/* OE_PASSWD */
#include "../pith/util.h"	/* IS_REMOTE() */
#include "../pith/remote.h"	/* REMOTE_ABOOK_SUBTYPE... */


typedef enum {Pinerc, Abook, Sig, Smime, NotSet} RemoteType;


int        parse_args(int, char **, int *, int *, char **, char **, RemoteType *);
RemoteType check_for_header_msg(MAILSTREAM *);
char      *ptype(RemoteType);
char      *spechdr(RemoteType);
int        add_initial_msg(MAILSTREAM *, char *, char *);
int        append_data(MAILSTREAM *, char *, char *, FILE *);
void       trim_data(MAILSTREAM *, int);
void       write_fake_headers(RFC822BUFFER *, char *, char *, char *);
char      *err_desc(int);
int        opt_enter(char *, int, char *, int *);


int   noshow_error = 0;
char *ustr = "usage: %s [-s trimSize] [-f] -t Type -l Local_file  -r Remote_folder\n";


/* look for my_timer_period in pico directory for an explanation */
int my_timer_period = ((IDLE_TIMEOUT + 1)*1000);


#ifdef _WINDOWS

#undef main

app_main (argc, argv)
    int argc;
    char argv[];
{
}

#endif /* _WINDOWS */


/*
 *  rpload [-s trimSize] [-f] -t Type -l Local_file -r Remote_folder
 *
 *  Type is one of abook
 *                 pinerc
 *                 smime
 *                 sig         (this is mostly obsolete, literal sigs
 *                              should be used instead)
 *                 -f means force the folder to be written even if it
 *                    doesn't look like it is of the right format
 *
 *   Note: We're not worrying about memory leaks.
 */
int
main(argc, argv)
    int   argc;
    char *argv[];
{
    MAILSTREAM *stream = NULL;
    FILE       *fp;
    int         delete_existing = 0, usage = 0;
    int         force = 0, trimsize = 0;
    char       *local = NULL, *remote = NULL, *special_hdr = NULL;
    RemoteType  rt, type = NotSet;

#include "../c-client/linkage.c"

    if(parse_args(argc, argv, &force, &trimsize, &local, &remote, &type)){
	fprintf(stderr, ustr, argv[0]);
	exit(-1);
    }

    if(!local || !*local){
	fprintf(stderr, "No local file specified\n");
	usage++;
    }

    if(!remote || !*remote){
	fprintf(stderr, "No remote folder specified\n");
	usage++;
    }

    if(type == NotSet){
	fprintf(stderr, "Type must be set to one of:\n");
	for(rt = Pinerc; rt != NotSet; rt++)
	  fprintf(stderr, "  %s\n", ptype(rt));

	usage++;
    }

    if(usage){
	fprintf(stderr, ustr, argv[0]);
	exit(-1);
    }

    if(!IS_REMOTE(remote)){
	fprintf(stderr,
		"Remote folder name \"%s\" %s\n", remote,
		(*remote != '{') ? "must begin with \"{\"" : "not valid");
	exit(-1);
    }

    if(IS_REMOTE(local)){
	fprintf(stderr, "Argument to -l (%s) must be a local filename", local);
	exit(-1);
    }

    if(access(local, ACCESS_EXISTS) != 0){
	fprintf(stderr, "Local file \"%s\" does not exist\n", local);
	exit(-1);
    }

    if(access(local, READ_ACCESS) != 0){
	fprintf(stderr,
		"Can't read local file \"%s\": %s\n",
		local, err_desc(errno));
	exit(-1);
    }

    /*
     * Try opening the local file.
     */
    if((fp = fopen(local, "r")) == NULL){
	fprintf(stderr, "Can't open \"%s\": %s\n", local, err_desc(errno));
	exit(-1);
    }

    /*
     * Try opening the remote folder. If it doesn't exist, create it.
     */

    /* failure would be normal here, so don't show it */
    noshow_error = 1;
    stream = mail_open(NULL, remote, 0L);
    if(!stream || stream->halfopen){
	if(stream && stream->halfopen){
	    noshow_error = 0;
	    if(!mail_create(stream, remote))
	      exit(-1);

	    stream = mail_open(stream, remote, 0L);
	    if(!stream || stream->halfopen)
	      exit(-1);
	}
	else{
	    fprintf(stderr, "Trouble opening remote folder \"%s\"\n", remote);
	    exit(-1);
	}
    }

    noshow_error = 0;

    if(stream->rdonly){
	fprintf(stderr, "Remote folder \"%s\" is not writable\n", remote);
	exit(-1);
    }

    if(stream->nmsgs > 0){
	/*
	 * There is a first message already. Check to see if it is one of
	 * our special header messages.
	 */
	rt = check_for_header_msg(stream);
	if(rt == NotSet){
	    if(force)
	      delete_existing++;
	    else{
		fprintf(stderr, "Folder \"%s\"\ndoes not appear to be an Alpine remote \"%s\" folder.\nUse -f to force.\n", remote, ptype(type));
		fprintf(stderr, "-f will cause %ld messages to be deleted\n",
			stream->nmsgs);
		exit(-1);
	    }
	}
	else if(rt != type){
	    if(force)
	      delete_existing++;
	    else{
		fprintf(stderr, "Folder \"%s\" is type \"%s\"\nUse -f to force switch.\n", remote, ptype(rt));
		fprintf(stderr, "-f will cause %ld messages to be deleted\n",
			stream->nmsgs);
		exit(-1);
	    }
	}
    }

    if(delete_existing){
	char sequence[20];

	mail_ping(stream);
	snprintf(sequence, sizeof(sequence), "1:%ld", stream->nmsgs);
	mail_flag(stream, sequence, "\\DELETED", ST_SET);
	mail_expunge(stream);
	mail_ping(stream);
    }

    special_hdr = spechdr(type);

    /*
     * Add the explanatory header message if needed.
     */
    if(stream->nmsgs == 0){
	if(add_initial_msg(stream, remote, special_hdr) != 0){
	    mail_close(stream);
	    exit(-1);
	}
    }

    /*
     * Add the actual data in a message.
     */
    if(append_data(stream, remote, special_hdr, fp) != 0){
	mail_close(stream);
	exit(-1);
    }

    /*
     * Trim the size of the remote folder.
     */
    if(trimsize)
      trim_data(stream, trimsize);

    mail_close(stream);
    exit(0);
}


RemoteType
check_for_header_msg(stream)
    MAILSTREAM *stream;
{
    STRINGLIST *sl;
    int         ret = NotSet;
    char       *h, *try;
    size_t      len;
    char       *pinerc, *abook, *sig, *smime;

    pinerc = spechdr(Pinerc);
    abook  = spechdr(Abook);
    sig    = spechdr(Sig);
    smime  = spechdr(Smime);
    
    len = MAX(MAX(strlen(pinerc), strlen(abook)), MAX(strlen(sig), strlen(smime)));

    sl = mail_newstringlist();
    sl->text.data = (unsigned char *)fs_get((len+1) * sizeof(unsigned char));
    try = pinerc;
    strncpy((char *)sl->text.data, try, len);
    sl->text.data[len] = '\0';
    sl->text.size = strlen((char *) sl->text.data);

    if(stream && (h=mail_fetch_header(stream,1L,NULL,sl,NULL,FT_PEEK))){

	if(strlen(h) >= sl->text.size && !struncmp(h, try, sl->text.size))
	  ret = Pinerc;
    }

    if(ret == NotSet){
	try = abook;
	strncpy((char *)sl->text.data, try, len);
	sl->text.data[len] = '\0';
	sl->text.size = strlen((char *) sl->text.data);
	if(stream && (h=mail_fetch_header(stream,1L,NULL,sl,NULL,FT_PEEK))){

	    if(strlen(h) >= sl->text.size && !struncmp(h, try, sl->text.size))
	      ret = Abook;
	}
    }

    if(ret == NotSet){
	try = sig;
	strncpy((char *)sl->text.data, try, len);
	sl->text.data[len] = '\0';
	sl->text.size = strlen((char *) sl->text.data);
	if(stream && (h=mail_fetch_header(stream,1L,NULL,sl,NULL,FT_PEEK))){

	    if(strlen(h) >= sl->text.size && !struncmp(h, try, sl->text.size))
	      ret = Sig;
	}
    }

    if(sl)
      mail_free_stringlist(&sl);

    if(pinerc)
      fs_give((void **)&pinerc);
    if(abook)
      fs_give((void **)&abook);
    if(sig)
      fs_give((void **)&sig);
    if(smime)
      fs_give((void **)&smime);

    return(ret);
}


char *
ptype(rtype)
    RemoteType rtype;
{
    char *ret = NULL;

    switch(rtype){
      case Pinerc:
        ret = cpystr("pinerc");
	break;
      case Abook:
        ret = cpystr("abook");
	break;
      case Sig:
        ret = cpystr("sig");
	break;
      case Smime:
        ret = cpystr("smime");
	break;
      default:
	break;
    }

    return(ret);
}


char *
spechdr(rtype)
    RemoteType rtype;
{
    char *ret = NULL;

    switch(rtype){
      case Pinerc:
        ret = cpystr(REMOTE_PINERC_SUBTYPE);
	break;
      case Abook:
        ret = cpystr(REMOTE_ABOOK_SUBTYPE);
	break;
      case Sig:
        ret = cpystr(REMOTE_SIG_SUBTYPE);
	break;
      case Smime:
        ret = cpystr(REMOTE_SMIME_SUBTYPE);
	break;
      default:
	break;
    }

    return(ret);
}


int
parse_args(argc, argv, force, trimsize, local, remote, type)
    int          argc;
    char       **argv;
    int         *force, *trimsize;
    char       **local, **remote;
    RemoteType  *type;
{
    int    ac;
    char **av;
    int    c;
    char  *str;
    int    usage = 0;
    RemoteType  rt;

    ac = argc;
    av = argv;

      /* while more arguments with leading - */
Loop: while(--ac > 0 && **++av == '-'){
	/* while more chars in this argument */
	while(*++*av){
	    switch(c = **av){
	      case 'h':
		usage++;
		break;
	      case 'f':
		(*force)++;
		break;

	      case 't': case 'l':	/* string args */
	      case 'r':
	      case 's':			/* integer args */
		if(*++*av)
		  str = *av;
		else if(--ac)
		  str = *++av;
		else{
		    fprintf(stderr, "missing argument for flag \"%c\"\n", c);
		    ++usage;
		    goto Loop;
		}

		switch(c){
		  case 'l':
		    if(str)
		      *local = str;

		    break;
		  case 'r':
		    if(str)
		      *remote = str;

		    break;
		  case 't':
		    for(rt = Pinerc; rt != NotSet; rt++){
			if(!strucmp(str, ptype(rt)))
			  break;
		    }

		    *type = rt;
		    break;
		  case 's':
		    if(!isdigit((unsigned char)str[0])){
			fprintf(stderr,
				"non-numeric argument for flag \"%c\"\n", c);
			++usage;
			break;
		    }

		    *trimsize = atoi(str);
		    if(*trimsize < 1){
			fprintf(stderr, "trimsize of %d is too small, have to leave at least one copy\n", *trimsize);
			++usage;
		    }
		    else if(*trimsize > 100){
			fprintf(stderr,
			"trimsize of %d is too large, 5 or 10 is sufficient\n",
				*trimsize);
			++usage;
		    }

		    break;
		}

		goto Loop;

		default:
		  fprintf(stderr, "unknown flag \"%c\"\n", c);
		  ++usage;
		  break;
	    }
	 }
      }

    if(ac != 0)
      usage++;

    return(usage);
}


long
dummy_soutr(stream, string)
    void *stream;
    char *string;
{
    return LONGT;
}


/*
 * Add an explanatory first message advising user that this is a
 * special sort of folder.
 */
int
add_initial_msg(stream, mailbox, special_hdr)
    MAILSTREAM *stream;
    char       *mailbox;
    char       *special_hdr;
{
    STRING        msg;
    char          buf[20000];
    RFC822BUFFER  rbuf;

    rbuf.f   = dummy_soutr;
    rbuf.s   = NULL;
    rbuf.beg = buf;
    rbuf.cur = buf;
    rbuf.end = buf+sizeof(buf)-1;
    write_fake_headers(&rbuf, "Header Message for Remote Data", "plain", special_hdr);
    *rbuf.cur = '\0';

    buf[sizeof(buf)-1] = '\0';

    if(!strucmp(special_hdr, REMOTE_ABOOK_SUBTYPE)){
	strncat(buf, "This folder contains a single Alpine addressbook.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "This message is just an explanatory message.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The last message in the folder is the live addressbook data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The rest of the messages contain previous revisions of the addressbook data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "To restore a previous revision just delete and expunge all of the messages\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "which come after it.\015\012", sizeof(buf)-strlen(buf)-1);
    }
    else if(!strucmp(special_hdr, REMOTE_PINERC_SUBTYPE)){
	strncat(buf, "This folder contains an Alpine config file.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "This message is just an explanatory message.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The last message in the folder is the live config data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The rest of the messages contain previous revisions of the data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "To restore a previous revision just delete and expunge all of the messages\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "which come after it.\015\012", sizeof(buf)-strlen(buf)-1);
    }
    else if(!strucmp(special_hdr, REMOTE_SMIME_SUBTYPE)){
	strncat(buf, "This folder contains Alpine S/MIME config information.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "This message is just an explanatory message.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The last message in the folder is the live data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The rest of the messages contain previous revisions of the data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "To restore a previous revision just delete and expunge all of the messages\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "which come after it.\015\012", sizeof(buf)-strlen(buf)-1);
    }
    else{
	strncat(buf, "This folder contains remote Alpine data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "This message is just an explanatory message.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The last message in the folder is the live data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "The rest of the messages contain previous revisions of the data.\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "To restore a previous revision just delete and expunge all of the messages\015\012", sizeof(buf)-strlen(buf)-1);
	strncat(buf, "which come after it.\015\012", sizeof(buf)-strlen(buf)-1);
    }

    INIT(&msg, mail_string, (void *)buf, strlen(buf));
    if(!mail_append(stream, mailbox, &msg))
      return(-1);
    
    return(0);
}


/*
 * Add a message to the folder with the contents of the local data
 * in it.
 */
int
append_data(stream, mailbox, special_hdr, fp)
    MAILSTREAM *stream;
    char       *mailbox;
    char       *special_hdr;
    FILE       *fp;
{
    STRING        msg;
    char          buf[20000], *sto, *p;
    struct stat   sbuf;
    long          filelen, len;
    int           c, nextc;
    RFC822BUFFER  rbuf;

    if(fstat(fileno(fp), &sbuf) != 0){
	fprintf(stderr, "fstat of local file failed\n");
	return(-1);
    }

    filelen = (long) sbuf.st_size;

    rbuf.f   = dummy_soutr;
    rbuf.s   = NULL;
    rbuf.beg = buf;
    rbuf.cur = buf;
    rbuf.end = buf+sizeof(buf)-1;
    write_fake_headers(&rbuf, "Pine Remote Data Container", special_hdr,
		       special_hdr);
    *rbuf.cur = '\0';

    buf[sizeof(buf)-1] = '\0';

    /* very conservative estimate of space needed */
    len = filelen + filelen + strlen(buf) + 10;
    sto = fs_get((len+1) * sizeof(char));

    strncpy(sto, buf, len);
    sto[len] = '\0';
    p = sto + strlen(sto);
    /* Write the contents */
    while((c = getc(fp)) != EOF){
	/*
	 * c-client expects CRLF-terminated lines. These lines
	 * can be either CRLF- or LF-terminated. We have to convert them
	 * when we copy into c-client.
	 */
	if(c == '\r' || c == '\n'){
	    if(c == '\r' && ((nextc = getc(fp)) != '\n') && nextc != EOF)
	      ungetc(nextc, fp);

	    /* write the CRFL */
	    if(p - sto < len)
	      *p++ = '\r';

	    if(p - sto < len)
	      *p++ = '\n';
	}
	else if(p - sto < len)
	  *p++ = c;
    }

    fclose(fp);
    if(p - sto < len)
      *p = '\0';

    sto[len] = '\0';

    INIT(&msg, mail_string, (void *)sto, strlen(sto));
    if(!mail_append(stream, mailbox, &msg)){
	fprintf(stderr, "Copy failed\n");
	return(-1);
    }

    fs_give((void **)&sto);

    return(0);
}


/*
 * Trim the number of saved copies of the remote data history in case
 * this is the only way this folder is ever updated. We leave
 * the first message there because it is supposed to be an explanatory
 * message, but we don't actually check to see whether or not it is
 * such a message or not.
 */
void
trim_data(stream, trimsize)
    MAILSTREAM *stream;
    int         trimsize;
{
    if(stream->nmsgs > trimsize + 1){
	char sequence[20];

	mail_ping(stream);
	snprintf(sequence, sizeof(sequence), "2:%ld", stream->nmsgs - trimsize);
	mail_flag(stream, sequence, "\\DELETED", ST_SET);
	mail_expunge(stream);
    }
}


void
write_fake_headers(where, subject, subtype, special_hdr)
    RFC822BUFFER *where;
    char *subject;
    char *subtype;
    char *special_hdr;
{
    ENVELOPE *fake_env;
    BODY     *fake_body;
    ADDRESS  *fake_from;
    char      date[200], vers[10];

    fake_env = (ENVELOPE *)fs_get(sizeof(ENVELOPE));
    memset(fake_env, 0, sizeof(ENVELOPE));
    fake_body = (BODY *)fs_get(sizeof(BODY));
    memset(fake_body, 0, sizeof(BODY));
    fake_from = (ADDRESS *)fs_get(sizeof(ADDRESS));
    memset(fake_from, 0, sizeof(ADDRESS));
    rfc822_date(date);

    fake_env->subject = cpystr(subject);
    fake_env->date = (unsigned char *) cpystr(date);
    fake_from->personal = cpystr("Pine Remote Data");
    fake_from->mailbox = cpystr("nobody");
    fake_from->host = cpystr("nowhere");
    fake_env->from = fake_from;
    fake_body->type = REMOTE_DATA_TYPE;
    fake_body->subtype = cpystr(subtype);

    snprintf(vers, sizeof(vers), "%d", REMOTE_DATA_VERS_NUM);

    /* re-use subtype for special header name, too */
    rfc822_output_header_line(where, special_hdr, 0L, vers);
    rfc822_output_header(where, fake_env, fake_body, NULL, 0L);
    mail_free_envelope(&fake_env);
    mail_free_body(&fake_body);
}


char *
err_desc(err)
    int err;
{
    return((char *) strerror(err));
}


void mm_exists(stream, number)
    MAILSTREAM *stream;
    unsigned long number;
{
}


void mm_expunged(stream, number)
    MAILSTREAM *stream;
    unsigned long number;
{
}


void mm_flags(stream, number)
    MAILSTREAM *stream;
    unsigned long number;
{
}


void mm_list(stream, delim, name, attrib)
    MAILSTREAM *stream;
    int   delim;
    char *name;
    long attrib;
{
}


void mm_lsub(stream, delimiter, name, attributes)
    MAILSTREAM *stream;
    int delimiter;
    char *name;
    long attributes;
{
}


void mm_notify(stream, string, errflg)
    MAILSTREAM *stream;
    char *string;
    long errflg;
{
    mm_log(string, errflg);
}


void mm_log(string, errflg)
    char *string;
    long errflg;
{
    if(noshow_error)
      return;

    switch(errflg){  
      case BYE:
      case NIL:
	break;

      case PARSE:
	fprintf(stderr, "PARSE: %s\n", string);
	break;

      case WARN:
	fprintf(stderr, "WARN: %s\n", string);
	break;

      case ERROR:
	fprintf(stderr, "ERROR: %s\n", string);
	break;

      default:
	fprintf(stderr, "%s\n", string);
	break;
    }
}


void mm_login(mb, user, pwd, trial)
    NETMBX *mb;
    char   *user;
    char   *pwd;
    long    trial;
{
    char prompt[100], *last;
    int  i, j, goal, ugoal, len, rc, flags = 0;
#define NETMAXPASSWD 100

    user[NETMAXUSER-1] = '\0';

    if(trial == 0L){
	if(mb->user && *mb->user){
	    strncpy(user, mb->user, NETMAXUSER);
	    user[NETMAXUSER-1] = '\0';
	}
    }

    if(!*mb->user){
	/* Dress up long hostnames */
	snprintf(prompt, sizeof(prompt), "%sHOST: ",
		(mb->sslflag||mb->tlsflag) ? "+ " : "");
	len = strlen(prompt);
	/* leave space for "HOST", "ENTER NAME", and 15 chars for input name */
	goal = 80 - (len + 20 + MIN(15, 80/5));
	last = "  ENTER LOGIN NAME: ";
	if(goal < 9){
	    last = " LOGIN: ";
	    if((goal += 13) < 9){
		last += 1;
		goal = 0;
	    }
	}

	if(goal){
	    for(i = len, j = 0;
		i < sizeof(prompt) && (prompt[i] = mb->host[j]); i++, j++)
	      if(i == goal && mb->host[goal+1] && i < sizeof(prompt)){
		  strncpy(&prompt[i-3], "...", sizeof(prompt)-(i-3));
		  prompt[sizeof(prompt)-1] = '\0';
		  break;
	      }
	}
	else
	  i = 0;

	strncpy(&prompt[i], last, sizeof(prompt)-i);
	prompt[sizeof(prompt)-1] = '\0';

	while(1) {
	    rc = opt_enter(user, NETMAXUSER, prompt, &flags);
	    if(rc != 4)
	      break;
	}

	if(rc == 1 || !user[0]) {
	    user[0]   = '\0';
	    pwd[0] = '\0';
	}
    }
    else
      strncpy(user, mb->user, NETMAXUSER);

    user[NETMAXUSER-1] = '\0';
    pwd[NETMAXPASSWD-1] = '\0';

    if(!user[0])
      return;


    /* Dress up long host/user names */
    /* leave space for "HOST", "USER" "ENTER PWD", 12 for user 6 for pwd */
    snprintf(prompt, sizeof(prompt), "%sHOST: ", (mb->sslflag||mb->tlsflag) ? "+ " : "");
    len = strlen(prompt);
    goal  = strlen(mb->host);
    ugoal = strlen(user);
    if((i = 80 - (len + 8 + 18 + 6)) < 14){
	goal = 0;		/* no host! */
	if((i = 80 - (6 + 18 + 6)) <= 6){
	    ugoal = 0;		/* no user! */
	    if((i = 80 - (18 + 6)) <= 0)
	      i = 0;
	}
	else{
	    ugoal = i;		/* whatever's left */
	    i     = 0;
	}
    }
    else
      while(goal + ugoal > i)
	if(goal > ugoal)
	  goal--;
	else
	  ugoal--;

    if(goal){
	snprintf(prompt, sizeof(prompt), "%sHOST: ",
		(mb->sslflag||mb->tlsflag) ? "+ " : "");
	for(i = len, j = 0;
	    i < sizeof(prompt) && (prompt[i] = mb->host[j]); i++, j++)
	  if(i == goal && mb->host[goal+1] && i < sizeof(prompt)){
	      strncpy(&prompt[i-3], "...", sizeof(prompt)-(i-3));
	      prompt[sizeof(prompt)-1] = '\0';
	      break;
	  }
    }
    else
      i = 0;

    if(ugoal){
	strncpy(&prompt[i], &"  USER: "[i ? 0 : 2], sizeof(prompt)-i);
	prompt[sizeof(prompt)-1] = '\0';
	for(i += strlen(&prompt[i]), j = 0;
	    i < sizeof(prompt) && (prompt[i] = user[j]); i++, j++)
	  if(j == ugoal && user[ugoal+1] && i < sizeof(prompt)){
	      strncpy(&prompt[i-3], "...", sizeof(prompt)-(i-3));
	      prompt[sizeof(prompt)-1] = '\0';
	      break;
	  }
    }

    strncpy(&prompt[i], &"  ENTER PASSWORD: "[i ? 0 : 8], sizeof(prompt)-i);
    prompt[sizeof(prompt)-1] = '\0';

    *pwd = '\0';
    while(1) {
	flags = OE_PASSWD;
        rc = opt_enter(pwd, NETMAXPASSWD, prompt, &flags);
	if(rc != 4)
          break;
    }

    if(rc == 1 || !pwd[0]) {
        user[0] = pwd[0] = '\0';
        return;
    }
}


void mm_critical(stream)
    MAILSTREAM *stream;
{
}


void mm_nocritical(stream)
    MAILSTREAM *stream;
{
}


long mm_diskerror(stream, errcode, serious)
    MAILSTREAM *stream;
    long errcode;
    long serious;
{
    return T;
}


void mm_fatal(string)
    char *string;
{
    fprintf(stderr, "%s\n", string);
}


void mm_searched(stream, msgno)
    MAILSTREAM *stream;
    unsigned long msgno;
{
}


void mm_status(stream, mailbox, status)
    MAILSTREAM *stream;
    char *mailbox;
    MAILSTATUS *status;
{
}

void mm_dlog(string)
    char *string;
{
    fprintf(stderr, "%s\n", string);
}


int
opt_enter(string, field_len, prompt, flags)
     char       *string, *prompt;
     int         field_len;
     int	*flags;
{
    char *pw;
    int   return_v = -10;

    while(return_v == -10){

	if(flags && *flags & OE_PASSWD){
	    if((pw = getpass(prompt)) != NULL){
		if(strlen(pw) < field_len){
		    strncpy(string, pw, field_len);
		    string[field_len-1] = '\0';
		    return_v = 0;
		}
		else{
		    fputs("Password too long\n", stderr);
		    return_v = -1;
		}
	    }
	    else
	      return_v = 1;	/* cancel? */
	}
	else{
	    char *p;

	    fputs(prompt, stdout);
	    fgets(string, field_len, stdin);
	    string[field_len-1] = '\0';
	    if((p = strpbrk(string, "\r\n")) != NULL)
	      *p = '\0';

	    return_v = 0;
	}
    }

    return(return_v);
}
