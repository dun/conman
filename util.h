/*****************************************************************************\
 *  $Id: util.h,v 1.15 2002/03/14 03:37:00 dun Exp $
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
\*****************************************************************************/


#ifndef _UTIL_H
#define _UTIL_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */


typedef void * (*PthreadFunc)(void *);

typedef void SigFunc(int);


#ifndef MAX
#  define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif /* !MAX */
#ifndef MIN
#  define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */


SigFunc * posix_signal(int signum, SigFunc *f);
/*
 *  A wrapper for the historical signal() function to do things the Posix way.
 */


#endif /* !_UTIL_H */
