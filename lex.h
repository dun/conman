/******************************************************************************\
 *  lex.h
 *    by Chris Dunlap <cdunlap@llnl.gov>
 *
 *  $Id: lex.h,v 1.3 2001/05/14 15:30:28 dun Exp $
\******************************************************************************/


#ifndef _LEX_H
#define _LEX_H


/******************************************************************************\
 *  Laws of the Lexer:
 *-----------------------------------------------------------------------------
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
\******************************************************************************/


/***************\
**  Constants  **
\***************/

#define LEX_MAX_STR 1024		/* max length of lexer string         */

enum common_tokens {
    LEX_ERR = -1,			/* lex error token                    */
    LEX_EOF = 0,			/* end-of-file/buffer token           */
    LEX_EOL = 256,			/* end-of-line token                  */
    LEX_INT,				/* integer token: ([+-]?[0-9]+)       */
    LEX_STR,				/* string token                       */
    LEX_TOK_OFFSET			/* (cf. LEX_UNTOK macro)              */
};


/************\
**  Macros  **
\************/

#define LEX_UNTOK(tok) \
    ( ((tok) < LEX_TOK_OFFSET) ? (tok) : ((tok) - LEX_TOK_OFFSET) )
/*
 *  LEX_TOK_OFFSET specifies the next available enumeration at which
 *    the array of strings supplied to lex_create (toks) can begin.
 *  LEX_UNTOK(tok) undoes this offset adjustment and returns the
 *    offset corresponding to this token within the (toks) array.
 */


/****************\
**  Data Types  **
\****************/

typedef struct lexer_state *Lex;
/*
 *  Lex opaque data type.
 */


/**********************\
**  Lexing Functions  **
\**********************/

Lex lex_create(void *buf, char *toks[]);
/*
 *  Creates and returns a new lexer, or NULL if creation fails.
 *  The text to be lexed is specified by the NUL-terminated buffer (buf);
 *    this buffer WILL NOT be modified by the lexer.
 *  The NULL-terminated array of strings (toks) defines the set of tokens
 *    that will be recognized by the lexer.
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
 *    array of strings begin at LEX_TOK_OFFSET.  (cf. LEX_UNTOK macro).
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
