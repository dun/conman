/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


#ifndef _TSELECT_H
#define _TSELECT_H


#include <sys/time.h>


/***********\
**  Notes  **
\***********/

/*  These routines are not thread-safe.
 *  Only invoke them from within a single thread.
 *
 *  When a timer memory allocation request fails, out_of_memory() is returned.
 *  By default, this is a macro definition that returns NULL; this macro may
 *  be redefined to invoke another routine instead.  Furthermore, if WITH_OOMF
 *  is defined, this macro will not be defined and the allocation routine
 *  will expect an external Out-Of-Memory Function to be defined.
 */


/****************\
**  Data Types  **
\****************/

typedef void (*CallBackF)(void *arg);
/*
 *  Function prototype for a timer callback function.
 */


/***************\
**  Functions  **
\***************/

int tselect(int maxfdp1, fd_set *rset, fd_set *wset, fd_set *xset);
/*
 *  Like select(), but all timer events are specified with timeout().
 *  Returns the number of events ready, 0 if no remaining events, -1 or error.
 */

int timeout(CallBackF callback, void *arg, int ms);
/*
 *  Sets a timer event for tselect() specifying how long (in milliseconds)
 *    the timer should run before it expires.  At expiration, the callback
 *    function will be invoked with the specified arg.
 *  Returns a timer ID > 0 for use with untimeout(),
 *    or -1 on memory allocation failure.
 */

int abtimeout(CallBackF callback, void *arg, const struct timeval *tvp);
/*
 *  Sets an "absolute" timer event for tselect() specifying when the timer
 *    should expire.  At expiration, the callback function will be invoked
 *    with the specified arg.
 *  Returns a timer ID > 0 for use with untimeout(),
 *    or -1 on memory allocation failure.
 */

void untimeout(int timerid);
/*
 *  Cancels a timer event before it expires.
 */


#endif /* !_TSELECT_H */
