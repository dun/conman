/******************************************************************************\
 *  $Id: util.c,v 1.16 2001/09/22 21:32:52 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "util.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */


#include <errno.h>
#include <signal.h>
#include "errors.h"
#include "util.h"


#ifdef USE_OOMF
#undef out_of_memory
void * out_of_memory(void)
{
/*  Example for an external Out-Of-Memory Function.
 */
    err_msg(0, "Out of memory");
    return(NULL);                       /* not reached, but prevents warnings */
}
#endif /* USE_OOMF */


SigFunc * posix_signal(int signum, SigFunc *f)
{
/*  A wrapper for the historical signal() function to do things the Posix way.
 *  cf. Stevens UNPv1 figure 5.6.
 */
    struct sigaction act0, act1;

    act1.sa_handler = f;
    sigemptyset(&act1.sa_mask);
    act1.sa_flags = 0;
    if (signum == SIGALRM) {
#ifdef SA_INTERRUPT
        act1.sa_flags |= SA_INTERRUPT;	/* SunOS 4.x */
#endif /* SA_INTERRUPT */
    }
    else {
#ifdef SA_RESTART
        act1.sa_flags |= SA_RESTART;	/* SVR4, 4.4BSD */
#endif /* SA_RESTART */
    }

    /*  Technically, this routine should return SIG_ERR if sigaction()
     *    fails here.  But the caller would just err_msg() away, anways.
     */
    if (sigaction(signum, &act1, &act0) < 0)
        err_msg(errno, "signal(%d) failed", signum);
    return(act0.sa_handler);
}
