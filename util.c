/******************************************************************************\
 *  $Id: util.c,v 1.11 2001/08/06 18:36:25 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "util.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "errors.h"
#include "util.h"


#define MAX_STR_SIZE 1024

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif /* !INET_ADDRSTRLEN */


char * create_string(const char *str)
{
    char *p;

    if (!str)
        return(NULL);
    if (!(p = strdup(str)))
        err_msg(0, "Out of memory");
    return(p);
}


char * create_format_string(const char *fmt, ...)
{
    va_list vargs;
    int n, len;
    char *p;

    if (!fmt)
        return(NULL);

    va_start(vargs, fmt);
    if ((len = vsnprintf(p, 0, fmt, vargs)) < 0)
        len = MAX_STR_SIZE;
    else				/* C99 standard returns needed strlen */
        len++;				/* reserve space for terminating NUL */
    if (!(p = (char *) malloc(len)))
        err_msg(0, "Out of memory");
    n = vsnprintf(p, len, fmt, vargs);
    va_end(vargs);

    if (n < 0 || n >= len)
        p[len - 1] = '\0';		/* ensure str is NUL-terminated */
    return(p);
}


size_t append_format_string(char *dst, size_t size, const char *fmt, ...)
{
    char *p = dst;
    int nAvail = size;
    int lenOrig;
    va_list vargs;
    int n;

    if (!dst || !fmt)
        return(0);

    while ((nAvail > 0) && *p)
        nAvail--, p++;
    if (nAvail <= 1)
        return(-1);
    lenOrig = p - dst;

    va_start(vargs, fmt);
    n = vsnprintf(p, nAvail, fmt, vargs);
    va_end(vargs);

    if (n < 0 || n >= nAvail) {
        dst[size - 1] = '\0';		/* ensure dst is NUL-terminated */
        return(-1);
    }
    return(lenOrig + n);
}


void destroy_string(char *str)
{
    if (str)
        free(str);
    return;
}


char * create_long_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 25;			/* MM/DD/YYYY HH:MM:SS ZONE + NUL */

    if (!(p = malloc(len)))
        err_msg(0, "Out of memory");

    get_localtime(&t, &tm);

    if (strftime(p, len, "%m/%d/%Y %H:%M:%S %Z", &tm) == 0)
        err_msg(0, "strftime() failed");

    return(p);
}


char * create_short_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 6;			/* HH:MM + NUL */

    if (!(p = malloc(len)))
        err_msg(0, "Out of memory");

    get_localtime(&t, &tm);

    if (strftime(p, len, "%H:%M", &tm) == 0)
        err_msg(0, "strftime() failed");

    return(p);
}


char * create_time_delta_string(time_t t)
{
    time_t now;
    long n;
    int years, weeks, days, hours, minutes, seconds;
    char buf[25];

    if (time(&now) == ((time_t) -1))
        err_msg(errno, "time() failed -- What time is it?");
    n = difftime(now, t);
    assert(n >= 0);

    seconds = n % 60;
    n /= 60;
    minutes = n % 60;
    n /= 60;
    hours = n % 24;
    n /= 24;
    days = n % 7;
    n /= 7;
    weeks = n % 52;
    n /= 52;
    years = n;

    if (years > 0)
        n = snprintf(buf, sizeof(buf), "%dy%dw%dd%dh%dm%ds",
            years, weeks, days, hours, minutes, seconds);
    else if (weeks > 0)
        n = snprintf(buf, sizeof(buf), "%dw%dd%dh%dm%ds",
            weeks, days, hours, minutes, seconds);
    else if (days > 0)
        n = snprintf(buf, sizeof(buf), "%dd%dh%dm%ds",
            days, hours, minutes, seconds);
    else if (hours > 0)
        n = snprintf(buf, sizeof(buf), "%dh%dm%ds", hours, minutes, seconds);
    else if (minutes > 0)
        n = snprintf(buf, sizeof(buf), "%dm%ds", minutes, seconds);
    else
        n = snprintf(buf, sizeof(buf), "%ds", seconds);

    assert((n >= 0) && (n < sizeof(buf)));
    return(create_string(buf));
}


struct tm * get_localtime(time_t *t, struct tm *tm)
{
#ifndef HAVE_LOCALTIME_R
    int rc;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    struct tm *ptm;
#endif /* !HAVE_LOCALTIME_R */

    assert(t);
    assert(tm);

    if (*t == 0) {
        if (time(t) == ((time_t) -1))
            err_msg(errno, "time() failed -- What time is it?");
    }

#ifndef HAVE_LOCALTIME_R

    /*  localtime() is not thread-safe, so it is protected by a mutex.
     */
    if ((rc = pthread_mutex_lock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed");
    if (!(ptm = localtime(t)))
        err_msg(errno, "localtime() failed");
    *tm = *ptm;
    if ((rc = pthread_mutex_unlock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed");

#else /* HAVE_LOCALTIME_R */

    if (!localtime_r(t, tm))
        err_msg(errno, "localtime_r() failed");

#endif /* !HAVE_LOCALTIME_R */

    return(tm);
}


void set_descriptor_nonblocking(int fd)
{
    int fval;

    if ((fval = fcntl(fd, F_GETFL, 0)) < 0)
        err_msg(errno, "fcntl(F_GETFL) failed");
    if (fcntl(fd, F_SETFL, fval | O_NONBLOCK) < 0)
        err_msg(errno, "fcntl(F_SETFL) failed");
    return;
}


ssize_t read_n(int fd, void *buf, size_t n)
{
    size_t nleft;
    ssize_t nread;
    unsigned char *p;

    p = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, p, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return(-1);
        }
        else if (nread == 0) {		/* EOF */
            break;
        }
        nleft -= nread;
        p += nread;
    }
    return(n - nleft);
}


ssize_t write_n(int fd, void *buf, size_t n)
{
    size_t nleft;
    ssize_t nwritten;
    unsigned char *p;

    p = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, p, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return(-1);
        }
        nleft -= nwritten;
        p += nwritten;
    }
    return(n);
}


ssize_t read_line(int fd, void *buf, size_t maxlen)
{
    ssize_t n, rc;
    unsigned char c, *p;

    n = 0;
    p = buf;
    while (n < maxlen - 1) {		/* reserve space for NUL-termination */

        if ((rc = read(fd, &c, 1)) == 1) {
            n++;
            *p++ = c;
            if (c == '\n')
                break;			/* store newline, like fgets() */
        }
        else if (rc == 0) {
            if (n == 0)			/* EOF, no data read */
                return(0);
            else			/* EOF, some data read */
                break;
        }
        else {
            if (errno == EINTR)
                continue;
            return(-1);
        }
    }

    *p = '\0';				/* NUL-terminate, like fgets() */
    return(n);
}


char * get_hostname_by_addr(void *addr, char *buf, int len)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    int rc;
    struct hostent *hostp;

    assert(addr);
    assert(buf);
    assert(len > 0);
    if ((rc = pthread_mutex_lock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed");

    /*  gethostbyaddr() is not thread-safe, so it is protected by a mutex.
     *  This assumes no other concurrent thread will be calling gethostbyaddr().
     */
    hostp = gethostbyaddr((const char *) addr, sizeof(struct in_addr), AF_INET);
    if (hostp != NULL)
        strlcpy(buf, hostp->h_name, len);

    if ((rc = pthread_mutex_unlock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed");

    return(hostp != NULL ? buf : NULL);
}


SigFunc * Signal(int signum, SigFunc *f)
{
/*  A wrapper for the historical signal() function to do things the Posix way.
 *  cf. Stevens UNPv1 figure 5.6.
 */
    struct sigaction act0, act1;

    act1.sa_handler = f;
    sigemptyset(&act1.sa_mask);
    act1.sa_flags = 0;
    if (signum == SIGALRM) {
#ifdef SA_INTERRUPT
        act1.sa_flags |= SA_INTERRUPT;	/* SunOS 4.x */
#endif /* SA_INTERRUPT */
    }
    else {
#ifdef SA_RESTART
        act1.sa_flags |= SA_RESTART;	/* SVR4, 4.4BSD */
#endif /* SA_RESTART */
    }

    /*  Technically, this routine should return SIG_ERR if sigaction()
     *    fails here.  But the caller would just err_msg() away, anways.
     */
    if (sigaction(signum, &act1, &act0) < 0)
        err_msg(errno, "signal(%d) failed", signum);
    return(act0.sa_handler);
}


#ifndef HAVE_INET_PTON
int inet_pton(int family, const char *str, void *addr)
{
/*  cf. Stevens UNPv1 p72.
 */
    struct in_addr tmpaddr;

    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return(-1);
    }
    if (!inet_aton(str, &tmpaddr))
        return(0);

    memcpy(addr, &tmpaddr, sizeof(struct in_addr));
    return(1);
}
#endif /* !HAVE_INET_PTON */


#ifndef HAVE_INET_NTOP
const char * inet_ntop(int family, const void *addr, char *str, size_t len)
{
/*  cf. Stevens UNPv1 p72.
 */
    const unsigned char *p = (const unsigned char *) addr;
    char tmpstr[INET_ADDRSTRLEN];

    assert(str);

    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return(NULL);
    }
    snprintf(tmpstr, sizeof(tmpstr), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
    if (strlen(tmpstr) >= len) {
        errno = ENOSPC;
        return(NULL);
    }
    strcpy(str, tmpstr);
    return(str);
}
#endif /* !HAVE_INET_NTOP */
