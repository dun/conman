/******************************************************************************\
 *  $Id: util.h,v 1.11 2001/09/06 21:50:52 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _UTIL_H
#define _UTIL_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */


#ifndef MAX
#  define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif /* !MAX */
#ifndef MIN
#  define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */


typedef void * (*PthreadFunc)(void *);

typedef void SigFunc(int);


SigFunc * posix_signal(int signum, SigFunc *f);
/*
 *  A wrapper for the historical signal() function to do things the Posix way.
 */


#endif /* !_UTIL_H */
