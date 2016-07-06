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


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "client.h"
#include "common.h"
#include "list.h"
#include "log.h"
#include "util-file.h"
#include "util-str.h"
#include "util.h"


static void read_consoles_from_file(List consoles, char *file);
static void display_client_help(client_conf_t *conf);


client_conf_t * create_client_conf(void)
{
    client_conf_t *conf;
    uid_t uid;
    char *p;
    struct passwd *passp = NULL;

    if (!(conf = malloc(sizeof(client_conf_t))))
        out_of_memory();

    conf->prog = NULL;
    conf->req = create_req();

    /*  Who am I?
     */
    uid = getuid();
    if ((p = getlogin()) || (p = getenv("USER")) || (p = getenv("LOGNAME")))
        passp = getpwnam(p);
    if ((p == NULL) || (passp == NULL) || (passp->pw_uid != uid))
        if (!(passp = getpwuid(uid)))
            log_err(errno, "Unable to lookup user name for UID=%d", uid);
    if (passp->pw_name && *passp->pw_name)
        conf->req->user = create_string(passp->pw_name);

    /*  Where am I?
     */
    p = ttyname(STDIN_FILENO);
    if (p && (strstr(p, "/dev/") == p))
        p += 5;
    if (p && *p)
        conf->req->tty = create_string(p);

    /*  Must copy host string constant since it will eventually be free()'d.
     */
    conf->req->host = create_string(CONMAN_HOST);
    conf->req->port = atoi(CONMAN_PORT);
    conf->req->command = CONMAN_CMD_CONNECT;

    conf->escapeChar = DEFAULT_CLIENT_ESCAPE;
    conf->log = NULL;
    conf->logd = -1;
    conf->errnum = CONMAN_ERR_NONE;
    conf->errmsg = NULL;
    conf->enableVerbose = 0;
    conf->isClosedByClient = 0;

    return(conf);
}


void destroy_client_conf(client_conf_t *conf)
{
    if (!conf)
        return;

    if (conf->prog)
        free(conf->prog);
    destroy_req(conf->req);
    if (conf->log)
        free(conf->log);
    if (conf->logd >= 0) {
        if (close(conf->logd) < 0)
            log_err(errno, "close() failed on fd=%d", conf->logd);
        conf->logd = -1;
    }
    if (conf->errmsg)
        free(conf->errmsg);

    free(conf);
    return;
}


void process_client_env_vars(client_conf_t *conf)
{
    char *p, *q;
    int i;

    if ((p = getenv("CONMAN_HOST"))) {
        if ((q = strchr(p, ':'))) {
            *q++ = '\0';
            if ((i = atoi(q)) > 0)
                conf->req->port = i;
        }
        if (*p) {
            if (conf->req->host)
                free(conf->req->host);
            conf->req->host = create_string(p);
        }
    }
    if ((p = getenv("CONMAN_PORT")) && (*p)) {
        if ((i = atoi(p)) > 0)
            conf->req->port = i;
    }
    if ((p = getenv("CONMAN_ESCAPE")) && (*p)) {
        conf->escapeChar = p[0];
    }
    return;
}


void process_client_cmd_line(int argc, char *argv[], client_conf_t *conf)
{
    int c;
    int i;
    char *p;
    int gotHelp = 0;

    if (conf->prog == NULL)
        conf->prog = create_string(argv[0]);

    opterr = 0;
    while ((c = getopt(argc, argv, "bd:e:fF:hjl:LmqQrvV")) != -1) {
        switch(c) {
        case 'b':
            conf->req->enableBroadcast = 1;
            break;
        case 'd':
            if ((p = strchr(optarg, ':'))) {
                *p++ = '\0';
                if ((i = atoi(p)) > 0)
                    conf->req->port = i;
            }
            if (*optarg) {
                if (conf->req->host)
                    free(conf->req->host);
                conf->req->host = create_string(optarg);
            }
            break;
        case 'e':
            conf->escapeChar = optarg[0];
            break;
        case 'f':
            conf->req->enableForce = 1;
            conf->req->enableJoin = 0;
            break;
        case 'F':
            read_consoles_from_file(conf->req->consoles, optarg);
            break;
        case 'h':
            gotHelp = 1;
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
        case 'L':
            printf("%s", conman_license);
            exit(0);
        case 'm':
            conf->req->command = CONMAN_CMD_MONITOR;
            break;
        case 'q':
            conf->req->command = CONMAN_CMD_QUERY;
            break;
        case 'Q':
            conf->req->enableQuiet = 1;
            break;
        case 'r':
            conf->req->enableRegex = 1;
            break;
        case 'v':
            conf->enableVerbose = 1;
            break;
        case 'V':
            printf("%s-%s%s\n", PROJECT, VERSION, CLIENT_FEATURES);
            exit(0);
        case '?':                       /* invalid option */
            log_err(0, "CMDLINE: invalid option \"%c\"", optopt);
            exit(1);
        default:
            log_err(0, "CMDLINE: option \"%c\" not implemented", c);
            exit(1);
        }
    }

    /*  Disable those options not used in R/O mode.
     */
    if (conf->req->command == CONMAN_CMD_MONITOR) {
        conf->req->enableBroadcast = 0;
        conf->req->enableForce = 0;
        conf->req->enableJoin = 0;
    }

    for (i=optind; i<argc; i++) {

        char *p, *q;

        /*  Process comma-separated console lists in order to appease Jim.  :)
         */
        p = argv[i];
        while (p) {
            if ((q = strchr(p, ',')))
                *q++ = '\0';
            if (*p)
                list_append(conf->req->consoles, create_string(p));
            p = q;
        }
    }

    if (gotHelp
        || ((conf->req->command != CONMAN_CMD_QUERY)
            && list_is_empty(conf->req->consoles))) {
        display_client_help(conf);
        exit(0);
    }
    return;
}


static void read_consoles_from_file(List consoles, char *file)
{
/*  Reads console names/patterns from 'file'.
 *  Returns an updated 'consoles' list.
 *  The format of the file is as follows:
 *    - one console name/pattern per line
 *    - leading/trailing whitespace and comments are ignored
 */
    FILE *fp;
    char buf[MAX_LINE];
    char *p, *q;

    assert(consoles != NULL);
    assert(file != NULL);

    if (!(fp = fopen(file, "r")))
        log_err(errno, "Unable to open \"%s\"", file);

    while (fgets(buf, sizeof(buf), fp) != NULL) {

        /*  Remove trailing whitespace.
         */
        q = strchr(buf, '\0') - 1;
        while ((q >= buf) && isspace((int) *q))
            *q-- = '\0';

        /*  Remove leading whitespace.
         */
        p = buf;
        while ((p < q) && isspace((int) *p))
            p++;

        /*  Skip comments and empty lines.
         */
        if ((*p == '#') || (*p == '\0'))
            continue;

        list_append(consoles, create_string(p));
    }

    if (fclose(fp) == EOF)
        log_err(errno, "Unable to close \"%s\"", file);

    return;
}


static void display_client_help(client_conf_t *conf)
{
    char esc[3];

    write_esc_char(conf->escapeChar, esc);

    printf("Usage: %s [OPTIONS] [CONSOLES]\n", conf->prog);
    printf("\n");
    printf("  -b        Broadcast to multiple consoles (write-only).\n");
    printf("  -d HOST   Specify server destination. [%s:%d]\n",
        conf->req->host, conf->req->port);
    printf("  -e CHAR   Specify escape character. [%s]\n", esc);
    printf("  -f        Force connection (console-stealing).\n");
    printf("  -F FILE   Read console names from file.\n");
    printf("  -h        Display this help.\n");
    printf("  -j        Join connection (console-sharing).\n");
    printf("  -l FILE   Log connection output to file.\n");
    printf("  -L        Display license information.\n");
    printf("  -m        Monitor connection (read-only).\n");
    printf("  -q        Query server about specified console(s).\n");
    printf("  -Q        Be quiet and suppress informational messages.\n");
    printf("  -r        Match console names via regex instead of globbing.\n");
    printf("  -v        Be verbose.\n");
    printf("  -V        Display version information.\n");
    printf("\n");
    printf("  Once a connection is established, enter \"%s%c\""
           " to close the session,\n", esc, ESC_CHAR_CLOSE);
    printf("    or \"%s%c\" to see a list of currently available"
           " escape sequences.\n", esc, ESC_CHAR_HELP);
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
        log_err(errno, "Unable to open \"%s\"", conf->log);

    now = create_long_time_string(0);
    str = create_format_string("%sLog started at %s%s",
        CONMAN_MSG_PREFIX, now, CONMAN_MSG_SUFFIX);
    if (write_n(conf->logd, str, strlen(str)) < 0)
        log_err(errno, "Unable to write to \"%s\"", conf->log);
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

    now = create_long_time_string(0);
    str = create_format_string("%sLog finished at %s%s",
        CONMAN_MSG_PREFIX, now, CONMAN_MSG_SUFFIX);
    if (write_n(conf->logd, str, strlen(str)) < 0)
        log_err(errno, "Unable to write to \"%s\"", conf->log);
    free(now);
    free(str);

    if (close(conf->logd) < 0)
        log_err(errno, "Unable to close \"%s\"", conf->log);
    conf->logd = -1;

    return;
}
