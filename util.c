/*****************************************************************************\
 *  $Id: util.c,v 1.19 2002/03/14 03:37:00 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman.html>.
 *  
 *  ConMan was produced at the University of California, Lawrence Livermore
 *  National Laboratory (UC LLNL) under contract no. W-7405-ENG-48
 *  (Contract 48) between the U.S. Department of Energy (DOE) and The Regents
 *  of the University of California (University) for the operation of UC LLNL.
 *  The rights of the Federal Government are reserved under Contract 48
 *  subject to the restrictions agreed upon by the DOE and University as
 *  allowed under DOE Acquisition Letter 97-1.
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
 *****************************************************************************
 *  Refer to "util.h" for documentation on public functions.
\*****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */


#include <errno.h>
#include <signal.h>
#include "errors.h"
#include "util.h"


#ifdef WITH_OOMF
#undef out_of_memory
void * out_of_memory(void)
{
/*  Example for an external Out-Of-Memory Function.
 */
    err_msg(0, "Out of memory");
    return(NULL);                       /* not reached but prevents warnings */
}
#endif /* WITH_OOMF */


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
        act1.sa_flags |= SA_INTERRUPT;  /* SunOS 4.x */
#endif /* SA_INTERRUPT */
    }
    else {
#ifdef SA_RESTART
        act1.sa_flags |= SA_RESTART;    /* SVR4, 4.4BSD */
#endif /* SA_RESTART */
    }

    /*  Technically, this routine should return SIG_ERR if sigaction()
     *    fails here.  But the caller would just err_msg() away, anways.
     */
    if (sigaction(signum, &act1, &act0) < 0)
        err_msg(errno, "signal(%d) failed", signum);
    return(act0.sa_handler);
}
