/******************************************************************************\
 *  $Id: common.c,v 1.2 2001/05/24 20:56:08 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>


char *proto_strs[] = {
/*
 *  Keep strings in sync w/ conman.h:proto_toks enum.
 */
    "OK",
    "ERROR",
    "HELLO",
    "QUERY",
    "MONITOR",
    "CONNECT",
    "EXECUTE",
    "GOODBYE",
    "CODE",
    "MESSAGE",
    "USER",
    "CONSOLE",
    "PROGRAM",
    "OPTION",
    "BROADCAST",
    "FORCE",
    NULL
};
