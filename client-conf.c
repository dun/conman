/******************************************************************************\
 *  $Id: client-conf.c,v 1.13 2001/06/12 16:17:47 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
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
#include "common.h"
#include "client.h"
#include "errors.h"
#include "util.h"


static void display_client_help(char *prog);


client_conf_t * create_client_conf(void)
{
    client_conf_t *conf;
    uid_t uid;
    char *p;
    struct passwd *passp;

    if (!(conf = malloc(sizeof(client_conf_t))))
        err_msg(0, "Out of memory");
    if (!(conf->req = create_req()))
        err_msg(0, "Out of memory");

    /*  Who am I?
     */
    uid = getuid();
    if ((p = getenv("USER")) || (p = getenv("LOGNAME")))
        passp = getpwnam(p);
    if ((p == NULL) || (passp == NULL) || (passp->pw_uid != uid))
        if (!(passp = getpwuid(uid)))
            err_msg(errno, "Unable to lookup user name for UID=%d", uid);
    conf->req->user = create_string(passp->pw_name);

    /*  Where am I?
     */
    p = ttyname(STDIN_FILENO);
    if (strstr(p, "/dev/") == p)
        p += 5;
    conf->req->tty = create_string(p);

    /*  Must copy host string constant since it will eventually be free()'d.
     */
    conf->req->host = create_string(DEFAULT_CONMAN_HOST);
    conf->req->port = DEFAULT_CONMAN_PORT;
    conf->req->command = CONNECT;

    conf->escapeChar = DEFAULT_CLIENT_ESCAPE;
    conf->enableVerbose = 0;
    conf->log = NULL;
    conf->logd = -1;
    conf->closedByClient = 0;
    conf->errnum = CONMAN_ERR_NONE;
    conf->errmsg = NULL;

    return(conf);
}


void destroy_client_conf(client_conf_t *conf)
{
    if (!conf)
        return;

    destroy_req(conf->req);
    if (conf->log)
        free(conf->log);
    if (conf->logd >= 0) {
        if (close(conf->logd) < 0)
            err_msg(errno, "close(%d) failed", conf->logd);
        conf->logd = -1;
    }
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
    while ((c = getopt(argc, argv, "bd:e:fhjl:qrx:vV")) != -1) {
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
            conf->req->enableBroadcast = 1;
            break;
        case 'd':
            if (conf->req->host)
                free(conf->req->host);
            if ((str = strchr(optarg, ':'))) {
                *str++ = '\0';
                conf->req->port = atoi(str);
            }
            conf->req->host = create_string(optarg);
            break;
        case 'e':
            conf->escapeChar = optarg[0];
            break;
        case 'f':
            conf->req->enableForce = 1;
            conf->req->enableJoin = 0;
            break;
        case 'j':
            conf->req->enableForce = 0;
            conf->req->enableJoin = 1;
            break;
        case 'l':
            if (conf->log)
                free(conf->log);
            conf->log = create_string(optarg);
            break;
        case 'q':
            conf->req->command = QUERY;
            break;
        case 'r':
            conf->req->command = MONITOR;
            break;
        case 'x':
            conf->req->command = EXECUTE;
            if (conf->req->program)
                free(conf->req->program);
            conf->req->program = create_string(optarg);
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
        if (!list_append(conf->req->consoles, str))
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
    printf("  -f        Force connection by stealing console.\n");
    printf("  -j        Join connection with existing write-sessions.\n");
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
    assert(conf->logd < 0);

    if ((conf->logd = open(conf->log, flags, S_IRUSR | S_IWUSR)) < 0)
        err_msg(errno, "Unable to open logfile [%s]", conf->log);

    now = create_date_time_string(0);
    str = create_fmt_string("\r\n%sLog started at %s%s\r\n",
        CONMAN_MSG_PREFIX, now, CONMAN_MSG_SUFFIX);
    if (write_n(conf->logd, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", conf->logd);
    free(now);
    free(str);

    return;
}


void close_client_log(client_conf_t *conf)
{
    char *now, *str;

    if (!conf->log)
        return;
    assert(conf->logd >= 0);

    now = create_date_time_string(0);
    str = create_fmt_string("\r\n%sLog finished at %s%s\r\n",
        CONMAN_MSG_PREFIX, now, CONMAN_MSG_SUFFIX);
    if (write_n(conf->logd, str, strlen(str)) < 0)
        err_msg(errno, "write(%d) failed", conf->logd);
    free(now);
    free(str);

    if (close(conf->logd) < 0)
        err_msg(errno, "close(%d) failed", conf->logd);
    conf->logd = -1;

    return;
}
