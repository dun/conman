/******************************************************************************\
 *  $Id: server-conf.c,v 1.9 2001/06/12 16:17:48 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "lex.h"
#include "list.h"
#include "server.h"
#include "util.h"


enum server_conf_toks {
    SERVER_CONF_CONSOLE = LEX_TOK_OFFSET,
    SERVER_CONF_NAME,
    SERVER_CONF_DEV,
    SERVER_CONF_LOG,
    SERVER_CONF_RST,
    SERVER_CONF_BPS,
    SERVER_CONF_LOGFILE,
};

static char *server_conf_strs[] = {
    "CONSOLE",
    "NAME",
    "DEV",
    "LOG",
    "RST",
    "BPS",
    "LOGFILE",
    NULL
};


static void display_server_help(char *prog);
static void parse_console_cmd(Lex l, server_conf_t *conf);
static void parse_logfile_cmd(Lex l, server_conf_t *conf);


server_conf_t * create_server_conf(void)
{
    server_conf_t *conf;

    if (!(conf = malloc(sizeof(server_conf_t))))
        err_msg(0, "Out of memory");
    conf->filename = create_string(DEFAULT_SERVER_CONF);
    conf->logname = NULL;
    conf->ld = -1;
    if (!(conf->objs = list_create((ListDelF) destroy_obj)))
        err_msg(0, "Out of memory");
    return(conf);
}


void destroy_server_conf(server_conf_t *conf)
{
    if (!conf)
        return;

    if (conf->filename)
        free(conf->filename);
    if (conf->logname)
        free(conf->logname);
    if (conf->ld >= 0) {
        if (close(conf->ld) < 0)
            err_msg(errno, "close(%d) failed", conf->ld);
        conf->ld = -1;
    }
    if (conf->objs)
        list_destroy(conf->objs);

    free(conf);
    return;
}


void process_server_cmd_line(int argc, char *argv[], server_conf_t *conf)
{
    int c;

    opterr = 1;
    while ((c = getopt(argc, argv, "c:hV")) != -1) {
        switch(c) {
        case '?':			/* invalid option */
            exit(1);
        case 'h':
            display_server_help(argv[0]);
            exit(0);
        case 'V':
            printf("%s-%s%s\n", PACKAGE, VERSION, DEBUG_STRING);
            exit(0);
        case 'c':
            if (conf->filename)
                free(conf->filename);
            conf->filename = create_string(optarg);
            break;
        default:
            printf("%s: option not implemented -- %c\n", argv[0], c);
            break;
        }
    }
    return;
}


void process_server_conf_file(server_conf_t *conf)
{
    int fd;
    struct stat fdStat;
    int len;
    char *buf;
    int n;
    Lex l;
    int tok;

    if ((fd = open(conf->filename, O_RDONLY)) < 0)
        err_msg(errno, "Unable to open \"%s\"", conf->filename);
    if (fstat(fd, &fdStat) < 0)
        err_msg(errno, "Unable to stat \"%s\"", conf->filename);
    len = fdStat.st_size;
    if (!(buf = malloc(len + 1)))
        err_msg(errno, "Unable to allocate memory for parsing \"%s\"",
            conf->filename);
    if ((n = read_n(fd, buf, len)) < 0)
        err_msg(errno, "Unable to read \"%s\"", conf->filename);
    assert(n == len);
    buf[len] = '\0';
    if (close(fd) < 0)
        err_msg(errno, "Unable to close \"%s\"", conf->filename);

    if (!(l = lex_create(buf, server_conf_strs)))
        err_msg(0, "Unable to create lexer");
    while ((tok = lex_next(l)) != LEX_EOF) {
        switch(tok) {
        case SERVER_CONF_CONSOLE:
            parse_console_cmd(l, conf);
            break;
        case SERVER_CONF_LOGFILE:
            parse_logfile_cmd(l, conf);
            break;
        case LEX_EOL:
            break;
        case LEX_ERR:
            printf("ERROR: %s:%d: unmatched quote.\n",
                conf->filename, lex_line(l));
            break;
        default:
            printf("ERROR: %s:%d: unrecognized token '%s'.\n",
                conf->filename, lex_line(l), lex_text(l));
            while (tok != LEX_EOL && tok != LEX_EOF)
                tok = lex_next(l);
            break;
        }
    }
    lex_destroy(l);

    free(buf);
    return;
}


static void display_server_help(char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("  -h        Display this help.\n");
    printf("  -c FILE   Specify alternate configuration (default: %s).\n",
        DEFAULT_SERVER_CONF);
    printf("  -V        Display version information.\n");
    printf("\n");
    return;
}


static void parse_console_cmd(Lex l, server_conf_t *conf)
{
/*  CONSOLE NAME="<str>" DEV="<str>" [LOG=<str>] [RST=<str>] [BPS=<int>]
 */
    char *directive;
    int tok;
    int done = 0;
    char err[MAX_LINE] = "";
    char name[MAX_LINE] = "";
    char dev[MAX_LINE] = "";
    char log[MAX_LINE] = "";
    char rst[MAX_LINE] = "";
    int bps = DEFAULT_CONSOLE_BAUD;
    obj_t *console;
    obj_t *logfile;

    directive = server_conf_strs[LEX_UNTOK(lex_prev(l))];

    while (!done && !*err) {
        tok = lex_next(l);
        switch(tok) {
        case SERVER_CONF_NAME:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s token",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(name, lex_text(l), MAX_LINE);
            break;
        case SERVER_CONF_DEV:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s token",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(dev, lex_text(l), MAX_LINE);
            strlcpy(dev, lex_text(l), MAX_LINE);
            break;
        case SERVER_CONF_LOG:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s token",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(log, lex_text(l), MAX_LINE);
            break;
        case SERVER_CONF_RST:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s token",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(rst, lex_text(l), MAX_LINE);
            break;
        case SERVER_CONF_BPS:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s token",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR && lex_prev(l) != LEX_INT)
                snprintf(err, sizeof(err), "expected INTEGER for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if ((bps = atoi(lex_text(l))) <= 0)
                snprintf(err, sizeof(err), "invalid %s value %d",
                    server_conf_strs[LEX_UNTOK(tok)], bps);
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        case LEX_ERR:
            snprintf(err, sizeof(err), "unmatched quote");
            break;
        default:
            snprintf(err, sizeof(err), "unrecognized token '%s'", lex_text(l));
            break;
        }
    }

    if (!*err && (!*name || !*dev)) {
        snprintf(err, sizeof(err), "incomplete %s directive", directive);
    }
    if (*err) {
        fprintf(stderr, "ERROR: %s:%d: %s.\n",
            conf->filename, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            lex_next(l);
    }
    else {
        if (!(console = create_console_obj(conf->objs, name, dev, rst, bps)))
            log_msg(0, "Unable to create console object '%s'", name);
        if (*log) {
            if (!(logfile = create_logfile_obj(conf->objs, log, console)))
                log_msg(0, "Unable to create logfile object '%s'", log);
            else
                link_objs(console, logfile);
        }
    }
    return;
}


static void parse_logfile_cmd(Lex l, server_conf_t *conf)
{
/*  LOGFILE NAME="<str>"
 */
    char *directive;
    int tok;
    int done = 0;
    char err[MAX_LINE] = "";
    char name[MAX_LINE] = "";

    directive = server_conf_strs[LEX_UNTOK(lex_prev(l))];

    while (!done && !*err) {
        tok = lex_next(l);
        switch(tok) {
        case SERVER_CONF_NAME:
            if (lex_next(l) != '=')
                snprintf(err, sizeof(err), "expected '=' after %s token",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else if (lex_next(l) != LEX_STR)
                snprintf(err, sizeof(err), "expected STRING for %s value",
                    server_conf_strs[LEX_UNTOK(tok)]);
            else
                strlcpy(name, lex_text(l), MAX_LINE);
            break;
        case LEX_EOF:
        case LEX_EOL:
            done = 1;
            break;
        case LEX_ERR:
            snprintf(err, sizeof(err), "unmatched quote");
            break;
        default:
            snprintf(err, sizeof(err), "unrecognized token '%s'", lex_text(l));
            break;
        }
    }

    if (!*err && !*name) {
        snprintf(err, sizeof(err), "incomplete %s directive", directive);
    }
    if (*err) {
        fprintf(stderr, "ERROR: %s:%d: %s.\n",
            conf->filename, lex_line(l), err);
        while (lex_prev(l) != LEX_EOL && lex_prev(l) != LEX_EOF)
            lex_next(l);
    }
    else {
        if (conf->logname)
            free(conf->logname);
        conf->logname = create_string(name);
    }
    return;
}
