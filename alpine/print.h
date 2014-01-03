/*
 * $Id: print.h 761 2007-10-23 22:35:18Z hubert@u.washington.edu $
 *
 * ========================================================================
 * Copyright 2006 University of Washington
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * ========================================================================
 */

#ifndef PINE_PRINT_INCLUDED
#define PINE_PRINT_INCLUDED


#include "../pith/state.h"


/* exported protoypes */
char	*printer_name(char *);
#ifndef DOS
void	 select_printer(struct pine *, int);
#endif


#endif /* PINE_PRINT_INCLUDED */
