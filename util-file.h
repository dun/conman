/*****************************************************************************\
 *  $Id: util-file.h,v 1.4 2002/02/08 18:12:25 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\*****************************************************************************/


#ifndef _UTIL_FILE_H
#define _UTIL_FILE_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <unistd.h>


void set_fd_closed_on_exec(int fd);
/*
 *  Sets the file descriptor (fd) to be closed on exec().
 */

void set_fd_nonblocking(int fd);
/*
 *  Sets the file descriptor (fd) for non-blocking I/O.
 */

int get_read_lock(int fd);
/*
 *  Obtain a read lock on the file specified by (fd).
 *  Returns 0 on success, or -1 if prevented from obtaining the lock.
 */

int get_readw_lock(int fd);
/*
 *  Obtain a read lock on the file specified by (fd),
 *    blocking until one becomes available.
 *  Returns 0 on success, or -1 on error.
 */

int get_write_lock(int fd);
/*
 *  Obtain a write lock on the file specified by (fd).
 *  Returns 0 on success, or -1 if prevented from obtaining the lock.
 */

int get_writew_lock(int fd);
/*
 *  Obtain a write lock on the file specified by (fd),
 *    blocking until one becomes available.
 *  Returns 0 on success, or -1 on error.
 */

int release_lock(int fd);
/*
 *  Release a lock held on the file specified by (fd).
 *  Returns 0 on success, or -1 on error.
 */

pid_t is_read_lock_blocked(int fd);
/*
 *  If a lock exists the would block a request for a read-lock
 *    (ie, if a write-lock is already being held on the file),
 *    returns the pid of the process holding the lock; o/w, returns 0.
 */

pid_t is_write_lock_blocked(int fd);
/*
 *  If a lock exists the would block a request for a write-lock
 *    (ie, if any lock is already being held on the file),
 *    returns the pid of a process holding the lock; o/w, returns 0.
 */

ssize_t read_n(int fd, void *buf, size_t n);
/*
 *  Reads up to (n) bytes from (fd) into (buf).
 *  Returns the number of bytes read, 0 on EOF, or -1 on error.
 */

ssize_t write_n(int fd, void *buf, size_t n);
/*
 *  Writes (n) bytes from (buf) to (fd).
 *  Returns the number of bytes written, or -1 on error.
 */

ssize_t read_line(int fd, void *buf, size_t maxlen);
/*
 *  Reads at most (maxlen-1) bytes up to a newline from (fd) into (buf).
 *  The (buf) is guaranteed to be NUL-terminated and will contain the
 *    newline if it is encountered within (maxlen-1) bytes.
 *  Returns the number of bytes read, 0 on EOF, or -1 on error.
 */


#endif /* !_UTIL_FILE_H */
