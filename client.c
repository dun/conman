/*****************************************************************************\
 *  $Id: client.c,v 1.19 2002/05/16 16:54:06 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <unistd.h>
#include "client.h"
#include "common.h"
#include "log.h"


int main(int argc, char *argv[])
{
    client_conf_t *conf;

#ifdef NDEBUG
    log_set_file(stderr, LOG_WARNING);
#else /* NDEBUG */
    log_set_file(stderr, LOG_DEBUG);
#endif /* NDEBUG */

    conf = create_client_conf();
    process_client_env_vars(conf);
    process_client_cmd_line(argc, argv, conf);
    open_client_log(conf);

    if (connect_to_server(conf) < 0)
        display_error(conf);
    if (send_greeting(conf) < 0)
        display_error(conf);
    else if (send_req(conf) < 0)
        display_error(conf);
    else if (recv_rsp(conf) < 0)
        display_error(conf);
    else if (conf->req->command == QUERY)
        display_consoles(conf, STDOUT_FILENO);
    else if ((conf->req->command == CONNECT)
      || (conf->req->command == MONITOR))
        connect_console(conf);
    else
        log_err(0, "INTERNAL: Invalid command=%d", conf->req->command);

    close_client_log(conf);
    destroy_client_conf(conf);
    return(0);
}
