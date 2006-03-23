/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *
 *  Copyright (C) 2001-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *****************************************************************************/


#ifndef _COMMON_LOG_H
#define _COMMON_LOG_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <syslog.h>


/*****************************************************************************
 *  Constants
 *****************************************************************************/

#define LOG_OPT_NONE            0x00
#define LOG_OPT_JUSTIFY         0x01    /* L-justify msg str after priority  */
#define LOG_OPT_PID             0x02    /* include pid if an identity is set */
#define LOG_OPT_PRIORITY        0x04    /* add priority string to message    */
#define LOG_OPT_TIMESTAMP       0x08    /* add timestamp to message          */


/*****************************************************************************
 *  Function Prototypes
 *****************************************************************************/

int log_open_file (FILE *fp, const char *identity, int options, int priority);

int log_open_syslog (const char *identity, int options, int facility);

void log_close_file (void);

void log_close_syslog (void);

void log_close_all (void);

void log_msg (int priority, const char *format, ...);


#endif /* !_COMMON_LOG_H */
