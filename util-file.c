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
 *****************************************************************************
 *  Refer to "util-file.h" for documentation on public functions.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "log.h"
#include "util-file.h"
#include "util-str.h"


static int get_file_lock(int fd, int cmd, int type);
static pid_t test_file_lock(int fd, int type);


void set_fd_closed_on_exec(int fd)
{
    assert(fd >= 0);

    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        log_err(errno, "fcntl(F_SETFD) failed");
    return;
}


void set_fd_blocking(int fd)
{
    int fval;

    assert(fd >= 0);

    if ((fval = fcntl(fd, F_GETFL, 0)) < 0)
        log_err(errno, "fcntl(F_GETFL) failed");
    if (fcntl(fd, F_SETFL, fval & (~O_NONBLOCK)) < 0)
        log_err(errno, "fcntl(F_SETFL) failed");
    return;
}


void set_fd_nonblocking(int fd)
{
    int fval;

    assert(fd >= 0);

    if ((fval = fcntl(fd, F_GETFL, 0)) < 0)
        log_err(errno, "fcntl(F_GETFL) failed");
    if (fcntl(fd, F_SETFL, fval | O_NONBLOCK) < 0)
        log_err(errno, "fcntl(F_SETFL) failed");
    return;
}


int get_read_lock(int fd)
{
    return(get_file_lock(fd, F_SETLK, F_RDLCK));
}


int get_readw_lock(int fd)
{
    return(get_file_lock(fd, F_SETLKW, F_RDLCK));
}


int get_write_lock(int fd)
{
    return(get_file_lock(fd, F_SETLK, F_WRLCK));
}


int get_writew_lock(int fd)
{
    return(get_file_lock(fd, F_SETLKW, F_WRLCK));
}


int release_lock(int fd)
{
    return(get_file_lock(fd, F_SETLK, F_UNLCK));
}


pid_t is_read_lock_blocked(int fd)
{
    return(test_file_lock(fd, F_RDLCK));
}


pid_t is_write_lock_blocked(int fd)
{
    return(test_file_lock(fd, F_WRLCK));
}


static int get_file_lock(int fd, int cmd, int type)
{
    struct flock lock;

    assert(fd >= 0);

    lock.l_type = type;
    lock.l_start = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len = 0;

    return(fcntl(fd, cmd, &lock));
}


static pid_t test_file_lock(int fd, int type)
{
    struct flock lock;

    assert(fd >= 0);

    lock.l_type = type;
    lock.l_start = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len = 0;

    if (fcntl(fd, F_GETLK, &lock) < 0)
        log_err(errno, "Unable to test for file lock");
    if (lock.l_type == F_UNLCK)
        return(0);
    return(lock.l_pid);
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
        else if (nread == 0) {          /* EOF */
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
    size_t n;
    ssize_t rv;
    unsigned char c, *p;

    if (buf == NULL) {
        errno = EINVAL;
        return(-1);
    }
    if (maxlen == 0) {
        return(0);
    }
    maxlen--;                           /* reserve space for NUL-termination */
    n = 0;
    p = buf;
    while (n < maxlen) {
        rv = read(fd, &c, sizeof(c));
        if (rv == 1) {
            n++;
            *p++ = c;
            if (c == '\n')
                break;                  /* store newline, like fgets() */
        }
        else if (rv == 0) {
            if (n == 0)                 /* EOF, no data read */
                return(0);
            else                        /* EOF, some data read */
                break;
        }
        else {
            if (errno == EINTR)
                continue;
            return(-1);
        }
    }

    *p = '\0';                          /* NUL-terminate, like fgets() */
    return((ssize_t) n);
}


char *
get_dir_name (const char *srcpath, char *dstdir, size_t dstdirlen)
{
    const char *p;
    size_t      len;

    if ((srcpath == NULL) || (dstdir == NULL)) {
        errno = EINVAL;
        return (NULL);
    }
    p = srcpath + strlen (srcpath) - 1;

    /*  Ignore trailing slashes except for the root slash.
     */
    while ((p > srcpath) && (*p == '/')) {
        p--;
    }
    /*  Skip over last path component.
     */
    while ((p >= srcpath) && (*p != '/')) {
        p--;
    }
    /*  Skip over adjacent slashes except for the root slash.
     */
    while ((p > srcpath) && (*p == '/')) {
        p--;
    }
    /*  A path not containing a slash shall return the dot directory.
     */
    if (p < srcpath) {
        if (dstdirlen < 2) {
            errno = ENAMETOOLONG;
            return (NULL);
        }
        dstdir [0] = '.';
        dstdir [1] = '\0';
    }
    /*  Otherwise, copy the directory string into dstdir.
     */
    else {
        /*  'p' now points at the last char to copy, so +1 to include that
         *    last char, then add the terminating null char afterwards.
         */
        len = p - srcpath + 1;
        if (len >= dstdirlen) {
            errno = ENAMETOOLONG;
            return (NULL);
        }
        (void) strncpy (dstdir, srcpath, len);
        dstdir [len] = '\0';
    }
    return (dstdir);
}


int
create_dirs (const char *dir_name)
{
    struct stat  st_buf;
    char         dir_buf [PATH_MAX];
    char        *p;
    char        *slash_ptr;
    mode_t       dir_mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

    if ((dir_name == NULL) || (*dir_name == '\0')) {
        errno = EINVAL;
        log_msg (LOG_WARNING, "No directory specified for creation");
        return (-1);
    }
    /*  Check if the directory already exists.
     */
    if (stat (dir_name, &st_buf) == 0) {
        if (S_ISDIR (st_buf.st_mode)) {
            return (0);
        }
        errno = EEXIST;
        log_msg (LOG_WARNING, "Cannot create directory \"%s\": %s",
                dir_name, strerror (errno));
        return (-1);
    }
    /*  Create copy of [dir_name] for modification.
     */
    if (strlcpy (dir_buf, dir_name, sizeof (dir_buf)) >= sizeof (dir_buf)) {
        errno = ENAMETOOLONG;
        log_msg (LOG_WARNING, "Exceeded maximum directory length of %d bytes",
                sizeof (dir_buf) - 1);
        return (-1);
    }
    /*  Remove trailing slashes from the directory name
     *    (while ensuring the root slash is not removed in the process).
     */
    p = dir_buf + strlen (dir_buf) - 1;
    while ((p > dir_buf) && (*p == '/')) {
        *p-- = '\0';
    }
    /*  The slash_ptr points to the leftmost unprocessed directory component.
     */
    slash_ptr = dir_buf;

    /*  Process directory components.
     */
    while (1) {

        /* Skip over adjacent slashes (omitting unnecessary calls to mkdir).
         */
        while (*slash_ptr == '/') {
            slash_ptr++;
        }
        /*  Advance slash_ptr to the next directory component.
         */
        slash_ptr = strchr (slash_ptr, '/');
        if (slash_ptr != NULL) {
            *slash_ptr = '\0';
        }
        /*  Create directory.
         */
        if (mkdir (dir_buf, dir_mode) < 0) {

            int mkdir_errno = errno;

            if ((mkdir_errno != EEXIST)
                    || (stat (dir_buf, &st_buf) < 0)
                    || (! S_ISDIR (st_buf.st_mode))) {
                log_msg (LOG_WARNING, "Cannot create directory \"%s\": %s",
                        dir_buf, strerror (mkdir_errno));
                return (-1);
            }
        }
        if (slash_ptr == NULL) {
            break;
        }
        *slash_ptr++ = '/';
    }
    return (0);
}
