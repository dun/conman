/******************************************************************************\
 *  $Id: tselect.h,v 1.4 2001/12/15 14:33:49 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


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
