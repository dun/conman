/*****************************************************************************\
 *  $Id: client.c,v 1.13 2002/02/08 18:12:25 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\*****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <unistd.h>
#include "common.h"
#include "client.h"
#include "errors.h"


int main(int argc, char *argv[])
{
    client_conf_t *conf;

    conf = create_client_conf();
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
        err_msg(0, "INTERNAL: Invalid command=%d", conf->req->command);

    close_client_log(conf);
    destroy_client_conf(conf);
    return(0);
}
