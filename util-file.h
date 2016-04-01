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


#ifndef _UTIL_FILE_H
#define _UTIL_FILE_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>
#include <unistd.h>


void set_fd_closed_on_exec(int fd);
/*
 *  Sets the file descriptor (fd) to be closed on exec().
 */

void set_fd_blocking(int fd);
/*
 *  Sets the file descriptor (fd) for blocking I/O.
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

char * get_dir_name (const char *srcpath, char *dstdir, size_t dstdirlen);
/*
 *  Copies the parent directory name of (srcpath) into the buffer (dstdir)
 *    of length (dstdirlen).
 *  If (srcpath) does not contain a slash, then (dstdir) shall contain the
 *    dot directory (ie, the string ".").
 *  Returns a pointer to (dstdir) on success (guaranteed to be nul terminated),
 *    or NULL on error (with errno set).
 */

int create_dirs (const char *dir_name);
/*
 *  Creates the directory (dir_name) and any parent directories that do not
 *    already exist.  Directories are created with permissions 0755 modified
 *    by the umask.
 *  Returns 0 on success, or -1 on error (logging a warning message).
 */


#endif /* !_UTIL_FILE_H */
