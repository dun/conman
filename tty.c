/*****************************************************************************\
 *  $Id: tty.c,v 1.1 2002/09/27 03:23:19 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include "log.h"


void tty_get_mode(int fd, struct termios *tty)
{
/*  Gets the tty values associated with 'fd' and stores them in 'tty'.
 */
    assert(fd >= 0);
    assert(tty != NULL);

    if (!isatty(fd))
        return;
    if (tcgetattr(fd, tty) < 0)
        log_err(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


void tty_set_mode(int fd, struct termios *tty)
{
/*  Sets the tty values associated with 'fd' to those stored in 'tty'.
 */
    assert(fd >= 0);
    assert(tty != NULL);

    if (!isatty(fd))
        return;
    if (tcsetattr(fd, TCSAFLUSH, tty) < 0)
        log_err(errno, "tcgetattr() failed on fd=%d", fd);
    return;
}


void tty_get_mode_raw(int fd, struct termios *tty)
{
/*  Gets the tty values associated with 'fd' and stores them in 'tty',
 *    adjusting them to reflect the device is operating in "raw" mode.
 *  Note that the 'fd' device is not placed in raw mode by this call;
 *    to do so, invoke set_tty_mode() with the updated termios struct.
 */
    assert(tty != NULL);

    tty_get_mode(fd, tty);

    tty->c_iflag = 0;
    tty->c_oflag = 0;

    /*  Set 8 bits/char.
     */
    tty->c_cflag &= ~CSIZE;
    tty->c_cflag |= CS8;

    /*  Disable parity checking.
     */
    tty->c_cflag &= ~PARENB;

    /*  Ignore modem status lines for locally attached device.
     */
    tty->c_cflag |= CLOCAL;
  
    /*  Disable echo, canonical mode, extended input processing, signal chars.
     */
    tty->c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /*  read() does not return until data is present (may block indefinitely).
     */
    tty->c_cc[VMIN] = 1;
    tty->c_cc[VTIME] = 0;
    return;
}
