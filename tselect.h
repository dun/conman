/******************************************************************************\
 *  $Id: tselect.h,v 1.1 2001/09/23 23:23:15 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _TSELECT_H
#define _TSELECT_H


/***********\
**  Notes  **
\***********/

/*  These routines are not thread-safe.
 *  Only invoke them from within a single thread.
 *
 *  When a timer memory allocation request fails, out_of_memory() is returned.
 *  By default, this is a macro definition that returns NULL; this macro may
 *  be redefined to invoke another routine instead.  Furthermore, if USE_OOMF
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
 *  Sets a timer event for tselect() specifying how long the timer should run
 *    and what action should be taken when it expires.
 *  Returns the timer ID used by untimeout(), or -1 on memory allocation error.
 */

void untimeout(int timerid);
/*
 *  Cancels a timer event before it expires.
 */


#endif /* !_TSELECT_H */
