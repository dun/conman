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
 *  Refer to "lex.h" for documentation on public functions.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lex.h"


/*******************\
**  Out of Memory  **
\*******************/

#ifdef WITH_OOMF
#  undef out_of_memory
   extern void * out_of_memory(void);
#else /* !WITH_OOMF */
#  ifndef out_of_memory
#    define out_of_memory() (NULL)
#  endif /* !out_of_memory */
#endif /* WITH_OOMF */


/***************\
**  Constants  **
\***************/

#define LEX_MAGIC 0xDEADBEEF


/****************\
**  Data Types  **
\****************/

struct lexer_state {
    char          *pos;                 /* current ptr in buffer             */
    char         **toks;                /* array of recognized strings       */
    int            numtoks;             /* number of strings in toks[]       */
    char           text[LEX_MAX_STR];   /* tmp buffer for lexed strings      */
    int            prev;                /* prev token returned by lex_next() */
    int            line;                /* current line number in buffer     */
    int            gotEOL;              /* true if next token is on new line */
#ifndef NDEBUG
    unsigned int   magic;               /* sentinel for asserting validity   */
#endif /* NDEBUG */
};


/****************\
**  Prototypes  **
\****************/

#ifndef NDEBUG
static int validate_sorted_tokens(char *toks[]);
#endif /* !NDEBUG */
static int lookup_token(char *str, char *toks[], int numtoks);


/************\
**  Macros  **
\************/

#ifndef MIN
#  define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */


/***************\
**  Functions  **
\***************/

Lex lex_create(void *buf, char *toks[])
{
    Lex l;

    assert(buf != NULL);
    assert(validate_sorted_tokens(toks) >= 0);

    if (!(l = (Lex) malloc(sizeof(struct lexer_state)))) {
        return(out_of_memory());
    }
    l->pos = buf;
    l->toks = toks;
    if (!toks) {
        l->numtoks = 0;
    }
    else {
        int n;
        for (n=0; toks[n] != NULL; n++) {;}
        l->numtoks = n;
    }
    l->text[0] = '\0';
    l->prev = 0;
    l->line = 0;
    l->gotEOL = 1;
    assert(l->magic = LEX_MAGIC);       /* set magic via assert abuse */
    return(l);
}


void lex_destroy(Lex l)
{
    assert(l != NULL);
    assert(l->magic == LEX_MAGIC);

    assert(l->magic = 1);               /* clear magic via assert abuse */
    free(l);
    return;
}


int lex_next(Lex l)
{
    char *p;
    int len;

    assert(l != NULL);
    assert(l->magic == LEX_MAGIC);

    if (l->gotEOL) {                    /* deferred line count increment */
        l->line++;
        l->gotEOL = 0;
    }

    for (;;) {
        switch (*l->pos) {
        case '\0':                      /* EOF */
            l->text[0] = '\0';
            return(l->prev = LEX_EOF);
            break;
        case ' ':                       /* ignore whitespace */
        case '\t':
        case '\v':
        case '\f':
            l->pos++;
            break;
        case '#':                       /* ignore comments */
            do {
                l->pos++;
            } while (*l->pos && (*l->pos != '\n') && (*l->pos != '\r'));
            break;
        case '\r':                      /* EOL: CR, LF, CR/LF */
            if (*(l->pos+1) == '\n')
                l->pos++;
            /* fall-thru... whee! */
        case '\n':
            l->text[0] = *l->pos++;
            l->text[1] = '\0';
            l->gotEOL = 1;              /* do not back up;severe tire damage */
            return(l->prev = LEX_EOL);
        case '"':
        case '\'':
            for (p=l->pos+1; *p && *p!=*l->pos && *p!='\r' && *p!='\n'; p++){;}
            if (*p == *l->pos) {        /* valid string */
                len = MIN(p - l->pos - 1, LEX_MAX_STR - 1);
                memcpy(l->text, l->pos + 1, len);
                l->text[len] = '\0';
                l->pos = p + 1;
                return(l->prev = LEX_STR);
            }
            else {                      /* unmatched quote */
                l->text[0] = '\0';
                l->pos = p;
                return(l->prev = LEX_ERR);
            }
        case '\\':
            if (*(l->pos+1) == '\n') {  /* ignore EOL, continue to next line */
                l->pos += 2;
                l->line++;
                break;
            }
            else if ((*(l->pos+1) == '\r') && (*(l->pos+2) == '\n')) {
                l->pos += 3;
                l->line++;
                break;
            }
            /* fall-thru... whee! */
        default:
            if (isalpha((int)*l->pos) || (*l->pos == '_')) {
                for (p=l->pos+1; *p && (isalnum((int)*p) || *p=='_'); p++) {;}
                len = MIN(p - l->pos, LEX_MAX_STR - 1);
                memcpy(l->text, l->pos, len);
                l->text[len] = '\0';
                l->pos = p;
                return(l->prev = lookup_token(l->text, l->toks, l->numtoks));
            }
            else if (isdigit((int)*l->pos)
              || (((*l->pos == '-') || (*l->pos == '+'))
              && isdigit((int)*(l->pos+1)))) {
                /* integer: [-+]?[0-9]+ */
                for (p=l->pos+1; *p && isdigit((int)*p); p++) {;}
                len = MIN(p - l->pos, LEX_MAX_STR - 1);
                memcpy(l->text, l->pos, len);
                l->text[len] = '\0';
                l->pos = p;
                return(l->prev = LEX_INT);
            }
            l->text[0] = *l->pos++;     /* single-character token */
            l->text[1] = '\0';
            return(l->prev = l->text[0]);
        }
    }
}


int lex_prev(Lex l)
{
    assert(l != NULL);
    assert(l->magic == LEX_MAGIC);
    return(l->prev);
}


int lex_line(Lex l)
{
    assert(l != NULL);
    assert(l->magic == LEX_MAGIC);
    return(l->line);
}


const char * lex_text(Lex l)
{
    assert(l != NULL);
    assert(l->magic == LEX_MAGIC);
    return(l->text);
}


const char * lex_tok_to_str(Lex l, int tok)
{
    int i;

    assert(l != NULL);
    assert(l->magic == LEX_MAGIC);
    assert(l->toks != NULL);
    assert(l->toks[l->numtoks] == NULL);

    if (!l || !l->toks) {
        return(NULL);
    }
    i = tok - LEX_TOK_OFFSET;
    if ((i >= 0) && (i < l->numtoks)) {
        return((const char *) l->toks[i]);
    }
    return(NULL);
}


#if ! HAVE_STRCASECMP
static int xstrcasecmp(const char *s1, const char *s2)
{
/*  Compares the two strings (s1) and (s2), ignoring the case of the chars.
 */
    const char *p, *q;

    p = s1;
    q = s2;
    while (*p && toupper((int) *p) == toupper((int) *q))
        p++, q++;
    return(toupper((int) *p) - toupper((int) *q));
}
#else
#  define xstrcasecmp strcasecmp
#endif /* !HAVE_STRCASECMP */


#ifndef NDEBUG
static int validate_sorted_tokens(char *toks[])
{
/*  Determines whether the NULL-terminated array of strings (toks) is sorted.
 *  Returns 0 if the array is sorted; o/w, returns -1.
 */
    char **pp;
    char *p, *q;

    if (!toks) {
        return(-1);
    }
    if ((pp = toks) && *pp) {
        for (p=*pp++, q=*pp++; q; p=q, q=*pp++) {
            if (xstrcasecmp(p, q) > 0)
                return(-1);
        }
    }
    return(0);
}
#endif /* !NDEBUG */


static int lookup_token(char *str, char *toks[], int numtoks)
{
/*  Determines if and where the string (str) is in the NULL-terminated array
 *    of (numtoks) sorted strings (toks).
 *  Returns the token corresponding to the matched string in the array (toks),
 *    or the generic string token if no match is found.
 */
    int low, middle, high;
    int x;

    if (toks) {
        low = 0;
        high = numtoks - 1;
        while (low <= high) {
            middle = (low + high) / 2;
            x = xstrcasecmp(str, toks[middle]);
            if (x < 0)
                high = middle - 1;
            else if (x > 0)
                low = middle + 1;
            else                        /* token found, whoohoo! */
                return(middle + LEX_TOK_OFFSET);
        }
    }
    return(LEX_STR);                    /* token not found; doh! */
}


char * lex_encode(char *str)
{
    char *p;

    if (!str)
        return(NULL);
    for (p=str; *p; p++) {
        assert(!(*p & 0x80));           /* assert all high bits are cleared */
        if (*p == '\'' || *p == '"')
            *p |= 0x80;                 /* set high bit to encode funky char */
    }
    return(str);
}


char * lex_decode(char *str)
{
    char *p;

    if (!str)
        return(NULL);
    for (p=str; *p; p++) {
        *p &= 0x7F;                     /* clear all high bits */
    }
    return(str);
}


void lex_parse_test(char *buf, char *toks[])
{
    Lex l;
    int tok;
    int newline = 1;
    const char *p;

    if (!buf || !(l = lex_create(buf, toks)))
        return;

    while ((tok = lex_next(l)) != LEX_EOF) {
        assert(lex_prev(l) == tok);
        if (newline) {
            printf("%3d: ", lex_line(l));
            newline = 0;
        }
        switch(tok) {
        case LEX_ERR:
            printf("ERR\n");
            newline = 1;
            break;
        case LEX_EOL:
            printf("EOL\n");
            newline = 1;
            break;
        case LEX_INT:
            printf("INT(%d) ", atoi(lex_text(l)));
            break;
        case LEX_STR:
            printf("STR(%s) ", lex_text(l));
            break;
        default:
            if (tok < LEX_TOK_OFFSET)
                printf("CHR(%c) ", lex_text(l)[0]);
            else if ((p = lex_tok_to_str(l, tok)))
                printf("TOK(%d:%s) ", tok, p);
            else
                printf("\nINTERNAL ERROR: line=%d, tok=%d, str=\"%s\"\n",
                    lex_line(l), lex_prev(l), lex_text(l));
            break;
        }
    }
    lex_destroy(l);
    return;
}
