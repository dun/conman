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


#ifndef _INEVENT_H
#define _INEVENT_H


/*****************************************************************************
 *  Data Types
 *****************************************************************************/

typedef void (*inevent_cb_f) (void *arg);
/*
 *  Function prototype for a inevent callback function.
 */


/*****************************************************************************
 *  Functions
 *****************************************************************************/

int inevent_add (const char *filename, inevent_cb_f cb_fnc, void *cb_arg);

int inevent_remove (const char *filename);

int inevent_get_fd (void);

int inevent_process (void);


#endif /* !_INEVENT_H */
