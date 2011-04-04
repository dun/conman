/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  Contributed by Levi Pearson <lpearson@lnxi.com>.
 *
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2011 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <http://conman.googlecode.com/>.
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <ipmiconsole.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "list.h"
#include "log.h"
#include "server.h"
#include "tpoll.h"
#include "util.h"
#include "util-file.h"
#include "util-str.h"
#include "wrapper.h"


static int parse_key(char *dst, const char *src, size_t dstlen);
static void disconnect_ipmi_obj(obj_t *ipmi);
static int connect_ipmi_obj(obj_t *ipmi);
static int initiate_ipmi_connect(obj_t *ipmi);
static int create_ipmi_ctx(obj_t *ipmi);
static int complete_ipmi_connect(obj_t *ipmi);
static void fail_ipmi_connect(obj_t *ipmi);
static void reset_ipmi_delay(obj_t *ipmi);

extern tpoll_t tp_global;               /* defined in server.c */
static int is_ipmi_engine_started = 0;


void ipmi_init(int num_consoles)
{
/*  Starts the ipmiconsole engine to handle 'num_consoles' IPMI SOL consoles.
 */
    int num_threads;

    if (num_consoles <= 0) {
        return;
    }
    if (is_ipmi_engine_started) {
        return;
    }
    num_threads = ((num_consoles - 1) / IPMI_ENGINE_CONSOLES_PER_THREAD) + 1;

    if (ipmiconsole_engine_init(num_threads, 0) < 0) {
        log_err(0, "Unable to start IPMI SOL engine");
    }
    else {
        log_msg(LOG_INFO,
            "IPMI SOL engine started with %d thread%s for %d console%s",
            num_threads, (num_threads == 1) ? "" : "s",
            num_consoles, (num_consoles == 1) ? "" : "s");
    }
    is_ipmi_engine_started = 1;
    return;
}


void ipmi_fini(void)
{
/*  Stops the ipmiconsole engine.
 */
    /*  Setting do_sol_session_cleanup to nonzero will cause
     *    ipmiconsole_engine_teardown() to block until all active
     *    ipmi sol sessions have been cleanly closed or timed-out.
     */
    int do_sol_session_cleanup = 1;

    if (!is_ipmi_engine_started) {
        return;
    }
    ipmiconsole_engine_teardown(do_sol_session_cleanup);
    is_ipmi_engine_started = 0;
    return;
}


int parse_ipmi_opts(
    ipmiopt_t *iopts, const char *str, char *errbuf, int errlen)
{
/*  Parses 'str' for ipmi device options 'iopts'.
 *    The 'iopts' struct should be initialized to a default value.
 *    The 'str' string is of the form "[<username>[,<password>[,<K_g key>]]]".
 *  An empty 'str' is valid and denotes specifying no username & password.
 *  Returns 0 and overwrites the 'iopts' struct on success; o/w, returns -1
 *    (writing an error message into 'errbuf' if defined).
 */
    ipmiopt_t ioptsTmp;
    char buf[MAX_LINE];
    const char * const separators = ",";
    char *tok;
    int n;

    assert(iopts != NULL);

    memset(&ioptsTmp, 0, sizeof(ioptsTmp));

    if (strlcpy(buf, str, sizeof(buf)) >= sizeof(buf)) {
        if ((errbuf != NULL) && (errlen > 0)) {
            snprintf(errbuf, errlen,
                "ipmiopt string exceeded buffer size");
        }
        return(-1);
    }
    if ((tok = strtok(buf, separators))) {
        n = strlcpy(ioptsTmp.username, tok, sizeof(ioptsTmp.username));
        if (n >= sizeof(ioptsTmp.username)) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "ipmiopt username exceeds %d-byte max length",
                    IPMI_MAX_USER_LEN);
            }
            return(-1);
        }
    }
    if ((tok = strtok(NULL, separators))) {
        n = parse_key(ioptsTmp.password, tok, sizeof(ioptsTmp.password));
        if (n < 0) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "ipmiopt password exceeds %d-byte max length",
                    IPMI_MAX_PSWD_LEN);
            }
            return(-1);
        }
#if 0
        /*  FIXME: A warning should be logged here when characters in the
         *    password key are ignored due to an embedded NUL character.
         *    But currently there is no clean way to pass this warning back up
         *    to the caller in order to associate it with a line number and
         *    console name.
         */
        if (n > strlen(ioptsTmp.password)) {
        }
#endif
    }
    if ((tok = strtok(NULL, separators))) {
        n = parse_key((char *) ioptsTmp.kg, tok, sizeof(ioptsTmp.kg));
        if (n < 0) {
            if ((errbuf != NULL) && (errlen > 0)) {
                snprintf(errbuf, errlen,
                    "ipmiopt k_g exceeds %d-byte max length",
                    IPMI_MAX_KG_LEN);
            }
            return(-1);
        }
        ioptsTmp.kgLen = n;
    }
    *iopts = ioptsTmp;
    return(0);
}


static int parse_key(char *dst, const char *src, size_t dstlen)
{
/*  Parses the NUL-terminated key string 'src', writing the result into buffer
 *    'dst' of length 'dstlen'.  The 'dst' buffer will be NUL-terminated if
 *    'dstlen' > 0.
 *  The 'src' is interpreted as ASCII text unless it is prefixed with
 *    "0x" or "0X" and contains only hexadecimal digits (ie, [0-9A-Fa-f]).
 *    A hexadecimal string will be converted to binary and may contain
 *    embedded NUL characters.
 *  Returns the length of the key (in bytes) written to 'dst'
 *    (not including the final NUL-termination character),
 *    or -1 if truncation occurred.
 */
    const char *hexdigits = "0123456789ABCDEFabcdef";
    char       *dstend;
    char       *p;
    char       *q;
    int         n;

    assert(dst != NULL);
    assert(src != NULL);

    if (dstlen == 0) {
        return(-1);
    }
    if ((src[0] == '0') && (src[1] == 'x' || src[1] == 'X')
            && (strspn(src + 2, hexdigits) == strlen(src + 2))) {
        dstend = dst + dstlen - 1;      /* reserve space for terminating NUL */
        p = (char *) src + 2;
        q = dst;
        n = 0;
        while (*p && (q < dstend)) {
            if (((p - src) & 0x01) == 0) {
                *q = (toint(*p++) << 4) & 0xf0;
                n++;
            }
            else {
                *q++ |= (toint(*p++)) & 0x0f;
            }
        }
        dst[n] = '\0';
        if (*p) {
            return(-1);
        }
    }
    else {
        n = strlcpy(dst, src, dstlen);
        if (n >= dstlen) {
            return(-1);
        }
    }
    assert(n < dstlen);
    return(n);
}


obj_t * create_ipmi_obj(server_conf_t *conf, char *name,
    ipmiopt_t *iconf, char *host, char *errbuf, int errlen)
{
/*  Creates a new IPMI device object and adds it to the master objs list.
 *  Returns the new object, or NULL on error.
 */
    ListIterator i;
    obj_t *ipmi;

    assert(conf != NULL);
    assert((name != NULL) && (name[0] != '\0'));
    assert(iconf != NULL);

    /*  Check for duplicate console names.
     */
    i = list_iterator_create(conf->objs);
    while ((ipmi = list_next(i))) {
        if (is_console_obj(ipmi) && !strcmp(ipmi->name, name)) {
            snprintf(errbuf, errlen,
                "console [%s] specifies duplicate console name", name);
            break;
        }
        if (is_ipmi_obj(ipmi) && !strcmp(ipmi->aux.ipmi.host, host)) {
            snprintf(errbuf, errlen,
                "console [%s] specifies duplicate hostname \"%s\"",
                name, host);
            break;
        }
    }
    list_iterator_destroy(i);
    if (ipmi != NULL) {
        return(NULL);
    }
    ipmi = create_obj(conf, name, -1, CONMAN_OBJ_IPMI);
    ipmi->aux.ipmi.host = create_string(host);
    ipmi->aux.ipmi.iconf = *iconf;
    ipmi->aux.ipmi.ctx = NULL;
    ipmi->aux.ipmi.logfile = NULL;
    ipmi->aux.ipmi.state = CONMAN_IPMI_DOWN;
    ipmi->aux.ipmi.timer = -1;
    ipmi->aux.ipmi.delay = IPMI_MIN_TIMEOUT;
    x_pthread_mutex_init(&ipmi->aux.ipmi.mutex, NULL);
    conf->numIpmiObjs++;
    /*
     *  Add obj to the master conf->objs list.
     */
    list_append(conf->objs, ipmi);

    return(ipmi);
}


int open_ipmi_obj(obj_t *ipmi)
{
/*  (Re)opens the specified 'ipmi' obj.
 *    A no-op is performed if the connection is already in the pending state.
 *  Returns 0 if the IPMI console is successfully opened; o/w, returns -1.
 */
    int rc = 0;
    ipmi_state_t state;

    assert(ipmi != NULL);
    assert(is_ipmi_obj(ipmi));

    x_pthread_mutex_lock(&ipmi->aux.ipmi.mutex);
    state = ipmi->aux.ipmi.state;
    x_pthread_mutex_unlock(&ipmi->aux.ipmi.mutex);

    if (state == CONMAN_IPMI_UP) {
        disconnect_ipmi_obj(ipmi);
        rc = connect_ipmi_obj(ipmi);
    }
    else if (state == CONMAN_IPMI_DOWN) {
        rc = connect_ipmi_obj(ipmi);
    }
    DPRINTF((9, "Opened [%s] via IPMI: fd=%d host=%s state=%d.\n",
        ipmi->name, ipmi->fd, ipmi->aux.ipmi.host,
        (int) ipmi->aux.ipmi.state));
    return(rc);
}


static void disconnect_ipmi_obj(obj_t *ipmi)
{
/*  Closes the existing connection with the specified 'ipmi' obj.
 */
    DPRINTF((10, "Disconnecting from <%s> via IPMI for [%s].\n",
        ipmi->aux.ipmi.host, ipmi->name));

    x_pthread_mutex_lock(&ipmi->aux.ipmi.mutex);

    if (ipmi->aux.ipmi.timer >= 0) {
        (void) tpoll_timeout_cancel(tp_global, ipmi->aux.ipmi.timer);
        ipmi->aux.ipmi.timer = -1;
    }
    if (ipmi->fd >= 0) {
        if (close(ipmi->fd) < 0) {
            log_msg(LOG_ERR,
                "Unable to close connection to <%s> for console [%s]: %s",
                ipmi->aux.ipmi.host, ipmi->name, strerror(errno));
        }
        ipmi->fd = -1;
    }
    /*  Notify linked objs when transitioning from an UP state.
     */
    if (ipmi->aux.ipmi.state == CONMAN_IPMI_UP) {
        write_notify_msg(ipmi, LOG_NOTICE,
            "Console [%s] disconnected from <%s>",
            ipmi->name, ipmi->aux.ipmi.host);
    }
    ipmi->aux.ipmi.state = CONMAN_IPMI_DOWN;

    x_pthread_mutex_unlock(&ipmi->aux.ipmi.mutex);

    return;
}


static int connect_ipmi_obj(obj_t *ipmi)
{
/*  Establishes a non-blocking connect with the specified (ipmi) obj.
 *  Returns 0 if the connection is successfully completed; o/w, returns -1.
 *
 *  Note that this routine can be invoked from both the main thread and the
 *    ipmiconsole engine thread.
 */
    int rc = 0;

    x_pthread_mutex_lock(&ipmi->aux.ipmi.mutex);

    /*  The if-guard for a !UP state is to protect against a race-condition
     *    where both the main thread and the ipmiconsole engine thread call
     *    this routine at the same time.  If the first thread successfully
     *    completes the connection and transitions to an UP state, it will
     *    set a reset_ipmi_delay timer.  The if-guard protects against the
     *    subsequent thread cancelling this timer.
     *  If the first thread fails to complete the connection, the subsequent
     *    thread will just retry a bit sooner than planned.
     */
    if (ipmi->aux.ipmi.state != CONMAN_IPMI_UP) {

        if (ipmi->aux.ipmi.timer >= 0) {
            (void) tpoll_timeout_cancel(tp_global, ipmi->aux.ipmi.timer);
            ipmi->aux.ipmi.timer = -1;
        }
        if (ipmi->aux.ipmi.state == CONMAN_IPMI_DOWN) {
            rc = initiate_ipmi_connect(ipmi);
        }
        else if (ipmi->aux.ipmi.state == CONMAN_IPMI_PENDING) {
            rc = complete_ipmi_connect(ipmi);
        }
        else {
            log_err(0, "Console [%s] in unexpected IPMI state=%d",
                ipmi->name, (int) ipmi->aux.ipmi.state);
        }
        if (rc < 0) {
            fail_ipmi_connect(ipmi);
        }
    }
    x_pthread_mutex_unlock(&ipmi->aux.ipmi.mutex);
    return(rc);
}


static int initiate_ipmi_connect(obj_t *ipmi)
{
/*  Initiates an IPMI connection attempt.
 *  Returns 0 if the connection initiation is successful, or -1 on error.
 *
 *  XXX: This routine assumes the ipmi mutex is already locked.
 */
    int rc;

    assert(ipmi->aux.ipmi.state == CONMAN_IPMI_DOWN);

    if (create_ipmi_ctx(ipmi) < 0) {
        return(-1);
    }
    DPRINTF((10, "Connecting to <%s> via IPMI for [%s].\n",
        ipmi->aux.ipmi.host, ipmi->name));

    rc = ipmiconsole_engine_submit(ipmi->aux.ipmi.ctx,
        (Ipmiconsole_callback) connect_ipmi_obj, ipmi);
    if (rc < 0) {
        return(-1);
    }
    ipmi->aux.ipmi.state = CONMAN_IPMI_PENDING;
    /*
     *  ipmiconsole_engine_submit() should always call its callback function,
     *    at which point the connection will be established or retried.
     *  This timer is simply an additional safety measure in case it doesn't.
     *  Any existing timer should have already been cancelled at the start of
     *    connect_ipmi_obj().
     */
    assert(ipmi->aux.ipmi.timer == -1);
    ipmi->aux.ipmi.timer = tpoll_timeout_relative(tp_global,
        (callback_f) connect_ipmi_obj, ipmi,
        IPMI_CONNECT_TIMEOUT * 1000);

    return(0);
}


static int create_ipmi_ctx(obj_t *ipmi)
{
/*  Creates a new IPMI context 'ipmi'.
 *  Returns 0 if the context is successfully created; o/w, returns -1.
 *
 *  XXX: This routine assumes the ipmi mutex is already locked.
 */
    struct ipmiconsole_ipmi_config ipmi_config;
    struct ipmiconsole_protocol_config protocol_config;
    struct ipmiconsole_engine_config engine_config;

    ipmi_config.username = ipmi->aux.ipmi.iconf.username;
    ipmi_config.password = ipmi->aux.ipmi.iconf.password;
    ipmi_config.k_g = ipmi->aux.ipmi.iconf.kg;
    ipmi_config.k_g_len = ipmi->aux.ipmi.iconf.kgLen;
    ipmi_config.privilege_level = -1;
    ipmi_config.cipher_suite_id = -1;
    ipmi_config.workaround_flags = 0;

    protocol_config.session_timeout_len = -1;
    protocol_config.retransmission_timeout_len = -1;
    protocol_config.retransmission_backoff_count = -1;
    protocol_config.keepalive_timeout_len = -1;
    protocol_config.retransmission_keepalive_timeout_len = -1;
    protocol_config.acceptable_packet_errors_count = -1;
    protocol_config.maximum_retransmission_count = -1;

    engine_config.engine_flags = 0;
    engine_config.behavior_flags = 0;
    engine_config.debug_flags = 0;

    /*  A context cannot be submitted to the ipmiconsole engine more than once,
     *    so create a new context if one already exists.
     */
    if (ipmi->aux.ipmi.ctx) {
        ipmiconsole_ctx_destroy(ipmi->aux.ipmi.ctx);
    }
    ipmi->aux.ipmi.ctx = ipmiconsole_ctx_create(
        ipmi->aux.ipmi.host, &ipmi_config, &protocol_config, &engine_config);

    if (!ipmi->aux.ipmi.ctx) {
        return(-1);
    }
    return(0);
}


static int complete_ipmi_connect(obj_t *ipmi)
{
/*  Completes an IPMI connection attempt.
 *  Returns 0 if the connection is successfully established, or -1 on error.
 *
 *  XXX: This routine assumes the ipmi mutex is already locked.
 */
    ipmiconsole_ctx_status_t status;

    assert(ipmi->aux.ipmi.state == CONMAN_IPMI_PENDING);

    status = ipmiconsole_ctx_status(ipmi->aux.ipmi.ctx);
    if (status != IPMICONSOLE_CTX_STATUS_SOL_ESTABLISHED) {
        return(-1);
    }
    if ((ipmi->fd = ipmiconsole_ctx_fd(ipmi->aux.ipmi.ctx)) < 0) {
        return(-1);
    }
    set_fd_nonblocking(ipmi->fd);
    set_fd_closed_on_exec(ipmi->fd);

    ipmi->gotEOF = 0;
    ipmi->aux.ipmi.state = CONMAN_IPMI_UP;

    /*  Require the connection to be up for a minimum length of time
     *    before resetting the reconnect delay back to the minimum.
     *  Any existing timer should have already been cancelled at the start of
     *    connect_ipmi_obj().
     */
    assert(ipmi->aux.ipmi.timer == -1);
    ipmi->aux.ipmi.timer = tpoll_timeout_relative(tp_global,
        (callback_f) reset_ipmi_delay, ipmi, IPMI_MIN_TIMEOUT * 1000);

    /*  Notify linked objs when transitioning into an UP state.
     */
    write_notify_msg(ipmi, LOG_INFO, "Console [%s] connected to <%s>",
        ipmi->name, ipmi->aux.ipmi.host);
    DPRINTF((15, "Connection established to <%s> via IPMI for [%s].\n",
        ipmi->aux.ipmi.host, ipmi->name));
    return (0);
}


static void fail_ipmi_connect(obj_t *ipmi)
{
/*  Logs an error message for the connection failure and sets a timer to
 *    establish a new connection attempt.
 *
 *  XXX: This routine assumes the ipmi mutex is already locked.
 */
    ipmi->aux.ipmi.state = CONMAN_IPMI_DOWN;

    if (!ipmi->aux.ipmi.ctx) {
        log_msg(LOG_INFO,
            "Unable to create IPMI context for [%s]", ipmi->name);
    }
    else {
        int e = ipmiconsole_ctx_errnum(ipmi->aux.ipmi.ctx);
        log_msg(LOG_INFO,
            "Unable to connect to <%s> via IPMI for [%s]: %s",
            ipmi->aux.ipmi.host, ipmi->name, ipmiconsole_ctx_strerror(e));
    }
    /*  Set timer for establishing new connection attempt.
     *  Any existing timer should have already been cancelled at the start of
     *    connect_ipmi_obj().
     */
    assert(ipmi->aux.ipmi.delay >= IPMI_MIN_TIMEOUT);
    assert(ipmi->aux.ipmi.delay <= IPMI_MAX_TIMEOUT);
    DPRINTF((15, "Reconnect attempt to <%s> via IPMI for [%s] in %ds.\n",
        ipmi->aux.ipmi.host, ipmi->name, ipmi->aux.ipmi.delay));
    assert(ipmi->aux.ipmi.timer == -1);
    ipmi->aux.ipmi.timer = tpoll_timeout_relative(tp_global,
        (callback_f) connect_ipmi_obj, ipmi,
        ipmi->aux.ipmi.delay * 1000);

    /*  Update timer delay via exponential backoff.
     */
    if (ipmi->aux.ipmi.delay < IPMI_MAX_TIMEOUT) {
        ipmi->aux.ipmi.delay = MIN(ipmi->aux.ipmi.delay * 2, IPMI_MAX_TIMEOUT);
    }
    return;
}


static void reset_ipmi_delay(obj_t *ipmi)
{
/*  Resets the 'ipmi' obj's delay between reconnect attempts.
 */
    assert(is_ipmi_obj(ipmi));

    x_pthread_mutex_lock(&ipmi->aux.ipmi.mutex);

    ipmi->aux.ipmi.delay = IPMI_MIN_TIMEOUT;

    /*  Also reset the timer ID since this routine is only invoked via a timer.
     */
    ipmi->aux.ipmi.timer = -1;

    x_pthread_mutex_unlock(&ipmi->aux.ipmi.mutex);
    return;
}


int send_ipmi_break(obj_t *ipmi)
{
/*  Generates a serial-break for the specified 'ipmi' obj.
 *  Returns 0 on success; o/w, returns -1.
 */
    int rc;

    assert(ipmi != NULL);
    assert(is_ipmi_obj(ipmi));

    x_pthread_mutex_lock(&ipmi->aux.ipmi.mutex);
    if (!ipmi->aux.ipmi.ctx) {
        log_msg(LOG_ERR,
            "Unable to send serial-break to [%s]: NULL IPMI context",
            ipmi->name);
        rc = -1;
    }
    else {
        rc = ipmiconsole_ctx_generate_break(ipmi->aux.ipmi.ctx);
    }
    x_pthread_mutex_unlock(&ipmi->aux.ipmi.mutex);
    return(rc);
}
