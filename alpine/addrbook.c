#if !defined(lint) && !defined(DOS)
static char rcsid[] = "$Id: addrbook.c 1266 2009-07-14 18:39:12Z hubert@u.washington.edu $";
#endif

/*
 * ========================================================================
 * Copyright 2006-2009 University of Washington
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

/*====================================================================== 
    addrbook.c
    display, browse and edit the address book.
 ====*/


#include "headers.h"
#include "addrbook.h"
#include "adrbkcmd.h"
#include "mailindx.h"
#include "mailcmd.h"
#include "status.h"
#include "confscroll.h"
#include "keymenu.h"
#include "radio.h"
#include "titlebar.h"
#include "help.h"
#include "folder.h"
#include "signal.h"
#include "alpine.h"
#include "../pith/adrbklib.h"
#include "../pith/abdlc.h"
#include "../pith/addrbook.h"
#include "../pith/ablookup.h"
#include "../pith/addrstring.h"
#include "../pith/bldaddr.h"
#include "../pith/state.h"
#include "../pith/bitmap.h"
#include "../pith/newmail.h"
#include "../pith/remote.h"
#include "../pith/util.h"
#include "../pith/thread.h"
#include "../pith/hist.h"
#include "../pith/list.h"


HelpType    gAbookHelp = NO_HELP;


/* internal prototypes */
void           end_adrbks(void);
int	       init_disp_form_prefix(PerAddrBook *, int *);
void           display_book(int, int, int, int, Pos *);
void           paint_line(int, long, int, Pos *);
void           redraw_addr_screen(void);
char          *addr_book(AddrBookArg, char *, char **);
void           ab_zoom(EXPANDED_S *, int *);
void           ab_unzoom(int *);
void           empty_warning(long);
void           clickable_warning(long);
void           no_tabs_warning(void);
int            ab_apply_cmd(struct pine *, AdrBk *, long, int);
void           ab_select(struct pine *, AdrBk *, long, int, int *);
int            ab_select_type(AdrBk *, int);
int            ab_select_text(AdrBk *, int);
int            match_check(AdrBk_Entry *, int, char *);
void           ab_goto_folder(int);
long           ab_whereis(int *, int);
int            any_addrs_avail(long);
int            entry_is_clickable(long);
int            entry_is_clickable_title(long);
int            is_empty(long);
int            entry_is_listent(long);
int            entry_is_addkey(long);
int            entry_is_askserver(long);
int            add_is_global(long);
int            next_selectable_line(long, long *);
int            prev_selectable_line(long, long *);
void           erase_checks(void);
int            search_book(long, int, long *, int *, int *);
int            find_in_book(long, char *, long *, int *);
int            search_in_one_line(AddrScrn_Disp *, AdrBk_Entry *, char *, char *);
int            abook_select_tool(struct pine *, int, CONF_S **, unsigned);
char          *choose_an_address_with_this_prefix(COMPLETE_S *);
#ifdef	_WINDOWS
int	       addr_scroll_up(long);
int	       addr_scroll_down(long);
int	       addr_scroll_to_pos(long);
int	       addr_scroll_callback(int, long);
char	      *pcpine_help_addrbook(char *);
#endif


/* TRANSLATORS: This is a placeholder for a list of addresses in address book */
#define CLICKHERE       _("[ Address List ]")
/* TRANSLATORS: This is an empty address list */
#define EMPTY           _("[ Empty ]")
#define ZOOM_EMPTY      _("[ No Selected Entries in this Address Book ]")
/* TRANSLATORS: Move here means to move the cursor to this line of the screen */
#define ADD_PERSONAL    _("    [ Move here to add a Personal Address Book ]")
/* TRANSLATORS: A global address book is a shared address book */
#define ADD_GLOBAL      _("    [ Move here to add a Global Address Book ]")
/* TRANSLATORS: A heading for an address book distribution list */
#define DISTLIST        _("DISTRIBUTION LIST:")
#define NOABOOKS        _("[ No Address Book Configured ]")
/* TRANSLATORS: Select here means to move the cursor to this line and then type
   the Select command. */
#define CLICKHERECMB    _("[ Select Here to See Expanded List ]")



/*
 * Returns the index of the current address book.
 */
int
cur_addr_book(void)
{
    return(adrbk_num_from_lineno(as.top_ent + as.cur_row));
}


/*
 * Returns 1 if current abook is open, else 0.
 */
int
cur_is_open(void)
{
    int current, ret = 0;

    if(as.initialized &&
       as.n_addrbk > 0 &&
       (current = cur_addr_book()) >= 0 &&
       current < as.n_addrbk &&
       !entry_is_askserver(as.top_ent+as.cur_row))
      ret = (as.adrbks[current].ostatus == Open);

    return(ret);
}


void
end_adrbks(void)
{
    int i;

    dprint((2, "- end_adrbks -\n"));

    if(!as.initialized)
      return;

    for(i = 0; i < as.n_addrbk; i++)
      init_abook(&as.adrbks[i], Closed);
    
    as.selections = 0;
    as.checkboxes = 0;
    ab_nesting_level = 0;
}



/*
 * We have to call this to set up the format of the columns. There is a
 * separate format for each addrbook, so we need to call this for each
 * addrbook. We call it when the pab's are built. It also depends on
 * whether or not as.checkboxes is set, so if we go into a Select mode
 * from the address book maintenance screen we need to re-call this. Since
 * we can't go back out of ListMode we don't have that problem. Restore_state
 * has to call it because of the as.checkboxes possibly being different in
 * the two states.
 */
void
init_disp_form(PerAddrBook *pab, char **list, int addrbook_num)
{
    addrbook_new_disp_form(pab, list, addrbook_num, init_disp_form_prefix);
}


int
init_disp_form_prefix(PerAddrBook *pab, int *columnp)
{
    int rv = 0;

    if(as.checkboxes){
	pab->disp_form[(*columnp)].wtype     = Fixed;
	pab->disp_form[(*columnp)].req_width = 3;
	pab->disp_form[(*columnp)++].type    = Checkbox;
    }
    else if(as.selections){
	if(F_ON(F_SELECTED_SHOWN_BOLD, ps_global) && StartBold()){
	    EndBold();
	    rv = 1;
	}
	else{
	    pab->disp_form[(*columnp)].wtype     = Fixed;
	    pab->disp_form[(*columnp)].req_width = 1;
	    pab->disp_form[(*columnp)++].type    = Selected;
	}
    }

    return(rv);
}


/*
 * save_and_restore - single interface to save_state and restore_state
 */
void
save_and_restore(int cmd, SAVE_STATE_S *state)
{
    switch(cmd){
      case SAR_SAVE :
	save_state(state);
	break;
      case SAR_RESTORE :
	restore_state(state);
	break;
    }
}


/*
 * Save the screen state and the Open or Closed status of the addrbooks.
 */
void
save_state(SAVE_STATE_S *state)
{
    int                 i;
    DL_CACHE_S         *dlc;

    dprint((9, "- save_state -\n"));

    /* allocate space for saving the screen structure and save it */
    state->savep    = (AddrScrState *)fs_get(sizeof(AddrScrState));
    *(state->savep) = as; /* copy the struct */


    if(as.initialized){
	/* allocate space for saving the ostatus for each addrbook */
	state->stp = (OpenStatus *)fs_get(as.n_addrbk * sizeof(OpenStatus));

	for(i = 0; i < as.n_addrbk; i++)
	  (state->stp)[i] = as.adrbks[i].ostatus;


	state->dlc_to_warp_to = (DL_CACHE_S *)fs_get(sizeof(DL_CACHE_S));
	dlc = get_dlc(as.top_ent + as.cur_row);
	*(state->dlc_to_warp_to) = *dlc; /* copy the struct */
    }
}


/*
 * Restore the state.
 *
 * Side effect: Flushes addrbook entry cache entries so they need to be
 * re-fetched afterwords.  This only applies to entries obtained since
 * the call to save_state.
 * Also flushes all dlc cache entries, so dlist calls need to be repeated.
 */
void
restore_state(SAVE_STATE_S *state)
{
    int i;

    dprint((9, "- restore_state -\n"));

    as = *(state->savep);  /* put back cur_row and all that */

    if(as.initialized){
	/* restore addressbook OpenStatus to what it was before */
	for(i = 0; i < as.n_addrbk; i++){
	    init_disp_form(&as.adrbks[i], ps_global->VAR_ABOOK_FORMATS, i);
	    init_abook(&as.adrbks[i], (state->stp)[i]);
	}

	/*
	 * jump cache back to where we were
	 */
	warp_to_dlc(state->dlc_to_warp_to, as.top_ent+as.cur_row);

	fs_give((void **)&state->dlc_to_warp_to);
	fs_give((void **)&state->stp);
    }

    if(state->savep)
      fs_give((void **)&state->savep);
}


/*
 * Returns the addrbook entry for this display row.
 */
AdrBk_Entry *
ae(long int row)
{
    PerAddrBook *pab;
    LineType type;
    AddrScrn_Disp *dl;

    dl = dlist(row);
    type = dl->type;
    if(!(type == Simple || type == ListHead ||
         type == ListEnt || type == ListClickHere))
      return((AdrBk_Entry *)NULL);

    pab = &as.adrbks[adrbk_num_from_lineno(row)];

    return(adrbk_get_ae(pab->address_book, (a_c_arg_t) dl->elnum));
}


/*
 * Args: start_disp     --  line to start displaying on when redrawing, 0 is
 *	 		    the top_of_screen
 *       cur_line       --  current line number (0 is 1st line we display)
 *       old_line       --  old line number
 *       redraw         --  flag requesting redraw as opposed to update of
 *			    current line
 *     start_pos        --  return position where highlighted text begins here
 *
 * Result: lines painted on the screen
 *
 * It either redraws the screen from line "start_disp" down or
 * moves the cursor from one field to another.
 */
void
display_book(int start_disp, int cur_line, int old_line, int redraw, Pos *start_pos)
{
    int screen_row, highlight;
    long global_row;
    Pos sp;

    dprint((9,
   "- display_book() -\n   top %d start %d cur_line %d old_line %d redraw %d\n",
	as.top_ent, start_disp, cur_line, old_line, redraw));

    if(start_pos){
	start_pos->row = 0;
	start_pos->col = 0;
    }

    if(as.l_p_page <= 0)
      return;

#ifdef _WINDOWS
    mswin_beginupdate();
#endif
    if(redraw){
        /*--- Repaint all of the screen or bottom part of screen ---*/
        global_row = as.top_ent + start_disp;
        for(screen_row = start_disp;
	    screen_row < as.l_p_page;
	    screen_row++, global_row++){

            highlight = (screen_row == cur_line);
	    ClearLine(screen_row + HEADER_ROWS(ps_global));
            paint_line(screen_row + HEADER_ROWS(ps_global), global_row,
		highlight, &sp);
	    if(start_pos && highlight)
	      *start_pos = sp;
        }
    }
    else{

        /*--- Only update current, or move the cursor ---*/
        if(cur_line != old_line){

            /*--- Repaint old position to erase "cursor" ---*/
            paint_line(old_line + HEADER_ROWS(ps_global), as.top_ent + old_line,
                       0, &sp);
        }

        /*--- paint the position with the cursor ---*/
        paint_line(cur_line + HEADER_ROWS(ps_global), as.top_ent + cur_line,
                   1, &sp);
	if(start_pos)
	  *start_pos = sp;
    }

#ifdef _WINDOWS
    scroll_setpos(as.top_ent);
    mswin_endupdate();
#endif
    fflush(stdout);
}


/*
 * Paint a line on the screen
 *
 * Args: line    --  Line on screen to paint
 *    global_row --  Row number of line to paint
 *     highlight --  Line should be highlighted
 *     start_pos --  return position where text begins here
 *
 * Result: Line is painted
 *
 * The three field widths for the formatting are passed in.  There is an
 * implicit 2 spaces between the fields.
 *
 *    | fld_width[0] chars |__| fld_width[1] |__| fld_width[2] | ...
 */
void
paint_line(int line, long int global_row, int highlight, Pos *start_pos)
{
    int	           scol, bolden = 0;
    char	  *start_hilite_here, *end_hilite_here;
    AddrScrn_Disp *dl;
    char           lbuf[6*MAX_SCREEN_COLS + 1];
    register char *p;

    dprint((10, "- paint_line(%d, %d) -\n", line, highlight));

    dl  = dlist(global_row);
    start_pos->row = line;
    start_pos->col = 0; /* default */

    switch(dl->type){
      case Beginning:
      case End:
        return;

      default:
	break;
    }

    p = get_abook_display_line(global_row, line == HEADER_ROWS(ps_global),
			       highlight ? &start_hilite_here : NULL,
			       highlight ? &end_hilite_here : NULL,
			       &scol, lbuf, sizeof(lbuf));

    if(as.selections &&
       as.do_bold &&
       (dl->type == ListHead || dl->type == Simple)){
	PerAddrBook *pab;

	pab = &as.adrbks[adrbk_num_from_lineno(global_row)];
	if(entry_is_selected(pab->address_book->selects, (a_c_arg_t)dl->elnum)){
	    bolden++;
	    StartBold();
	}
    }

    if(p && *p){
	if(highlight){
	    char save_char;

	    MoveCursor(line, 0);

	    /*
	     * print part before highlight starts
	     */
	    if(start_hilite_here != NULL){
		save_char = *start_hilite_here;
		*start_hilite_here = '\0';
		Write_to_screen(p);
		*start_hilite_here = save_char;
	    }

	    /*
	     * print highlighted part
	     */

	    if(end_hilite_here != NULL){
		save_char = *end_hilite_here;
		*end_hilite_here = '\0';
	    }

	    StartInverse();
	    Write_to_screen(start_hilite_here ? start_hilite_here : p);
	    EndInverse();

	    /*
	     * print part after highlight ends
	     */
	    if(end_hilite_here != NULL){
		*end_hilite_here = save_char;
		Write_to_screen(end_hilite_here);
	    }
	}
	else
	  PutLine0(line, 0, p);
    }

    if(bolden)
      EndBold();

    if(scol > 0)
      start_pos->col = scol;
}


/*
 * Assemble a line suitable for displaying on screen
 *
 * Args: global_row -- Row number of line to assemble.
 *     continuation -- This is the top line of screen display
 *         s_hilite -- Location in the returned line where highlight
 *                       should start, if desired
 *         e_hilite -- Location in the returned line where highlight
 *                       should end, if desired
 *           retcol -- Return column where text begins here.
 *             lbuf -- Put the output here. Lbuf should be at least
 *                     size 6*screen_cols+1.
 *         lbufsize -- Size of lbuf array
 *
 * Result: Pointer to lbuf is returned.
 *
 * There is an implicit 2 spaces between the fields.
 *
 *    | fld_width[0] chars |__| fld_width[1] |__| fld_width[2] | ...
 */
char *
get_abook_display_line(long int global_row, int continuation, char **s_hilite,
		       char **e_hilite, int *retcol, char *lbuf, size_t lbufsize)
{
    int		  fld_col[NFIELDS],
		  fld_width[NFIELDS],
		  screen_width, width,
		  col, special_col = 0,
		  width_consumed,
		  scol = -1,
		  fld;
    char	  *string, *writeptr;
    char           special[6*MAX_SCREEN_COLS-1];
    AddrScrn_Disp *dl;
    AdrBk_Entry	  *abe;
    PerAddrBook	  *pab;
#define LSPACE() (lbufsize - (writeptr - lbuf) - 1)

    dprint((10, "- get_display_line(%d) -\n", global_row));

    dl  = dlist(global_row);
    if(retcol)
      *retcol = 0;	/* default */

    if(s_hilite){
      *s_hilite = NULL;	/* NULL in these means to hilight whole line */
      *e_hilite = NULL;
    }

    writeptr = lbuf;
    memset(writeptr, 0, lbufsize);
    memset(special, 0, sizeof(special));

    switch(dl->type){
      case Beginning:
      case End:
        return lbuf;

      default:
	break;
    }

    screen_width = ps_global->ttyo->screen_cols;

    /* the types in this set span all columns */
    switch(dl->type){
      /* center these */
      case Text:
      case Title:
      case TitleCmb:
      case ClickHereCmb:
      case Empty:
      case ZoomEmpty:
      case NoAbooks:
	if(dl->type == Empty)
	  string = EMPTY;
	else if(dl->type == ZoomEmpty)
	  string = ZOOM_EMPTY;
	else if(dl->type == NoAbooks)
	  string = NOABOOKS;
	else if(dl->type == ClickHereCmb)
	  string = CLICKHERECMB;
	else
	  string = dl->usst;

	/* center it */
	col = (screen_width - (int) utf8_width(string))/2;
	col = MAX(col, 0);
	width_consumed = 0;

	/* col spaces to start */
	if(col > 0 && LSPACE() >= col){
	    memset(writeptr, ' ', col);
	    writeptr += col;
	    width_consumed += col;
	}

	if((width=utf8_width(string)) <= screen_width-col){
	    strncpy(writeptr, string, LSPACE());
	    width_consumed += width;
	}
	else{
	    utf8_pad_to_width(writeptr, string, LSPACE(), screen_width-col, 1);
	    width_consumed = screen_width;
	}

	if(s_hilite && *s_hilite == NULL){
	    *s_hilite = writeptr;
	    *e_hilite = writeptr + strlen(writeptr);
	}

	writeptr += strlen(writeptr);
	if(width_consumed < screen_width)
	  memset(writeptr, ' ', screen_width-width_consumed);

	if(retcol)
	  *retcol = col;

	return lbuf;

      /* left adjust these */
      case AddFirstPers:
      case AddFirstGlob:
      case AskServer:
	if(dl->type == AddFirstPers)
	  string = ADD_PERSONAL;
	else if(dl->type == AddFirstGlob)
	  string = ADD_GLOBAL;
	else
	  string = dl->usst;

	/* left adjust it */
	col = 0;
	width_consumed = 0;
	if((width=utf8_width(string)) <= screen_width){
	    strncpy(writeptr, string, LSPACE());
	    width_consumed += width;
	}
	else{
	    utf8_pad_to_width(writeptr, string, LSPACE(), screen_width, 1);
	    width_consumed = screen_width;
	}

	if(s_hilite && *s_hilite == NULL){
	    *s_hilite = writeptr;
	    *e_hilite = writeptr + strlen(writeptr);
	}

	writeptr += strlen(writeptr);
	if(width_consumed < screen_width)
	  memset(writeptr, ' ', screen_width-width_consumed);

	if(retcol)
	  *retcol = col;

	return lbuf;

      default:
	break;
    }

    pab = &as.adrbks[adrbk_num_from_lineno(global_row)];
    for(fld = 0; fld < NFIELDS; fld++)
      fld_width[fld] = pab->disp_form[fld].width;

    fld_col[0] = 0;
    for(fld = 1; fld < NFIELDS; fld++)
      fld_col[fld] = MIN(fld_col[fld-1]+fld_width[fld-1]+2, screen_width);

    width_consumed = 0;

    /* fill in the fields */
    for(fld = 0; fld < NFIELDS; fld++){
      if(fld_width[fld] == 0
	 && !(pab->disp_form[fld].type == Addr
	      && (dl->type == ListClickHere || dl->type == ListEmpty)))
	continue;

      switch(pab->disp_form[fld].type){
	case Notused:
	  break;

	case Nickname:
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      abe = ae(global_row);
	      string = (abe && abe->nickname) ? abe->nickname : "";
	      if(scol == -1)
		scol = fld_col[fld];

	      /* left adjust string in field */
	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);
	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];
	      break;

	    default:
	      break;
	  }

	  break;

	case Fullname:
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      abe = ae(global_row);
	      string = (abe && abe->fullname) ? abe->fullname : "";
	      if(scol == -1)
		scol = fld_col[fld];

	      /* left adjust string in field */
	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);
	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];
	      break;

	    case ListEnt:
	      /* continuation line */
	      if(continuation){
		int  width1, width2;

		width2 = MIN(11, fld_width[fld]);
		width1 = MAX(fld_width[fld] - width2 - 1, 0);

	        abe = ae(global_row);
	        string = (abe && abe->fullname) ? abe->fullname : "";

		if(width1){
		  utf8_pad_to_width(writeptr, string, LSPACE(), width1, 1);
		  writeptr += strlen(writeptr);
		  if(LSPACE() > 0)
		    *writeptr++ = ' ';
		}

		/* TRANSLATORS: continuation line meaning there is more on the next page */
		if(width2 && LSPACE() >= strlen(_("(continued)"))){
		    strncpy(writeptr, _("(continued)"), width2); 
		    lbuf[lbufsize-1] = '\0';
		    writeptr += strlen(writeptr);
		}

		width_consumed += fld_width[fld];

		lbuf[lbufsize-1] = '\0';
	      }

	      break;

	    default:
	      break;
	  }

	  break;

	case Addr:
	  switch(dl->type){
	    case ListClickHere:
	    case ListEmpty:
	      if(dl->type == ListClickHere)
		string = CLICKHERE;
	      else
	        string = EMPTY;

	      if((width=utf8_width(string)) <= fld_width[fld]){

		strncpy(writeptr, string, LSPACE());

		if(s_hilite && *s_hilite == NULL){
		  *s_hilite = writeptr;
		  *e_hilite = writeptr + strlen(writeptr);
		}

		writeptr += strlen(writeptr);
		width_consumed += width;

	        if(scol == -1)
		  scol = fld_col[fld];
	      }
	      else{
		/*
		 * Place the string in special array and overlay it
		 * onto the right edge of the screen at the end.
		 */
		if(width <= screen_width){
		  strncpy(special, string, sizeof(special));
		  special_col = screen_width - width;
		}
		else{
	          utf8_pad_to_width(special, string, sizeof(special), screen_width, 1);
		  special_col = 0;
		}

		special[sizeof(special)-1] = '\0';
	      }

	      break;

	    case ListHead:
	      if(scol == -1)
		scol = fld_col[fld];

	      string = DISTLIST;
	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);
	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];
	      break;

	    case Simple:
	      abe = ae(global_row);
#ifdef	ENABLE_LDAP
	      if(abe && abe->addr.addr &&
		 !strncmp(abe->addr.addr, QRUN_LDAP, LEN_QRL))
		string = LDAP_DISP;
	      else
#endif
	        string = (abe && abe->tag == Single && abe->addr.addr) ?
						      abe->addr.addr : "";
	      if(scol == -1)
		scol = fld_col[fld];

	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);
	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];
	      break;

	    case ListEnt:
	      string = listmem(global_row) ? listmem(global_row) : "";
	      if(scol == -1)
		scol = fld_col[fld];

	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);

	      if(s_hilite && *s_hilite == NULL){
		  *s_hilite = writeptr;
		  *e_hilite = writeptr + strlen(writeptr);
	      }

	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];

	      break;

	    default:
	      break;
	  }
	  break;

	case Filecopy:
	case Comment:
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      abe = ae(global_row);
	      if(pab->disp_form[fld].type == Filecopy)
	        string = (abe && abe->fcc) ? abe->fcc : "";
	      else
	        string = (abe && abe->extra) ? abe->extra : "";

	      if(scol == -1)
		scol = fld_col[fld];

	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);
	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];
	      break;

	    default:
	      break;
	  }

	  break;

	case Checkbox:
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      if(entry_is_checked(pab->address_book->checks,
				  (a_c_arg_t)dl->elnum))
		string = "[X]";
	      else
	        string = "[ ]";

	      if(scol == -1)
		scol = fld_col[fld];

	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);
	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];
	      break;

	    default:
	      break;
	  }

	  break;

	case Selected:
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      if(entry_is_selected(pab->address_book->selects,
				   (a_c_arg_t)dl->elnum))
		string = "X";
	      else
	        string = " ";

	      if(scol == -1)
		scol = fld_col[fld];

	      utf8_pad_to_width(writeptr, string, LSPACE(), fld_width[fld], 1);
	      writeptr += strlen(writeptr);
	      width_consumed += fld_width[fld];
	      break;

	    default:
	      break;
	  }

	  break;

	case WhenNoAddrDisplayed:
	  switch(dl->type){
	    case ListClickHere:
	    case ListEmpty:
	    case ListEnt:
	      if(dl->type == ListClickHere)
		string = CLICKHERE;
	      else if(dl->type == ListEmpty)
	        string = EMPTY;
	      else
		string = listmem(global_row) ? listmem(global_row) : "";

	      if((width=utf8_width(string)) <= fld_width[fld])
		strncpy(special, string, sizeof(special));
	      else
	        utf8_pad_to_width(special, string, sizeof(special), fld_width[fld], 1);

	      special[sizeof(special)-1] = '\0';
	      special_col = screen_width - fld_width[fld];
	      special_col = MAX(0, special_col);
	      break;

	    default:
	      break;
	  }

	  break;

        default:
	  break;
      }

      if(fld < NFIELDS-1)
        while(width_consumed < fld_col[fld+1] && LSPACE() > 0){
	  *writeptr++ = ' ';
	  width_consumed++;
	}
    }

    /*
     * Right adjust the special string in lbuf, writing over
     * anything in our way.
     */
    if(special[0]){
	unsigned got_width;

	writeptr = utf8_count_forw_width(lbuf, special_col, &got_width);

	strncpy(writeptr, special, LSPACE());

	if(scol == -1)
	  scol = got_width;

	if(s_hilite && *s_hilite == NULL){
	  *s_hilite = writeptr;
	  *e_hilite = writeptr + strlen(writeptr);
	}

	writeptr += strlen(writeptr);

	width_consumed = (int) utf8_width(lbuf);
	while(width_consumed++ < screen_width && LSPACE() > 0)
	  *writeptr++ = ' ';

	if(LSPACE() > 0)
	  *writeptr = '\0';
    }

    if(scol > 0 && retcol)
      *retcol = scol;

    lbuf[lbufsize-1] = '\0';
    return lbuf;
}


/*
 * Set field widths for the columns of the display.  The idea is to
 * try to come up with something that works pretty well.  Getting it just
 * right isn't important.
 *
 * Col1 and col2 are arrays which contain some widths useful for
 * formatting the screen.  The 0th element is the max
 * width in that column.  The 1st element is the max of the third largest
 * width in each addressbook (yup, strange).
 *
 * The info above applies to the default case (AllAuto).  The newer methods
 * where the user specifies the format is the else part of the big if and
 * is quite a bit different.  It's all sort of ad hoc when we're asked
 * to calculate one of the fields for the user.
 *
 * Returns non-zero if the widths changed since the last time called.
 */
int
calculate_field_widths(void)
{
  int space_left, i, j, screen_width;
  int ret = 0;
  PerAddrBook *pab;
  WIDTH_INFO_S *widths;
  int max_nick, max_full, max_addr, third_full, third_addr, third_fcc;
  int col1[5], col2[5];
  int nick = -1, full = -1, addr = -1;
#define SEP 2  /* space between columns */

  dprint((9, "- calculate_field_widths -\n"));

  screen_width = ps_global->ttyo->screen_cols;

  /* calculate widths for each addrbook independently */
  for(j = 0; j < as.n_addrbk; j++){
    pab = &as.adrbks[j];

    max_nick   = 0;
    max_full   = 2;
    max_addr   = 2;
    third_full = 2;
    third_addr = 2;
    third_fcc  = 2;

    if(pab->address_book){
      widths = &pab->address_book->widths;
      max_nick   = MIN(MAX(max_nick, widths->max_nickname_width), 25);
      max_full   = MAX(max_full, widths->max_fullname_width);
      max_addr   = MAX(max_addr, widths->max_addrfield_width);
      third_full = MAX(third_full, widths->third_biggest_fullname_width);
      if(third_full == 2)
        third_full = MAX(third_full, 2*max_full/3);

      third_addr = MAX(third_addr, widths->third_biggest_addrfield_width);
      if(third_addr == 2)
        third_addr = MAX(third_addr, 2*max_addr/3);

      third_fcc  = MAX(third_fcc, widths->third_biggest_fccfield_width);
      if(third_fcc == 2)
        third_fcc = MAX(third_fcc, 2*widths->max_fccfield_width/3);
    }

    /* figure out which order they're in and reset widths */
    for(i = 0; i < NFIELDS; i++){
      pab->disp_form[i].width = 0;
      switch(pab->disp_form[i].type){
        case Nickname:
	  nick = i;
	  break;

	case Fullname:
	  full = i;
	  break;

	case Addr:
	  addr = i;
	  break;

        default:
	  break;
      }
    }

    /* Compute default format */
    if(pab->disp_form[1].wtype == AllAuto){

      col1[0] = max_full;
      col2[0] = max_addr;
      col1[1] = third_full;
      col2[1] = third_addr;
      col1[2] = 3;
      col2[2] = 3;
      col1[3] = 2;
      col2[3] = 2;
      col1[4] = 1;
      col2[4] = 1;

      space_left = screen_width;

      if(pab->disp_form[0].type == Selected ||
             pab->disp_form[0].type == Checkbox){
	pab->disp_form[0].width = MIN(pab->disp_form[0].req_width,space_left);
	space_left = MAX(space_left-(pab->disp_form[0].width + SEP), 0);
      }

      /*
       * All of the nickname field should be visible,
       * and make it at least 3.
       */
      pab->disp_form[nick].width = MIN(MAX(max_nick, 3), space_left);

      /*
       * The SEP is for two blank columns between nickname and next field.
       * Those blank columns are taken automatically in paint_line().
       */
      space_left -= (pab->disp_form[nick].width + SEP);

      if(space_left > 0){
	for(i = 0; i < 5; i++){
	  /* try fitting most of each field in if possible */
	  if(col1[i] + SEP + col2[i] <= space_left){
	    int extra;

	    extra = space_left - col1[i] - SEP - col2[i];
	    /*
	     * try to stabilize nickname column shifts
	     * so that screen doesn't jump around when we make changes
	     */
	    if(i == 0 && pab->disp_form[nick].width < 7 &&
			extra >= (7 - pab->disp_form[nick].width)){
	      extra -= (7 - pab->disp_form[nick].width);
	      space_left -= (7 - pab->disp_form[nick].width);
	      pab->disp_form[nick].width = 7;
	    }

	    pab->disp_form[addr].width = col2[i] + extra/2;
	    pab->disp_form[full].width =
				space_left - SEP - pab->disp_form[addr].width;
	    break;
	  }
	}

	/*
	 * None of them would fit.  Toss addr field.
	 */
	if(i == 5){
	  pab->disp_form[full].width = space_left;
	  pab->disp_form[addr].width = 0;
	}
      }
      else{
	pab->disp_form[full].width = 0;
	pab->disp_form[addr].width = 0;
      }

      dprint((10, "Using %s choice: %d %d %d", enth_string(i+1),
	    pab->disp_form[nick].width, pab->disp_form[full].width,
	    pab->disp_form[addr].width));
    }
    else{  /* non-default case */
      int some_to_calculate = 0;
      int columns = 0;
      int used = 0;
      int avail_screen;
      int all_percents = 1;
      int pc_tot;

      /*
       * First count how many fields there are.
       * Fill in all the Fixed's while we're at it.
       */
      for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++){
	if(pab->disp_form[i].wtype == Fixed){
	  pab->disp_form[i].width = pab->disp_form[i].req_width;
	  all_percents = 0;
	}
	else if(pab->disp_form[i].wtype == WeCalculate){
	  pab->disp_form[i].width = pab->disp_form[i].req_width; /* for now */
	  some_to_calculate++;
	  all_percents = 0;
	}

	if(pab->disp_form[i].wtype != Special){
	  used += pab->disp_form[i].width;
	  columns++;
	}
      }

      used += ((columns-1) * SEP);
      avail_screen = screen_width - used;

      /*
       * Now that we know how much space we've got, we can
       * calculate the Percent columns.
       */
      if(avail_screen > 0){
        for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++){
	  if(pab->disp_form[i].wtype == Percent){
	    /* The 2, 200, and +100 are because we're rounding */
	    pab->disp_form[i].width =
		     ((2*pab->disp_form[i].req_width*avail_screen)+100) / 200;
	    used += pab->disp_form[i].width;
	  }
        }
      }

      space_left = screen_width - used;

      if(space_left < 0){
	/*
	 * If they're all percentages, and the percentages add up to 100,
	 * then we should fix the rounding problem.
	 */
	pc_tot = 0;
	if(all_percents){
          for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++)
	    if(pab->disp_form[i].wtype == Percent)
	      pc_tot += pab->disp_form[i].req_width;
	}

	/* fix the rounding problem */
	if(all_percents && pc_tot <= 100){
	  int col = columns;
	  int this_col = 0;
	  int fix = used - screen_width;

	  while(fix--){
	    if(col < 0)
	      col = columns;

	    /* find col'th column */
	    for(i=0, this_col=0; i < NFIELDS; i++){
	      if(pab->disp_form[i].wtype == Percent){
	        if(col == ++this_col)
	          break;
	      }
	    }

	    pab->disp_form[i].width--;
	    col--;
	  }
	}
	/*
	 * Assume they meant to have them add up to over 100%, so we
	 * just truncate the right hand edge.
	 */
	else{
	  int this_fix, space_over;

	  /* have to reduce space_over down to zero.  */
	  space_over = used - screen_width;
	  for(i=NFIELDS-1; i >= 0 && space_over > 0; i--){
	    if(pab->disp_form[i].type != Notused){
	      this_fix = MIN(pab->disp_form[i].width, space_over);
	      pab->disp_form[i].width -= this_fix;
	      space_over -= this_fix;
	    }
	  }
	}
      }
      else if(space_left > 0){
	if(some_to_calculate){
	  /* make nickname big enough to show all nicknames */
	  if(nick >= 0 && pab->disp_form[nick].wtype == WeCalculate){
	    --some_to_calculate;
	    if(pab->disp_form[nick].width != max_nick){
	      int this_fix;

	      this_fix = MIN(max_nick-pab->disp_form[nick].width, space_left);
	      pab->disp_form[nick].width += this_fix;
	      space_left -= this_fix;
	    }
	  }

	  if(!some_to_calculate && space_left > 0)
	    goto none_to_calculate;

	  if(space_left > 0){
	    int weight  = 0;
	    int used_wt = 0;

	    /* add up total weight */
	    for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++){
	      if(i != nick && pab->disp_form[i].wtype == WeCalculate){
	        switch(pab->disp_form[i].type){
		  case Fullname:
		    weight  += MAX(third_full, pab->disp_form[i].width);
		    used_wt += pab->disp_form[i].width;
		    break;

		  case Addr:
		    weight  += MAX(third_addr, pab->disp_form[i].width);
		    used_wt += pab->disp_form[i].width;
		    break;

		  case Filecopy:
		    weight  += MAX(third_fcc, pab->disp_form[i].width);
		    used_wt += pab->disp_form[i].width;
		    break;

		  default:
		    break;
		}
	      }
	    }

	    if(weight > 0){
	      int this_fix;

	      if(weight - used_wt <= space_left){
	        for(i = 0;
	            i < NFIELDS && pab->disp_form[i].type != Notused;
		    i++){
		  if(i != nick && pab->disp_form[i].wtype == WeCalculate){
		    switch(pab->disp_form[i].type){
		      case Fullname:
		        this_fix = third_full - pab->disp_form[i].width;
		        space_left -= this_fix;
			pab->disp_form[i].width += this_fix;
			break;

		      case Addr:
			this_fix = third_addr - pab->disp_form[i].width;
			space_left -= this_fix;
			pab->disp_form[i].width += this_fix;
			break;

		      case Filecopy:
			this_fix = third_fcc - pab->disp_form[i].width;
			space_left -= this_fix;
			pab->disp_form[i].width += this_fix;
			break;

		      default:
			break;
		    }
		  }
		}

		/* if still space left and a comment field, all to comment */
		if(space_left){
	          for(i = 0;
		      i < NFIELDS && pab->disp_form[i].type != Notused;
		      i++){
		    if(pab->disp_form[i].type == Comment &&
		       pab->disp_form[i].wtype == WeCalculate){
		      pab->disp_form[i].width += space_left;
		      space_left = 0;
		    }
		  }
		}
	      }
	      else{ /* not enough space, dole out weighted pieces */
		int was_sl = space_left;

	        for(i = 0;
		    i < NFIELDS && pab->disp_form[i].type != Notused;
		    i++){
		  if(i != nick && pab->disp_form[i].wtype == WeCalculate){
		    switch(pab->disp_form[i].type){
		      case Fullname:
			/* round down */
			this_fix = (third_full * was_sl)/weight;
			space_left -= this_fix;
			pab->disp_form[i].width += this_fix;
			break;

		      case Addr:
			this_fix = (third_addr * was_sl)/weight;
			space_left -= this_fix;
			pab->disp_form[i].width += this_fix;
			break;

		      case Filecopy:
			this_fix = (third_fcc * was_sl)/weight;
			space_left -= this_fix;
			pab->disp_form[i].width += this_fix;
			break;

		      default:
			break;
		    }
		  }
		}
	      }
	    }

	    /* give out rest */
	    while(space_left > 0){
	      for(i=NFIELDS-1; i >= 0 && space_left > 0; i--){
	        if(i != nick && pab->disp_form[i].wtype == WeCalculate){
		  pab->disp_form[i].width++;
		  space_left--;
		}
	      }
	    }
	  }
	}
	else{
	  /*
	   * If they're all percentages, and the percentages add up to 100,
	   * then we just have to fix a rounding problem.  Otherwise, we'll
	   * assume the user meant to have them add up to less than 100.
	   */
none_to_calculate:
	  pc_tot = 0;
	  if(all_percents){
	    for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++)
	      if(pab->disp_form[i].wtype == Percent)
	        pc_tot += pab->disp_form[i].req_width;
	  }

	  if(all_percents && pc_tot >= 100){
	    int col = columns;
	    int this_col = 0;
	    int fix = screen_width - used;

	    while(fix--){
	      if(col < 0)
	        col = columns;

	      /* find col'th column */
	      for(i=0, this_col=0; i < NFIELDS; i++){
	        if(pab->disp_form[i].wtype == Percent){
	          if(col == ++this_col)
		    break;
		}
	      }

	      pab->disp_form[i].width++;
	      col--;
	    }
	  }
	  /* else, user specified less than 100%, leave it */
	}
      }
      /* else space_left == zero, nothing to do */

      /*
       * Check for special case.  If we find it, this is the case where
       * we want to display the list entry field even though there is no
       * address field displayed.  All of the display width is probably
       * used up by now, so we just need to pick some arbitrary width
       * for these lines.  Since these lines are separate from the other
       * lines we've been calculating, we don't have to worry about running
       * into them, except for list continuation lines which we're not
       * going to worry about.
       */
      for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++){
	if(pab->disp_form[i].wtype == Special){
	    pab->disp_form[i].width = MIN(utf8_width(CLICKHERE), screen_width);
	    break;
	}
      }
    }

    /* check for width changes */
    for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++){
      if(pab->disp_form[i].width != pab->disp_form[i].old_width){
	ret++;  /* Tell the caller the screen changed */
	pab->disp_form[i].old_width = pab->disp_form[i].width;
      }
    }

    pab->nick_is_displayed    = 0;
    pab->full_is_displayed    = 0;
    pab->addr_is_displayed    = 0;
    pab->fcc_is_displayed     = 0;
    pab->comment_is_displayed = 0;
    for(i = 0; i < NFIELDS && pab->disp_form[i].type != Notused; i++){
      if(pab->disp_form[i].width > 0){
        switch(pab->disp_form[i].type){
	  case Nickname:
	    pab->nick_is_displayed++;
	    break;
	  case Fullname:
	    pab->full_is_displayed++;
	    break;
	  case Addr:
	    pab->addr_is_displayed++;
	    break;
	  case Filecopy:
	    pab->fcc_is_displayed++;
	    break;
	  case Comment:
	    pab->comment_is_displayed++;
	    break;
	  default:
	    break;
	}
      }
    }
  }

  if(ret)
    dprint((9, "   some widths changed\n"));

  return(ret);
}


void
redraw_addr_screen(void)
{
    dprint((7, "- redraw_addr_screen -\n"));

    ab_resize();
    if(as.l_p_page <= 0)
      return;

    (void)calculate_field_widths();
    display_book(0, as.cur_row, -1, 1, (Pos *)NULL);
}


/*
 * Little front end for address book screen so it can be called out
 * of the main command loop in alpine.c
 */
void
addr_book_screen(struct pine *pine_state)
{
    dprint((3, "\n\n --- ADDR_BOOK_SCREEN ---\n\n"));

    mailcap_free(); /* free resources we won't be using for a while */

    if(setjmp(addrbook_changed_unexpectedly)){
	/* TRANSLATORS: a warning message telling the user that the address book
	   is being reset (re-sychronized, restarted) */
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_screen!\n"));
	addrbook_reset();
    }

    ab_nesting_level = 1;  /* come here only from main menu */

    /* TRANSLATORS: a screen title for address book screen. Almost all of
       the translatable strings which are all upper case are screen titles. */
    (void)addr_book(AddrBookScreen, _("ADDRESS BOOK"), NULL);
    end_adrbks();

    pine_state->prev_screen = addr_book_screen;
}


void
addr_book_config(struct pine *pine_state, int edit_exceptions)
{
    if(edit_exceptions){
	q_status_message(SM_ORDER, 3, 7,
			 _("Exception Setup not implemented for address books"));
	return;
    }

    dprint((3, "\n\n --- ADDR_BOOK_CONFIG ---\n\n"));

    mailcap_free(); /* free resources we won't be using for a while */

    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_config!\n"));
	addrbook_reset();
    }

    ab_nesting_level = 1;

    /* TRANSLATORS: A screen title */
    (void)addr_book(AddrBookConfig, _("SETUP ADDRESS BOOKS"), NULL);
    end_adrbks();

    pine_state->prev_screen = addr_book_screen;
}


/*
 * Return a single address
 *
 * Returns: pointer to returned address, or NULL if nothing returned
 */
char *
addr_book_oneaddr(void)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "--- addr_book_oneaddr ---\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_oneaddr!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    /* TRANSLATORS: a screen title */
    p = addr_book(SelectAddr, _("SELECT ADDRESS"), NULL);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Return a list of addresses without fullname
 *
 * Returns: pointer to returned address, or NULL if nothing returned
 */
char *
addr_book_multaddr_nf(void)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "--- addr_book_multaddr_nf ---\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_multaddr_nf!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    p = addr_book(SelectMultNoFull, _("SELECT ADDRESS"), NULL);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Return a single address without fullname phrase
 *
 * Returns: pointer to returned address, or NULL if nothing returned
 */
char *
addr_book_oneaddr_nf(void)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "--- addr_book_oneaddr_nf ---\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_oneaddr_nf!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    p = addr_book(SelectAddrNoFull, _("SELECT ADDRESS"), NULL);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Call address book from message composer
 *
 * Args: error_mess -- pointer to return error messages in (unused here)
 *
 * Returns: pointer to returned address, or NULL if nothing returned
 */
char *
addr_book_compose(char **error)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "--- addr_book_compose ---\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_compose!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    /* TRANSLATORS: a screen title */
    p = addr_book(SelectNicksCom, _("COMPOSER: SELECT ADDRESS"), error);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Call address book from message composer for Lcc line
 *
 * Args: error_mess -- pointer to return error messages in (unused here)
 *
 * Returns: pointer to returned address, or NULL if nothing returned
 */
char *
addr_book_compose_lcc(char **error)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "--- addr_book_compose_lcc ---\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_compose_lcc!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    /*
     * We used to use SelectAddrLccCom here but decided it wasn't necessary
     * to restrict the selection to a list.
     */
    /* TRANSLATORS: a screen title, user is composing a message and should select
       a distribution list from a list of addresses in the address book. */
    p = addr_book(SelectNicksCom, _("COMPOSER: SELECT LIST"), error);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Call address book from message composer for Lcc line
 *
 * Args: error_mess -- pointer to return error messages in (unused here)
 *
 * Returns: pointer to returned address, or NULL if nothing returned
 */
char *
addr_book_change_list(char **error)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "--- addr_book_change_list ---\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book... addr_book_change_list!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    /* TRANSLATORS: a screen title */
    p = addr_book(SelectNicksCom, _("ADDRESS BOOK (Update): SELECT ADDRESSES"),
		  error);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Call address book from pine_simple_send
 *
 * Returns: pointer to returned address, or NULL if nothing returned
 */
char *
addr_book_bounce(void)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "- addr_book_bounce -\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book...addr_book_bounce!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    p = addr_book(SelectManyNicks, _("SELECT ADDRESSES"), NULL);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Call address book from take address screen
 *
 * Returns: pointer to returned nickname, or NULL if nothing returned
 */
char *
addr_book_takeaddr(void)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "- addr_book_takeaddr -\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book...addr_book_takeaddr!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    /* TRANSLATORS: a screen title */
    p = addr_book(SelectNickTake, _("TAKEADDR: SELECT NICKNAME"), NULL);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Call address book from editing screen for nickname field.
 *
 * Returns: pointer to returned nickname, or NULL if nothing returned
 */
char *
addr_book_nick_for_edit(char **error)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;
    int     save_n_serv;

    dprint((3, "- addr_book_nick_for_edit -\n"));

    save_n_serv = as.n_serv;

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book...addr_book_nick_for_edit!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    /*
     * This is kind of hoaky. We want to prevent the Directory Query
     * options from turning on when we're coming in looking for a nickname
     * and this seemed to be the easiest way to accomplish that.
     */
    as.n_serv = 0;
    /* TRANSLATORS: a screen title, user selecting a nickname from the address book */
    p = addr_book(SelectNickCom, _("SELECT NICKNAME"), error);
    as.n_serv = save_n_serv;

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 * Call address book for generic nickname select
 *
 * Returns: pointer to returned nickname, or NULL if nothing returned
 */
char *
addr_book_selnick(void)
{
    char   *p;
    jmp_buf save_jmp_buf;
    int    *save_nesting_level;

    dprint((3, "- addr_book_selnick -\n"));

    save_nesting_level = cpyint(ab_nesting_level);
    memcpy(save_jmp_buf, addrbook_changed_unexpectedly, sizeof(jmp_buf));
    if(setjmp(addrbook_changed_unexpectedly)){
	q_status_message(SM_ORDER, 5, 10, _("Resetting address book..."));
	dprint((1, "RESETTING address book...addr_book_selnick!\n"));
	addrbook_reset();
	ab_nesting_level = *save_nesting_level;
    }

    ab_nesting_level++;

    p = addr_book(SelectNick, _("SELECT NICKNAME"), NULL);

    if(ab_nesting_level <= 1)
      end_adrbks();
    else
      ab_nesting_level--;

    memcpy(addrbook_changed_unexpectedly, save_jmp_buf, sizeof(jmp_buf));
    if(save_nesting_level)
      fs_give((void **)&save_nesting_level);

    return(p);
}


/*
 *  Main address book screen 
 *
 * Control loop for address book.  Commands are executed out of a big
 * switch and screen painting is done.
 * The style argument controls whether or not it is called to return an address
 * to the composer, a nickname to the TakeAddr screen, or just for address
 * book maintenance.
 *
 * Args: style -- how we were called
 *
 * Return: might return a string for the composer to edit, or a nickname, ...
 */
char *
addr_book(AddrBookArg style, char *title, char **error_message)
{
    UCS              c;
    int		     cmd, r, i,
		     command_line,
		     did_delete,
                     quit,           /* loop control                         */
                     km_popped,      /* menu is popped up in blank menu mode */
		     current_changed_flag,  /* only current row needs update */
		     was_clickable, is_clickable,
		     was_collapsible_listent, is_collapsible_listent,
		     was_global_config, is_global_config,
		     was_addkey, is_addkey,
		     was_opened, is_opened,
		     was_custom_title, is_custom_title,
		     setall_changed,
		     start_disp,     /* Paint from this line down (0 is top) */
		     rdonly,         /* cur addrbook read only               */
		     empty,          /* cur addrbook empty                   */
		     are_selecting,  /* called as ^T selector                */
#ifdef	ENABLE_LDAP
		     directory_ok,   /* called from composer, not Lcc        */
#endif
		     from_composer,  /* from composer                        */
		     listmode_ok,    /* ok to do ListMode with this style    */
		     selecting_one_nick,
		     selecting_mult_nicks,
		     checkedn,       /* how many are checked                 */
		     def_key,        /* default key                          */
                     warped;         /* we warped through hyperspace to a
				        new location in the display list     */
    long	     fl,
		     new_top_ent,    /* entry on top of screen after oper    */
		     new_line;       /* new line number after operation      */
    char            *addr;
    char            *utf8str;
    bitmap_t         bitmap;
    struct key_menu *km;
    OtherMenu        what;
    PerAddrBook     *pab = NULL;
    AddrScrn_Disp   *dl;
    struct pine     *ps;
    Pos              cursor_pos;

    dprint((2, "--- addr_book ---  (%s)\n",
	       style==AddrBookScreen      ? "AddrBookScreen"		:
		style==SelectAddrLccCom    ? "SelectAddrLccCom"		:
		 style==SelectNicksCom      ? "SelectNicksCom"		:
		  style==SelectNick          ? "SelectNick"		:
		   style==SelectNickTake      ? "SelectNickTake"	:
		    style==SelectNickCom       ? "SelectNickCom"	:
		     style==AddrBookConfig      ? "AddrBookConfig"	:
		      style==SelectManyNicks     ? "SelectManyNicks"	:
		       style==SelectAddr          ? "SelectAddr"	:
		        style==SelectAddrNoFull    ? "SelectAddrNoFull"	:
		         style==SelectMultNoFull    ? "SelectMultNoFull":
			                              "UnknownStyle"));

    km = &ab_keymenu;
    ps = ps_global;
    ps->next_screen = SCREEN_FUN_NULL;

    from_composer  = (style == SelectAddrLccCom
		      || style == SelectNicksCom
		      || style == SelectNickCom);
    are_selecting  = (style != AddrBookScreen
		      && style != AddrBookConfig);
    selecting_one_nick = (style == SelectNick
			  || style == SelectNickTake
		          || style == SelectNickCom);
    selecting_mult_nicks = (style == SelectAddrLccCom
			    || style == SelectNicksCom
			    || style == SelectManyNicks);
    listmode_ok    = (style == SelectAddrLccCom
		      || style == SelectNicksCom
		      || style == SelectManyNicks
		      || style == SelectMultNoFull);
    as.config      = (style == AddrBookConfig);
#ifdef	ENABLE_LDAP
    directory_ok   = (style == SelectNicksCom
		      || style == AddrBookScreen
		      || style == SelectManyNicks);
#endif

    /* Coming in from the composer, may need to reset the window */
    if(from_composer){
	fix_windsize(ps);
	init_sigwinch();
	mark_status_dirty();
	mark_titlebar_dirty();
	mark_keymenu_dirty();
    }

    command_line = -FOOTER_ROWS(ps); /* third line from the bottom */
    what         = FirstMenu;
    c = 'x'; /* For display_message the first time through */

    if(ps->remote_abook_validity > 0)
      (void)adrbk_check_and_fix_all(ab_nesting_level == 1, 0, 0);

    if(!init_addrbooks(HalfOpen, 1, !as.config, !are_selecting)){
	if(are_selecting){
	    q_status_message(SM_ORDER | SM_DING, 0, 4,
			     _("No Address Book Configured"));
	    display_message(c);
	    sleep(2);
	    return NULL;
	}
	else if(!as.config){
            ps->next_screen = main_menu_screen;
	    q_status_message(SM_ORDER | SM_DING, 3, 4,
		    _("No Address Book Configured, Use SETUP Addressbook screen"));
	    ps->mangled_screen = 1;
	    return NULL;
	}
    }
    else if(style == AddrBookScreen && as.n_addrbk == 1 && as.n_serv == 0){
	if(as.adrbks[0].access == ReadOnly)
	  q_status_message(SM_ORDER, 0, 4, _("AddressBook is Read Only"));
	else if(as.adrbks[0].access == NoAccess)
	  q_status_message(SM_ORDER, 0, 4,
			   _("AddressBook not accessible, permission denied"));
    }

    if(as.l_p_page < 1){
	q_status_message(SM_ORDER, 3, 3,
			 _("Screen too small to use Address book"));
	return NULL;
    }

    erase_checks();
    as.selections = 0;
    as.zoomed = 0;

    (void) calculate_field_widths();

    quit			= 0;
    km_popped			= 0;
    ps->mangled_screen		= 1;
    current_changed_flag	= 0;
    start_disp			= 0;
    was_clickable		= 0;
    is_clickable		= 0;
    was_collapsible_listent	= 0;
    is_collapsible_listent	= 0;
    was_global_config		= 0;
    is_global_config		= 0;
    was_addkey			= 0;
    is_addkey			= 0;
    was_opened			= 0;
    is_opened			= 0;
    was_custom_title		= 0;
    is_custom_title		= 0;
    setall_changed		= 0;
    checkedn			= 0;


    while(!quit){
	ps->user_says_cancel = 0;
	if(km_popped){
	    km_popped--;
	    if(km_popped == 0){
		clearfooter(ps);
		/*
		 * Have to repaint from earliest change down, including
		 * at least the last two body lines.
		 */
		if(ps->mangled_body) /* it was already mangled */
		  start_disp = MIN(start_disp, as.l_p_page -2);
		else if(current_changed_flag){
		    ps->mangled_body = 1;
		    start_disp = MIN(MIN(as.cur_row, as.l_p_page -2),
					as.old_cur_row);
		}
		else{
		    ps->mangled_body = 1;
		    start_disp = as.l_p_page -2;
		}
	    }
	}

	ps->redrawer = redraw_addr_screen;

        if(new_mail(0, NM_TIMING(c), NM_STATUS_MSG | NM_DEFER_SORT) >= 0)
	  ps->mangled_header = 1;
	
        if(streams_died())
          ps->mangled_header = 1;

	if(ps->mangled_screen){
	    ps->mangled_header   = 1;
	    ps->mangled_body     = 1;
	    ps->mangled_footer   = 1;
	    start_disp           = 0;
	    current_changed_flag = 0;
	    ps->mangled_screen   = 0;
	}

	if(ps->mangled_body){

	    if(calculate_field_widths())
	      start_disp = 0;

	    display_book(start_disp,
			 as.cur_row,  
			 as.old_cur_row,  
			 1,
			 &cursor_pos);

	    as.old_cur_row   = as.cur_row;
	    ps->mangled_body = 0;
            start_disp       = 0;
	    as.cur           = cur_addr_book();
	    pab              = (as.n_addrbk &&
				as.cur >= 0 && as.cur < as.n_addrbk &&
			        !entry_is_askserver(as.top_ent+as.cur_row))
				    ? &as.adrbks[as.cur] : NULL;

#ifdef	_WINDOWS
	    {
		long i, last_sel;

		for(i = as.top_ent; dlist(i)->type != End; i++)
		  next_selectable_line(i,&last_sel);
		as.last_ent = i;

		scroll_setrange(as.l_p_page, last_sel);
	    }
#endif
	}
	/* current entry has been changed */
	else if(current_changed_flag){
	    int need_redraw;

	    need_redraw = calculate_field_widths();

	    /*---------- Update the current entry, (move or change) -------*/
	    display_book(need_redraw ? 0 : as.cur_row,
			 as.cur_row,  
			 as.old_cur_row,  
			 need_redraw,
			 &cursor_pos);

	    as.old_cur_row       = as.cur_row;
	    current_changed_flag = 0;
	    as.cur               = cur_addr_book();
	    pab                  = (as.n_addrbk &&
				    as.cur >= 0 && as.cur < as.n_addrbk &&
				    !entry_is_askserver(as.top_ent+as.cur_row))
					? &as.adrbks[as.cur] : NULL;
        }

	is_custom_title = (F_OFF(F_CMBND_ABOOK_DISP,ps_global) &&
			   style == AddrBookScreen &&
			   as.n_addrbk > 1 &&
			   cur_is_open() &&
			   pab->abnick &&
			   pab->abnick[0]);
	if(( was_custom_title && !is_custom_title) ||
	   (!was_custom_title &&  is_custom_title)){
	    ps->mangled_header = 1;
	    was_custom_title = is_custom_title;
	}


	if(ps->mangled_header){
	    char buf[80], *bp;

	    if(style == AddrBookScreen){
		if(F_ON(F_CMBND_ABOOK_DISP,ps_global)){
		  if(as.n_addrbk > 1)
		    snprintf(buf, sizeof(buf), _("ADDRESS BOOKS"));
		  else
		    snprintf(buf, sizeof(buf), _("ADDRESS BOOK"));
		}
		else
		  /* TRANSLATORS: a screen title, the %s arguments are for a custom part
		     of the title. Move them as a group. There are two normal cases.
		     Either ADDRESS BOOK LIST when a list of names of address books is
		     displayed, or ADDRESS BOOK <name of address book> when one is open. */
		  snprintf(buf, sizeof(buf), _("ADDRESS BOOK%s%s%s"),
			  /* TRANSLATORS: This is the LIST that goes with title above */
			  is_custom_title ? " <" : cur_is_open() ? "" : _(" LIST"),
		          is_custom_title ? pab->abnick : "",
		          is_custom_title ? ">"  : "");

		buf[sizeof(buf)-1] = '\0';

		bp = buf;
	    }
	    else
	      bp = title;

            set_titlebar(bp, ps->mail_stream,
                         ps->context_current, ps->cur_folder,
                         ps->msgmap, 1,
                         FolderName, 0, 0, NULL);
	    ps->mangled_header = 0;
	}

	dprint((9, "addr_book: top of loop, addrbk %d top_ent %ld cur_row %d\n", as.cur, as.top_ent, as.cur_row));

	/*
	 * This is a check to catch when a move from one row to another
	 * should cause the list of commands to change.
	 */
	is_clickable           = entry_is_clickable(as.top_ent+as.cur_row);
	is_collapsible_listent = F_OFF(F_EXPANDED_DISTLISTS,ps) &&
				 entry_is_listent(as.top_ent+as.cur_row);
	is_global_config       = as.config && pab && pab->type & GLOBAL;
	is_addkey              = entry_is_addkey(as.top_ent+as.cur_row);
	is_opened              = (F_OFF(F_CMBND_ABOOK_DISP,ps_global) &&
				  cur_is_open());
	if(( was_clickable && !is_clickable)				||
	   (!was_clickable &&  is_clickable)				||
	   ( was_collapsible_listent && !is_collapsible_listent)	||
	   (!was_collapsible_listent &&  is_collapsible_listent)	||
	   ( was_global_config && !is_global_config)			||
	   (!was_global_config &&  is_global_config)			||
	   ( was_addkey && !is_addkey)					||
	   (!was_addkey &&  is_addkey)					||
	   ( was_opened && !is_opened)					||
	   (!was_opened &&  is_opened)					||
	   ( setall_changed)){

	    ps->mangled_footer = 1;
	    was_clickable           = is_clickable;
	    was_collapsible_listent = is_collapsible_listent;
	    was_global_config       = is_global_config;
	    was_addkey              = is_addkey;
	    was_opened              = is_opened;
	    setall_changed          = 0;
	}


        if(ps->mangled_footer){

	    setbitmap(bitmap);
	    menu_clear_binding(km, '>');
	    menu_clear_binding(km, '.');
	    menu_clear_binding(km, KEY_RIGHT);
	    menu_clear_binding(km, ctrl('M'));
	    menu_clear_binding(km, ctrl('J'));
	    menu_clear_binding(km, KEY_LEFT);
	    menu_clear_binding(km, '<');
	    menu_clear_binding(km, ',');
	    def_key = THREE_KEY;	/* default default key */
	    if(as.config){
		km->how_many = 1;

		clrbitn(OTHER_KEY, bitmap);
		/* TRANSLATORS: a command name for a particular key */
		menu_init_binding(km, 'E', MC_EXIT, "E", N_("Exit Setup"), TWO_KEY);
		KS_OSDATASET(&km->keys[TWO_KEY], KS_EXITMODE);

		/*
		 * Don't show Delete or Shuffle command if there is nothing
		 * to delete or shuffle.
		 */
		if(is_addkey){
		    clrbitn(DELETE_KEY, bitmap);
		    clrbitn(SENDTO_KEY, bitmap);
		    clrbitn(THREE_KEY, bitmap);
		    menu_init_binding(km, 'A', MC_ADDABOOK, "A",
				      add_is_global(as.top_ent+as.cur_row)
					/* TRANSLATORS: Add Global Address book (one defined by someone else) */
					? "[" N_("Add Glob Abook") "]"
					/* TRANSLATORS: Add Personal Address book */
					: "[" N_("Add Pers Abook") "]",
				      ADD_KEY);
		    def_key = ADD_KEY;
		}
		else{
		    /* TRANSLATORS: Delete Address book command */
		    menu_init_binding(km, 'D', MC_DELABOOK, "D", N_("Del Abook"),
				      DELETE_KEY);
		    /* TRANSLATORS: Shuffle refers to shuffling the order of things,
		       that is, changing the order */
		    menu_init_binding(km, '$', MC_SHUFFLE, "$", N_("Shuffle"),
				      SENDTO_KEY);
		    /* TRANSLATORS: Change is a command meaning Change some item,
		       or Edit some item to change it */
		    menu_init_binding(km, 'C', MC_EDITABOOK, "C", "[" N_("Change") "]",
				      THREE_KEY);
		    menu_init_binding(km, 'A', MC_ADDABOOK, "A",
				      add_is_global(as.top_ent+as.cur_row)
					? N_("Add Glob Abook")
					: N_("Add Pers Abook"),
				      ADD_KEY);
		}
	    }
	    else if(are_selecting){
		km->how_many = 1;

		/*
		 * The OTHER_KEY is used as the Exit key in selection mode.
		 * This is because the TWO_KEY is being used for < actions.
		 */
		/* TRANSLATORS: Command to Exit the Select screen. This would
		   be a way to make no Selection and go back to where you
		   came from. */
		menu_init_binding(km, 'E', MC_EXIT, "E", N_("ExitSelect"),
				  OTHER_KEY);
		KS_OSDATASET(&km->keys[OTHER_KEY], KS_EXITMODE);

		/*
		 * Use the TWO_KEY for the go back key.
		 */
		if(cur_is_open() && (as.n_addrbk > 1 || as.n_serv)){
		    if(F_OFF(F_EXPANDED_DISTLISTS,ps) &&
		       entry_is_listent(as.top_ent+as.cur_row))
		      cmd = MC_UNEXPAND;
		    else if(F_OFF(F_CMBND_ABOOK_DISP,ps_global))
		      cmd = MC_POPUP;
		    else
		      cmd = MC_NONE;

		    if(cmd == MC_NONE)
		      clrbitn(TWO_KEY, bitmap);
		    else{
			menu_init_binding(km, '<', cmd, "<",
					  cmd == MC_POPUP ? N_("AddressBkList")
					/* TRANSLATORS: Unexpand is the opposite of Expand.
					   We might expand an address book distribution
					   list to see all the members of the list. */
							  : N_("Unexpand"),
					  TWO_KEY);
			menu_add_binding(km, ',', cmd);
			if(F_ON(F_ARROW_NAV,ps))
			  menu_add_binding(km, KEY_LEFT, cmd);
		    }
		}
		else if(as.checkboxes && (as.n_addrbk > 1 || as.n_serv)){
		    if(checkedn){
			if(entry_is_clickable_title(as.top_ent+as.cur_row)){
			    menu_init_binding(km, 'S', MC_CHOICE, "S",
					      /* TRANSLATORS: Select something, choose something */
					      N_("Select"), TWO_KEY);
			}
			else{
			    menu_init_binding(km, 'S', MC_CHOICE, "S",
					      "[" N_("Select") "]", TWO_KEY);
			    def_key = TWO_KEY;
			}
		    }
		    else
		      menu_init_binding(km, 'S', MC_CHOICE, "S", N_("Select"),
				        TWO_KEY);
		}
		else
		  clrbitn(TWO_KEY, bitmap);

		/*
		 * The THREE_KEY is used as the select key in selection mode,
		 * but it doesn't show up at the top-level. Instead, the
		 * key becomes the ViewAbook key.
		 */
		if(entry_is_askserver(as.top_ent+as.cur_row) && !as.checkboxes){
		    menu_init_binding(km, '>', MC_QUERY_SERV, ">", "[" N_("Search") "]",
				      THREE_KEY);
		    menu_add_binding(km, 's', MC_QUERY_SERV);
		    menu_add_binding(km, '.', MC_QUERY_SERV);
		    if(F_ON(F_ARROW_NAV,ps))
		      menu_add_binding(km, KEY_RIGHT, MC_QUERY_SERV);
		}
		else if(entry_is_clickable_title(as.top_ent+as.cur_row)){
		    /* TRANSLATORS: View this address book */
		    menu_init_binding(km, '>', MC_OPENABOOK, ">", "[" N_("ViewAbook") "]",
				      THREE_KEY);
		    menu_add_binding(km, 'v', MC_OPENABOOK);
		    menu_add_binding(km, '.', MC_OPENABOOK);
		    if(F_ON(F_ARROW_NAV,ps))
		      menu_add_binding(km, KEY_RIGHT, MC_OPENABOOK);
		}
		else if(cur_is_open()){
		    menu_init_binding(km, 'S', MC_CHOICE, "S", "[" N_("Select") "]",
				      THREE_KEY);
		}
		else
		  clrbitn(THREE_KEY, bitmap);

		KS_OSDATASET(&km->keys[THREE_KEY], KS_NONE);

		/*
		 * The Expand command gets stuck out in right field.
		 */
		if(entry_is_clickable(as.top_ent+as.cur_row) &&
		   !entry_is_clickable_title(as.top_ent+as.cur_row)){
		    menu_init_binding(km, '>', MC_EXPAND, ">", N_("Expand"),
				      SENDTO_KEY);
		    menu_add_binding(km, '.', MC_EXPAND);
		    if(F_ON(F_ARROW_NAV,ps))
		      menu_add_binding(km, KEY_RIGHT, MC_EXPAND);
		}
		else
		  clrbitn(SENDTO_KEY, bitmap);

		if(cur_is_open() && as.checkboxes){
		    /* TRANSLATORS: Set/Unset means that this particular command
		       will toggle between setting something (turning it on) and
		       unsetting it (turning it off). For example, it might be
		       a program option that can be turned on or off or it might
		       be a way to mark which addresses to send a message to. */
		    menu_init_binding(km, 'X', MC_TOGGLE, "X", N_("Set/Unset"),
				      DELETE_KEY);

		}
		else if(cur_is_open() && listmode_ok){
		    /* TRANSLATORS: List mode is a type of screen in pine that
		       allows the user to select several of something. This is
		       the name of the command to go into the List mode style
		       of operating. */
		    menu_init_binding(km, 'L', MC_LISTMODE, "L", N_("ListMode"),
				      DELETE_KEY);
		}
		else
		  clrbitn(DELETE_KEY, bitmap);

		if(cur_is_open() && as.checkboxes){
		    menu_init_binding(km, 'A', MC_SELALL, "A",
				      /* TRANSLATORS: when selecting from a list of items
				         the unsetall (unset all) means to start over
					 with nothing selected.
					 The set all command means select everything
					 in the list. */
				      checkedn ? N_("unsetAll") : N_("setAll"),
				      ADD_KEY);
		}
		else
		  clrbitn(ADD_KEY, bitmap);

		KS_OSDATASET(&km->keys[DELETE_KEY], KS_NONE);
	    }
	    else{
		/*
		 * Reset first Other key. Selection screen may have
		 * blasted it. Do this by hand because menu_init_binding
		 * will remove the other two OTHER CMDS bindings.
		 * Should figure out how to do this correctly with a
		 * reasonable function call.
		 */
		km->keys[OTHER_KEY].name       = "O";
		/* TRANSLATORS: This is the name of the command that will show
		   which other commands are available. 12 commands are shown at
		   the bottom of the screen, this command would show the next set
		   of 12 */
		km->keys[OTHER_KEY].label      = N_("OTHER CMDS");
		km->keys[OTHER_KEY].bind.cmd   = MC_OTHER;
		km->keys[OTHER_KEY].bind.ch[0] = 'O';
		km->keys[OTHER_KEY].bind.nch   = 1;
		KS_OSDATASET(&km->keys[OTHER_KEY], KS_NONE);

		km->how_many = 2;

		if(F_ON(F_CMBND_ABOOK_DISP,ps_global)){
		    if(F_ON(F_ENABLE_AGG_OPS, ps))
		      km->how_many = 3;

		    /*
		     * The TWO_KEY is used as the go back key in
		     * non-selection mode.
		     */
		    if(F_OFF(F_EXPANDED_DISTLISTS,ps) &&
		       entry_is_listent(as.top_ent+as.cur_row)){
			cmd = MC_UNEXPAND;
			menu_init_binding(km, '<', cmd, "<", N_("Unexpand"),
					  TWO_KEY);
			KS_OSDATASET(&km->keys[TWO_KEY], KS_NONE);
		    }
		    else{
			cmd = MC_MAIN;
			menu_init_binding(km, 'M', cmd, "<", N_("Main Menu"),
					  TWO_KEY);
			KS_OSDATASET(&km->keys[TWO_KEY], KS_MAINMENU);
		    }

		    if(cur_is_open()){
			/*
			 * Add or delete entries from this address book.
			 */
			/* TRANSLATORS: Add a new entry (this is a command) */
			menu_init_binding(km, '@', MC_ADD, "@", N_("AddNew"),
					  ADD_KEY);
			menu_init_binding(km, 'D', MC_DELETE, "D", N_("Delete"),
					  DELETE_KEY);
			/* TRANSLATORS: Compose a message to be sent to the current address
			   book entry */
			menu_init_binding(km, 'C', MC_COMPOSE, "C", N_("ComposeTo"),
					  SENDTO_KEY);
			KS_OSDATASET(&km->keys[SENDTO_KEY], KS_COMPOSER);
			menu_init_binding(km, '#', MC_ROLE, "#", N_("Role"),
					  RCOMPOSE_KEY);
		    }
		    else{
			clrbitn(ADD_KEY, bitmap);
			clrbitn(DELETE_KEY, bitmap);
			clrbitn(SENDTO_KEY, bitmap);
			clrbitn(RCOMPOSE_KEY, bitmap);
			clrbitn(SAVE_KEY, bitmap);
			clrbitn(TAKE_KEY, bitmap);
			clrbitn(FORW_KEY, bitmap);
		    }

		    clrbitn(SECONDARY_MAIN_KEY, bitmap);
		}
		else if(cur_is_open()){
		    if(F_ON(F_ENABLE_AGG_OPS, ps))
		      km->how_many = 3;

		    /*
		     * The TWO_KEY is used as the go back key in
		     * non-selection mode.
		     */
		    if(F_OFF(F_EXPANDED_DISTLISTS,ps) &&
		       entry_is_listent(as.top_ent+as.cur_row)){
			cmd = MC_UNEXPAND;
			menu_init_binding(km, '<', cmd, "<", N_("Unexpand"),
					  TWO_KEY);
			KS_OSDATASET(&km->keys[TWO_KEY], KS_NONE);
		    }
		    else{
			if(as.n_addrbk > 1 || as.n_serv){
			    cmd = MC_POPUP;
			    menu_init_binding(km, '<', cmd, "<",
					      N_("AddressBkList"), TWO_KEY);
			    KS_OSDATASET(&km->keys[TWO_KEY], KS_NONE);
			}
			else{
			    cmd = MC_MAIN;
			    menu_init_binding(km, 'M', cmd, "<", N_("Main Menu"),
					      TWO_KEY);
			    KS_OSDATASET(&km->keys[TWO_KEY], KS_MAINMENU);
			}
		    }

		    if(pab->access != NoAccess){
			/*
			 * Add or delete entries from this address book.
			 */
			menu_init_binding(km, '@', MC_ADD, "@", N_("AddNew"),
					  ADD_KEY);
			menu_init_binding(km, 'D', MC_DELETE, "D", N_("Delete"),
					  DELETE_KEY);
		    }
		    else{
			clrbitn(ADD_KEY, bitmap);
			clrbitn(DELETE_KEY, bitmap);
		    }

		    /* Find someplace to put Main Menu command */
		    if(cmd == MC_POPUP){
			menu_init_binding(km, 'M', MC_MAIN, "M", N_("Main Menu"),
					  SECONDARY_MAIN_KEY);
			KS_OSDATASET(&km->keys[SECONDARY_MAIN_KEY],KS_MAINMENU);
		    }
		    else
		      clrbitn(SECONDARY_MAIN_KEY, bitmap);

		    menu_init_binding(km, 'C', MC_COMPOSE, "C", N_("ComposeTo"),
				      SENDTO_KEY);
		    KS_OSDATASET(&km->keys[SENDTO_KEY], KS_COMPOSER);
		    menu_init_binding(km, '#', MC_ROLE, "#", N_("Role"),
				      RCOMPOSE_KEY);
		}
		else{
		    /*
		     * The TWO_KEY is used as the go back key in
		     * non-selection mode.
		     */
		    cmd = MC_MAIN;
		    menu_init_binding(km, 'M', cmd, "<", N_("Main Menu"),
				      TWO_KEY);
		    KS_OSDATASET(&km->keys[TWO_KEY], KS_MAINMENU);

		    clrbitn(SENDTO_KEY, bitmap);
		    clrbitn(RCOMPOSE_KEY, bitmap);
		    clrbitn(ADD_KEY, bitmap);
		    clrbitn(DELETE_KEY, bitmap);
		    clrbitn(SECONDARY_MAIN_KEY, bitmap);
		    clrbitn(SAVE_KEY, bitmap);
		    clrbitn(TAKE_KEY, bitmap);
		    clrbitn(FORW_KEY, bitmap);
		}

		/* can't be on third menu if we just reduced to 2 */
		if(km->how_many == 2 && km->which == 2)
		  km->which = 0;

		menu_add_binding(km, '<', cmd);
		menu_add_binding(km, ',', cmd);
		if(F_ON(F_ARROW_NAV,ps))
		  menu_add_binding(km, KEY_LEFT, cmd);

		KS_OSDATASET(&km->keys[DELETE_KEY], KS_DELETE);

		/*
		 * The THREE_KEY is the burrow into the hierarchy key.
		 */
		if(entry_is_askserver(as.top_ent+as.cur_row))
		  cmd = MC_QUERY_SERV;
		else if(entry_is_clickable(as.top_ent+as.cur_row)){
		    if(entry_is_clickable_title(as.top_ent+as.cur_row))
		      cmd = MC_OPENABOOK;
		    else
		      cmd = MC_EXPAND;
		}
		else
		  cmd = MC_VIEW_ENTRY;

		menu_init_binding(km, '>', cmd, ">",
				  cmd == MC_EXPAND ? "[" N_("Expand") "]" :
				    cmd == MC_QUERY_SERV ? "[" N_("Search") "]" :
				      /* TRANSLATORS: In the address book the user can
				         view a particular address book entry. It is
					 called View/Update because the way to update
					 an entry is to first view it and then there
					 will be an opportunity to update it from there. */
				      cur_is_open() ? "[" N_("View/Update") "]"
						    : "[" N_("ViewAbook") "]",
				  THREE_KEY);

		if(cmd == MC_QUERY_SERV)
		  menu_add_binding(km, 's', cmd);
		else if(cmd == MC_OPENABOOK || cmd == MC_VIEW_ENTRY)
		  menu_add_binding(km, 'v', cmd);

		menu_add_binding(km, '.', cmd);
		if(F_ON(F_ARROW_NAV,ps))
		  menu_add_binding(km, KEY_RIGHT, cmd);
	    }

	    menu_add_binding(km, ctrl('M'), km->keys[def_key].bind.cmd);
	    menu_add_binding(km, ctrl('J'), km->keys[def_key].bind.cmd);

	    if(km_popped){
		FOOTER_ROWS(ps) = 3;
		clearfooter(ps);
	    }

	    draw_keymenu(km, bitmap, ps->ttyo->screen_cols,
			 1-FOOTER_ROWS(ps), 0, what);
	    ps->mangled_footer = 0;
	    what	       = SameMenu;
	    if(km_popped){
		FOOTER_ROWS(ps) = 1;
		mark_keymenu_dirty();
	    }
	}

	rdonly   = (pab && pab->access == ReadOnly);
	empty    = is_empty(as.cur_row+as.top_ent);

	/*------------ display any status messages ------------------*/
	if(km_popped){
	    FOOTER_ROWS(ps) = 3;
	    mark_status_unknown();
	}

	display_message(c);
	if(km_popped){
	    FOOTER_ROWS(ps) = 1;
	    mark_status_unknown();
	}

	if(F_OFF(F_SHOW_CURSOR, ps)){
	    /* reset each time through to catch screen size changes */
	    cursor_pos.row = ps->ttyo->screen_rows-FOOTER_ROWS(ps);
	    cursor_pos.col = 0;
	}

	MoveCursor(cursor_pos.row, cursor_pos.col);


	/*---------------- Get command and validate -------------------*/
#ifdef	MOUSE
	mouse_in_content(KEY_MOUSE, -1, -1, 0, 0);
	register_mfunc(mouse_in_content, HEADER_ROWS(ps), 0,
		       ps->ttyo->screen_rows-(FOOTER_ROWS(ps)+1),
		       ps->ttyo->screen_cols);
#endif

	/* sort out what help needs to be displayed if asked for */
	if(as.config)
	  gAbookHelp = h_abook_config;
	else if(are_selecting){
	    if(cur_is_open()){
		if(style == SelectNickTake)
		  gAbookHelp = h_abook_select_nicks_take;
		else if(selecting_one_nick)
		  gAbookHelp = h_abook_select_nick;
		else if(as.checkboxes)
		  gAbookHelp = h_abook_select_checks;
		else if(listmode_ok)
		  gAbookHelp = h_abook_select_listmode;
		else
		  gAbookHelp = h_abook_select_addr;
	    }
	    else
	      gAbookHelp = h_abook_select_top;
	}
	else
	  gAbookHelp = cur_is_open() ? h_abook_opened : h_abook_top;

#ifdef	_WINDOWS
	mswin_setscrollcallback(addr_scroll_callback);
	mswin_sethelptextcallback(pcpine_help_addrbook);
#endif
	c = READ_COMMAND(&utf8str);
#ifdef	MOUSE
	clear_mfunc(mouse_in_content);
#endif
#ifdef	_WINDOWS
	mswin_setscrollcallback(NULL);
	mswin_sethelptextcallback(NULL);
#endif
	cmd = menu_command(c, km);

        dprint((2, "Addrbook command: %d (0x%x %s)\n", cmd,
		   c, pretty_command(c)));

	/* this may be a safe place to update addrbooks */
	if(ps->remote_abook_validity > 0 &&
	   adrbk_check_and_fix_all(ab_nesting_level < 2 &&
				   !any_ab_open() && checkedn == 0, 0, 0))
	  ps->mangled_footer = 1;

	if(km_popped)
	  switch(cmd){
	    case MC_NONE:
	    case MC_OTHER:
	    case MC_RESIZE:
	    case MC_REPAINT:
	      km_popped++;
	      break;
	    
	    default:
	      clearfooter(ps);
	      break;
	  }

	/*------------- execute command ----------------*/
	switch(cmd){

            /*------------ Noop   (new mail check) --------------*/
	  case MC_NONE:
	    break;


            /*----------- Help -------------------*/
	  case MC_HELP:
	    if(FOOTER_ROWS(ps) == 1 && km_popped == 0){
		km_popped = 2;
		ps->mangled_footer = 1;
		break;
	    }

	    if(as.config)
	      helper(gAbookHelp, _("HELP ON CONFIGURING ADDRESS BOOKS"),
		     HLPD_NONE);
	    else if(are_selecting)
	      helper(gAbookHelp, _("HELP ON ADDRESS BOOK"),
		     HLPD_SIMPLE | HLPD_NEWWIN);
	    else	/* general maintenance screen */
	      helper(gAbookHelp, _("HELP ON ADDRESS BOOK"), HLPD_NONE);

	    /*
	     * Helper() may have a Main Menu key. If user types that
	     * they'll set next_screen. We don't have to do anything
	     * special but we want to make sure that that doesn't happen
	     * when we're selecting, even though it shouldn't be
	     * possible because HLPD_SIMPLE is set.
	     */
	    if(are_selecting)
	      ps->next_screen = SCREEN_FUN_NULL;  /* probably not needed */

	    ps->mangled_screen = 1;
	    break;

             
            /*---------- display other key bindings ------*/
          case MC_OTHER:
	    warn_other_cmds();
	    what = NextMenu;
	    ps->mangled_footer = 1;
	    break;


            /*------------ Unexpand list -----------------*/
          case MC_UNEXPAND:
	    if(F_OFF(F_EXPANDED_DISTLISTS,ps) &&
	       entry_is_listent(as.top_ent+as.cur_row)){
		DL_CACHE_S *dlc_to_flush;
		long        global_row_num;

		/*
		 * unexpand list
		 */

		/*
		 * redraw screen starting with first ListEnt
		 */
		dl = dlist(as.top_ent+as.cur_row);
		dlc_to_flush = get_dlc(as.top_ent+as.cur_row);

		/*
		 * New cur_row should be the line after the ListHead line
		 * that takes the place of the first address in the list.
		 */
		as.cur_row = as.cur_row - dl->l_offset;

		/*
		 * If the list header for the new row is off the screen,
		 * adjust it.
		 */
		if(as.cur_row < 0){  /* center it */
		    global_row_num = dlc_to_flush->global_row -
					    dlc_to_flush->dlcoffset;
		    as.top_ent = first_line(global_row_num - as.l_p_page/2L);
		    as.cur_row = global_row_num - as.top_ent;
		    start_disp = 0;
		}
		else if(as.cur_row == 0){  /* just slide up in this case */
		    as.top_ent -= 1;
		    as.cur_row  = 1;
		    start_disp = 0;
		}
		else
		  start_disp = as.cur_row;

		exp_unset_expanded(pab->address_book->exp,
				   (a_c_arg_t)dlc_to_flush->dlcelnum);
		flush_dlc_from_cache(dlc_to_flush);
		ps->mangled_body = 1;
		ps->mangled_footer = 1;
	    }
	    else
	      q_status_message(SM_ORDER | SM_DING, 3, 4,
			       "Can't happen in MC_UNEXPAND");

	    break;

            /*------ Popup to top level display ----------*/
          case MC_POPUP:
	    if(F_ON(F_EXPANDED_DISTLISTS,ps) ||
	       !entry_is_listent(as.top_ent+as.cur_row)){
		DL_CACHE_S dlc_restart;
		long new_row; 

		(void)init_addrbooks((checkedn || as.selections)
					     ? ThreeQuartOpen : HalfOpen,
				     0, 0, !are_selecting);
		dlc_restart.adrbk_num = as.cur;
		dlc_restart.type      = DlcTitle;
		warp_to_dlc(&dlc_restart, 0L);
		/*
		 * Put the current entry in a nice spot on the screen.
		 * Will everything above fit and still leave ours on screen?
		 */
		new_row = LINES_PER_ABOOK * as.cur +
			      (((pab->type & GLOBAL) &&
				(as.how_many_personals > 0 || as.config))
				   ? XTRA_LINES_BETWEEN : 0) +
			      ((as.how_many_personals == 0 && as.config)
				   ? LINES_PER_ADD_LINE : 0);
		new_row = MAX(MIN(new_row, as.l_p_page-VIS_LINES_PER_ABOOK), 0);

		as.cur_row = new_row;
		as.top_ent = 0L - as.cur_row;
		start_disp         = 0;
		ps->mangled_screen = 1;
	    }
	    else
	      q_status_message(SM_ORDER | SM_DING, 3, 4,
			       "Can't happen in MC_POPUP");

	    break;


            /*------------- Back to main menu or exit to caller -------*/
          case MC_EXIT:
          case MC_MAIN:
	    if(!are_selecting)
              ps->next_screen = main_menu_screen;

	    if(!(are_selecting && as.checkboxes && checkedn > 0)
	       /* TRANSLATORS: we are asking for confirmation about abandonding selections
	          in the address book. */
	       || want_to(_("Really abandon your selections "),
			  'y', 'x', NO_HELP, WT_NORM) == 'y')
	      quit++;
	    else
              ps->next_screen = SCREEN_FUN_NULL;

	    break;


            /*------- Open an address book ----------*/
	  case MC_OPENABOOK:
openabook:
	    if(entry_is_clickable_title(as.top_ent+as.cur_row)){
		DL_CACHE_S *dlc_to_flush;

		dlc_to_flush = get_dlc(as.top_ent+as.cur_row);
		if(dlc_to_flush->type == DlcTitle ||
		   dlc_to_flush->type == DlcClickHereCmb){

		    /*
		     * open this addrbook and fill in display list
		     */

		    init_abook(pab, Open);

		    if(pab->access == ReadWrite || pab->access == ReadOnly){
			if(!are_selecting && pab->access == ReadOnly)
			  q_status_message1(SM_ORDER, 0, 4, _("AddressBook %s is Read Only"), pab->abnick);

			/*
			 * successful open, burrow into addrbook
			 */
			if(F_OFF(F_CMBND_ABOOK_DISP,ps_global)){
			    warp_to_top_of_abook(as.cur);
			    as.top_ent = 0L;
			    as.cur_row = 0L;
			    start_disp = 0;
			    ps->mangled_screen = 1;
			}
			else{
			    flush_dlc_from_cache(dlc_to_flush);
			    start_disp = as.cur_row;
			    ps->mangled_footer = 1;
			    ps->mangled_body = 1;
			}
		    }
		    else{	/* open failed */
			/*
			 * Flush the title line so that it will change into
			 * a permission denied line,
			 */
			flush_dlc_from_cache(dlc_to_flush);
			start_disp = as.cur_row;
			ps->mangled_footer = 1;
			ps->mangled_body = 1;
		    }
		}
		else if(dlc_to_flush->type == DlcTitleNoPerm)
	          q_status_message(SM_ORDER, 0, 4,
				   _("Cannot access address book."));
	    }
	    else
	      q_status_message(SM_ORDER | SM_DING, 3, 4,
			       "Can't happen in MC_OPENABOOK");

	    break;


            /*------- Expand addresses in List ------*/
	  case MC_EXPAND:
expand:
	    if(entry_is_clickable(as.top_ent+as.cur_row)){
		DL_CACHE_S *dlc_to_flush;

		dlc_to_flush = get_dlc(as.top_ent+as.cur_row);
		if(dlc_to_flush->type == DlcListClickHere){
		    start_disp = as.cur_row;  /* will redraw from here down */

		    /*
		     * Mark this list expanded, then flush the
		     * current line from dlc cache.  When we get the
		     * line again it will notice the expanded flag and change
		     * the type to DlcListEnt (if any entries).
		     */

		    if(F_OFF(F_EXPANDED_DISTLISTS,ps))
		      exp_set_expanded(pab->address_book->exp,
			(a_c_arg_t)dlc_to_flush->dlcelnum);

		    flush_dlc_from_cache(dlc_to_flush);
		    dlc_to_flush = get_dlc(as.top_ent+as.cur_row);
		    /*
		     * If list is empty, back cursor up one.
		     */
		    if(dlc_to_flush->type == DlcListEmpty){
			as.cur_row--;
			start_disp--;
			if(as.cur_row < 0){
			    as.top_ent--;
			    as.cur_row = 0;
			    start_disp  = 0;
			}
		    }

		    ps->mangled_body = 1;
		}
	    }
	    else
	      q_status_message(SM_ORDER | SM_DING, 3, 4,
			       "Can't happen in MC_EXPAND");

	    break;


            /*------- Select ---------------*/
	  case MC_CHOICE:
select:
	    if(are_selecting){
              /* Select an entry to mail to or a nickname to add to */
	      if(!any_addrs_avail(as.top_ent+as.cur_row)){
	          q_status_message(SM_ORDER | SM_DING, 0, 4,
	   _("No entries in address book. Use ExitSelect to leave address books"));
	          break;
	      }

	      if(as.checkboxes || is_addr(as.top_ent+as.cur_row)){
		  BuildTo        bldto;
		  char          *to    = NULL;
		  char          *error = NULL;
		  AdrBk_Entry   *abe;

		  dl = dlist(as.top_ent+as.cur_row);

		  if(selecting_one_nick){
		      char nickbuf[MAX_NICKNAME + 1];

		      strncpy(nickbuf,
			  ae(as.top_ent+as.cur_row)->nickname,
			  sizeof(nickbuf)-1);
		      nickbuf[sizeof(nickbuf)-1] = '\0';
		      return(cpystr(nickbuf));
		  }
		  else if(as.checkboxes && checkedn <= 0){
		      q_status_message(SM_ORDER, 0, 1,
			_("Use \"X\" to mark addresses or lists"));
		      break;
		  }
		  else if(as.checkboxes){
		      size_t incr = 100, avail, alloced;

		      /*
		       * Have to run through all of the checked entries
		       * in all of the address books.
		       * Put the nicknames together into one long
		       * string with comma separators and let 
		       * our_build_address handle the parsing.
		       */
		      to = (char *)fs_get(incr);
		      *to = '\0';
		      avail = incr;
		      alloced = incr;

		      for(i = 0; i < as.n_addrbk; i++){
			  EXPANDED_S *next_one;
			  adrbk_cntr_t num;
			  AddrScrn_Disp fake_dl;
			  char *a_string;

			  pab = &as.adrbks[i];
			  if(pab->address_book)
			    next_one = pab->address_book->checks;
			  else
			    continue;

			  while((num = entry_get_next(&next_one)) != NO_NEXT){
			      abe = adrbk_get_ae(pab->address_book,
						 (a_c_arg_t) num);
			      /*
			       * Since we're picking up address book entries
			       * directly from the address books and have
			       * no knowledge of the display lines they came
			       * from, we don't know the dl's that go with
			       * them.  We need to pass a dl to abe_to_nick
			       * but it really is only going to use the
			       * type in this case.
			       */
			      dl = &fake_dl;
			      dl->type = (abe->tag == Single) ? Simple
							      : ListHead;
			      a_string = abe_to_nick_or_addr_string(abe, dl, i);

			      while(abe && avail < (size_t)strlen(a_string)+1){
				  alloced += incr;
				  avail   += incr;
				  fs_resize((void **)&to, alloced);
			      }

			      if(!*to){
				strncpy(to, a_string, alloced);
				to[alloced-1] = '\0';
			      }
			      else{
				  strncat(to, ",", alloced-strlen(to)-1);
				  strncat(to, a_string, alloced-strlen(to)-1);
			      }

			      avail -= (strlen(a_string) + 1);
			  }
		      }

		      /*
		       * Return the nickname list for lcc so that the
		       * correct fullname can make it to the To line.
		       * If we expand it ahead of time, the list name
		       * and first user's fullname will get mushed together.
		       * If an entry doesn't have a nickname then we're
		       * out of luck as far as getting the right entry
		       * in the To line goes.
		       */
		      if(selecting_mult_nicks)
			return(to);

		      bldto.type    = Str;
		      bldto.arg.str = to;
		  }
		  else{
		      /* Select an address, but not using checkboxes */
		      if(selecting_mult_nicks){
			if(dl->type != ListHead && style == SelectAddrLccCom){
			    q_status_message(SM_ORDER, 0, 4,
	  _("You may only select lists for Lcc, use Bcc for other addresses"));
			    break;
			}
			else{
			    /*
			     * Even though we're supposedly selecting
			     * nicknames, we have a special case here to
			     * select a single member of a distribution
			     * list.  This happens with style SelectNicksCom
			     * which is the regular ^T entry from the
			     * composer, and it allows somebody to mail to
			     * a single member of a distribution list.
			     */
			    abe = ae(as.top_ent+as.cur_row);
			    return(abe_to_nick_or_addr_string(abe, dl, as.cur));
			}
		      }
		      else{
			  if(dl->type == ListEnt){
			      bldto.type    = Str;
			      bldto.arg.str =
				      listmem_from_dl(pab->address_book, dl);
			  }
			  else{
			      bldto.type    = Abe;
			      bldto.arg.abe = ae(as.top_ent+as.cur_row);
			  }
		      }
		  }
		      
		  (void)our_build_address(bldto, &addr, &error, NULL, save_and_restore);
		  /* Have to rfc1522_decode the addr */
		  if(addr){
		      char    *tmp_a_string, *p;
		      ADDRESS *a = NULL;

		      if(style == SelectAddrNoFull){
			  tmp_a_string = cpystr(addr);
			  rfc822_parse_adrlist(&a,tmp_a_string,ps->maildomain);
			  fs_give((void **)&tmp_a_string);
			  if(a){
			      fs_give((void **)&addr);
			      addr = cpystr(simple_addr_string(a, tmp_20k_buf,
						    	       SIZEOF_20KBUF));
			      mail_free_address(&a);
			  }
		      }
		      else if(style == SelectMultNoFull){
			  tmp_a_string = cpystr(addr);
			  rfc822_parse_adrlist(&a,tmp_a_string,ps->maildomain);
			  fs_give((void **)&tmp_a_string);
			  if(a){
			      fs_give((void **)&addr);
			      addr = cpystr(simple_mult_addr_string(a,
								tmp_20k_buf,
						    	        SIZEOF_20KBUF,
								","));
			      mail_free_address(&a);
			  }
		      }
		      else{
			  size_t len;
			  len = 4*strlen(addr)+1;
			  p = (char *)fs_get(len * sizeof(char));
			  if(rfc1522_decode_to_utf8((unsigned char *)p, len, addr) == (unsigned char *)p){
			      fs_give((void **)&addr);
			      addr = p;
			  }
			  else
			    fs_give((void **)&p);
		      }
		  }

		  if(to)
		    fs_give((void **)&to);

		  if(error){
		      q_status_message1(SM_ORDER, 3, 4, "%s", error);
		      fs_give((void **)&error);
		  }

		  return(addr);  /* Caller frees this */
	      }
	      else{
		  if(entry_is_clickable(as.top_ent+as.cur_row))
		    clickable_warning(as.top_ent+as.cur_row);
		  else if(entry_is_askserver(as.top_ent+as.cur_row))
	            q_status_message(SM_ORDER, 3, 4, _("Use select to select an address or addresses from address books"));
		  else
	            q_status_message(SM_ORDER, 3, 4, _("No address selected"));

	          break;
	      }
	    }
	    else
	      q_status_message(SM_ORDER | SM_DING, 3, 4,
			       "Can't happen in MC_CHOICE");

	    break;


            /*----- View an entry --------------------*/
	  case MC_VIEW_ENTRY:
view:
	    if(empty){
		empty_warning(as.top_ent+as.cur_row);
                break;
	    }

	    view_abook_entry(ps, as.top_ent+as.cur_row);
	    break;


            /*----- Add new ---------*/
	  case MC_ADD:
	   {long old_l_p_p, old_top_ent, old_cur_row;

	    if(adrbk_check_all_validity_now()){
		if(resync_screen(pab, style, checkedn)){
		    q_status_message(SM_ORDER | SM_DING, 3, 4,
		     _("Address book changed. AddNew cancelled. Try again."));
		    ps->mangled_screen = 1;
		    break;
		}
	    }

	    if(rdonly){
		q_status_message(SM_ORDER, 0, 4, _("AddressBook is Read Only"));
                break;
	    }

	    warped = 0;
	    dprint((9,
		       "Calling edit_entry to add entry manually\n"));
	    /* TRANSLATORS: add as in add a new entry to something */
	    edit_entry(pab->address_book, (AdrBk_Entry *)NULL, NO_NEXT,
		       NotSet, 0, &warped, _("add"));

	    /*
	     * Warped means we got plopped down somewhere in the display
	     * list so that we don't know where we are relative to where
	     * we were before we warped.  The current line number will
	     * be zero, since that is what the warp would have set.
	     */
	    if(warped){
		as.top_ent = first_line(0L - as.l_p_page/2L);
		as.cur_row = 0L - as.top_ent;
	    }
	    else{
		/*
		 * If we didn't warp, that means we didn't change at all,
		 * so keep old screen.
		 */
		old_l_p_p   = as.l_p_page;
		old_top_ent = as.top_ent;
		old_cur_row = as.cur_row;
	    }

	    /* Window size may have changed while in pico. */
	    ab_resize();

	    /* fix up what ab_resize messed up */
	    if(!warped && old_l_p_p == as.l_p_page){
		as.top_ent     = old_top_ent;
		as.cur_row     = old_cur_row;
		as.old_cur_row = old_cur_row;
	    }
	   }

	    ps->mangled_screen = 1;
	    break;


            /*---------- Add a new address book -------------------*/
	  case MC_ADDABOOK:
	    {int old_abook_num, new_abook_num, new_row, global;
	     int stay_put, old_cur_row;

add_abook:
	    stay_put = (dlist(as.top_ent+as.cur_row)->type == AddFirstPers ||
			dlist(as.top_ent+as.cur_row)->type == AddFirstGlob);
	    old_cur_row = as.cur_row;
	    old_abook_num = adrbk_num_from_lineno(as.top_ent+as.cur_row);
	    global = (dlist(as.top_ent+as.cur_row)->type == AddFirstGlob ||
		       (dlist(as.top_ent+as.cur_row)->type != AddFirstPers &&
			(old_abook_num >= as.how_many_personals) &&
			as.n_addrbk > 0));
	    if((new_abook_num =
	        ab_add_abook(global,
			      stay_put ? -1
			        : adrbk_num_from_lineno(as.top_ent+as.cur_row)))
			      >= 0){
		DL_CACHE_S dlc_restart;

		(void)init_addrbooks(HalfOpen, 0, 0, !are_selecting);

		erase_checks();
		erase_selections();

		dlc_restart.adrbk_num = new_abook_num;
		dlc_restart.type      = DlcTitle;
		warp_to_dlc(&dlc_restart, 0L);

		/*
		 * Put the current entry in a nice spot on the screen.
		 */
		new_row = old_cur_row +
			  (stay_put
			    ? 0
			    : LINES_PER_ABOOK*(new_abook_num - old_abook_num));

		if(new_row >= 0 &&
		   new_row <= (as.l_p_page-VIS_LINES_PER_ABOOK))/* ok, use it */
		  as.cur_row = new_row;
		else{
		    /*
		     * Will everything above fit and still leave ours on screen?
		     */
		    new_row = LINES_PER_ABOOK * new_abook_num +
				  ((global &&
				    (as.how_many_personals > 0 || as.config))
				       ? XTRA_LINES_BETWEEN : 0) +
				  ((as.how_many_personals == 0 && as.config)
				       ? LINES_PER_ADD_LINE : 0);
		    new_row = MAX(MIN(new_row,
				      as.l_p_page - VIS_LINES_PER_ABOOK), 0);
		    as.cur_row = new_row;
		}

		as.top_ent = 0L - as.cur_row;
		dprint((5, "addrbook added: %s\n",
		       as.adrbks[new_abook_num].filename
		         ? as.adrbks[new_abook_num].filename : "?"));
	    }

	    ps->mangled_screen = 1;

	    }

	    break;


            /*---------- Change address book config -------------------*/
	  case MC_EDITABOOK:
change_abook:
	    if(entry_is_clickable_title(as.top_ent+as.cur_row)){
		int        abook_num, old_cur_row, global;
		long       old_top_ent;
		DL_CACHE_S dlc_restart, *dlc;
		char      *serv = NULL, *folder = NULL, *p, *q;
		char      *nick = NULL, *file = NULL;

		abook_num = adrbk_num_from_lineno(as.top_ent+as.cur_row);
		global = (abook_num >= as.how_many_personals);
		old_cur_row = as.cur_row;
		old_top_ent = as.top_ent;
		dlc = get_dlc(as.top_ent+as.cur_row);
		dlc_restart = *dlc;

		if(global)
		  q = ps_global->VAR_GLOB_ADDRBOOK[abook_num -
						   as.how_many_personals];
		else
		  q = ps_global->VAR_ADDRESSBOOK[abook_num];
		
		get_pair(q, &nick, &file, 0, 0);

		if(nick && !*nick)
		  fs_give((void **)&nick);
		
		if(file && *file == '{'){
		    q = file + 1;
		    if((p = strindex(file, '}'))){
			*p = '\0';
			serv = q;
			folder = p+1;
		    }
		    else{
			q_status_message1(SM_ORDER|SM_DING, 0, 4,
					  _("Missing \"}\" in config: %s"), q);
			if(nick)
			  fs_give((void **)&nick);
			if(file)
			  fs_give((void **)&file);
			
			break;
		    }
		}
		else
		  folder = file;
		
		if(ab_edit_abook(global, abook_num, serv, folder, nick) >= 0){
		    (void)init_addrbooks(HalfOpen, 0, 0, !are_selecting);

		    erase_checks();
		    erase_selections();

		    dlc_restart.type      = DlcTitle;
		    warp_to_dlc(&dlc_restart, old_top_ent+(long)old_cur_row);
		    as.cur_row = old_cur_row;
		    as.top_ent = old_top_ent;

		    dprint((5, "addrbook config edited: %s\n",
			   as.adrbks[dlc_restart.adrbk_num].filename
			    ? as.adrbks[dlc_restart.adrbk_num].filename : "?"));
		}

		if(nick)
		  fs_give((void **)&nick);
		if(file)
		  fs_give((void **)&file);

		ps->mangled_screen = 1;
	    }
	    else
	      /* TRANSLATORS: the user tried to change the current line of the address
	         book but the line could not be changed */
              q_status_message(SM_ORDER, 0, 4, _("Not a changeable line"));

	    break;


            /*---------- Delete an address book -------------------*/
	  case MC_DELABOOK:
	    if(as.n_addrbk == 0){
                q_status_message(SM_ORDER, 0, 4, _("Nothing to delete"));
                break;
	    }

	    {char *err = NULL;

	    if(ab_del_abook(as.top_ent+as.cur_row, command_line, &err) >= 0){
		DL_CACHE_S dlc_restart;
		int new_abook_num, old_abook_num, old_pers, old_glob, new_row;

		/*
		 * Careful, these are only ok because ab_del_abook didn't
		 * mess with the as globals, like as.how_many_personals.
		 * The addrbook_reset does reset them, of course.
		 */
		old_abook_num = adrbk_num_from_lineno(as.top_ent+as.cur_row);
		old_pers = as.how_many_personals;
		old_glob = as.n_addrbk - as.how_many_personals;
		addrbook_reset();
		(void)init_addrbooks(HalfOpen, 0, 0, !are_selecting);

		erase_checks();
		erase_selections();

		if(old_abook_num >= as.n_addrbk) /* we deleted last addrbook */
		  new_abook_num = as.n_addrbk - 1;
		else
		  new_abook_num = old_abook_num;

		/*
		 * Pick a line to highlight and center
		 */
		if(as.how_many_personals == 0 && old_pers == 1)
		  dlc_restart.type        = DlcPersAdd;
		else if((as.n_addrbk - as.how_many_personals) == 0 &&
			old_glob == 1)
		  dlc_restart.type        = DlcGlobAdd;
		else if(as.n_addrbk == 0)
		  dlc_restart.type        = DlcPersAdd;
		else{
		    dlc_restart.adrbk_num = new_abook_num;
		    dlc_restart.type      = DlcTitle;
		}

		warp_to_dlc(&dlc_restart, 0L);

		/*
		 * Will everything above fit and still leave ours on screen?
		 */
		if(dlc_restart.type == DlcTitle)
		  new_row = LINES_PER_ABOOK * new_abook_num +
			      (((as.adrbks[new_abook_num].type & GLOBAL) &&
				(as.how_many_personals > 0 || as.config))
				   ? XTRA_LINES_BETWEEN : 0) +
			      ((as.how_many_personals == 0 && as.config)
				   ? LINES_PER_ADD_LINE : 0);
		else if(dlc_restart.type == DlcGlobAdd)
		  new_row = LINES_PER_ABOOK * as.n_addrbk +
			      ((as.how_many_personals > 0 || as.config)
				   ? XTRA_LINES_BETWEEN : 0) +
			      ((as.how_many_personals == 0 && as.config)
				   ? LINES_PER_ADD_LINE : 0);
		else
		  new_row = 0;

		new_row = MAX(MIN(new_row, as.l_p_page-VIS_LINES_PER_ABOOK), 0);
		as.cur_row = new_row;
		as.top_ent = 0L - as.cur_row;
		start_disp = 0;
		ps->mangled_body = 1;
		ps->mangled_footer = 1;
		/* TRANSLATORS: This is just comforting confirmation that the
		   address book being deleted was successfully deleted */
		q_status_message(SM_ORDER, 0, 3, _("Address book deleted"));
	    }
	    else{
		if(err){
		    q_status_message(SM_ORDER, 0, 4, err);
		    dprint((5, "addrbook delete failed: %s\n",
			   err ? err : "?"));
		}
	    }

	    }

	    break;


            /*---- Reorder an addressbook list ---------*/
	  case MC_SHUFFLE:
	    if(entry_is_addkey(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4,
				 _("Highlight entry you wish to shuffle"));
                break;
	    }

	    {int      slide;
	     char    *msg = NULL;
	     int      ret, new_anum;

	    if((ret = ab_shuffle(pab, &slide, command_line, &msg)) > 0){
		DL_CACHE_S dlc_restart;
		int new_row;

		new_anum = ret - 1;	/* see ab_shuffle return value */
		addrbook_reset();
		(void)init_addrbooks(HalfOpen, 0, 0, !are_selecting);

		erase_checks();
		erase_selections();

		/* put cursor on new_anum */
		dlc_restart.adrbk_num = new_anum;
		dlc_restart.type      = DlcTitle;
		warp_to_dlc(&dlc_restart, 0L);
		/*
		 * Will everything above fit and still leave ours on screen?
		 */
		new_row = LINES_PER_ABOOK * new_anum +
			      (((as.adrbks[new_anum].type & GLOBAL) &&
				(as.how_many_personals > 0 || as.config))
				   ? XTRA_LINES_BETWEEN : 0) +
			      ((as.how_many_personals == 0 && as.config)
				   ? LINES_PER_ADD_LINE : 0);
		new_row = MAX(MIN(new_row, as.l_p_page-VIS_LINES_PER_ABOOK), 0);
		as.cur_row = new_row;
		as.top_ent = 0L - as.cur_row;
		start_disp = 0;
		ps->mangled_body = 1;
	    }
	    else if(ret == 0){
		DL_CACHE_S *dlc_to_flush;

		if(slide <  0){			/* moved it up */
		    as.cur_row += slide;
		    start_disp  = MAX(as.cur_row - 1, 0);
		    dlc_to_flush = get_dlc(as.top_ent+as.cur_row);
		    /*
		     * If we backed off the top of the screen, just
		     * inch the display up so we can see it.
		     */
		    if(as.cur_row < 0){
			as.top_ent += as.cur_row;  /* cur_row is negative */
			as.cur_row  = 0;
			start_disp  = 0;
		    }
		}
		else{				/* moved it down */
		    start_disp  = as.cur_row;
		    dlc_to_flush = get_dlc(as.top_ent+as.cur_row);
		    as.cur_row += slide;
		    if(as.cur_row > as.l_p_page - VIS_LINES_PER_ABOOK){
			as.top_ent += (as.cur_row -
					(as.l_p_page - VIS_LINES_PER_ABOOK));
			as.cur_row  = MAX(as.l_p_page-VIS_LINES_PER_ABOOK, 0);
			start_disp  = 0;
		    }
		}

		flush_dlc_from_cache(dlc_to_flush);
		ps->mangled_body = 1;
	    }

	    q_status_message(SM_ORDER, 0, 3,
			     msg ? msg :
			      (ret < 0) ? _("Shuffle failed") :
			       _("Address books shuffled"));
	    if(ret < 0)
	      dprint((5, "addrbook shuffle failed: %s\n",
		     msg ? msg : "?"));

	    if(msg)
	      fs_give((void **)&msg);

	    }

	    break;


            /*----------------------- Move Up ---------------------*/
	  case MC_CHARUP:
	  case MC_PREVITEM:
	    r = prev_selectable_line(as.cur_row+as.top_ent, &new_line);
	    if(r == 0){
		/* find first line so everything is displayed */
		new_top_ent = as.cur_row+as.top_ent;
		for(dl=dlist(new_top_ent-1);
		    dl->type != Beginning;
		    dl = dlist((--new_top_ent) - 1))
		  ;

		if(new_top_ent == as.top_ent ||
		   (as.cur_row + (as.top_ent-new_top_ent) > as.l_p_page - 1)){
		    q_status_message(SM_INFO, 0, 1, _("Already on first line."));
		}
		else{
		    as.cur_row += (as.top_ent - new_top_ent);
		    as.top_ent  = new_top_ent;
		    start_disp       = 0;
		    ps->mangled_body = 1;
		}

		break;
	    }

	    i = ((cmd == MC_CHARUP)
		 && dlist(as.top_ent - 1L)->type != Beginning)
		  ? MIN(HS_MARGIN(ps), as.cur_row) : 0;
	    as.cur_row = new_line - as.top_ent;
	    if(as.cur_row - i < 0){
		if(cmd == MC_CHARUP){
		    /*-- Past top of page --*/
		    as.top_ent += (as.cur_row - i);
		    as.cur_row  = i;
		}
		else{
		    new_top_ent = first_line(as.top_ent - as.l_p_page);
		    as.cur_row += (as.top_ent - new_top_ent);
		    as.top_ent  = new_top_ent;
		    /* if it is still off screen */
		    if(as.cur_row - i < 0){
			as.top_ent += (as.cur_row - i);
			as.cur_row  = i;
		    }
		}

		start_disp       = 0;
		ps->mangled_body = 1;
	    }
	    else
	      current_changed_flag++;

	    break;


            /*------------------- Move Down -------------------*/
	  case MC_CHARDOWN:
	  case MC_NEXTITEM:
	    r = next_selectable_line(as.cur_row+as.top_ent, &new_line);
	    if(r == 0){
		long new_end_line;

		/* find last line so everything is displayed */
		new_end_line = as.cur_row+as.top_ent;
		for(dl=dlist(new_end_line+1);
		    dl->type != End;
		    dl = dlist((++new_end_line)+1))
		  ;

		if(new_end_line - as.top_ent <= as.l_p_page - 1 ||
		   as.cur_row - (new_end_line-as.top_ent-(as.l_p_page-1)) < 0){
		    q_status_message(SM_INFO, 0, 1, _("Already on last line."));
		}
		else{
		    as.cur_row -= (new_end_line-as.top_ent-(as.l_p_page-1));
		    as.top_ent += (new_end_line-as.top_ent-(as.l_p_page-1));
		    start_disp       = 0;
		    ps->mangled_body = 1;
		}

		break;
	    }

	    /* adjust for scrolling margin */
	    i = (cmd == MC_CHARDOWN) ? HS_MARGIN(ps) : 0;
	    as.cur_row = new_line - as.top_ent;
	    if(as.cur_row >= as.l_p_page - i){
		if(cmd == MC_CHARDOWN){
		    /*-- Past bottom of page --*/
		    as.top_ent += (as.cur_row - (as.l_p_page - i) + 1);
		    as.cur_row  = (as.l_p_page - i) - 1;
		}
		else{
		    /*-- Changed pages --*/
		    as.top_ent += as.l_p_page;
		    as.cur_row -= as.l_p_page;
		    /* if it is still off screen */
		    if(as.cur_row >= as.l_p_page - i){
			as.top_ent += (as.cur_row - (as.l_p_page - i) + 1);
			as.cur_row  = (as.l_p_page - i) - 1;
		    }
		}

		start_disp       = 0;
		ps->mangled_body = 1;
	    }
	    else
	      current_changed_flag++;

	    break;


#ifdef MOUSE		
	  case MC_MOUSE:
	    {
		MOUSEPRESS mp;
	    
		/*
		 * Get the mouse down.  Convert to content row number.
		 * If the row is selectable, do the single or double click
		 * operation.
		 */
		mouse_get_last(NULL, &mp);
		mp.row -= HEADER_ROWS(ps);
		if(line_is_selectable(as.top_ent + mp.row)){
		  if(mp.button == M_BUTTON_LEFT){
		    if(mp.doubleclick){
			/*
			 * A mouse double click does the default action to
			 * the selected line. Since we need to do a goto
			 * to get there, we have this ugly bit of code.
			 *
			 * On the first mouse click we go around the loop
			 * once and set def_key appropriately for the
			 * line as.top_ent+mp.row, which will then be
			 * the same as as.top_ent+as.cur_row.
			 */
			switch(km->keys[def_key].bind.cmd){
			  case MC_CHOICE:
			    if (as.checkboxes) goto togglex;
			    else goto select;
			  case MC_OPENABOOK:
			    goto openabook;
			  case MC_EXPAND:
			    goto expand;
			  case MC_TOGGLE:
			    goto togglex;
			  case MC_SELALL:
			    goto selall;
			  case MC_VIEW_ENTRY:
			    goto view;
			  case MC_ADDABOOK:
			    goto add_abook;
			  case MC_EDITABOOK:
			    goto change_abook;
#ifdef	ENABLE_LDAP
			  case MC_QUERY_SERV:
			    goto q_server;
#endif
			  default:
			    q_status_message(SM_INFO, 0, 1,
					     "Can't happen in MC_MOUSE");
			    break;
			}
		    }

		    as.cur_row = mp.row;
		    current_changed_flag++;
		  }
		  else if(mp.button == M_BUTTON_RIGHT){
#ifdef	_WINDOWS
		    int need_redraw;
#endif
		    as.cur_row = mp.row;
		    current_changed_flag++;


#ifdef	_WINDOWS
		    need_redraw = calculate_field_widths();

	    /*---------- Update the current entry, (move or change) -------*/
		    display_book(need_redraw ? 0 : as.cur_row,
				 as.cur_row,  
				 as.old_cur_row,  
				 need_redraw,
				 &cursor_pos);

		    as.old_cur_row       = as.cur_row;
		    current_changed_flag = 0;
		    as.cur               = cur_addr_book();
		    pab              = (as.n_addrbk &&
				    !entry_is_askserver(as.top_ent+as.cur_row))
					? &as.adrbks[as.cur] : NULL;

		    if(!mp.doubleclick){
			MPopup addr_pu[20];
			int    n, i;

			/* Make what they clicked "current" */
			memset(addr_pu, 0, 20 * sizeof(MPopup));

			addr_pu[n = 0].type = tQueue;
			addr_pu[n].data.val = km->keys[def_key].bind.ch[0];
			addr_pu[n].label.style = lNormal;
			addr_pu[n++].label.string = km->keys[def_key].label;

			if((i = menu_clear_binding(km, '<')) != MC_UNKNOWN){
			    menu_add_binding(km, '<', i);

			    if((i = menu_binding_index(km, i)) >= 0){
				addr_pu[n++].type = tSeparator;

				addr_pu[n].type = tQueue;
				addr_pu[n].data.val = km->keys[i].bind.ch[0];
				addr_pu[n].label.style = lNormal;
				addr_pu[n++].label.string = km->keys[i].label;
			    }
			}

			addr_pu[n].type = tTail;

			mswin_popup(addr_pu);
		    }
#endif
		  }
		}
	    }
	    break;
#endif	    


            /*------------- Page Up or Down --------*/
	  case MC_PAGEUP:
	  case MC_PAGEDN:
	    if(cmd == MC_PAGEUP){
		/* find first line on prev page */
		new_top_ent = first_line(as.top_ent - as.l_p_page);
		if(new_top_ent == NO_LINE)
		    break;

		/* find first selectable line */
		fl = first_selectable_line(new_top_ent);

		/* If we didn't move, we'd better move now */
		if(fl == as.top_ent+as.cur_row){
		  if(!prev_selectable_line(as.cur_row+as.top_ent, &new_line)){
		      long lineno;

		      /* find first line so everything is displayed */
		      lineno = as.cur_row+as.top_ent;
		      for(dl=dlist(lineno);
			  dl->type != Beginning;
			  dl = dlist(--lineno))
			;

		      /*
		       * If this new_top_ent is the same as the old_top_ent
		       * we'll get the warning message.
		       */
		      new_top_ent = first_line(lineno);
		      if(fl - new_top_ent >= as.l_p_page)
			new_top_ent += (fl - new_top_ent - as.l_p_page + 1);
		  }
		  else
		    fl = new_line;
		}

		if(fl == NO_LINE)
		    break;

		if(as.top_ent == new_top_ent && as.cur_row == (fl-as.top_ent)){
		    q_status_message(SM_INFO, 0, 1, _("Already on first page."));
		    break;
		}

		if(as.top_ent == new_top_ent)
                    current_changed_flag++;
		else
		    as.top_ent = new_top_ent;
	    }
	    else{  /* Down */
		/* find first selectable line on next page */
		fl = first_selectable_line(as.top_ent + as.l_p_page);
		if(fl == NO_LINE)
		    break;

		/* if there is another page, scroll */
		if(fl - as.top_ent >= as.l_p_page){
		    new_top_ent = as.top_ent + as.l_p_page;
		}
		/* on last page already */
		else{
		    new_top_ent = as.top_ent;
		    if(as.cur_row == (fl - as.top_ent)){ /* no change */
			long new_end_line;

			/* find last line so everything is displayed */
			new_end_line = as.cur_row+as.top_ent;
			for(dl=dlist(new_end_line+1);
			    dl->type != End;
			    dl = dlist((++new_end_line)+1))
			  ;

			if(new_end_line - as.top_ent <= as.l_p_page - 1 ||
			   as.cur_row -
			       (new_end_line-as.top_ent-(as.l_p_page-1)) < 0){
			    q_status_message(SM_INFO, 0, 1,
					     _("Already on last page."));
			}
			else{
			    as.cur_row -=
				(new_end_line-as.top_ent-(as.l_p_page-1));
			    as.top_ent +=
				(new_end_line-as.top_ent-(as.l_p_page-1));
			    start_disp       = 0;
			    ps->mangled_body = 1;
			}

			break;
		    }
		}

		if(as.top_ent == new_top_ent)
                    current_changed_flag++;
		else
		    as.top_ent = new_top_ent;
	    }

	    /*
	     * Stuff in common for up or down.
	     */
	    as.cur_row = fl - as.top_ent;

	    /* if it is still off screen */
	    if(as.cur_row < 0){
		as.top_ent += as.cur_row;
		as.cur_row  = 0;
	    }
	    else if(as.cur_row >= as.l_p_page){
		as.top_ent += (as.cur_row - as.l_p_page + 1);
		as.cur_row  = as.l_p_page - 1;
	    }

	    if(!current_changed_flag){
		ps->mangled_body = 1;
		start_disp  = 0;
	    }

	    break;


	    /*------------- Delete item from addrbook ---------*/
	  case MC_DELETE:
	    if(adrbk_check_all_validity_now()){
		if(resync_screen(pab, style, checkedn)){
		    q_status_message(SM_ORDER | SM_DING, 3, 4,
			  _("Address book changed. Delete cancelled. Try again."));
		    ps->mangled_screen = 1;
		    break;
		}
	    }

	    if(!any_addrs_avail(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("No entries to delete"));
                break;
	    }

	    if(entry_is_clickable(as.top_ent+as.cur_row)){
		clickable_warning(as.top_ent+as.cur_row);
		break;
	    }

	    if(rdonly){
	        q_status_message(SM_ORDER, 0, 4, _("AddressBook is Read Only"));
		break;
	    }

	    if(empty){
		empty_warning(as.top_ent+as.cur_row);
                break;
	    }

	    warped = 0;
            did_delete = single_entry_delete(pab->address_book,
                                             as.cur_row+as.top_ent,
                                             &warped);
	    ps->mangled_footer = 1;
	    if(did_delete){
		if(warped){
		    as.top_ent = first_line(0L - as.l_p_page/2L);
		    as.cur_row = 0L - as.top_ent;
		    start_disp = 0;
		}
		else{
		    /*
		     * In case the line we're now at is not a selectable
		     * field.
		     */
		    new_line = first_selectable_line(as.cur_row+as.top_ent);
		    if(new_line != NO_LINE
		       && new_line != as.cur_row+as.top_ent){
			as.cur_row = new_line - as.top_ent;
			if(as.cur_row < 0){
			    as.top_ent -= as.l_p_page;
			    as.cur_row += as.l_p_page;
			}
			else if(as.cur_row >= as.l_p_page){
			    as.top_ent += as.l_p_page;
			    as.cur_row -= as.l_p_page;
			}
		    }

		    start_disp = MIN(as.cur_row, as.old_cur_row);
		}

	        ps->mangled_body = 1;
	    }

            break;


	    /*------------- Toggle checkbox ---------*/
	  case MC_TOGGLE:
togglex:
	    if(!any_addrs_avail(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("No entries to select"));
                break;
	    }

	    if(entry_is_clickable(as.top_ent+as.cur_row)){
		clickable_warning(as.top_ent+as.cur_row);
		break;
	    }

	    if(empty){
		empty_warning(as.top_ent+as.cur_row);
                break;
	    }

	    if(is_addr(as.top_ent+as.cur_row)){
		dl = dlist(as.top_ent+as.cur_row);

		if(style == SelectAddrLccCom && dl->type == ListEnt)
		  q_status_message(SM_ORDER, 0, 4,
	  _("You may only select whole lists for Lcc"));
		else if(style == SelectAddrLccCom && dl->type != ListHead)
		  q_status_message(SM_ORDER, 0, 4,
	  _("You may only select lists for Lcc, use Bcc for personal entries"));
		else if(dl->type == ListHead || dl->type == Simple){
                    current_changed_flag++;
		    if(entry_is_checked(pab->address_book->checks,
				        (a_c_arg_t)dl->elnum)){
			entry_unset_checked(pab->address_book->checks,
					    (a_c_arg_t)dl->elnum);
			checkedn--;
			if(checkedn == 0)
			  setall_changed++;
		    }
		    else{
			entry_set_checked(pab->address_book->checks,
					    (a_c_arg_t)dl->elnum);
			if(checkedn == 0)
			  setall_changed++;

			checkedn++;
		    }
		}
		else
		  q_status_message(SM_ORDER, 0, 4,
      _("You may not select list members, only whole lists or personal entries"));
	    }
	    else
              q_status_message(SM_ORDER, 0, 4,
			       _("You may only select addresses or lists"));

            break;


	    /*------ Turn all checkboxes on ---------*/
	  case MC_SELALL:
selall:
	    if(!any_addrs_avail(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("No entries to select"));
                break;
	    }

	    if(entry_is_clickable(as.top_ent+as.cur_row)){
		clickable_warning(as.top_ent+as.cur_row);
		break;
	    }

	    if(empty){
		empty_warning(as.top_ent+as.cur_row);
                break;
	    }

	    {
		adrbk_cntr_t num, ab_count;
	     
		ab_count = adrbk_count(pab->address_book);
		setall_changed++;
		if(checkedn){				/* unset All */
		    for(num = 0; num < ab_count; num++){
			if(entry_is_checked(pab->address_book->checks,
					    (a_c_arg_t)num)){
			    entry_unset_checked(pab->address_book->checks,
					        (a_c_arg_t)num);
			    checkedn--;
			}
		    }
		}
		else{					/* set All */
		    for(num = 0; num < ab_count; num++){
			if(!entry_is_checked(pab->address_book->checks,
					     (a_c_arg_t)num)){
			    entry_set_checked(pab->address_book->checks,
					      (a_c_arg_t)num);
			    checkedn++;
			}
		    }
		}

		ps->mangled_body = 1;
		start_disp = 0;
	    }

            break;


	    /*---------- Turn on ListMode -----------*/
	  case MC_LISTMODE:
	    as.checkboxes = 1;
	    for(i = 0; i < as.n_addrbk; i++){
		pab = &as.adrbks[i];
		init_disp_form(pab, ps->VAR_ABOOK_FORMATS, i);
	    }

	    (void)calculate_field_widths();
	    ps->mangled_footer = 1;
	    ps->mangled_body = 1;
	    start_disp  = 0;
            q_status_message(SM_ORDER, 0, 4,
		  _("Use \"X\" to select addresses or lists"));
            break;


            /*--------- Compose -----------*/
	  case MC_COMPOSE:
	    (void)ab_compose_to_addr(as.top_ent+as.cur_row, 0, 0);
            break;


            /*--------- Alt Compose -----------*/
	  case MC_ROLE:
	    (void)ab_compose_to_addr(as.top_ent+as.cur_row, 0, 1);
            break;


#ifdef	ENABLE_LDAP
            /*------ Query Directory ------*/
	  case MC_QUERY_SERV:
q_server:
	   {char *err_mess = NULL, **err_mess_p;
	    int   query_type;

	    if(!directory_ok){
                q_status_message(SM_ORDER, 0, 4,
				 (style == SelectAddrLccCom)
				    ? "Can't search server for Lcc"
				    : "Can't search server from here");
                break;
	    }
	    else if(as.checkboxes){
                q_status_message(SM_ORDER, 0, 4,
				 "Can't search server when using ListMode");
                break;
	    }

	    if(error_message)
	      err_mess_p = error_message;
	    else
	      err_mess_p = &err_mess;

	    /*
	     * The query lines are indexed by their adrbk_num. (Just using
	     * that because it was handy and not used for addrbooks.)
	     */
	    query_type = adrbk_num_from_lineno(as.top_ent+as.cur_row);

	    if((addr = query_server(ps, are_selecting, &quit, query_type,
				    err_mess_p)) != NULL){
		if(are_selecting)
		  return(addr);
		
		fs_give((void **)&addr);
	    }

	    if(err_mess_p && *err_mess_p && !error_message){
		q_status_message(SM_ORDER, 0, 4, *err_mess_p);
		fs_give((void **)err_mess_p);
	    }
	   }

            break;
#endif	/* ENABLE_LDAP */


            /*----------- Where is (search) ----------------*/
	  case MC_WHEREIS:
	    warped = 0;
	    new_top_ent = ab_whereis(&warped, command_line);

	    if(new_top_ent != NO_LINE){
		if(warped || new_top_ent != as.top_ent){
		    as.top_ent     = new_top_ent;
		    start_disp     = 0;
		    ps->mangled_body = 1;
		}
		else
		    current_changed_flag++;
	    }

	    ps->mangled_footer = 1;
	    break;


            /*----- Select entries to work on --*/
	  case MC_SELECT:
	    if(!any_addrs_avail(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("No entries to select"));
                break;
	    }

	    if(!cur_is_open()){
		if(entry_is_askserver(as.top_ent+as.cur_row))
		  q_status_message(SM_ORDER, 0, 4,
	   _("Select is only available from within an expanded address book"));
		else
		  clickable_warning(as.top_ent+as.cur_row);

		break;
	    }

	    dl = dlist(as.top_ent+as.cur_row);
	    if(dl->type == Empty){
		empty_warning(as.top_ent+as.cur_row);
		break;
	    }

	    {int were_selections = as.selections;

	      ab_select(ps, pab ? pab->address_book : NULL,
		         as.top_ent+as.cur_row, command_line, &start_disp);

	      if((!were_selections && as.selections)
	         || (were_selections && !as.selections)){
		ps->mangled_footer = 1;
		for(i = 0; i < as.n_addrbk; i++)
		  init_disp_form(&as.adrbks[i],
				 ps->VAR_ABOOK_FORMATS, i);
	      }
	    }

            break;


            /*----------- Select current entry ----------*/
	  case MC_SELCUR:
	    if(!any_addrs_avail(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("No entries to select"));
                break;
	    }

	    if(entry_is_clickable(as.top_ent+as.cur_row)){
		clickable_warning(as.top_ent+as.cur_row);
		break;
	    }

	    if(empty){
		empty_warning(as.top_ent+as.cur_row);
                break;
	    }

	    if(is_addr(as.top_ent+as.cur_row)){
		dl = dlist(as.top_ent+as.cur_row);

		if(dl->type == ListHead || dl->type == Simple){
		    int do_init_disp = 0;
		    long ll;

		    current_changed_flag++;

		    if(entry_is_selected(pab->address_book->selects,
					 (a_c_arg_t)dl->elnum)){
			DL_CACHE_S *dlc, dlc_restart;

			as.selections--;
			if(as.selections == 0)
			  do_init_disp++;

			entry_unset_selected(pab->address_book->selects,
					     (a_c_arg_t)dl->elnum);

			if(as.zoomed){
			    dlc = get_dlc(as.top_ent+as.cur_row);
			    if(as.selections){
				flush_dlc_from_cache(dlc);
				dlc = get_dlc(as.top_ent+as.cur_row);
				ps->mangled_body = 1;
				if(dlc->type == DlcEnd){
				    r = prev_selectable_line(as.cur_row +
							     as.top_ent,
							     &new_line);
				    if(r){
					as.cur_row = new_line - as.top_ent;
					if(as.cur_row < 0){
					    as.top_ent += as.cur_row;
					    as.cur_row  = 0;
					    start_disp  = 0;
					}
					else
					  start_disp = as.cur_row;
				    }
				}
				else
				  start_disp = MAX(as.cur_row-1,0);
			    }
			    else{
				dlc_restart = *dlc;
				as.zoomed = 0;
				q_status_message(SM_ORDER, 0, 2,
				  _("Zoom Mode is now off, no entries selected"));

				warp_to_dlc(&dlc_restart, 0L);
				/* put current entry in middle of screen */
				as.top_ent =
					first_line(0L - (long)as.l_p_page/2L);
				as.cur_row = 0L - as.top_ent;
				ps->mangled_body = 1;
				start_disp = 0;
			    }
			}
			else if(F_OFF(F_UNSELECT_WONT_ADVANCE,ps_global)){

			    r = next_selectable_line(as.cur_row+as.top_ent,
						     &new_line);
			    if(r){

				for(ll = new_line;
				    (dl=dlist(ll))->type != End;
				    ll++)
				  if(dl->type == ListHead || dl->type == Simple)
				    break;

				if(dl->type != End)
				  new_line = ll;

				as.cur_row = new_line - as.top_ent;
				if(as.cur_row >= as.l_p_page){
				    /*-- Changed pages --*/
				    as.top_ent += as.l_p_page;
				    as.cur_row -= as.l_p_page;
				    /* if it is still off screen */
				    if(as.cur_row >= as.l_p_page){
					as.top_ent += (as.cur_row-as.l_p_page+1);
					as.cur_row  = (as.l_p_page - 1);
				    }

				    start_disp       = 0;
				    ps->mangled_body = 1;
				}
			    }
			}
		    }
		    else{
			if(as.selections == 0)
			  do_init_disp++;
			
			as.selections++;

			entry_set_selected(pab->address_book->selects,
					 (a_c_arg_t)dl->elnum);
			r = next_selectable_line(as.cur_row+as.top_ent,
						 &new_line);
			if(r){

			    for(ll = new_line;
				(dl=dlist(ll))->type != End;
				ll++)
			      if(dl->type == ListHead || dl->type == Simple)
				break;

			    if(dl->type != End)
			      new_line = ll;

			    as.cur_row = new_line - as.top_ent;
			    if(as.cur_row >= as.l_p_page){
				/*-- Changed pages --*/
				as.top_ent += as.l_p_page;
				as.cur_row -= as.l_p_page;
				/* if it is still off screen */
				if(as.cur_row >= as.l_p_page){
				    as.top_ent += (as.cur_row-as.l_p_page+1);
				    as.cur_row  = (as.l_p_page - 1);
				}

				start_disp       = 0;
				ps->mangled_body = 1;
			    }
			}
		    }

		    /*
		     * If we switch from selected to non-selected or
		     * vice versa, we have to init_disp_form() for all
		     * the addrbooks, in case we're using the X instead
		     * of bold.
		     */
		    if(do_init_disp){
			ps->mangled_footer = 1;
			for(i = 0; i < as.n_addrbk; i++)
			  init_disp_form(&as.adrbks[i],
					 ps->VAR_ABOOK_FORMATS, i);
		    }
		}
		else
		  q_status_message(SM_ORDER, 0, 4,
      _("You may not select list members, only whole lists or personal entries"));
	    }
	    else
              q_status_message(SM_ORDER, 0, 4,
			       _("You may only select addresses or lists"));

            break;


            /*--- Zoom in and look only at selected entries (or zoom out) --*/
	  case MC_ZOOM:
	    as.zoomed = (1 - as.zoomed);
	    if(as.zoomed)
	      ab_zoom((pab && pab->address_book) ? pab->address_book->selects
						 : NULL,
		      &start_disp);
	    else{
		q_status_message(SM_ORDER, 0, 2, _("Zoom Mode is now off"));
		ab_unzoom(&start_disp);
	    }

            break;


            /*--- Apply a command -----------*/
	  case MC_APPLY:
	    if(as.selections){
		if(((ab_apply_cmd(ps, pab ? pab->address_book : NULL,
	                        as.top_ent+as.cur_row, command_line) &&
		     F_ON(F_AUTO_UNZOOM, ps)) || !as.selections) && as.zoomed){

		    ab_unzoom(NULL);
		    ps_global->mangled_body = 1;
		}

		/*
		 * In case the line we're now at is not a selectable
		 * field.
		 *
		 * We set start_disp to zero here but rely on the called
		 * routine to set mangled_body if appropriate.
		 */
		start_disp = 0;
		new_line = first_selectable_line(as.cur_row+as.top_ent);
		if(new_line != NO_LINE
		   && new_line != as.cur_row+as.top_ent){
		    as.cur_row = new_line - as.top_ent;
		    if(as.cur_row < 0){
			as.top_ent += as.cur_row;
			as.cur_row  = 0;
		    }
		    else if(as.cur_row >= as.l_p_page){
			as.top_ent += (as.cur_row - as.l_p_page + 1);
			as.cur_row  = as.l_p_page - 1;
		    }
		}
	    }
	    else
	      q_status_message(SM_ORDER, 0, 2,
				   _("No selected entries to apply command to"));

            break;


            /*--------- QUIT pine -----------*/
	  case MC_QUIT:
	    dprint((7, "Quitting pine from addrbook\n"));
            ps->next_screen = quit_screen;
            break;


            /*--------- Top of Folder list -----------*/
	  case MC_COLLECTIONS:
	    dprint((7, "Goto folder lister from addrbook\n"));
            ps->next_screen = folder_screen;
            break;


            /*---------- Open specific new folder ----------*/
	  case MC_GOTO:
	    dprint((7, "Goto from addrbook\n"));
	    ab_goto_folder(command_line);
            break;


            /*--------- Index -----------*/
	  case MC_INDEX:
	    dprint((7, "Goto message index from addrbook\n"));
	    if(THREADING()
	       && sp_viewing_a_thread(ps->mail_stream)
	       && unview_thread(ps, ps->mail_stream, ps->msgmap)){
		ps->next_screen = mail_index_screen;
		ps->view_skipped_index = 0;
		ps->mangled_screen = 1;
	    }

            ps->next_screen = mail_index_screen;
            break;


	    /*----------------- Print --------------------*/
	  case MC_PRINTTXT:
	    (void)ab_print(0);
	    ps->mangled_screen = 1;
            break;


	    /*------ Copy entries into an abook ----*/
	  case MC_SAVE:
	    if(!any_addrs_avail(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("No entries to save"));
                break;
	    }

	    if(entry_is_clickable(as.top_ent+as.cur_row)){
		clickable_warning(as.top_ent+as.cur_row);
		break;
	    }

	    if(empty){
		empty_warning(as.top_ent+as.cur_row);
                break;
	    }

	    (void)ab_save(ps, pab ? pab->address_book : NULL,
	                  as.top_ent+as.cur_row, command_line, 0);
	    break;


	    /*------ Forward an entry in mail -----------*/
	  case MC_FORWARD:
	    if(!any_addrs_avail(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("No entries to forward"));
                break;
	    }

	    if(entry_is_clickable(as.top_ent+as.cur_row)){
		clickable_warning(as.top_ent+as.cur_row);
		break;
	    }

	    if(empty){
		empty_warning(as.top_ent+as.cur_row);
                break;
	    }

	    if(!is_addr(as.top_ent+as.cur_row)){
                q_status_message(SM_ORDER, 0, 4, _("Nothing to forward"));
                break;
	    }

	    dl = dlist(as.top_ent+as.cur_row);
	    if(dl->type != ListHead && dl->type != Simple){
		q_status_message(SM_ORDER, 0, 4,
		    _("Can only forward whole entries"));
		break;
	    }

	    (void)ab_forward(ps, as.top_ent+as.cur_row, 0);
            ps->mangled_footer = 1;
            break;


	  case MC_REPAINT:
	    /* ^L attempts to resynchronize with changed addrbooks */
	    if(adrbk_check_all_validity_now())
	      (void)resync_screen(pab, style, checkedn);

	    /* fall through */

	  case MC_RESIZE:
	    mark_status_dirty();
	    mark_titlebar_dirty();
	    mark_keymenu_dirty();
	    ClearBody();
            ps->mangled_screen = 1;
	    if(c == KEY_RESIZE)
	      ab_resize();

	    break;


	  case MC_UTF8:
	    bogus_utf8_command(utf8str, F_ON(F_USE_FK, ps) ? "F1" : "?");
	    break;


	    /*------ Some back compatibility messages -----*/
	  case MC_UNKNOWN:
	    if(c == 'e' && !are_selecting){
		q_status_message(SM_ORDER | SM_DING, 0, 2,
	  _("Command \"E\" not defined.  Use \"View/Update\" to edit an entry"));
		break;
	    }
	    else if(c == 's'
	      && !(are_selecting || entry_is_clickable(as.top_ent+as.cur_row))){
		q_status_message(SM_ORDER | SM_DING, 0, 2,
	    _("Command \"S\" not defined.  Use \"AddNew\" to create a list"));
		break;
	    }
	    else if(c == 'z' && !are_selecting){
		q_status_message(SM_ORDER | SM_DING, 0, 2,
	 _("Command \"Z\" not defined.  Use \"View/Update\" to add to a list"));
		break;
	    }
	    /* else, fall through */

	  default:
	    bogus_command(c, F_ON(F_USE_FK, ps) ? "F1" : "?");
	    break;
	}

	if(ps->next_screen != SCREEN_FUN_NULL)
	  quit++;
    }
    
    erase_selections();
    return NULL;
}


/*
 * Turn on zoom mode and zoom in if applicable.
 *
 * Args   selecteds -- tells which entries are selected in current abook
 *       start_disp -- Passed in so we can set it back in the caller
 */
void
ab_zoom(EXPANDED_S *selecteds, int *start_disp)
{
    AddrScrn_Disp   *dl;
    DL_CACHE_S *dlc, dlc_restart;

    as.zoomed = 1;

    if(as.selections){
	q_status_message(SM_ORDER, 0, 2, _("Zoom Mode is now on"));
	if(cur_is_open()){
	    dl = dlist(as.top_ent+as.cur_row);
	    if((dl->type == ListHead ||
		dl->type == Simple ||
		dl->type == ListEmpty ||
		dl->type == ListClickHere ||
		dl->type == ListEnt) &&
	       entry_is_selected(selecteds, (a_c_arg_t)dl->elnum)){
		dlc = get_dlc(as.top_ent+as.cur_row);
		dlc_restart = *dlc;
		warp_to_dlc(&dlc_restart, 0L);
		/* put current entry in middle of screen */
		as.top_ent = first_line(0L - (long)as.l_p_page/2L);
		as.cur_row = 0L - as.top_ent;
	    }
	    else{
		long new_ent;

		warp_to_top_of_abook(as.cur);
		as.top_ent     = 0L;
		new_ent        = first_selectable_line(0L);
		if(new_ent == NO_LINE)
		  as.cur_row = 0L;
		else
		  as.cur_row = new_ent;

		/* if it is off screen */
		if(as.cur_row >= as.l_p_page){
		    as.top_ent += (as.cur_row - as.l_p_page + 1);
		    as.cur_row  = (as.l_p_page - 1);
		}
	    }
	}
	else{
	    dlc = get_dlc(as.top_ent+as.cur_row);
	    dlc_restart = *dlc;
	    warp_to_dlc(&dlc_restart, 0L);
	    /* put current entry in middle of screen */
	    as.top_ent = first_line(0L - (long)as.l_p_page/2L);
	    as.cur_row = 0L - as.top_ent;
	}

	ps_global->mangled_body = 1;
	*start_disp  = 0;
    }
    else{
	as.zoomed = 0;
	q_status_message(SM_ORDER, 0, 2, _("No selected entries to zoom on"));
    }
}


/*
 * Turn off zoom mode and zoom out if applicable.
 *
 * Args   start_disp -- Passed in so we can set it back in the caller
 */
void
ab_unzoom(int *start_disp)
{
    DL_CACHE_S *dlc, dlc_restart;

    as.zoomed = 0;
    dlc = get_dlc(as.top_ent+as.cur_row);
    if(dlc->type == DlcZoomEmpty){
	long new_ent;

	warp_to_beginning();
	as.top_ent = 0L;
	new_ent    = first_selectable_line(0L);
	if(new_ent == NO_LINE)
	  as.cur_row = 0L;
	else
	  as.cur_row = new_ent;
	
	/* if it is off screen */
	if(as.cur_row >= as.l_p_page){
	    as.top_ent += (as.cur_row - as.l_p_page + 1);
	    as.cur_row  = (as.l_p_page - 1);
	}
    }
    else{
	dlc_restart = *dlc;
	warp_to_dlc(&dlc_restart, 0L);
	/* put current entry in middle of screen */
	as.top_ent = first_line(0L - (long)as.l_p_page/2L);
	as.cur_row = 0L - as.top_ent;
    }

    ps_global->mangled_body = 1;
    if(start_disp)
      *start_disp  = 0;
}


/*
 * Post an empty addrbook warning.
 *
 * Args: cur_line     -- The current line position (in global display list)
 *			 of cursor
 */
void
empty_warning(long int cur_line)
{
    register AddrScrn_Disp *dl;

    dl = dlist(cur_line);
    if(dl->type == NoAbooks)
      q_status_message(SM_ORDER, 0, 4,
		       _("No address books configured, use Setup"));
    else if(dl->type == Empty)
      q_status_message(SM_ORDER, 0, 4, _("Address Book is Empty"));
    else
      q_status_message(SM_ORDER, 0, 4, _("Distribution List is Empty"));
}


/*
 * Tell user to click on this to expand.
 *
 * Args: cur_line     -- The current line position (in global display list)
 *			 of cursor
 */
void
clickable_warning(long int cur_line)
{
    register AddrScrn_Disp *dl;

    dl = dlist(cur_line);
    if(dl->type == Title || dl->type == ClickHereCmb)
      q_status_message(SM_ORDER, 0, 4, _("Address Book not expanded, use \">\" to expand"));
    else
      q_status_message(SM_ORDER, 0, 4, _("Distribution List not expanded, use \">\" to expand"));
}


/*
 * Post a no tabs warning.
 */
void
no_tabs_warning(void)
{
    q_status_message(SM_ORDER, 0, 4, "Tabs not allowed in address book");
}


/*
 * Prompt for command to apply to selected entries
 *
 * Returns: 1 if the entries are successfully commanded.
 *          0 otherwise
 */
int
ab_apply_cmd(struct pine *ps, AdrBk *abook, long int cur_line, int command_line)
{
    int ret = 0;
    static ESCKEY_S opts[] = {
	{'c', 'c', "C", "ComposeTo"},
	{'d', 'd', "D", "Delete"},
	{'%', '%', "%", "Print"},
	{'f', 'f', "F", "Forward"},
	{'s', 's', "S", "Save"},
	{'#', '#', "#", "Role"},
	{  0, '%',  "",  ""},
	{-1,   0, NULL, NULL}};
#define PHANTOM_PRINT 6

    dprint((7, "- ab_apply_cmd -\n"));


    opts[PHANTOM_PRINT].ch = (F_ON(F_ENABLE_PRYNT, ps_global)) ? 'y' : -1;

    switch(radio_buttons("APPLY command : ", command_line, opts, 'z', 'x',
			 NO_HELP, RB_NORM)){
      case 'c':
	ret = ab_compose_to_addr(cur_line, 1, 0);
	break;

      case '#':
	ret = ab_compose_to_addr(cur_line, 1, 1);
	break;

      case 'd':
	ret = ab_agg_delete(ps, 1);
	break;

      case '%':
	ret = ab_print(1);
	break;

      case 'f':
	ret = ab_forward(ps, cur_line, 1);
	break;

      case 's':
	ret = ab_save(ps, abook, cur_line, command_line, 1);
	break;

      case 'x':
	cmd_cancelled("Apply command");
	break;

      case 'z':
        q_status_message(SM_INFO, 0, 2,
			 "Cancelled, there is no default command");
	break;
    }

    ps_global->mangled_footer = 1;

    return(ret);
}


/*
 * Allow user to mark some entries "selected".
 */
void
ab_select(struct pine *ps, AdrBk *abook, long int cur_line, int command_line, int *start_disp)
{
    static ESCKEY_S sel_opts1[] = {
	{'a', 'a', "A", "unselect All"},
	{ 0 , 'c', "C", NULL},
	{'b', 'b', "B", "Broaden selctn"},
	{'n', 'n', "N", "Narrow selctn"},
	{'f', 'f', "F", "Flip selected"},
	{-1, 0, NULL, NULL}
    };
    static char *sel_pmt1 = "ALTER selection : ";
    static ESCKEY_S sel_opts2[] = {
	{'a', 'a', "A", "select All"},
	{'c', 'c', "C", "select Cur"},
	{'t', 't', "T", "Text"},
	{'s', 's', "S", "Status"},
	{-1, 0, NULL, NULL}
    };
    static char *sel_pmt2 = "SELECT criteria : ";
    ESCKEY_S      *sel_opts;
    HelpType       help = NO_HELP;
    adrbk_cntr_t   num, ab_count;
    int            q = 0, rv = 0, narrow = 0,
                   do_flush = 0, do_warp = 0, prevsel,
                   move_current = 0, do_beginning = 0; 
    long           new_ent;
    AddrScrn_Disp *dl;
    DL_CACHE_S    *dlc, dlc_restart;

    dprint((5, "- ab_select -\n"));

    if(cur_is_open()){		/* select applies only to this addrbook */

	ps->mangled_footer = 1;
	prevsel = as.selections;
	dl = dlist(cur_line);
	sel_opts = sel_opts2;

	/*
	 * If already some selected, ask how to alter that selection.
	 */
	if(as.selections){
	    sel_opts += 2;	/* don't offer all or current below */
	    if(dl && (dl->type == ListHead || dl->type == Simple)){
		sel_opts1[1].label = entry_is_selected(abook->selects,
						       (a_c_arg_t)dl->elnum)
					       ? "unselect Cur"
					       : "select Cur";
		sel_opts1[1].ch = 'c';
	    }
	    else
	      sel_opts1[1].ch = -2;		/* don't offer this choice */

	    switch(q = radio_buttons(sel_pmt1, command_line, sel_opts1,
				     'a', 'x', help, RB_NORM)){
	      case 'n':			/* narrow selection */
		narrow++;
	      case 'b':			/* broaden selection */
		q = 0;			/* but don't offer criteria prompt */
		break;

	      case 'c':			/* select or unselect current */
	      case 'a':			/* select or unselect all */
	      case 'f':			/* flip selections */
	      case 'x':			/* cancel */
		break;

	      default:
		q_status_message(SM_ORDER | SM_DING, 3, 3,
				 "Unsupported Select option");
		return;
	    }
	}

	if(abook && dl &&
	   (dl->type == ListHead || dl->type == Simple)){
	    sel_opts1[1].label = entry_is_selected(abook->selects,
						   (a_c_arg_t)dl->elnum)
					   ? "unselect Cur"
					   : "select Cur";
	    sel_opts1[1].ch = 'c';
	}
	else
	  sel_opts1[1].ch = -2;		/* don't offer this choice */

	if(!q)
	  q = radio_buttons(sel_pmt2, command_line, sel_opts,
			    'c', 'x', help, RB_NORM);

	*start_disp = 0;
	dlc = get_dlc(cur_line);
	dlc_restart = *dlc;
	ab_count = adrbk_count(abook);

	switch(q){
	  case 'x':				/* cancel */
	    cmd_cancelled("Select command");
	    break;

	  case 'c':			/* select/unselect current */
	    ps->mangled_body = 1;
	    if(entry_is_selected(abook->selects, (a_c_arg_t)dl->elnum)){
		entry_unset_selected(abook->selects, (a_c_arg_t)dl->elnum);
		as.selections--;

		if(as.selections == 0 && as.zoomed){
		    as.zoomed = 0;
		    q_status_message(SM_ORDER, 0, 2,
				 "Zoom Mode is now off, no entries selected");
		    do_warp++;
		}
		else if(as.zoomed){
		    move_current++;
		    do_flush++;
		}
		else
		  do_flush++;
	    }
	    else{
		entry_set_selected(abook->selects, (a_c_arg_t)dl->elnum);
		as.selections++;

		if(as.selections == 1 && !as.zoomed && F_ON(F_AUTO_ZOOM, ps)){
		    as.zoomed = 1;
		    as.top_ent = dlc_restart.global_row;
		    as.cur_row = 0;
		    do_warp++;
		}
		else
		  do_flush++;
	    }

	    break;

	  case 'a':
	    ps->mangled_body = 1;
	    if(any_selected(abook->selects)){	/* unselect all */
		for(num = 0; num < ab_count; num++){
		    if(entry_is_selected(abook->selects, (a_c_arg_t)num)){
			as.selections--;
			entry_unset_selected(abook->selects, (a_c_arg_t)num);
		    }
		}

		if(as.selections == 0 && as.zoomed){
		    as.zoomed = 0;
		    q_status_message(SM_ORDER, 0, 2,
				"Zoom Mode is now off, all entries UNselected");
		    do_warp++;
		}
		else{
		    char bb[100];

		    snprintf(bb, sizeof(bb), "%s entries UNselected%s%s%s",
			   comatose(prevsel-as.selections),
			   as.selections ? ", still " : "",
			   as.selections ? comatose(as.selections) : "",
			   as.selections ? " selected in other addrbooks" : "");
		    bb[sizeof(bb)-1] = '\0';
		    q_status_message(SM_ORDER, 0, 2, bb);
		    if(as.zoomed)
		      do_beginning++;
		    else
		      do_flush++;
		}
	    }
	    else{				/* select all */
		for(num = 0; num < ab_count; num++){
		    if(!entry_is_selected(abook->selects, (a_c_arg_t)num)){
			as.selections++;
			entry_set_selected(abook->selects, (a_c_arg_t)num);
		    }
		}

		q_status_message1(SM_ORDER, 0, 2, "All %s entries selected",
				 comatose(ab_count));
		if(prevsel == 0 && as.selections > 0 &&
		   !as.zoomed && F_ON(F_AUTO_ZOOM, ps)){
		    as.zoomed = 1;
		    as.top_ent = dlc_restart.global_row - as.cur_row;
		    do_warp++;
		}
		else if(dlc_restart.type == DlcZoomEmpty &&
			as.selections > prevsel)
		  do_beginning++;
		else if(as.zoomed)
		  do_warp++;
		else
		  do_flush++;
	    }

	    break;

	  case 'f':			/* flip selections in this abook */
	    ps->mangled_body = 1;
	    for(num = 0; num < ab_count; num++){
		if(entry_is_selected(abook->selects, (a_c_arg_t)num)){
		    entry_unset_selected(abook->selects, (a_c_arg_t)num);
		    as.selections--;
		}
		else{
		    entry_set_selected(abook->selects, (a_c_arg_t)num);
		    as.selections++;
		}
	    }

	    if(as.zoomed){
		if(as.selections)
		  do_beginning++;
		else{
		    as.zoomed = 0;
		    q_status_message(SM_ORDER, 0, 2, "Zoom Mode is now off");
		    do_warp++;
		}
	    }
	    else
	      do_warp++;

	    q_status_message1(SM_ORDER, 0, 2, "%s entries now selected",
			     comatose(as.selections));

	    break;

	  case 't':
	  case 's':
	    switch(q){
	      case 't':
		rv = ab_select_text(abook, narrow);
		break;

	      case 's':
		rv = ab_select_type(abook, narrow);
		break;
	    }

	    if(!rv){
		ps->mangled_body = 1;
		if(dlc_restart.type == DlcZoomEmpty &&
		   as.selections > prevsel)
		  do_beginning++;
		else if(as.zoomed){
		    if(as.selections == 0){
			as.zoomed = 0;
			q_status_message(SM_ORDER, 0, 2,
					 "Zoom Mode is now off");
			do_warp++;
		    }
		    else
		      do_beginning++;
		}
		else{
		    if(prevsel == 0 && as.selections > 0 &&
		       !as.zoomed && F_ON(F_AUTO_ZOOM, ps)){
			as.zoomed = 1;
			do_beginning++;
		    }
		    else
		      do_warp++;
		}

		if(prevsel == as.selections && prevsel > 0){
		    if(as.selections == 1)
		      q_status_message(SM_ORDER, 0, 2,
			    "No change resulted, 1 entry remains selected");
		    else
		      q_status_message1(SM_ORDER, 0, 2,
			    "No change resulted, %s entries remain selected",
			    comatose(as.selections));
		}
		else if(prevsel == 0){
		    if(as.selections == 1)
		      q_status_message(SM_ORDER, 0, 2,
				       "Select matched 1 entry");
		    else if(as.selections > 1)
		      q_status_message1(SM_ORDER, 0, 2,
				        "Select matched %s entries",
				        comatose(as.selections));
		    else
		      q_status_message(SM_ORDER, 0, 2,
				       "Select failed! No entries selected");
		}
		else if(as.selections == 0){
		    if(prevsel == 1)
		      q_status_message(SM_ORDER, 0, 2,
				    "The single selected entry is UNselected");
		    else
		      q_status_message1(SM_ORDER, 0, 2,
					"All %s entries UNselected",
					comatose(prevsel));
		}
		else if(narrow){
		    if(as.selections == 1 && (prevsel-as.selections) == 1)
		      q_status_message(SM_ORDER, 0, 2,
			    "1 entry now selected, 1 entry was UNselected");
		    else if(as.selections == 1)
		      q_status_message1(SM_ORDER, 0, 2,
			    "1 entry now selected, %s entries were UNselected",
			    comatose(prevsel-as.selections));
		    else if((prevsel-as.selections) == 1)
		      q_status_message1(SM_ORDER, 0, 2,
			    "%s entries now selected, 1 entry was UNselected",
			    comatose(as.selections));
		    else
		      q_status_message2(SM_ORDER, 0, 2,
		"%s entries now selected, %s entries were UNselected",
			comatose(as.selections),
			comatose(prevsel-as.selections));
		}
		else{
		    if((as.selections-prevsel) == 1)
		      q_status_message1(SM_ORDER, 0, 2,
				"1 new entry selected, %s entries now selected",
				comatose(as.selections));
		    else if(as.selections == 1)
		      q_status_message1(SM_ORDER, 0, 2,
				"%s new entries selected, 1 entry now selected",
				comatose(as.selections-prevsel));
		    else
		      q_status_message2(SM_ORDER, 0, 2,
		    "%s new entries selected, %s entries now selected",
			    comatose(as.selections-prevsel),
			    comatose(as.selections));
		}
	    }

	    break;

	  default :
	    q_status_message(SM_ORDER | SM_DING, 3, 3,
			     "Unsupported Select option");
	    break;
	}
    }
    else{
	if(F_ON(F_CMBND_ABOOK_DISP,ps_global))
	  q_status_message(SM_ORDER | SM_DING, 3, 3,
	   "Select is only available from within an expanded address book");
	else
	  q_status_message(SM_ORDER | SM_DING, 3, 3,
	   "Select is only available when viewing an individual address book");

	return;
    }

    if(rv)
      return;

    if(do_beginning){
	warp_to_beginning();	/* just go to top */
	as.top_ent = 0L;
	new_ent    = first_selectable_line(0L);
	if(new_ent == NO_LINE)
	  as.cur_row = 0L;
	else
	  as.cur_row = new_ent;
	
	/* if it is off screen */
	if(as.cur_row >= as.l_p_page){
	    as.top_ent += (as.cur_row - as.l_p_page + 1);
	    as.cur_row  = (as.l_p_page - 1);
	}
    }
    else if(do_flush)
      flush_dlc_from_cache(&dlc_restart);
    else if(do_warp)
      warp_to_dlc(&dlc_restart, dlc_restart.global_row);
    
    if(move_current){
	dlc = get_dlc(cur_line);
	if(dlc->type == DlcEnd){
	    as.cur_row--;
	    if(as.cur_row < 0){
		as.top_ent += as.cur_row;  /* plus a negative number */
		as.cur_row = 0;
		*start_disp = 0;
	    }
	    else
	      *start_disp = as.cur_row;

	}
	else
	  *start_disp = as.cur_row;
    }
}


/*
 * Selects based on whether an entry is a list or not.
 *
 * Returns:  0 if search went ok
 *          -1 if there was a problem
 */
int
ab_select_type(AdrBk *abook, int narrow)
{
    static ESCKEY_S ab_sel_type_opt[] = {
	{'s', 's', "S", "Simple"},
	{'l', 'l', "L", "List"},
	{-1, 0, NULL, NULL}
    };
    static char *ab_sel_type = "Select Lists or Simples (non Lists) ? ";
    int          type;
    adrbk_cntr_t num, ab_count;

    dprint((6, "- ab_select_type -\n"));

    if(!abook)
      return -1;

    switch(type = radio_buttons(ab_sel_type, -FOOTER_ROWS(ps_global),
				ab_sel_type_opt, 'l', 'x', NO_HELP, RB_NORM)){
      case 'l':
	break;

      case 's':
	break;

      case 'x':
	cmd_cancelled("Select");
	return -1;

      default:
	dprint((1,"\n - BOTCH: ab_select_type unknown option\n"));
	return -1;
    }

    ab_count = adrbk_count(abook);
    for(num = 0; num < ab_count; num++){
	AdrBk_Entry *abe;
	int matched;

	/*
	 * If it won't possibly change state, don't look at it.
	 */
	if((narrow && !entry_is_selected(abook->selects, (a_c_arg_t)num)) ||
	   (!narrow && entry_is_selected(abook->selects, (a_c_arg_t)num)))
	  continue;
	
	abe = adrbk_get_ae(abook, (a_c_arg_t) num);
	matched = ((type == 's' && abe->tag == Single) ||
		   (type == 'l' && abe->tag == List));

	if(narrow && !matched){
	    entry_unset_selected(abook->selects, (a_c_arg_t)num);
	    as.selections--;
	}
	else if(!narrow && matched){
	    entry_set_selected(abook->selects, (a_c_arg_t)num);
	    as.selections++;
	}
    }

    return 0;
}


/*
 * Selects based on string matches in various addrbook fields.
 *
 * Returns:  0 if search went ok
 *          -1 if there was a problem
 */
int
ab_select_text(AdrBk *abook, int narrow)
{
    static ESCKEY_S ab_sel_text_opt[] = {
	{'n', 'n', "N", "Nickname"},
	{'a', 'a', "A", "All Text"},
	{'f', 'f', "F", "Fullname"},
	{'e', 'e', "E", "Email Addrs"},
	{'c', 'c', "C", "Comment"},
	{'z', 'z', "Z", "Fcc"},
	{-1, 0, NULL, NULL}
    };
    static char *ab_sel_text =
     "Select based on Nickname, All text, Fullname, Addrs, Comment, or Fcc ? ";
    HelpType     help = NO_HELP;
    int          type, r;
    char         sstring[80+1], prompt[80];
    adrbk_cntr_t num, ab_count;
    char        *fmt = "String in \"%s\" to match : ";

    dprint((6, "- ab_select_text -\n"));

    if(!abook)
      return -1;

    switch(type = radio_buttons(ab_sel_text, -FOOTER_ROWS(ps_global),
				ab_sel_text_opt, 'a', 'x', NO_HELP, RB_NORM)){
      case 'n':
	snprintf(prompt, sizeof(prompt), fmt, "Nickname");
	break;
      case 'a':
	snprintf(prompt, sizeof(prompt), fmt, "All Text");
	break;
      case 'f':
	snprintf(prompt, sizeof(prompt), fmt, "Fullname");
	break;
      case 'e':
	snprintf(prompt, sizeof(prompt), fmt, "addresses");
	break;
      case 'c':
	snprintf(prompt, sizeof(prompt), fmt, "Comment");
	break;
      case 'z':
	snprintf(prompt, sizeof(prompt), fmt, "Fcc");
	break;
      case 'x':
	break;
      default:
	dprint((1,"\n - BOTCH: ab_select_text unknown option\n"));
	return -1;
    }

    prompt[sizeof(prompt)-1] = '\0';

    sstring[0] = '\0';
    while(type != 'x'){
	int flags = OE_APPEND_CURRENT;

	r = optionally_enter(sstring, -FOOTER_ROWS(ps_global), 0,
			     sizeof(sstring), prompt, NULL, help, &flags);
	switch(r){
	  case 3:	/* BUG, no help */
	  case 4:
	    continue;
	
	  default:
	    break;
	}

	if(r == 1 || sstring[0] == '\0')
	  r = 'x';
	
	break;
    }

    if(type == 'x' || r == 'x'){
	cmd_cancelled("Select");
	return -1;
    }

    ab_count = adrbk_count(abook);
    for(num = 0; num < ab_count; num++){
	AdrBk_Entry *abe;
	int matched;

	/*
	 * If it won't possibly change state, don't look at it.
	 */
	if((narrow && !entry_is_selected(abook->selects, (a_c_arg_t)num)) ||
	   (!narrow && entry_is_selected(abook->selects, (a_c_arg_t)num)))
	  continue;
	
	abe = adrbk_get_ae(abook, (a_c_arg_t) num);
	matched = match_check(abe, type, sstring);

	if(narrow && !matched){
	    entry_unset_selected(abook->selects, (a_c_arg_t)num);
	    as.selections--;
	}
	else if(!narrow && matched){
	    entry_set_selected(abook->selects, (a_c_arg_t)num);
	    as.selections++;
	}
    }

    return 0;
}


/*
 * Returns: 1 if a match is found for the entry
 *          0 no match
 *         -1 error
 */
int
match_check(AdrBk_Entry *abe, int type, char *string)
{
    static int err = -1;
    static int match = 1;
    static int nomatch = 0;
    unsigned int checks;
#define CK_NICKNAME	0x01
#define CK_FULLNAME	0x02
#define CK_ADDRESSES	0x04
#define CK_FCC 		0x08
#define CK_COMMENT 	0x10
#define CK_ALL		0x1f

    checks = 0;

    switch(type){
      case 'n':				/* Nickname */
	checks |= CK_NICKNAME;
	break;

      case 'f':				/* Fullname */
	checks |= CK_FULLNAME;
	break;

      case 'e':				/* Addrs */
	checks |= CK_ADDRESSES;
	break;

      case 'a':				/* All Text */
	checks |= CK_ALL;
	break;

      case 'z':				/* Fcc */
	checks |= CK_FCC;
	break;

      case 'c':				/* Comment */
	checks |= CK_COMMENT;
	break;

      default:
	q_status_message(SM_ORDER | SM_DING, 3, 3, "Unknown type");
	return(err);
    }

    if(checks & CK_NICKNAME){
	if(abe && abe->nickname && srchstr(abe->nickname, string))
	  return(match);
    }

    if(checks & CK_FULLNAME){
	if(abe &&
	   abe->fullname &&
	   abe->fullname[0] &&
	   srchstr((char *)rfc1522_decode_to_utf8((unsigned char *)tmp_20k_buf,
						  SIZEOF_20KBUF, abe->fullname),
		   string))
	  return(match);
    }

    if(checks & CK_ADDRESSES){
	if(abe &&
	   abe->tag == Single &&
	   abe->addr.addr &&
	   abe->addr.addr[0] &&
	   srchstr((char *)rfc1522_decode_to_utf8((unsigned char *)tmp_20k_buf,
						  SIZEOF_20KBUF, abe->addr.addr),
		   string))
	  return(match);
	else if(abe &&
		abe->tag == List &&
		abe->addr.list){
	    char **p;

	    for(p = abe->addr.list; p != NULL && *p != NULL; p++){
		if(srchstr((char *)rfc1522_decode_to_utf8((unsigned char *)tmp_20k_buf,
							  SIZEOF_20KBUF, *p),
		   string))
		  return(match);
	    }
	}
    }

    if(checks & CK_FCC){
	if(abe &&
	   abe->fcc &&
	   abe->fcc[0] &&
	   srchstr((char *)rfc1522_decode_to_utf8((unsigned char *)tmp_20k_buf,
						  SIZEOF_20KBUF, abe->fcc),
		   string))
	  return(match);
    }

    if(checks & CK_COMMENT && abe && abe->extra && abe->extra[0]){
	size_t n, len;
	unsigned char *p, *tmp = NULL;
	int found_it = 0;

	if((n = 4*strlen(abe->extra)) > SIZEOF_20KBUF-1){
	    len = n+1;
	    p = tmp = (unsigned char *)fs_get(len * sizeof(char));
	}
        else{
	    len = SIZEOF_20KBUF;
	    p = (unsigned char *)tmp_20k_buf;
	}

        if(srchstr((char *)rfc1522_decode_to_utf8(p, len, abe->extra), string))
	  found_it++;

	if(tmp)
	  fs_give((void **)&tmp);

	if(found_it)
	  return(match);
    }

    return(nomatch);
}


/*
 * Go to folder.
 *
 *       command_line -- The screen line on which to prompt
 */
void
ab_goto_folder(int command_line)
{
    char      *go_folder;
    CONTEXT_S *tc;
    int        notrealinbox = 0;

    dprint((2, "- ab_goto_folder -\n"));

    tc = ps_global->context_current;

    go_folder = broach_folder(command_line, 1, &notrealinbox, &tc);

    if(go_folder != NULL)
      visit_folder(ps_global, go_folder, tc, NULL, notrealinbox ? 0L : DB_INBOXWOCNTXT);
}


/*
 * Execute whereis command.
 *
 * Returns value of the new top entry, or NO_LINE if cancelled.
 */
long
ab_whereis(int *warped, int command_line)
{
    int rc, wrapped = 0;
    long new_top_ent, new_line;

    dprint((5, "- ab_whereis -\n"));

    rc = search_book(as.top_ent+as.cur_row, command_line,
		    &new_line, &wrapped, warped);

    new_top_ent = NO_LINE;

    if(rc == -2)
      q_status_message(SM_INFO, 0, 2, _("Address book search cancelled"));

    else if(rc == -1)
      q_status_message(SM_ORDER, 0, 4, _("Word not found"));

    else if(rc == 0){  /* search succeeded */

	if(wrapped == 1)
	  q_status_message(SM_INFO, 0, 2, _("Search wrapped to beginning"));
	else if(wrapped == 2)
	  q_status_message(SM_INFO, 0, 2,
			   _("Current line contains the only match"));

	/* know match is on the same page */
	if(!*warped &&
	    new_line >= as.top_ent &&
	    new_line < as.top_ent+as.l_p_page)
	    new_top_ent = as.top_ent;
	/* don't know whether it is or not, reset top_ent */
	else
	  new_top_ent = first_line(new_line - as.l_p_page/2);

	as.cur_row  = new_line - new_top_ent;
    }

    return(new_top_ent);
}


/*
 * recalculate display parameters for window size change
 */
void
ab_resize(void)
{
    long new_line;
    int  old_l_p_p;
    DL_CACHE_S dlc_buf, *dlc_restart;

    old_l_p_p   = as.l_p_page;
    as.l_p_page = ps_global->ttyo->screen_rows - FOOTER_ROWS(ps_global)
					       - HEADER_ROWS(ps_global);

    dprint((9, "- ab_resize -\n    l_p_p was %d, now %d\n",
	old_l_p_p, as.l_p_page));

    if(as.l_p_page <= 0){
	as.no_op_possbl++;
	return;
    }
    else
      as.no_op_possbl = 0;

    new_line       = as.top_ent + as.cur_row;
    as.top_ent     = first_line(new_line - as.l_p_page/2);
    as.cur_row     = new_line - as.top_ent;
    as.old_cur_row = as.cur_row;

    /* need this to re-initialize Text and Title lines in display */
    /* get the old current line (which may be the wrong width) */
    dlc_restart = get_dlc(new_line);
    /* flush it from cache */
    flush_dlc_from_cache(dlc_restart);
    /* re-get it (should be right now) */
    dlc_restart = get_dlc(new_line);
    /* copy it to local storage */
    dlc_buf = *dlc_restart;
    dlc_restart = &dlc_buf;
    /* flush everything from cache and add that one line back in */
    warp_to_dlc(dlc_restart, new_line);
}


/*
 * Returns 0 if we know for sure that there are no
 * addresses available in any of the addressbooks.
 *
 * Easiest would be to start at 0 and go through the addrbook, but that will
 * be very slow for big addrbooks if we're not close to 0 already.  Instead,
 * starting_hint is a hint at a good place to start looking.
 */
int
any_addrs_avail(long int starting_hint)
{
    register AddrScrn_Disp *dl;
    long lineno;

    /*
     * Look from lineno backwards first, in hopes of finding it in cache.
     */
    lineno = starting_hint;
    for(dl=dlist(lineno);
	dl->type != Beginning;
	dl = dlist(--lineno)){
	if(dl->type == NoAbooks)
	  return 0;

	switch(dl->type){
	  case Simple:
	  case ListEnt:
	  case ListHead:
	  case ZoomEmpty:
	  case Title:
	  case ListClickHere:
	  case ClickHereCmb:
	    return 1;
	  default:
	    break;
	}
    }

    /* search from here forward if we still don't know */
    lineno = starting_hint;
    for(dl=dlist(lineno);
	dl->type != End;
	dl = dlist(++lineno)){
	if(dl->type == NoAbooks)
	  return 0;

	switch(dl->type){
	  case Simple:
	  case ListEnt:
	  case ListHead:
	  case ZoomEmpty:
	  case Title:
	  case ListClickHere:
	  case ClickHereCmb:
	    return 1;
	  default:
	    break;
	}
    }

    return 0;
}


/*
 * Returns 1 if this line is a clickable line.
 */
int
entry_is_clickable(long int lineno)
{
    register AddrScrn_Disp *dl;

    if((dl = dlist(lineno)) &&
       (dl->type == Title || dl->type == ListClickHere ||
        dl->type == ClickHereCmb))
      return 1;

    return 0;
}


/*
 * Returns 1 if this line is a clickable Title line.
 */
int
entry_is_clickable_title(long int lineno)
{
    register AddrScrn_Disp *dl;

    if((dl = dlist(lineno)) && (dl->type == Title || dl->type == ClickHereCmb))
      return 1;

    return 0;
}


/*
 * Returns 1 if an address or list is selected.
 */
int
is_addr(long int lineno)
{
    register AddrScrn_Disp *dl;

    if((dl = dlist(lineno)) && (dl->type == ListHead ||
					dl->type == ListEnt  ||
					dl->type == Simple))
      return 1;

    return 0;
}


/*
 * Returns 1 if type of line is Empty.
 */
int
is_empty(long int lineno)
{
    register AddrScrn_Disp *dl;

    if((dl = dlist(lineno)) &&
       (dl->type == Empty || dl->type == ListEmpty || dl->type == ZoomEmpty))
      return 1;

    return 0;
}


/*
 * Returns 1 if lineno is a list entry
 */
int
entry_is_listent(long int lineno)
{
    register AddrScrn_Disp *dl;

    if((dl = dlist(lineno)) && dl->type == ListEnt)
      return 1;

    return 0;
}


/*
 * Returns 1 if lineno is a fake addrbook for config screen add
 *           or if it is an AskServer line for LDAP query
 */
int
entry_is_addkey(long int lineno)
{
    register AddrScrn_Disp *dl;

    if((dl = dlist(lineno)) && (dl->type == AddFirstPers ||
                                dl->type == AddFirstGlob ||
                                dl->type == AskServer))
      return 1;

    return 0;
}


/*
 * Returns 1 if lineno is the line to ask for directory server query
 */
int
entry_is_askserver(long int lineno)
{
    register AddrScrn_Disp *dl;

    if((dl = dlist(lineno)) && dl->type == AskServer)
      return 1;

    return 0;
}


/*
 * Returns 1 if an add abook here would be global, not personal
 */
int
add_is_global(long int lineno)
{
    register AddrScrn_Disp *dl;

    dl = dlist(lineno);

    if(dl){
	if(dl->type == Title){
	    register PerAddrBook   *pab;

	    pab = &as.adrbks[as.cur];
	    if(pab && pab->type & GLOBAL) 
	      return 1;
	}
	else if(dl->type == AddFirstGlob)
	  return 1;
    }

    return 0;
}


/*
 * Find the first line greater than or equal to line.  (Any line, not
 * necessarily selectable.)
 *
 * Returns the line number of the found line or NO_LINE if there is none.
 *
 * Warning:  This just starts at the passed in line and goes forward until
 * it runs into a line that isn't a Beginning line.  If the line passed in
 * is not in the dlc cache, it will have no way to know when it gets to the
 * real beginning.
 */
long
first_line(long int line)
{
    long lineno;
    register PerAddrBook *pab;
    int i;

    for(lineno=line;
       dlist(lineno)->type == Beginning;
       lineno++)
	;/* do nothing */

    if(dlist(lineno)->type != End)
      return(lineno);
    else{
	for(i = 0; i < as.n_addrbk; i++){
	    pab = &as.adrbks[i];
	    if(pab->ostatus != Open &&
	       pab->ostatus != HalfOpen &&
	       pab->ostatus != ThreeQuartOpen)
	      return NO_LINE;
	}

	as.no_op_possbl++;
	return(NO_LINE);
    }
}


/*
 * Find the line number of the next selectable line.
 *
 * Args: cur_line     -- The current line position (in global display list)
 *			 of cursor
 *       new_line     -- Return value: new line position
 *
 * Result: The new line number is set.
 *       The value 1 is returned if OK or 0 if there is no next line.
 */
int
next_selectable_line(long int cur_line, long int *new_line)
{
    /* skip over non-selectable lines */
    for(cur_line++;
	!line_is_selectable(cur_line) && dlist(cur_line)->type != End;
	cur_line++)
	;/* do nothing */

    if(dlist(cur_line)->type == End)
      return 0;

    *new_line = cur_line;
    return 1;
}


/*
 * Find the line number of the previous selectable line.
 *
 * Args: cur_line     -- The current line position (in global display list)
 *			 of cursor
 *       new_line     -- Return value: new line position
 *
 * Result: The new line number is set.
 *       The value 1 is returned if OK or 0 if there is no previous line.
 */
int
prev_selectable_line(long int cur_line, long int *new_line)
{
    /* skip backwards over non-selectable lines */
    for(cur_line--;
	!line_is_selectable(cur_line) && dlist(cur_line)->type != Beginning;
	cur_line--)
	;/* do nothing */

    if(dlist(cur_line)->type == Beginning)
      return 0;

    *new_line = cur_line;

    return 1;
}


/*
 * Resync the display with the addrbooks, which were just discovered
 * to be out of sync with the display.
 *
 * Returns   1 -- current address book had to be resynced
 *           0 -- current address book not resynced
 */
int
resync_screen(PerAddrBook *pab, AddrBookArg style, int checkedn)
{
    AddrScrn_Disp *dl;
    int            current_resynced = 0;
    DL_CACHE_S     dlc_restart, *dlc = NULL;

    /*
     * The test below gives conditions under which it is safe to go ahead
     * and resync all the addrbooks that are out of sync now, and the
     * display won't change. Otherwise, we have to be careful to preserve
     * some of our state so that we can attempt to restore the screen to
     * a state that is as close as possible to what we have now. If the
     * currently opened address book (pab) is out of date we will lose
     * the expanded state of its distribution lists, which is no big deal.
     * Since resyncing also loses the checked status if we're selecting with
     * ListMode, we don't even attempt it in that case.
     */
    if((ab_nesting_level < 2 && !cur_is_open() && checkedn == 0) ||
       (style == AddrBookScreen &&
        pab &&
	pab->address_book &&
        !(pab->address_book->flags & FILE_OUTOFDATE ||
	  (pab->address_book->rd &&
	   pab->address_book->rd->flags & REM_OUTOFDATE)))){

	if(F_ON(F_CMBND_ABOOK_DISP,ps_global)){
	    dlc = get_dlc(as.top_ent+as.cur_row);
	    dlc_restart = *dlc;
	}

	if(adrbk_check_and_fix_all(1, 0, 1)){
	    ps_global->mangled_footer = 1;  /* why? */
	    if(F_ON(F_CMBND_ABOOK_DISP,ps_global)){
		warp_to_dlc(&dlc_restart, 0L);
		/* put current entry in middle of screen */
		as.top_ent = first_line(0L - (long)as.l_p_page/2L);
		as.cur_row = 0L - as.top_ent;
		ps_global->mangled_screen = 1;
	    }
	}
    }
    else if(style == AddrBookScreen){
	char          *savenick = NULL;
	AdrBk_Entry   *abe;
	adrbk_cntr_t   old_entry_num, new_entry_num;
	long           old_global_row;

	current_resynced++;

	/*
	 * We're going to try to get the nickname of the current
	 * entry and find it again after the resync.
	 */
	dl = dlist(as.top_ent+as.cur_row);
	if(dl->type == ListEnt ||
	   dl->type == ListEmpty ||
	   dl->type == ListClickHere ||
	   dl->type == ListHead ||
	   dl->type == Simple){
	    abe = ae(as.top_ent+as.cur_row);
	    old_entry_num = dl->elnum;
	    old_global_row = as.top_ent+as.cur_row;
	    if(abe && abe->nickname && abe->nickname[0])
	      savenick = cpystr(abe->nickname);
	}

	/* this will close and re-open current addrbook */
	(void)adrbk_check_and_fix_all(1, 0, 1);

	abe = NULL;
	if(savenick){
	    abe = adrbk_lookup_by_nick(pab->address_book, savenick,
				       &new_entry_num);
	    fs_give((void **)&savenick);
	}

	if(abe){	/* If we found the same nickname, move to it */
	    dlc_restart.adrbk_num = as.cur;
	    dlc_restart.dlcelnum  = new_entry_num;
	    dlc_restart.type      = (abe->tag == Single)
					? DlcSimple : DlcListHead;
	    if(old_entry_num == new_entry_num)
	      warp_to_dlc(&dlc_restart, old_global_row);
	    else
	      warp_to_dlc(&dlc_restart, 0L);
	}
	else
	  warp_to_top_of_abook(as.cur);
	
	if(!abe || old_entry_num != new_entry_num){
	    /* put current entry in middle of screen */
	    as.top_ent = first_line(0L - (long)as.l_p_page/2L);
	    as.cur_row = 0L - as.top_ent;
	}
    }

    return(current_resynced);
}


/*
 * Erase all the check marks.
 */
void
erase_checks(void)
{
    int i;
    PerAddrBook *pab;

    for(i = 0; i < as.n_addrbk; i++){
	pab = &as.adrbks[i];
	if(pab->address_book && pab->address_book->checks)
	  exp_free(pab->address_book->checks);

	init_disp_form(pab, ps_global->VAR_ABOOK_FORMATS, i);
    }
}


/*
 * Erase all the selections.
 */
void
erase_selections(void)
{
    int i;
    PerAddrBook *pab;

    for(i = 0; i < as.n_addrbk; i++){
	pab = &as.adrbks[i];
	if(pab->address_book && pab->address_book->selects)
	  exp_free(pab->address_book->selects);
    }

    for(i = 0; i < as.n_addrbk; i++){
	pab = &as.adrbks[i];
	init_disp_form(pab, ps_global->VAR_ABOOK_FORMATS, i);
    }

    as.selections = 0;
}


/*
 * return values of search_in_one_line are or'd combination of these
 */
#define MATCH_NICK      0x1  /* match in field 0 */
#define MATCH_FULL      0x2  /* match in field 1 */
#define MATCH_ADDR      0x4  /* match in field 2 */
#define MATCH_FCC       0x8  /* match in fcc field */
#define MATCH_COMMENT  0x10  /* match in comment field */
#define MATCH_BIGFIELD 0x20  /* match in one of the fields that crosses the
				whole screen, like a Title field */
#define MATCH_LISTMEM  0x40  /* match list member */
/*
 * Prompt user for search string and call find_in_book.
 *
 * Args: cur_line     -- The current line position (in global display list)
 *			 of cursor
 *       command_line -- The screen line to prompt on
 *       new_line     -- Return value: new line position
 *       wrapped      -- Wrapped to beginning of display, tell user
 *       warped       -- Warped to a new location in the addrbook
 *
 * Result: The new line number is set if the search is successful.
 *         Returns 0 if found, -1 if not, -2 if cancelled.
 *       
 */
int
search_book(long int cur_line, int command_line, long int *new_line, int *wrapped, int *warped)
{
    int          i=0, find_result, rc, flags, ku;
    static HISTORY_S *history = NULL;
    char         search_string[MAX_SEARCH + 1];
    char         prompt[MAX_SEARCH + 50], nsearch_string[MAX_SEARCH+1], *p;
    HelpType	 help;
    ESCKEY_S     ekey[6];
    PerAddrBook *pab;
    long         nl;

    dprint((7, "- search_book -\n"));

    init_hist(&history, HISTSIZE);

    search_string[0] = '\0';
    if((p = get_prev_hist(history, "", 0, NULL)) != NULL){
	strncpy(search_string, p, sizeof(search_string));
	search_string[sizeof(search_string)-1] = '\0';
    }

    snprintf(prompt, sizeof(prompt), _("Word to search for [%.*s]: "), MAX_SEARCH, search_string);
    prompt[sizeof(prompt)-1] = '\0';
    help              = NO_HELP;
    nsearch_string[0] = '\0';

    ekey[i].ch    = 0;
    ekey[i].rval  = 0;
    ekey[i].name  = "";
    ekey[i++].label = "";

    ekey[i].ch    = ctrl('Y');
    ekey[i].rval  = 10;
    ekey[i].name  = "^Y";
    /* TRANSLATORS: User is searching in address book. One of the options is to
       search for the First Address */
    ekey[i++].label = _("First Adr");

    ekey[i].ch    = ctrl('V');
    ekey[i].rval  = 11;
    ekey[i].name  = "^V";
    /* TRANSLATORS: Last Address */
    ekey[i++].label = _("Last Adr");

    ekey[i].ch      = KEY_UP;
    ekey[i].rval    = 30;
    ekey[i].name    = "";
    ku = i;
    ekey[i++].label = "";

    ekey[i].ch      = KEY_DOWN;
    ekey[i].rval    = 31;
    ekey[i].name    = "";
    ekey[i++].label = "";

    ekey[i].ch    = -1;

    flags = OE_APPEND_CURRENT | OE_KEEP_TRAILING_SPACE;
    while(1){

	/*
	 * 2 is really 1 because there will be one real entry and
	 * one entry of "" because of the get_prev_hist above.
	 */
	if(items_in_hist(history) > 2){
	    ekey[ku].name  = HISTORY_UP_KEYNAME;
	    ekey[ku].label = HISTORY_KEYLABEL;
	    ekey[ku+1].name  = HISTORY_DOWN_KEYNAME;
	    ekey[ku+1].label = HISTORY_KEYLABEL;
	}
	else{
	    ekey[ku].name  = "";
	    ekey[ku].label = "";
	    ekey[ku+1].name  = "";
	    ekey[ku+1].label = "";
	}

        rc = optionally_enter(nsearch_string, command_line, 0,
			      sizeof(nsearch_string),
                              prompt, ekey, help, &flags);
        if(rc == 3){
            help = help == NO_HELP ? h_oe_searchab : NO_HELP;
            continue;
        }
	else if(rc == 10){
	    *warped = 1;
	    warp_to_beginning();  /* go to top of addrbooks */
	    if((nl=first_selectable_line(0L)) != NO_LINE){
		*new_line = nl;
		q_status_message(SM_INFO, 0, 2, _("Searched to first entry"));
		return 0;
	    }
	    else{
		q_status_message(SM_INFO, 0, 2, _("No entries"));
		return -1;
	    }
	}
	else if(rc == 11){
	    *warped = 1;
	    warp_to_end();  /* go to bottom */
	    if((nl=first_selectable_line(0L)) != NO_LINE){
		*new_line = nl;
		q_status_message(SM_INFO, 0, 2, _("Searched to last entry"));
		return 0;
	    }
	    else{
		q_status_message(SM_INFO, 0, 2, _("No entries"));
		return -1;
	    }
	}
	else if(rc == 30){
	    if((p = get_prev_hist(history, nsearch_string, 0, NULL)) != NULL){
		strncpy(nsearch_string, p, sizeof(nsearch_string));
		nsearch_string[sizeof(nsearch_string)-1] = '\0';
	    }
	    else
	      Writechar(BELL, 0);

	    continue;
	}
	else if(rc == 31){
	    if((p = get_next_hist(history, nsearch_string, 0, NULL)) != NULL){
		strncpy(nsearch_string, p, sizeof(nsearch_string));
		nsearch_string[sizeof(nsearch_string)-1] = '\0';
	    }
	    else
	      Writechar(BELL, 0);

	    continue;
	}

        if(rc != 4){			/* 4 is redraw */
	    save_hist(history, nsearch_string, 0, NULL);
	    break;
	}
    }

        
    if(rc == 1 || (search_string[0] == '\0' && nsearch_string[0] == '\0'))
      return -2;

    if(nsearch_string[0] != '\0'){
        strncpy(search_string, nsearch_string, sizeof(search_string)-1);
        search_string[sizeof(search_string)-1] = '\0';
    }

    find_result = find_in_book(cur_line, search_string, new_line, wrapped);
    
    if(*wrapped == 1)
      *warped = 1;

    if(find_result){
	int also = 0, notdisplayed = 0;

	pab = &as.adrbks[adrbk_num_from_lineno(*new_line)];
	if(find_result & MATCH_NICK){
	    if(pab->nick_is_displayed)
	      also++;
	    else
	      notdisplayed++;
	}

	if(find_result & MATCH_FULL){
	    if(pab->full_is_displayed)
	      also++;
	    else
	      notdisplayed++;
	}

	if(find_result & MATCH_ADDR){
	    if(pab->addr_is_displayed)
	      also++;
	    else
	      notdisplayed++;
	}

	if(find_result & MATCH_FCC){
	    if(pab->fcc_is_displayed)
	      also++;
	    else
	      notdisplayed++;
	}

	if(find_result & MATCH_COMMENT){
	    if(pab->comment_is_displayed)
	      also++;
	    else
	      notdisplayed++;
	}

	if(find_result & MATCH_LISTMEM){
	    AddrScrn_Disp *dl;

	    dl = dlist(*new_line);
	    if(F_OFF(F_EXPANDED_DISTLISTS,ps_global)
	      && !exp_is_expanded(pab->address_book->exp, (a_c_arg_t)dl->elnum))
	      notdisplayed++;
	}

	if(notdisplayed > 1 && *wrapped == 0){
	  if(also)
	     /* TRANSLATORS: These "matched" messages are advisory messages explaining
	        how a search command matched an entry */
	     q_status_message1(SM_ORDER,0,4, _("Also matched string in %s other fields"),
			       comatose(notdisplayed));
	  else
	     q_status_message1(SM_ORDER,0,4, _("Matched string in %s fields"),
			       comatose(notdisplayed));
	}
	else if(notdisplayed == 1 && *wrapped == 0){
	  if(also){
	    if(find_result & MATCH_NICK && !pab->nick_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Also matched string in Nickname field"));
	    else if(find_result & MATCH_FULL && !pab->full_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Also matched string in Fullname field"));
	    else if(find_result & MATCH_ADDR && !pab->addr_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Also matched string in Address field"));
	    else if(find_result & MATCH_FCC && !pab->fcc_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Also matched string in Fcc field"));
	    else if(find_result & MATCH_COMMENT && !pab->comment_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Also matched string in Comment field"));
	    else if(find_result & MATCH_LISTMEM)
	      q_status_message(SM_ORDER,0,4, _("Also matched string in list member address"));
	    else
	      q_status_message(SM_ORDER,0,4, _("Also matched string in ?"));
	  }
	  else{
	    if(find_result & MATCH_NICK && !pab->nick_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Matched string in Nickname field"));
	    else if(find_result & MATCH_FULL && !pab->full_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Matched string in Fullname field"));
	    else if(find_result & MATCH_ADDR && !pab->addr_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Matched string in Address field"));
	    else if(find_result & MATCH_FCC && !pab->fcc_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Matched string in Fcc field"));
	    else if(find_result & MATCH_COMMENT && !pab->comment_is_displayed)
	      q_status_message(SM_ORDER,0,4, _("Matched string in Comment field"));
	    else if(find_result & MATCH_LISTMEM)
	      q_status_message(SM_ORDER,0,4, _("Matched string in list member address"));
	    else
	      q_status_message(SM_ORDER,0,4, _("Matched string in ?"));
	  }
	}


	/* be sure to be on a selectable field */
	if(!line_is_selectable(*new_line))
	  if((nl=first_selectable_line(*new_line+1)) != NO_LINE)
	    *new_line = nl;
    }

    return(find_result ? 0 : -1);
}


/*
 * Search the display list for the given string.
 *
 * Args: cur_line     -- The current line position (in global display list)
 *			 of cursor
 *       string       -- String to search for
 *       new_line     -- Return value: new line position
 *       wrapped      -- Wrapped to beginning of display during search
 *
 * Result: The new line number is set if the search is successful.
 *         Returns 0 -- string not found
 *          Otherwise, a bitmask of which fields the string was found in.
 */
int
find_in_book(long int cur_line, char *string, long int *new_line, int *wrapped)
{
    register AddrScrn_Disp *dl;
    long                    nl, nl_save;
    int			    fields;
    AdrBk_Entry            *abe;
    char                   *listaddr = NULL;
    DL_CACHE_S             *dlc,
			    dlc_save; /* a local copy */


    dprint((9, "- find_in_book -\n"));

    /*
     * Save info to allow us to get back to where we were if we can't find
     * the string. Also used to stop our search if we wrap back to the
     * start and search forward.
     */

    nl_save = cur_line;
    dlc = get_dlc(nl_save);
    dlc_save = *dlc;

    *wrapped = 0;
    nl = cur_line + 1L;

    /* start with next line and search to the end of the disp_list */
    dl = dlist(nl);
    while(dl->type != End){
	if(dl->type == Simple ||
	   dl->type == ListHead ||
	   dl->type == ListEnt ||
	   dl->type == ListClickHere){
	    abe = ae(nl);
	    if(dl->type == ListEnt)
	      listaddr = listmem(nl);
	}
	else
	  abe = (AdrBk_Entry *)NULL;

	if((fields=search_in_one_line(dl, abe, listaddr, string)) != 0)
	  goto found;

	dl = dlist(++nl);
    }


    /*
     * Wrap back to the start of the addressbook and search forward
     * from there.
     */
    warp_to_beginning();  /* go to top of addrbooks */
    nl = 0L;  /* line number is always 0 after warp_to_beginning */
    *wrapped = 1;

    dlc = get_dlc(nl);
    while(!matching_dlcs(&dlc_save, dlc) && dlc->type != DlcEnd){

	fill_in_dl_field(dlc);
	dl = &dlc->dl;

	if(dl->type == Simple ||
	   dl->type == ListHead ||
	   dl->type == ListEnt ||
	   dl->type == ListClickHere){
	    abe = ae(nl);
	    if(dl->type == ListEnt)
	      listaddr = listmem(nl);
	}
	else
	  abe = (AdrBk_Entry *)NULL;

	if((fields=search_in_one_line(dl, abe, listaddr, string)) != 0)
	  goto found;

	dlc = get_dlc(++nl);
    }

    /* see if it is in the current line */
    fill_in_dl_field(dlc);
    dl = &dlc->dl;

    if(dl->type == Simple ||
       dl->type == ListHead ||
       dl->type == ListEnt ||
       dl->type == ListClickHere){
	abe = ae(nl);
	if(dl->type == ListEnt)
	  listaddr = listmem(nl);
    }
    else
      abe = (AdrBk_Entry *)NULL;

    fields = search_in_one_line(dl, abe, listaddr, string);
    if(dl->usst &&
       (dl->type == Text || dl->type == Title || dl->type == TitleCmb))
      fs_give((void **)&dl->usst);

    /* jump cache back to where we started */
    *wrapped = 0;
    warp_to_dlc(&dlc_save, nl_save);
    if(fields){
	*new_line = nl_save;  /* because it was in current line */
	*wrapped = 2;
    }

    nl = *new_line;

found:
    *new_line = nl;
    return(fields);
}


/*
 * Look in line dl for string.
 *
 * Args: dl     -- the display list for this line
 *       abe    -- AdrBk_Entry if it is an address type
 *     listaddr -- list member if it is of type ListEnt
 *       string -- look for this string
 *
 * Result:  0   -- string not found
 *          Otherwise, a bitmask of which fields the string was found in.
 *      MATCH_NICK      0x1
 *      MATCH_FULL      0x2
 *      MATCH_ADDR      0x4
 *      MATCH_FCC       0x8
 *      MATCH_COMMENT  0x10
 *      MATCH_BIGFIELD 0x20
 *      MATCH_LISTMEM  0x40
 */
int
search_in_one_line(AddrScrn_Disp *dl, AdrBk_Entry *abe, char *listaddr, char *string)
{
    register int c;
    int ret_val = 0;
    char **lm;

    for(c = 0; c < 5; c++){
      switch(c){
	case 0:
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      if(srchstr(abe->nickname, string))
		ret_val |= MATCH_NICK;

	      break;

	    case Text:
	    case Title:
	    case TitleCmb:
	    case AskServer:
	      if(srchstr(dl->usst, string))
		ret_val |= MATCH_BIGFIELD;

	    default:
	      break;
	  }
	  break;

	case 1:
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      if(abe && srchstr(
			(char *)rfc1522_decode_to_utf8((unsigned char *)tmp_20k_buf,
						       SIZEOF_20KBUF, abe->fullname),
			string))
		ret_val |= MATCH_FULL;

	    default:
	      break;
	  }
	  break;

	case 2:
	  switch(dl->type){
	    case Simple:
	      if(srchstr((abe && abe->tag == Single) ?
		  abe->addr.addr : NULL, string))
		ret_val |= MATCH_ADDR;

	      break;

	    case ListEnt:
	      if(srchstr(
		 (char *)rfc1522_decode_to_utf8((unsigned char *)tmp_20k_buf,
						SIZEOF_20KBUF, listaddr), string))
		ret_val |= MATCH_LISTMEM;

	      break;

	    case ListClickHere:
	      if(abe)
	        for(lm = abe->addr.list;
		    !(ret_val & MATCH_LISTMEM) && *lm; lm++)
	          if(srchstr(*lm, string))
		    ret_val |= MATCH_LISTMEM;

	      break;

	    case Empty:
	    case ListEmpty:
	      if(srchstr(EMPTY, string))
		ret_val |= MATCH_BIGFIELD;

	      break;

	    case AddFirstPers:
	      if(srchstr(ADD_PERSONAL, string))
		ret_val |= MATCH_BIGFIELD;

	      break;

	    case AddFirstGlob:
	      if(srchstr(ADD_GLOBAL, string))
		ret_val |= MATCH_BIGFIELD;

	      break;

	    case NoAbooks:
	      if(srchstr(NOABOOKS, string))
		ret_val |= MATCH_BIGFIELD;

	      break;

	    default:
	      break;
	  }
	  break;

	case 3:  /* fcc */
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      if(abe && srchstr(abe->fcc, string))
		ret_val |= MATCH_FCC;

	    default:
	      break;
	  }
	  break;

	case 4:  /* comment */
	  switch(dl->type){
	    case Simple:
	    case ListHead:
	      if(abe){
		  size_t n, len;
		  unsigned char *p, *tmp = NULL;

		  if((n = 4*strlen(abe->extra)) > SIZEOF_20KBUF-1){
		      len = n+1;
		      p = tmp = (unsigned char *)fs_get(len * sizeof(char));
		  }
		  else{
		      len = SIZEOF_20KBUF;
		      p = (unsigned char *)tmp_20k_buf;
		  }

		  if(srchstr((char *)rfc1522_decode_to_utf8(p, len, abe->extra),
			     string))
		    ret_val |= MATCH_COMMENT;
		  
		  if(tmp)
		    fs_give((void **)&tmp);

	      }

	    default:
	      break;
	  }
	  break;
      }
    }

    return(ret_val);
}


/*
 * These chars in nicknames will mess up parsing.
 *
 * Returns 0 if ok, 1 if not.
 * Returns an allocated error message on error.
 */
int
nickname_check(char *nickname, char **error)
{
    register char *t;
    char buf[100];

    if((t = strindex(nickname, SPACE)) ||
       (t = strindex(nickname, ',')) ||
       (t = strindex(nickname, '"')) ||
       (t = strindex(nickname, ';')) ||
       (t = strindex(nickname, ':')) ||
       (t = strindex(nickname, '@')) ||
       (t = strindex(nickname, '(')) ||
       (t = strindex(nickname, ')')) ||
       (t = strindex(nickname, '\\')) ||
       (t = strindex(nickname, '[')) ||
       (t = strindex(nickname, ']')) ||
       (t = strindex(nickname, '<')) ||
       (t = strindex(nickname, '>'))){
	char s[4];
	s[0] = '"';
	s[1] = *t;
	s[2] = '"';
	s[3] = '\0';
	if(error){
	    /*
	     * TRANSLATORS: this is telling the user that one of the characters
	     * they have included in a nickname will not work. It will say something like
	     * Blank spaces not allowed in nicknames    or
	     * Commas not...
	     * etc.
	     */
	    snprintf(buf, sizeof(buf), _("%s not allowed in nicknames"),
		*t == SPACE ?
		    _("Blank spaces") :
		    *t == ',' ?
			_("Commas") :
			*t == '"' ?
			    _("Quotes") :
			    s);
	    buf[sizeof(buf)-1] = '\0';
	    *error = cpystr(buf);
	}

	return 1;
    }

    return 0;
}


char *
abook_select_screen(struct pine *ps)
{
    CONF_S        *ctmp = NULL, *first_line = NULL;
    OPT_SCREEN_S   screen;
    int            adrbknum;
    char          *helptitle;
    HelpType       help;
    PerAddrBook   *pab;
    char          *abook = NULL;

    helptitle = _("HELP FOR SELECTING AN ADDRESS BOOK");
    help      = h_role_abook_select;

    init_ab_if_needed();

    for(adrbknum = 0; adrbknum < as.n_addrbk; adrbknum++){
	new_confline(&ctmp);
	if(!first_line)
	  first_line = ctmp;

	pab = &as.adrbks[adrbknum];

	ctmp->value        = cpystr((pab && pab->abnick)
				      ? pab->abnick
				      : (pab && pab->filename)
				        ? pab->filename
					: "?");

	ctmp->d.b.selected  = &abook;
	ctmp->d.b.abookname = ctmp->value;
	ctmp->keymenu       = &abook_select_km;
	ctmp->help          = help;
	ctmp->help_title    = helptitle;
	ctmp->tool          = abook_select_tool;
	ctmp->flags         = CF_STARTITEM;
	ctmp->valoffset     = 4;
    }

    if(first_line){
	memset(&screen, 0, sizeof(screen));
	(void) conf_scroll_screen(ps, &screen, first_line,
				  /* TRANSLATORS: Print something1 using something2.
				     abooks is something1 */
				  _("SELECT ADDRESS BOOK"), _("abooks"), 0);
    }
    else
      q_status_message(SM_ORDER|SM_DING, 3, 3, _("No address books defined!"));

    ps->mangled_screen = 1;
    return(abook);
}


int
abook_select_tool(struct pine *ps, int cmd, CONF_S **cl, unsigned int flags)
{
    int retval;

    switch(cmd){
      case MC_CHOICE :
	*((*cl)->d.b.selected) = cpystr((*cl)->d.b.abookname);
	retval = simple_exit_cmd(flags);
	break;

      case MC_EXIT :
        retval = simple_exit_cmd(flags);
	break;

      default:
	retval = -1;
	break;
    }

    if(retval > 0)
      ps->mangled_body = 1;

    return(retval);
}


/*
 * This isn't actually just a nickname completion anymore. The user types
 * in a prefix of either a nickname, or a full address, or the addr@mailbox
 * part of an address and we look in all of the address books for matches
 * like that. We return the longest unambiguous match in answer.
 *
 * Args    prefix  -- The part of the "nickname" that has been typed so far
 *         answer  -- The answer is returned here.
 *         tabtab  -- If the answer returned from adrbk_list_of_completions is
 *                    ambiguous and tabtab is set, then offer up a
 *                    selector screen for all the possible answers.
 *          flags  -- ANC_AFTERCOMMA -- This means that the passed in
 *                                      prefix may be a list of comma
 *                                      separated addresses and we're only
 *                                      completing the last one.
 *
 * Returns    0 -- no matches at all
 *            1 -- more than one nickname (or address) begins with
 *                              the answer being returned
 *            2 -- the returned answer is a complete answer, either a
 *                              nickname or a complete address, and there are
 *				no longer matches for the prefix
 *
 * Allocated answer is returned in answer argument.
 * Caller needs to free the answer.
 */
int
abook_nickname_complete(char *prefix, char **answer, int tabtab, unsigned flags)
{
    int ambiguity;
    COMPLETE_S *completions, *cp;
    char *saved_beginning = NULL;
    char *potential_answer = NULL;

    wp_exit = wp_nobail = 0;

    /* there shouldn't be a case where answer is NULL */
    if(answer)
      *answer = NULL;

    /*
     * If we need to handle case where no prefix is passed in,
     * figure that out when we need it.
     */
    if(!(prefix && prefix[0]))
      return(0);

    if(flags & ANC_AFTERCOMMA){
	char *lastnick;

	/*
	 * Find last comma, save the part before that, operate
	 * only on the last address.
	 */
	if((lastnick = strrchr(prefix ? prefix : "", ',')) != NULL){
	    lastnick++;
	    while(!(*lastnick & 0x80) && isspace((unsigned char) (*lastnick)))
	      lastnick++;

	    saved_beginning = cpystr(prefix);
	    saved_beginning[lastnick-prefix] = '\0';
	    prefix = lastnick;
	}
    }

    if(!(prefix && prefix[0]))
      return(0);

    completions = adrbk_list_of_completions(prefix,
			ps_global->cur_uid_stream, ps_global->cur_uid,
			ALC_INCLUDE_ADDRS | ((strlen(prefix) >= 3) ? ALC_INCLUDE_LDAP : 0));

    if(!completions)
      ambiguity = 0;
    else if(completions && completions->next)
      ambiguity = 1;
    else
      ambiguity = 2;

    if(ambiguity == 2){
	if(completions->full_address && completions->full_address[0])
	  potential_answer = cpystr(completions->full_address);
	else if(completions->nickname && completions->nickname[0])
	  potential_answer = cpystr(completions->nickname);
	else if(completions->addr && completions->addr[0])
	  potential_answer = cpystr(completions->addr);
	else
	  potential_answer = cpystr(prefix);
    }
    /* answer is ambiguous and caller wants a choose list in that case */
    else if(ambiguity == 1 && tabtab){
	potential_answer = choose_an_address_with_this_prefix(completions);

	if(potential_answer)
	  ambiguity = 2;
	else{
	    ambiguity = 1;
	    potential_answer = cpystr(prefix);
	}
    }
    else if(ambiguity == 1){
	int  k;
	char cand1_kth_char, cand2_kth_char;
	char unambig[1000];

	/* find the longest unambiguous prefix */
	strncpy(unambig, prefix, sizeof(unambig));
	unambig[sizeof(unambig)-1] = '\0';
	k = strlen(unambig);

	/*
	 * First verify that they all match through prefix. LDAP sometimes gives
	 * weird, inexplicable answers that don't seem to match at all.
	 */
	for(cp = completions; cp; cp = cp->next)
	  if(!(		/* not a match */
/*     no NICK bit                    OR            nickname matches prefix */
    (!(cp->matches_bitmap & ALC_NICK) || (cp->nickname && strlen(cp->nickname) >= k && !struncmp(unambig, cp->nickname, k)))
/* AND no ADDR bit                    OR            addr matches prefix */
 && (!(cp->matches_bitmap & ALC_ADDR) || (cp->addr && strlen(cp->addr) >= k && !struncmp(unambig, cp->addr, k)))
/* AND   neither FULL bit is set                      OR            one of the two fulls matches prefix */
 && (!(cp->matches_bitmap & (ALC_FULL | ALC_REVFULL)) || ((cp->matches_bitmap & ALC_FULL && cp->full_address && strlen(cp->full_address) >= k && !struncmp(unambig, cp->full_address, k)) || (cp->matches_bitmap & ALC_REVFULL && cp->rev_fullname && strlen(cp->rev_fullname) >= k && !struncmp(unambig, cp->rev_fullname, k))))
		   ))
	    break;

	/* if cp that means there was not a universal match up through prefix, stop */
	if(!cp)
	  do{
	    cand1_kth_char = cand2_kth_char = '\0';
	    if(completions->matches_bitmap & ALC_NICK && completions->nickname && strlen(completions->nickname) >= k)
	      cand1_kth_char = completions->nickname[k];
	    else if(completions->matches_bitmap & ALC_ADDR && completions->addr && strlen(completions->addr) >= k)
	      cand1_kth_char = completions->addr[k];
	    else{
		if(completions->matches_bitmap & ALC_FULL && completions->full_address && strlen(completions->full_address) >= k)
		  cand1_kth_char = completions->full_address[k];

		if(completions->matches_bitmap & ALC_REVFULL && completions->rev_fullname && strlen(completions->rev_fullname) >= k)
		  cand2_kth_char = completions->rev_fullname[k];
	    }

	    /*
	     * You'll want a wide screen to read this. There are two possible
	     * candidate chars for the next position. One or the other of them
	     * has to match in all of the possible completions. We consider it
	     * a match if either of the fullname completions for this entry is
	     * a match. That may not match what the user expects but it may.
	     */
	    for(cp = completions; cp; cp = cp->next){
                if(!(              /* candidate 1 is not a match */
  /*    candidate 1 is defined   */
          cand1_kth_char && cand1_kth_char != ','
  /* AND   no NICK bit                    OR            nickname char is a match */
     && (!(cp->matches_bitmap & ALC_NICK) || (cp->nickname && strlen(cp->nickname) >= k && cp->nickname[k] == cand1_kth_char))
  /* AND   no ADDR bit                    OR            addr char is a match */
     && (!(cp->matches_bitmap & ALC_ADDR) || (cp->addr && strlen(cp->addr) >= k && cp->addr[k] == cand1_kth_char))
  /* AND   neither FULL bit is set                       OR            one of the two full chars is a match */
     && (!(cp->matches_bitmap & (ALC_FULL | ALC_REVFULL)) || ((cp->matches_bitmap & ALC_FULL && cp->full_address && strlen(cp->full_address) >= k && cp->full_address[k] == cand1_kth_char) || (cp->matches_bitmap & ALC_REVFULL && cp->rev_fullname && strlen(cp->rev_fullname) >= k && cp->rev_fullname[k] == cand1_kth_char)))
                       ))
		  cand1_kth_char = '\0';	/* mark that it isn't a match */

                if(!cand1_kth_char && !(              /* cand1 is not a match AND cand2 is not a match */
  /*    candidate 2 is defined   */
          cand2_kth_char && cand2_kth_char != ','
  /* AND   no NICK bit                    OR            nickname char is a match */
     && (!(cp->matches_bitmap & ALC_NICK) || (cp->nickname && strlen(cp->nickname) >= k && cp->nickname[k] == cand2_kth_char))
  /* AND   no ADDR bit                    OR            addr char is a match */
     && (!(cp->matches_bitmap & ALC_ADDR) || (cp->addr && strlen(cp->addr) >= k && cp->addr[k] == cand2_kth_char))
  /* AND   neither FULL bit is set                       OR            one of the two full chars is a match */
     && (!(cp->matches_bitmap & (ALC_FULL | ALC_REVFULL)) || ((cp->matches_bitmap & ALC_FULL && cp->full_address && strlen(cp->full_address) >= k && cp->full_address[k] == cand2_kth_char) || (cp->matches_bitmap & ALC_REVFULL && cp->rev_fullname && strlen(cp->rev_fullname) >= k && cp->rev_fullname[k] == cand2_kth_char)))
                       ))
		  cand2_kth_char = '\0';	/* mark that it isn't a match */

		if(!cand1_kth_char && !cand2_kth_char)
		  break;	/* no match so break */
	    }

	    if(!cp)			/* they all matched */
	      unambig[k++] = cand1_kth_char ? cand1_kth_char : cand2_kth_char;

	  }while(!cp && k < sizeof(unambig)-1);

	unambig[k] = '\0';
	unambig[sizeof(unambig)-1] = '\0';

	/* don't return answer with trailing space */
	while(--k >= 0 && isspace((unsigned char) unambig[k]))
	  unambig[k] = '\0';

	potential_answer = cpystr(unambig);
    }

    if(completions)
      free_complete_s(&completions);

    if(answer && ambiguity != 0){
	if(potential_answer){
	    if(saved_beginning){
		size_t l1, l2;

		l1 = strlen(saved_beginning);
		l2 = strlen(potential_answer);
		*answer = (char *) fs_get((l1+l2+1) * sizeof(char));
		strncpy(*answer, saved_beginning, l1+l2);
		strncpy(*answer+l1, potential_answer, l2);
		(*answer)[l1+l2] = '\0';
	    }
	    else{
		*answer = potential_answer;
		potential_answer = NULL;
	    }
	}
	else{
	    /* this can't happen */
	    ambiguity = 0;
	}
    }

    if(saved_beginning)
      fs_give((void **) &saved_beginning);

    if(potential_answer)
      fs_give((void **) &potential_answer);

    return(ambiguity);
}


/*
 * Returns an allocated nickname choice from user that begins with
 * prefix.
 */
char *
choose_an_address_with_this_prefix(COMPLETE_S *completions)
{
    char buf[1000];
    char *chosen_address = NULL;
    char **lp, **da, **possible_addrs = NULL, **display_addrs = NULL;
    COMPLETE_S *cp;
    size_t cnt = 0;
    int show_nick, show_revfull;

    /*
     * Count how many and allocate an array for choose_item_from_list().
     */
    for(cnt = 0, cp = completions; cp; cp = cp->next)
      cnt++;

    /*
     * Copy completions into an array.
     */
    if(cnt > 0){
	lp = possible_addrs = (char **) fs_get((cnt+1) * sizeof(*possible_addrs));
	memset(possible_addrs, 0, (cnt+1) * sizeof(*possible_addrs));
	da = display_addrs = (char **) fs_get((cnt+1) * sizeof(*display_addrs));
	memset(display_addrs, 0, (cnt+1) * sizeof(*display_addrs));
	for(cp = completions; cp; cp = cp->next){
	    show_nick = (cp->matches_bitmap & ALC_NICK) && cp->nickname && cp->nickname[0];
	    show_revfull = 0;
	    if(!show_nick && !(cp->matches_bitmap & (ALC_NICK | ALC_ADDR | ALC_FULL))
	       && (cp->matches_bitmap & ALC_REVFULL)
	       && cp->rev_fullname && cp->rev_fullname[0])
	      show_revfull = 1;

	    snprintf(buf, sizeof(buf), "%s%s%s%s%s",
		     cp->full_address ? cp->full_address : "?",
		     (show_nick || show_revfull) ? " (" : "",
		     show_nick ? cp->nickname : "",
		     show_revfull ? cp->rev_fullname : "",
		     (show_nick || show_revfull) ? ")" : "");
	    *da++ = cpystr(buf);
	    *lp++ = cpystr(cp->full_address ? cp->full_address : "?");
	}
    }

    if(possible_addrs){
	chosen_address = choose_item_from_list(possible_addrs, display_addrs,
						_("SELECT AN ADDRESS"),
						_("addresses"),
						h_select_address_screen,
						_("HELP FOR SELECTING AN ADDRESS"),
						NULL);
	free_list_array(&possible_addrs);
    }

    if(display_addrs)
      free_list_array(&display_addrs);

    return(chosen_address);
}


#ifdef	_WINDOWS
/*
 * addr_scroll_up - adjust the global screen state struct such that pine's
 *		    window on the data is shifted DOWN (i.e., the data's
 *		    scrolled up).
 */
int
addr_scroll_up(count)
    long count;
{
    int next;

    if(count < 0)
	return(addr_scroll_down(-count));
    else if(count){
	long i;
	
	i=count;
	as.cur_row += as.top_ent;
	while(i && as.top_ent + 1 < as.last_ent){
	  if(line_is_selectable(as.top_ent)){
	    if(next_selectable_line(as.top_ent,&next)){
	      as.cur_row = next;
	      i--;
	      as.top_ent++;
	    }
	    else i = 0;
	  }
	  else {
	    i--;
	    as.top_ent++;
	  }
	}
	as.cur_row = as.cur_row - as.top_ent; /* must always be positive */

	as.old_cur_row = as.cur_row;
    }

    return(1);
}


/*
 * addr_scroll_down - adjust the global screen state struct such that pine's
 *		    window on the data is shifted UP (i.e., the data's
 *		    scrolled down).
 */
int
addr_scroll_down(count)
    long count;
{
    if(count < 0)
      return(addr_scroll_up(-count));
    else if(count){
	long i;

	for(i = count; i && as.top_ent; i--, as.top_ent--)
	  as.cur_row++;

	while (as.cur_row >= as.l_p_page){
	  prev_selectable_line(as.cur_row+as.top_ent, &as.cur_row);
	  as.cur_row = as.cur_row - as.top_ent;
	}

	as.old_cur_row = as.cur_row;
    }

    return(1);
}


/*
 * addr_scroll_to_pos - scroll the address book data in pine's window such
 *			tthat the given "line" is at the top of the page.
 */
int
addr_scroll_to_pos(line)
    long line;
{
    return(addr_scroll_up(line - as.top_ent));
}


/*----------------------------------------------------------------------
     MSWin scroll callback.  Called during scroll message processing.
	     


  Args: cmd - what type of scroll operation.
	scroll_pos - parameter for operation.  
			used as position for SCROLL_TO operation.

  Returns: TRUE - did the scroll operation.
	   FALSE - was not able to do the scroll operation.
 ----*/
int
addr_scroll_callback (cmd, scroll_pos)
    int	 cmd;
    long scroll_pos;
{
    int paint = TRUE;
    
    switch (cmd) {
      case MSWIN_KEY_SCROLLUPLINE:
	paint = addr_scroll_down (scroll_pos);
	break;

      case MSWIN_KEY_SCROLLDOWNLINE:
	paint = addr_scroll_up (scroll_pos);
	break;

      case MSWIN_KEY_SCROLLUPPAGE:
	paint = addr_scroll_down (as.l_p_page);
	break;

      case MSWIN_KEY_SCROLLDOWNPAGE:
	paint = addr_scroll_up (as.l_p_page);
	break;

      case MSWIN_KEY_SCROLLTO:
	paint = addr_scroll_to_pos (scroll_pos);
	break;
    }

    if(paint)
      display_book(0, as.cur_row, -1, 1, (Pos *)NULL);

    return(paint);
}


char *
pcpine_help_addrbook(title)
    char *title;
{
    /*
     * Title is size 256. Fix this to pass the titlelen.
     */
    if(title)
      strncpy(title, (as.config)
		       ? _("Alpine CONFIGURING ADDRESS BOOKS Help")
		       : _("Alpine ADDRESS_BOOK Help"), 256);

    return(pcpine_help(gAbookHelp));
}
#endif	/* _WINDOWS */
