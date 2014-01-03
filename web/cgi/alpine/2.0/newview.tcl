#!./tclsh
# $Id: newview.tcl 1266 2009-07-14 18:39:12Z hubert@u.washington.edu $
# ========================================================================
# Copyright 2008 University of Washington
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# ========================================================================

#  newview.tcl
#
#  Purpose:  CGI script that generates a page displaying a message
#            list of the indicated folder. 
#
#  Input:    PATH_INFO: [/<col_number>]/<folder_name>[/<uid_of_first_msg>
#            along with possible search parameters:
set newview_args {
  {op	{}	""}
  {df	{}	""}
  {img	{}	0}
  {page {}	""}
}

# inherit global config
source ./alpine.tcl
source ./common.tcl
source ./foldercache.tcl

# default newview state
set showimg ""

# TEST
proc cgi_suffix {args} {
  return ""
}

proc next_message {_n _u {i 1}} {
  upvar 1 $_n n
  upvar 1 $_u u

  if {[catch {WPCmd PEMailbox next $n $i} nnext]} {
    WPCmd PEInfo statmsg "Cannot get next message: $nnext"
  } else {
    set n $nnext
    if {[catch {WPCmd PEMailbox uid $n} u]} {
      WPCmd PEInfo statmsg "Message $nnext has no UID: $u"
      set n 0
      set u 0
    }
  }
}

# grok PATH_INFO for collection 'c' and folder 'f'
if {[info exists env(PATH_INFO)] && [string length $env(PATH_INFO)]} {
  if {[regexp {^/([0-9]+)/(.*)/([0-9]+)$} $env(PATH_INFO) dummy c f u]} {
    # Import data validate it and get session id
    if {[catch {WPGetInputAndID sessid} result]} {
      set harderr "Invalid Session: $result"
      set deadsession 1
    } else {
      # grok parameters
      foreach item $newview_args {
	if {[catch {eval WPImport $item} result]} {
	  set harderr "Cannot read input: $result"
	  break;
	}
      }

      if {[catch {WPCmd PEMessage $u number} n]} {
	set harderr "Message no longer exists in $f"
      }
    }
  } else {
    set harderr "Invalid path: $env(PATH_INFO)"
    set nofolder 1
  }
} else {
  set harderr "No Folder Specified!"
  set nofolder 1
}

if {[info exists harderr]} {
  if {[info exists deadsession]} {
    cgi_puts "Content-type: text/html; charset=\"UTF-8\"\n"
    cgi_puts "Your Web Alpine Session has been closed."
  } else {
    catch { WPCmd PEInfo statmsg "$harderr" }
    unset harderr

    if {[info exists nofolder]} {
      set env(PATH_INFO) "/0/INBOX"
    } else {
      set env(PATH_INFO) "/${c}/${f}"
    }

    source newlist.tcl
  }

  exit
}

cgi_puts "Content-type: text/html; charset=\"UTF-8\"\n"

if {[catch {WPCmd PEMessage $u number} n]} {
  WPCmd PEInfo statmsg "Message access error: $n"
  set n 0
} else {
  switch -regexp -- $op {
    ^next$ {
      next_message n u
    }
    ^prev$ {
      next_message n u -1
    }
    ^hdron$ {
      WPCmd PEInfo mode full-header-mode 2
    }
    ^hdroff$ {
      WPCmd PEInfo mode full-header-mode 0
    }
    ^unread$ {
      if {[catch {WPCmd PEMessage $u flag new 1} result]} {
	WPCmd PEInfo statmsg "Delete of $u failed: $result"
      } else {
	next_message n u
      }
    }
    ^delete$ {
      if {[catch {WPCmd PEMessage $u flag deleted 1} result]} {
	WPCmd PEInfo statmsg "Delete of $u failed: $result"
      } else {
	WPCmd PEInfo statmsg "Message moved to Trash"
	if {$n == 1} {
	  next_message n u
	  set n 1
	} elseif {$n == [WPCmd PEMailbox messagecount]} {
	  next_message n u -1
	} else {
	  set nnext $n
	  next_message n u
	  set n $nnext
	}
      }
    }
    ^trash$ {
      if {[catch {WPCmd PEFolder empty $c [wpLiteralFolder $c $f] $u} result]} {
	WPCmd PEInfo statmsg "Cannot delete forever: $result"
      } else {
	WPCmd PEInfo statmsg "Message deleted forever"
	set mc [WPCmd PEMailbox messagecount]
	if {$mc == 0} {
	  set n 0
	  set u 0
	} else {
	  if {$n > $mc} {
	    set n $mc
	  }

	  if {[catch {WPCmd PEMailbox uid $n} u]} {
	    WPCmd PEInfo statmsg "Cannot get UID of $n: $u"
	    set n 0
	    set u 0
	  }
	}
      }
    }
    ^move$ -
    ^copy$ {
      if {[string length $df] && [regexp {^([0-9]+)/(.*)$} $df dummy dfc dfn] && [string length $dfn]} {
	if {[catch {WPCmd PEMessage $u $op $dfc [wpLiteralFolder $dfc $dfn]} result]} {
	  WPCmd PEInfo statmsg "Cannot $op message $n: $result"
	} else {
	  if {0 == [string compare $op move]} {
	    if {$c == [WPCmd PEFolder defaultcollection]
		&& (([info exists _wp(spamfolder)] && 0 == [string compare $f $_wp(spamfolder)])
		    || 0 == [string compare $f Trash])} {
	      if {[catch {WPCmd PEFolder empty $c $f $u} result]} {
		WPCmd PEInfo statmsg "Cannot empty Trash: $result"
	      } else {
		set mc [WPCmd PEMailbox messagecount]
		if {$mc == 0} {
		  set n 0
		  set u 0
		} else {
		  if {$n > $mc} {
		    set n $mc
		  }

		  if {[catch {WPCmd PEMailbox uid $n} u]} {
		    WPCmd PEInfo statmsg "Cannot get UID of $n: $u"
		    set n 0
		    set u 0
		  }
		}
	      }
	    } else {
	      if {[catch {WPCmd PEMailbox expunge} blasted] || [string length $blasted]} {
		WPCmd PEInfo statmsg "Move Problem: $blasted"
	      }

	      next_message n u
	    }
	  }

	  # feedback from alpined
	  if {[string compare -nocase inbox $dfn]} {
	    addSaveCache $dfn
	    set savecachechange $dfn
	  }
	}
      } else {
	WPCmd PEInfo statmsg "Cannot $op to $df"
      }
    }
    ^spam$ {
      if {[info exists _wp(spamsubj)]} {
	set spamsubj $_wp(spamsubj)
      } else {
	set spamsubj "Spam Report"
      }

      if {[info exists _wp(spamfolder)] && [string length $_wp(spamfolder)]
	  && [catch {
	    set defc [WPCmd PEFolder defaultcollection]
	    if {[WPCmd PEFolder exists $defc $_wp(spamfolder)] == 0} {
	      WPCmd PEFolder create $defc $_wp(spamfolder)
	    }

	    WPCmd PEMessage $u save $defc $_wp(spamfolder)
	  } result]} {
	WPCmd PEInfo statmsg "Error spamifying message $message: $result"

      } elseif {[info exists _wp(spamaddr)] && [string length $_wp(spamaddr)]
		&& [catch {WPCmd PEMessage $u spam $_wp(spamaddr) $spamsubj} result]} {
	WPCmd PEInfo statmsg "Can't Report Spam: $result"
      } elseif {[catch {WPCmd PEMessage $u flag deleted 1} result]} {
	WPCmd PEInfo statmsg "Error spamifying message $message: $result"
      } else {
	WPCmd PEInfo statmsg "Message $n reported as Spam and flagged for deletion"
	if {$n == 1} {
	  next_message n u
	  set n 1
	} elseif {$n == [WPCmd PEMailbox messagecount]} {
	  next_message n u -1
	} else {
	  set nnext $n
	  next_message n u
	  set n $nnext
	}
      }
    }
    ^$ -
    ^noop$ {
      # $img == 0 : remove current from from allow_cid_images list
      # $img == 1 : allow images for this message this once
      # $img == "from" : always allow images from this address
      if {[regexp {0|1|from} $img]} {
	set showimg $img
      }
    }
    default {
      WPCmd PEInfo statmsg "Unrecognized option: $op"
    }
  }
}

if {$n > 0} {
  cgi_puts [WPCmd cgi_buffer "drawMessageText $c {$f} $u $showimg"]
  cgi_puts "<script>"
  if {0 == [catch {WPCmd PEMessage $u needpasswd}]} {
    cgi_put "getSmimePassphrase({sessid:'$_wp(sessid)',control:this,parms:{op:'noop',page:'new'}});"
  }
  if {[catch {WPCmd PEMailbox next $n 1} nnext]} {
    WPCmd PEInfo statmsg "Cannot get next message: $nnext"
  } else {
    if {[catch {WPCmd PEMailbox uid $nnext} unext]} {
      WPCmd PEInfo statmsg "Message $nnext has no UID: $unext"
      set unext 0
    }
  }
  cgi_puts "updateViewLinksAndSuch(\{u:$u,n:$n,unext:$unext,unread:[WPCmd PEMailbox flagcount [list unseen undeleted]],count:[WPCmd PEMailbox messagecount],selected:[WPCmd PEMailbox selected]\});"
  if {0 == [string compare new $page]} {
    cgi_puts "showViewMenus();"
    cgi_puts "initMenus();"
    cgi_puts "initMorcButton('viewMorcButton');"
    wpSaveMenuJavascript "view" $c $f [WPCmd PEFolder defaultcollection] morcInViewDone
  }
  if {[info exists savecachechange]} {
    wpSaveMenuJavascript "view" $c $f [WPCmd PEFolder defaultcollection] morcInViewDone $savecachechange
  }

  cgi_puts "document.getElementById('alpineContent').scrollTop = 0;"
  wpStatusAndNewmailJavascript
  cgi_puts "setCheckMailFunction('gCheck', newMailCheck);"
  cgi_puts "setNewMailCheckInterval([WPCmd PEInfo inputtimeout]);"
  cgi_puts "</script>"
} else {
  cgi_puts "<script>"
  cgi_puts "updateViewLinksAndSuch({});"
  wpStatusAndNewmailJavascript
  cgi_puts "</script>"
}
