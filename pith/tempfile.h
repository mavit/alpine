/*
 * $Id: tempfile.h 770 2007-10-24 00:23:09Z hubert@u.washington.edu $
 *
 * ========================================================================
 * Copyright 2006-2007 University of Washington
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

#ifndef PITH_TEMPFILE_INCLUDED
#define PITH_TEMPFILE_INCLUDED


/* exported protoypes */
char	     *tempfile_in_same_dir(char *, char *, char **);
int	      in_dir(char *, char *);


#endif /* PITH_TEMPFILE_INCLUDED */
