/******************************************************************************\
 *  $Id: client-conf.c,v 1.36 2001/12/30 20:08:54 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
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
#include "common.h"
#include "client.h"
#include "errors.h"
#include "list.h"
#include "util-file.h"
#include "util-str.h"


static void read_consoles_from_file(List consoles, char *file);
static void display_client_help(char *prog);


client_conf_t * create_client_conf(void)
{
    client_conf_t *conf;
    uid_t uid;
    char *p;
    struct passwd *passp;

    if (!(conf = malloc(sizeof(client_conf_t))))
        out_of_memory();
    conf->req = create_req();

    /*  Who am I?
     */
    uid = getuid();
    if ((p = getenv("USER")) || (p = getenv("LOGNAME")) || (p = getlogin()))
        passp = getpwnam(p);
    if ((p == NULL) || (passp == NULL) || (passp->pw_uid != uid))
        if (!(passp = getpwuid(uid)))
            err_msg(errno, "Unable to lookup user name for UID=%d", uid);
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
    conf->req->command = CONNECT;

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

    destroy_req(conf->req);
    if (conf->log)
        free(conf->log);
    if (conf->logd >= 0) {
        if (close(conf->logd) < 0)
            err_msg(errno, "close() failed on fd=%d", conf->logd);
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

    opterr = 0;
    while ((c = getopt(argc, argv, "bd:e:fF:hjl:mqQrvV")) != -1) {
        switch(c) {
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
        case 'F':
            read_consoles_from_file(conf->req->consoles, optarg);
            break;
        case 'h':
            display_client_help(argv[0]);
            exit(0);
        case 'j':
            conf->req->enableForce = 0;
            conf->req->enableJoin = 1;
            break;
        case 'l':
            if (conf->log)
                free(conf->log);
            conf->log = create_string(optarg);
            break;
        case 'm':
            conf->req->command = MONITOR;
            break;
        case 'q':
            conf->req->command = QUERY;
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
        case '?':			/* invalid option */
            fprintf(stderr, "ERROR: Invalid option \"%c\".\n", optopt);
            exit(1);
        default:
            fprintf(stderr, "ERROR: Option \"%c\" not implemented.\n", c);
            exit(1);
        }
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
        err_msg(errno, "Unable to open \"%s\"", file);

    while (fgets(buf, sizeof(buf), fp) != NULL) {

        /*  Remove trailing whitespace.
         */
        q = strchr(buf, '\0') - 1;
        while ((q >= buf) && isspace(*q))
            *q-- = '\0';

        /*  Remove leading whitespace.
         */
        p = buf;
        while ((p < q) && isspace(*p))
            p++;

        /*  Skip comments and empty lines.
         */
        if ((*p == '#') || (*p == '\0'))
            continue;

        list_append(consoles, create_string(p));
    }

    if (fclose(fp) != 0)
        err_msg(errno, "Unable to close \"%s\"", file);

    return;
}


static void display_client_help(char *prog)
{
    char esc[3];

    write_esc_char(DEFAULT_CLIENT_ESCAPE, esc);

    printf("Usage: %s [OPTIONS] <console(s)>\n", prog);
    printf("\n");
    printf("  -b        Broadcast (write-only) to multiple consoles.\n");
    printf("  -d HOST   Specify server destination. [%s:%d]\n",
        CONMAN_HOST, atoi(CONMAN_PORT));
    printf("  -e CHAR   Specify escape character. [%s]\n", esc);
    printf("  -f        Force connection (console-stealing).\n");
    printf("  -F FILE   Read console names from file.\n");
    printf("  -h        Display this help.\n");
    printf("  -j        Join connection (console-sharing).\n");
    printf("  -l FILE   Log connection output to file.\n");
    printf("  -m        Monitor connection (read-only).\n");
    printf("  -q        Query server about specified console(s).\n");
    printf("  -Q        Be quiet and suppress informational messages.\n");
    printf("  -r        Match console names via regex instead of globbing.\n");
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

    now = create_long_time_string(0);
    str = create_format_string("%sLog started at %s%s",
        CONMAN_MSG_PREFIX, now, CONMAN_MSG_SUFFIX);
    if (write_n(conf->logd, str, strlen(str)) < 0)
        err_msg(errno, "write() failed on fd=%d", conf->logd);
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
        err_msg(errno, "write() failed on fd=%d", conf->logd);
    free(now);
    free(str);

    if (close(conf->logd) < 0)
        err_msg(errno, "close() failed on fd=%d", conf->logd);
    conf->logd = -1;

    return;
}
