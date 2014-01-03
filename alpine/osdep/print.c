#if !defined(lint) && !defined(DOS)
static char rcsid[] = "$Id: print.c 942 2008-03-04 18:21:33Z hubert@u.washington.edu $";
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
#include <system.h>
#include <general.h>

#include "../c-client/mail.h"	/* for MAILSTREAM and friends */
#include "../c-client/osdep.h"
#include "../c-client/rfc822.h"	/* for soutr_t and such */
#include "../c-client/misc.h"	/* for cpystr proto */
#include "../c-client/utf8.h"	/* for CHARSET and such*/
#include "../c-client/imap4r1.h"

#include "../../pith/charconv/utf8.h"
#include "../../pith/charconv/filesys.h"

#include "../../pith/osdep/color.h"
#include "../../pith/osdep/temp_nam.h"
#include "../../pith/osdep/err_desc.h"
#include "../../pith/osdep/collate.h"

#include "../../pith/debug.h"
#include "../../pith/conf.h"
#include "../../pith/store.h"
#include "../../pith/filttype.h"

#include "../../pico/estruct.h"	/* for ctrl() */
#include "../../pico/keydefs.h"	/* for KEY_* */

#include "../status.h"
#include "../signal.h"
#include "../radio.h"
#include "../../pico/estruct.h"
#include "../../pico/pico.h"
#include "../mailview.h"

#ifdef _WINDOWS
#include "../../pico/osdep/mswin.h"
#endif

#include "../../pico/osdep/raw.h"

#include "termin.gen.h"

#include "print.h"


/*======================================================================
    print routines
   
    Functions having to do with printing on paper and forking of spoolers

    In general one calls open_printer() to start printing. One of
    the little print functions to send a line or string, and then
    call print_end() when complete. This takes care of forking off a spooler
    and piping the stuff down it. No handles or anything here because there's
    only one printer open at a time.

 ====*/



#ifndef _WINDOWS
static char *trailer;  /* so both open and close_printer can see it */
static int   ansi_off;
static CBUF_S cb;
#endif /* !_WINDOWS */


/*----------------------------------------------------------------------
       Open the printer

  Args: desc -- Description of item to print. Should have one trailing blank.

  Return value: < 0 is a failure.
		0 a success.

This does most of the work of popen so we can save the standard output of the
command we execute and send it back to the user.
  ----*/
int
open_printer(char *desc)
{
#ifndef _WINDOWS
    char command[201], prompt[200];
    int  cmd, rc, just_one;
    char *p, *init, *nick;
    char aname[100], wname[100];
    char *printer;
    int	 done = 0, i, lastprinter, cur_printer = 0;
    HelpType help;
    char   **list;
    static ESCKEY_S ekey[] = {
	/* TRANSLATORS: these are command labels for printing screen */
	{'y', 'y', "Y", N_("Yes")},
	{'n', 'n', "N", N_("No")},
	/* TRANSLATORS: go to Previous Printer in list */
	{ctrl('P'), 10, "^P", N_("Prev Printer")},
	{ctrl('N'), 11, "^N", N_("Next Printer")},
	{-2,   0,   NULL, NULL},
	/* TRANSLATORS: use Custom Print command */
	{'c', 'c', "C", N_("CustomPrint")},
	{KEY_UP,    10, "", ""},
	{KEY_DOWN,  11, "", ""},
	{-1, 0, NULL, NULL}};
#define PREV_KEY   2
#define NEXT_KEY   3
#define CUSTOM_KEY 5
#define UP_KEY     6
#define DOWN_KEY   7

    trailer      = NULL;
    init         = NULL;
    nick         = NULL;
    command[sizeof(command)-1] = '\0';

    if(ps_global->VAR_PRINTER == NULL){
        q_status_message(SM_ORDER | SM_DING, 3, 5,
	"No printer has been chosen.  Use SETUP on main menu to make choice.");
	return(-1);
    }

    /* Is there just one print command available? */
    just_one = (ps_global->printer_category!=3&&ps_global->printer_category!=2)
	       || (ps_global->printer_category == 2
		   && !(ps_global->VAR_STANDARD_PRINTER
			&& ps_global->VAR_STANDARD_PRINTER[0]
			&& ps_global->VAR_STANDARD_PRINTER[1]))
	       || (ps_global->printer_category == 3
		   && !(ps_global->VAR_PERSONAL_PRINT_COMMAND
			&& ps_global->VAR_PERSONAL_PRINT_COMMAND[0]
			&& ps_global->VAR_PERSONAL_PRINT_COMMAND[1]));

    if(F_ON(F_CUSTOM_PRINT, ps_global))
      ekey[CUSTOM_KEY].ch = 'c'; /* turn this key on */
    else
      ekey[CUSTOM_KEY].ch = -2;  /* turn this key off */

    if(just_one){
	ekey[PREV_KEY].ch = -2;  /* turn these keys off */
	ekey[NEXT_KEY].ch = -2;
	ekey[UP_KEY].ch   = -2;
	ekey[DOWN_KEY].ch = -2;
    }
    else{
	ekey[PREV_KEY].ch = ctrl('P'); /* turn these keys on */
	ekey[NEXT_KEY].ch = ctrl('N');
	ekey[UP_KEY].ch   = KEY_UP;
	ekey[DOWN_KEY].ch = KEY_DOWN;
	/*
	 * count how many printers in list and find the default in the list
	 */
	if(ps_global->printer_category == 2)
	  list = ps_global->VAR_STANDARD_PRINTER;
	else
	  list = ps_global->VAR_PERSONAL_PRINT_COMMAND;

	for(i = 0; list[i]; i++)
	  if(strcmp(ps_global->VAR_PRINTER, list[i]) == 0)
	    cur_printer = i;
	
	lastprinter = i - 1;
    }

    help = NO_HELP;
    ps_global->mangled_footer = 1;

    while(!done){
	if(init)
	  fs_give((void **)&init);

	if(trailer)
	  fs_give((void **)&trailer);

	if(just_one)
	  printer = ps_global->VAR_PRINTER;
	else
	  printer = list[cur_printer];

	parse_printer(printer, &nick, &p, &init, &trailer, NULL, NULL);
	strncpy(command, p, sizeof(command)-1);
	command[sizeof(command)-1] = '\0';
	fs_give((void **)&p);
	/* TRANSLATORS: Print something1 using something2.
	   For example, Print configuration using printer three. */
	snprintf(prompt, sizeof(prompt), _("Print %s using \"%s\" ? "),
		desc ? desc : "",
		*nick ? nick : command);
	prompt[sizeof(prompt)-1] = '\0';

	fs_give((void **)&nick);
	
	cmd = radio_buttons(prompt, -FOOTER_ROWS(ps_global),
				 ekey, 'y', 'x', help, RB_NORM);
	
	switch(cmd){
	  case 'y':
	    q_status_message1(SM_ORDER, 0, 9,
		"Printing with command \"%s\"", command);
	    done++;
	    break;

	  case 10:
	    cur_printer = (cur_printer>0)
				? (cur_printer-1)
				: lastprinter;
	    break;

	  case 11:
	    cur_printer = (cur_printer<lastprinter)
				? (cur_printer+1)
				: 0;
	    break;

	  case 'n':
	  case 'x':
	    done++;
	    break;

	  case 'c':
	    done++;
	    break;

	  default:
	    break;
	}
    }

    if(cmd == 'c'){
	if(init)
	  fs_give((void **)&init);

	if(trailer)
	  fs_give((void **)&trailer);

	snprintf(prompt, sizeof(prompt), "Enter custom command : ");
	prompt[sizeof(prompt)-1] = '\0';
	command[0] = '\0';
	rc = 1;
	help = NO_HELP;
	while(rc){
	    int flags = OE_APPEND_CURRENT;

	    rc = optionally_enter(command, -FOOTER_ROWS(ps_global), 0,
		sizeof(command), prompt, NULL, help, &flags);
	    
	    if(rc == 1){
		cmd = 'x';
		rc = 0;
	    }
	    else if(rc == 3)
	      help = (help == NO_HELP) ? h_custom_print : NO_HELP;
	    else if(rc == 0){
		removing_trailing_white_space(command);
		removing_leading_white_space(command);
		q_status_message1(SM_ORDER, 0, 9,
		    "Printing with command \"%s\"", command);
	    }
	}
    }

    if(cmd == 'x' || cmd == 'n'){
	q_status_message(SM_ORDER, 0, 2, "Print cancelled");
	if(init)
	  fs_give((void **)&init);

	if(trailer)
	  fs_give((void **)&trailer);

	return(-1);
    }

    display_message('x');

    ps_global->print = (PRINT_S *)fs_get(sizeof(PRINT_S));
    memset(ps_global->print, 0, sizeof(PRINT_S));

    strncpy(aname, ANSI_PRINTER, sizeof(aname)-1);
    aname[sizeof(aname)-1] = '\0';
    strncat(aname, "-no-formfeed", sizeof(aname)-strlen(aname)-1);
    strncpy(wname, WYSE_PRINTER, sizeof(wname)-1);
    wname[sizeof(wname)-1] = '\0';
    strncat(wname, "-no-formfeed", sizeof(wname)-strlen(wname)-1);
    if(strucmp(command, ANSI_PRINTER) == 0
       || strucmp(command, aname) == 0
       || strucmp(command, WYSE_PRINTER) == 0
       || strucmp(command, wname) == 0){
        /*----------- Attached printer ---------*/
        q_status_message(SM_ORDER, 0, 9,
	    "Printing to attached desktop printer...");
        display_message('x');
	xonxoff_proc(1);			/* make sure XON/XOFF used */
	crlf_proc(1);				/* AND LF->CR xlation */
	if(strucmp(command, ANSI_PRINTER) == 0
	   || strucmp(command, aname) == 0){
	    fputs("\033[5i", stdout);
	    ansi_off = 1;
	}
	else{
	    ansi_off = 0;
	    printf("%c", 18); /* aux on for wyse60,
			         Chuck Everett <ceverett@odessa.edu> */
	}

        ps_global->print->fp = stdout;
        if(strucmp(command, ANSI_PRINTER) == 0
	   || strucmp(command, WYSE_PRINTER) == 0){
	    /* put formfeed at the end of the trailer string */
	    if(trailer){
		int len = strlen(trailer);

		fs_resize((void **)&trailer, len+2);
		trailer[len] = '\f';
		trailer[len+1] = '\0';
	    }
	    else
	      trailer = cpystr("\f");
	}
    }
    else{
        /*----------- Print by forking off a UNIX command ------------*/
        dprint((4, "Printing using command \"%s\"\n",
	       command ? command : "?"));
	ps_global->print->result = temp_nam(NULL, "pine_prt");
	if(ps_global->print->result &&
	   (ps_global->print->pipe = open_system_pipe(command,
						      &ps_global->print->result, NULL,
						      PIPE_WRITE | PIPE_STDERR, 0,
						      pipe_callback, NULL))){
	    ps_global->print->fp = ps_global->print->pipe->out.f;
	}
	else{
	    if(ps_global->print->result){
		our_unlink(ps_global->print->result);
		fs_give((void **)&ps_global->print->result);
	    }

            q_status_message1(SM_ORDER | SM_DING, 3, 4,
			      "Error opening printer: %s",
                              error_description(errno));
            dprint((2, "Error popening printer \"%s\"\n",
                      error_description(errno)));
	    if(init)
	      fs_give((void **)&init);

	    if(trailer)
	      fs_give((void **)&trailer);
	    
	    return(-1);
        }
    }

    ps_global->print->err = 0;
    if(init){
	if(*init)
	  fputs(init, ps_global->print->fp);

	fs_give((void **)&init);
    }

    cb.cbuf[0] = '\0';
    cb.cbufp   = cb.cbuf;
    cb.cbufend = cb.cbuf;
#else /* _WINDOWS */
    int status;
    LPTSTR desclpt = NULL;

    if(desc)
      desclpt = utf8_to_lptstr(desc);

    if (status = mswin_print_ready (0, desclpt)) {
        q_status_message1(SM_ORDER | SM_DING, 3, 4,
			  "Error starting print job: %s",
			  mswin_print_error(status));
	if(desclpt)
	  fs_give((void **) &desclpt);

        return(-1);
    }

    if(desclpt)
      fs_give((void **) &desclpt);

    q_status_message(SM_ORDER, 0, 9, "Printing to windows printer...");
    display_message('x');

    /* init print control structure */
    ps_global->print = (PRINT_S *)fs_get(sizeof(PRINT_S));
    memset(ps_global->print, 0, sizeof(PRINT_S));

    ps_global->print->err = 0;
#endif /* _WINDOWS */

    return(0);
}



/*----------------------------------------------------------------------
     Close printer
  
  If we're piping to a spooler close down the pipe and wait for the process
to finish. If we're sending to an attached printer send the escape sequence.
Also let the user know the result of the print
 ----*/
void
close_printer(void)
{
#ifndef _WINDOWS
    if(trailer){
	if(*trailer)
	  fputs(trailer, ps_global->print->fp);

	fs_give((void **)&trailer);
    }

    if(ps_global->print->fp == stdout) {
	if(ansi_off)
          fputs("\033[4i", stdout);
	else
	  printf("%c", 20); /* aux off for wyse60 */

        fflush(stdout);
	if(F_OFF(F_PRESERVE_START_STOP, ps_global))
	  xonxoff_proc(0);			/* turn off XON/XOFF */

	crlf_proc(0);				/* turn off CF->LF xlantion */
    } else {
	(void) close_system_pipe(&ps_global->print->pipe, NULL, pipe_callback);
	display_output_file(ps_global->print->result, "PRINT", NULL, 1);
	if(ps_global->print && ps_global->print->result)
	  fs_give((void **) &ps_global->print->result);
    }
#else /* _WINDOWS */
    mswin_print_done();
#endif /* _WINDOWS */

    if(ps_global->print)
      fs_give((void **) &ps_global->print);

    q_status_message(SM_ASYNC, 0, 3, "Print command completed");
    display_message('x');
}


/*----------------------------------------------------------------------
     Print a single character, translate from UTF-8 to user's locale charset.

  Args: c -- char to print
  Returns: 1 on success, 0 on ps_global->print->err
 ----*/
int
print_char(int c)
{
#ifndef _WINDOWS
    int i, outchars;
    unsigned char obuf[MAX(MB_LEN_MAX,32)];

    if(!ps_global->print->err
       && (outchars = utf8_to_locale(c, &cb, obuf, sizeof(obuf)))){
	for(i = 0; i < outchars && !ps_global->print->err; i++)
	  if(putc(obuf[i], ps_global->print->fp) == EOF)
	    ps_global->print->err = 1;
    }
#else /* _WINDOWS */
    if(!ps_global->print->err
       && (ps_global->print->err = mswin_print_char_utf8(c)))
      q_status_message1(SM_ORDER, 0, 9, "Print cancelled: %s",
		     mswin_print_error((unsigned short)ps_global->print->err));
#endif /* _WINDOWS */

    return(!ps_global->print->err);
}


/*----------------------------------------------------------------------
     Send a line of text to the printer

  Args:  line -- Text to print

  ----*/
void
print_text(char *line)
{
#ifndef _WINDOWS
    int slen = strlen(line);

    while(!ps_global->print->err && slen--)
      if(print_char(*line++) == 0)
        ps_global->print->err = 1;
#else /* _WINDOWS */
    if(!ps_global->print->err
       && (ps_global->print->err = mswin_print_text_utf8(line)))
      q_status_message1(SM_ORDER, 0, 9, "Print cancelled: %s",
		     mswin_print_error((unsigned short)ps_global->print->err));
#endif /* _WINDOWS */
}


/*----------------------------------------------------------------------
      printf style formatting with one arg for printer

 Args: line -- The printf control string
       a1   -- The 1st argument for printf
 ----*/
void
print_text1(char *line, char *a1)
{
    char buf[64000];

    if(!ps_global->print->err && snprintf(buf, sizeof(buf), line, a1) < 0)
      ps_global->print->err = 1;
    else
      print_text(buf);
}
