/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *
 *  Copyright (C) 2001-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "conf.h"
#include "log.h"


/*****************************************************************************
 *  Internal Function Prototypes
 *****************************************************************************/

static void _display_help (char *arg0);

static void _display_license (void);

static void _display_version (void);


/*****************************************************************************
 *  Command-Line Options
 *****************************************************************************/

struct option opt_table[] = {
    { "help",    0, NULL, 'h' },
    { "license", 0, NULL, 'L' },
    { "version", 0, NULL, 'V' },
    {  NULL,     0, NULL,  0  }
};

const char * const opt_string = ":hLV";


/*****************************************************************************
 *  Global Variables
 *****************************************************************************/

conf_t conf = NULL;


/*****************************************************************************
 *  External Functions
 *****************************************************************************/

conf_t
create_conf (void)
{
/*  Constructor for the configuration.
 *  Returns a new configuration, or NULL on error.
 */
    conf_t conf;

    if (!(conf = malloc (sizeof (struct conf)))) {
        log_msg (LOG_ERR, "cannot allocate memory for configuration");
    }
    return (conf);
}


void
destroy_conf (conf_t conf)
{
/*  Destructor for the configuration.
 */
    if (!conf) {
        return;
    }
    free (conf);
    return;
}


void
parse_cmdline (conf_t conf, int argc, char **argv)
{
/*  Parse the command-line.
 *  Return an updated configuration [conf].
 */
    int c;

    opterr = 0;                         /* suppress default getopt err msgs */

    assert (conf != NULL);
    assert (argc > 0);
    assert (argv != NULL);

    for (;;) {

        c = getopt_long (argc, argv, opt_string, opt_table, NULL);

        if (c == -1) {
            break;
        }
        switch (c) {
            case 'h':
                _display_help (argv[0]);
                exit (EXIT_SUCCESS);
                break;
            case 'L':
                _display_license ();
                exit (EXIT_SUCCESS);
                break;
            case 'V':
                _display_version ();
                exit (EXIT_SUCCESS);
                break;
            case '?':
                if (optopt > 0) {
                    log_msg (LOG_ERR, "option \"-%c\" is invalid\n", optopt);
                }
                else {
                    log_msg (LOG_ERR, "option \"%s\" is invalid\n",
                        argv[optind - 1]);
                }
                exit (EXIT_FAILURE);
                break;
            case ':':
                if (optopt > 0) {
                    log_msg (LOG_ERR,
                        "option \"-%c\" is missing an argument\n", optopt);
                }
                else {
                    log_msg (LOG_ERR, "option \"%s\" is missing an argument\n",
                        argv[optind - 1]);
                }
                exit (EXIT_FAILURE);
                break;
            default:
                log_msg (LOG_ERR, "option \"-%c\" is not implemented\n", c);
                exit (EXIT_FAILURE);
                break;
        }
    }
}


/*****************************************************************************
 *  Internal Functions
 *****************************************************************************/

static void
_display_help (char *arg0)
{
/*  Display usage information.
 */
    char *progname;
    const int w = -24;                  /* pad for width of option string */

    assert (arg0 != NULL);
    progname = (progname = strrchr (arg0, '/')) ? progname + 1 : arg0;

    printf ("Usage: %s [OPTIONS]\n", progname);
    printf ("\n");

    printf ("  %*s %s\n", w, "-h, --help",
            "Display this help");

    printf ("  %*s %s\n", w, "-L, --license",
            "Display license information");

    printf ("  %*s %s\n", w, "-V, --version",
            "Display version information");

    printf ("\n");
    return;
}


static void
_display_license (void)
{
/*  Display license information.
 */
    static const char *license =                                              \
      "ConMan: The Console Manager\n"                                         \
      "Copyright (C) 2001-2006 The Regents of the University of California\n" \
      "Produced at Lawrence Livermore National Laboratory\n"                  \
      "Written by Chris Dunlap <cdunlap@llnl.gov>\n"                          \
      "http://www.llnl.gov/linux/conman/\n"                                   \
      "UCRL-CODE-2002-009\n"                                                  \
      "\n"                                                                    \
      "ConMan is free software; you can redistribute it and/or modify it\n"   \
      "under the terms of the GNU General Public License as published by\n"   \
      "the Free Software Foundation; either version 2 of the License, or\n"   \
      "(at your option) any later version.\n"                                 \
      "\n";

    printf ("%s", license);
    return;
}


static void
_display_version (void)
{
/*  Display version information.
 */
    printf ("conmand %s", META_VERSION);
#ifdef META_RELEASE
    printf ("-%s", META_RELEASE);
#endif /* META_RELEASE */
#ifdef META_DATE
    printf (" (%s)", META_DATE);
#endif /* META_DATE */
    printf ("\n");
    return;
}
