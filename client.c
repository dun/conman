/******************************************************************************\
 *  $Id: client.c,v 1.7 2001/06/15 17:04:29 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <unistd.h>
#include "common.h"
#include "client.h"
#include "errors.h"


int main(int argc, char *argv[])
{
    client_conf_t *conf;

    if (!isatty(STDIN_FILENO))
        err_msg(0, "Standard Input is not a terminal device.");
    if (!isatty(STDOUT_FILENO))
        err_msg(0, "Standard Output is not a terminal device.");

    conf = create_client_conf();
    process_client_cmd_line(argc, argv, conf);
    open_client_log(conf);

    connect_to_server(conf);

    if (send_greeting(conf) < 0)
        display_error(conf);
    else if (send_req(conf) < 0)
        display_error(conf);
    else if (recv_rsp(conf) < 0)
        display_error(conf);
    else if (conf->req->command == QUERY)
        display_consoles(conf, STDOUT_FILENO);
    else if (conf->req->command == EXECUTE)
        display_data(conf, STDOUT_FILENO);
    else if ((conf->req->command == CONNECT)
      || (conf->req->command == MONITOR))
        connect_console(conf);
    else
        err_msg(0, "Invalid command (%d) at %s:%d",
            conf->req->command, __FILE__, __LINE__);

    close_client_log(conf);
    destroy_client_conf(conf);
    return(0);
}
