/*****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2016 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <https://dun.github.io/conman/>.
 *
 *  ConMan is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#ifndef _UTIL_H
#define _UTIL_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */


typedef void * (*PthreadFunc)(void *);

typedef void SigFunc(int);


#ifndef MAX
#  define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif /* !MAX */

#ifndef MIN
#  define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */

#ifdef WITH_OOMF
#undef out_of_memory
void * out_of_memory(void);
#endif /* WITH_OOMF */


SigFunc * posix_signal(int signum, SigFunc *f);
/*
 *  A wrapper for the historical signal() function to do things the Posix way.
 */


#endif /* !_UTIL_H */
