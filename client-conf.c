/******************************************************************************\
 *  client-conf.c
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: client-conf.c,v 1.9 2001/05/24 20:51:26 dun Exp $
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "conman.h"
#include "client.h"
#include "errors.h"
#include "util.h"


#ifndef NDEBUG
#define DEBUG_STRING " (debug)"
#else
#define DEBUG_STRING ""
#endif /* !NDEBUG */


static void display_client_help(char *prog);


client_conf_t * create_client_conf(void)
{
    client_conf_t *conf;
    uid_t uid;
    struct passwd *passp;

    if (!(conf = malloc(sizeof(client_conf_t))))
        err_msg(0, "Out of memory");

    conf->sd = -1;

    uid = getuid();
    if (!(passp = getpwuid(uid)))
        err_msg(errno, "Unable to lookup UID %d", uid);
    conf->user = create_string(passp->pw_name);

    /*  Must copy host string constant since it will eventually be free()'d.
     */
    conf->dhost = create_string(DEFAULT_CONMAN_HOST);
    conf->dport = DEFAULT_CONMAN_PORT;

    conf->command = CONNECT;
    conf->escapeChar = DEFAULT_CLIENT_ESCAPE;
    conf->enableBroadcast = 0;
    conf->enableForce = 0;
    conf->enableVerbose = 0;
    conf->program = NULL;
    conf->log = NULL;
    conf->ld = -1;
    if (!(conf->consoles = list_create((ListDelF) destroy_string)))
        err_msg(0, "Out of memory");
    conf->closedByClient = 0;
    conf->errnum = CONMAN_ERR_NONE;
    conf->errmsg = NULL;

    return(conf);
}


void destroy_client_conf(client_conf_t *conf)
{
    if (conf->sd >= 0) {
        if (close(conf->sd) < 0)
            err_msg(errno, "close(%d) failed", conf->sd);
        conf->sd = -1;
    }
    if (conf->user)
        free(conf->user);
    if (conf->program)
        free(conf->program);
    if (conf->log)
        free(conf->log);
    if (conf->ld >= 0) {
        if (close(conf->ld) < 0)
            err_msg(errno, "close(%d) failed", conf->ld);
        conf->ld = -1;
    }
    if (conf->consoles)
        list_destroy(conf->consoles);
    if (conf->errmsg)
        free(conf->errmsg);
    free(conf);
    return;
}


void process_client_cmd_line(int argc, char *argv[], client_conf_t *conf)
{
    int c;
    int i;
    char *str;

    opterr = 1;
    while ((c = getopt(argc, argv, "bd:e:fhl:qrx:vV")) != -1) {
        switch(c) {
        case '?':			/* invalid option */
            exit(1);
        case 'h':
            display_client_help(argv[0]);
            exit(0);
        case 'V':
            printf("%s-%s%s\n", PACKAGE, VERSION, DEBUG_STRING);
            exit(0);
        case 'b':
            conf->enableBroadcast = 1;
            break;
        case 'd':
            if (conf->dhost)
                free(conf->dhost);
            if ((str = strchr(optarg, ':'))) {
                *str++ = '\0';
                conf->dport = atoi(str);
            }
            conf->dhost = create_string(optarg);
            break;
        case 'e':
            conf->escapeChar = optarg[0];
            break;
        case 'f':
            conf->enableForce = 1;
            break;
        case 'l':
            if (conf->log)
                free(conf->log);
            conf->log = create_string(optarg);
            break;
        case 'q':
            conf->command = QUERY;
            break;
        case 'r':
            conf->command = MONITOR;
            break;
        case 'x':
            conf->command = EXECUTE;
            if (conf->program)
                free(conf->program);
            conf->program = create_string(optarg);
            break;
        case 'v':
            conf->enableVerbose = 1;
            break;
        default:
            printf("%s: option not implemented -- %c\n", argv[0], c);
            break;
        }
    }

    for (i=optind; i<argc; i++) {
        str = create_string(argv[i]);
        if (!list_append(conf->consoles, str))
            err_msg(0, "Out of memory");
    }
    return;
}


static void display_client_help(char *prog)
{
    char esc[3];

    write_esc_char(DEFAULT_CLIENT_ESCAPE, esc);

    printf("Usage: %s [OPTIONS] <console(s)>\n", prog);
    printf("\n");
    printf("  -h        Display this help.\n");
    printf("  -b        Broadcast (write-only) to multiple consoles.\n");
    printf("  -d HOST   Specify location of server"
        " (default: %s:%d).\n", DEFAULT_CONMAN_HOST, DEFAULT_CONMAN_PORT);
    printf("  -e CHAR   Set escape character (default: '%s').\n", esc);
    printf("  -f        Force open connection.\n");
    printf("  -l FILE   Log connection to file.\n");
    printf("  -q        Query server about specified console(s).\n");
    printf("  -r        Monitor (read-only) a particular console.\n");
    printf("  -x FILE   Execute file on specified console(s).\n");
    printf("  -v        Be verbose.\n");
    printf("  -V        Display version information.\n");
    printf("\n");
    return;
}


void open_client_log(client_conf_t *conf)
{
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    char *now, *str;

    if (!conf->log)
        return;
    assert(conf->ld < 0);

    if ((conf->ld = open(conf->log, flags, S_IRUSR | S_IWUSR)) < 0)
        err_msg(errno, "Unable to open logfile [%s]", conf->log);

    now = create_time_string(0);
    str = create_fmt_string("\r\n%s Log started %s.\r\n",
        CONMAN_MSG_PREFIX, now);
    if (write_n(conf->ld, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", conf->ld);
    free(now);
    free(str);

    return;
}


void close_client_log(client_conf_t *conf)
{
    char *now, *str;

    if (!conf->log)
        return;
    assert(conf->ld >= 0);

    now = create_time_string(0);
    str = create_fmt_string("\r\n%s Log finished %s.\r\n",
        CONMAN_MSG_PREFIX, now);
    if (write_n(conf->ld, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", conf->ld);
    free(now);
    free(str);

    if (close(conf->ld) < 0)
        err_msg(errno, "close(%d) failed", conf->ld);
    conf->ld = -1;

    return;
}
