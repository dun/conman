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


#ifndef CONMAND_CONF_H
#define CONMAND_CONF_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */


/*****************************************************************************
 *  Data Types
 *****************************************************************************/

struct conf {
    int foo;
};

typedef struct conf * conf_t;


/*****************************************************************************
 *  External Variables
 *****************************************************************************/

extern conf_t conf;                     /* defined in conf.c                 */


/*****************************************************************************
 *  External Function Prototypes
 *****************************************************************************/

conf_t create_conf (void);

void destroy_conf (conf_t conf);

void parse_cmdline (conf_t conf, int argc, char **argv);


#endif /* !CONMAND_CONF_H */
