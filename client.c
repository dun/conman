/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <http://home.gna.org/conman/>.
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
    log_set_file(stderr, LOG_WARNING, 0);
#else /* NDEBUG */
    log_set_file(stderr, LOG_DEBUG, 0);
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
    else if (conf->req->command == CONMAN_CMD_QUERY)
        display_consoles(conf, STDOUT_FILENO);
    else if ((conf->req->command == CONMAN_CMD_CONNECT)
      || (conf->req->command == CONMAN_CMD_MONITOR))
        connect_console(conf);
    else
        log_err(0, "INTERNAL: Invalid command=%d", conf->req->command);

    close_client_log(conf);
    destroy_client_conf(conf);
    return(0);
}
