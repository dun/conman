/******************************************************************************\
 *  $Id: errors.h,v 1.3 2001/05/24 20:56:08 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _ERRORS_H
#define _ERRORS_H


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>


/*  DPRINTF(fmt, args...)
 *    A debugging-printf that prints the format string.
 *
 *  LDPRINTF(fmt, args...)
 *    Another debugging-printf that prints the format string
 *      preceded with the file name and line number.
 */
#ifndef NDEBUG
#define DPRINTF(fmt, args...) \
  do { fprintf(stderr, fmt, ##args); } while (0)
#define LDPRINTF(fmt, args...) \
  do { fprintf(stderr, "%s:%d: " fmt, __FILE__, __LINE__, ##args); } while (0)
#else /* NDEBUG */
#define DPRINTF(fmt, args...)
#define LDPRINTF(fmt, args...)
#endif /* NDEBUG */


int open_msg_log(char *filename);
/*
 *  DOCUMENT_ME
 */

void close_msg_log(void);
/*
 *  DOCUMENT_ME
 */

#define log_msg(level, fmt, args...) \
  do { fprintf(stderr, fmt, ##args); fprintf(stderr, "\n"); } while (0)
/*
 *  FIX_ME: Remove kludge macro once function is implemented.
 *
void log_msg(int priority, const char *fmt, ...);
 *
 *  DOCUMENT_ME
 */

void err_msg(int errnum, const char *fmt, ...);
/*
 *  Display fatal error message and exit.
 *  If the error is related to a failing system call,
 *    'errnum' specifies the non-zero return code (eg, errno).
 */


#endif /* !_ERRORS_H */
