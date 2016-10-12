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


#ifndef _LEX_H
#define _LEX_H


/*****************************************************************************\
 *  Laws of the Lexer:
 *----------------------------------------------------------------------------
 *  - Whitespace is ignored.
 *  - Comments are ignored (from the pound char to the newline).
 *  - Lines may be terminated by either carriage-returns (CR),
 *    linefeeds (LF), or carriage-return/linefeed (CR/LF) pairs.
 *  - A newline may be escaped by immediately preceding it with a backslash.
 *  - Integers may begin with either a plus or minus, and contain only digits.
 *  - Strings may be single-quoted or double-quoted.
 *  - Strings cannot contain CRs or LFs.
 *  - Unquoted strings are sequences of letters, digits, and underscores;
 *    they may not begin with a digit (just like a C identifier).
 *  - Tokens are unquoted case-insensitive strings.
\*****************************************************************************/


/***********\
**  Notes  **
\***********/

/*  When a memory allocation request fails, the lexer returns out_of_memory().
 *  By default, this is a macro definition that returns NULL; this macro may
 *  be redefined to invoke another routine instead.  Furthermore, if WITH_OOMF
 *  is defined, this macro will not be defined and the lexer will expect an
 *  external Out-Of-Memory Function to be defined.
 */


/***************\
**  Constants  **
\***************/

#define LEX_MAX_STR 1024                /* max length of lexer string        */

enum common_tokens {
    LEX_ERR = -1,                       /* lex error token                   */
    LEX_EOF = 0,                        /* end-of-file/buffer token          */
    LEX_EOL = 256,                      /* end-of-line token                 */
    LEX_INT,                            /* integer token: ([+-]?[0-9]+)      */
    LEX_STR,                            /* string token                      */
    LEX_TOK_OFFSET                      /* enum value at which toks[] begin  */
};


/****************\
**  Data Types  **
\****************/

typedef struct lexer_state *Lex;
/*
 *  Lex opaque data type.
 */


/************\
**  Macros  **
\************/

#define LEX_TOK2STR(tokstrs,tok) ((tokstrs)[(tok) - LEX_TOK_OFFSET])
/*
 *  Returns a string in the (tokstrs) array corresponding to the token (tok).
 *  Only use when (tok) is known to be a valid array index corresponding to a
 *    string in the (tokstrs) array of strings since no bounds-checking is
 *    performed.
 */


/**********************\
**  Lexing Functions  **
\**********************/

Lex lex_create(void *buf, char *toks[]);
/*
 *  Creates and returns a new lexer, or out_of_memory() on failure.
 *  The text to be lexed is specified by the NUL-terminated buffer (buf);
 *    this buffer WILL NOT be modified by the lexer.
 *  The NULL-terminated array of strings (toks) defines the set of tokens
 *    that will be recognized by the lexer; these strings must be listed
 *    in a case-insensitive ascending order (ie, according to strcasecmp).
 *  Note: Abadoning a lexer without calling lex_destroy() will result
 *    in a memory leak.
 */

void lex_destroy(Lex l);
/*
 *  Destroys lexer (l), freeing memory used for the lexer itself.
 */

int lex_next(Lex l);
/*
 *  Returns the next token in the buffer given to lex_create()
 *    according to the Laws of the Lexer.
 *  Single-character tokens (eg, punctuation) are specified by
 *    their ASCII code.  Common tokens are specified by the
 *    common_token enumeration.  Tokens specified by the (toks)
 *    array of strings begin at LEX_TOK_OFFSET.
 */

int lex_prev(Lex l);
/*
 *  Returns the last token returned by lex_next().
 */

int lex_line(Lex l);
/*
 *  Returns the line number of the last token returned by lex_next().
 */

const char * lex_text(Lex l);
/*
 *  Returns the string corresponding to the last token returned by lex_next().
 */

const char * lex_tok_to_str(Lex l, int tok);
/*
 *  Returns the string from the lex_create() toks[] array corresponding to the
 *    token (tok), or NULL if tok is outside of the toks[] array bounds.
 */


/*************************\
**  Auxiliary Functions  **
\*************************/

char * lex_encode(char *str);
/*
 *  Encodes the string (str) so that it may safely be used by the lexer.
 *    This is needed if the string may contain quote characters.
 *    The string cannot be a constant as it will be modified in place.
 *  Returns the encoded string.
 */

char * lex_decode(char *str);
/*
 *  Decodes the string (str) that has been encoded with lex_encode().
 *    The string cannot be a constant as it will be modified in place.
 *  Returns the decoded string.
 */


/********************\
**  Test Functions  **
\********************/

void lex_parse_test(char *buf, char *toks[]);
/*
 *  Example code that tokenizes the buffer (buf) based upon the
 *    NULL-terminated array of strings (toks) that defines the
 *    set of recognized tokens.
 */


#endif /* !_LEX_H */
