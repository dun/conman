/******************************************************************************\
 *  $Id: lex.c,v 1.9 2001/09/23 00:46:06 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "lex.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
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

#ifdef USE_OOMF
#  undef out_of_memory
   extern void * out_of_memory(void);
#else /* !USE_OOMF */
#  ifndef out_of_memory
#    define out_of_memory() (NULL)
#  endif /* !out_of_memory */
#endif /* USE_OOMF */


/***************\
**  Constants  **
\***************/

#define LEX_MAGIC 0xCD


/****************\
**  Data Types  **
\****************/

struct lexer_state {
    char           *pos;		/* current ptr in buffer              */
    char          **toks;		/* array of recognized strings        */
    char            text[LEX_MAX_STR];	/* tmp buffer for lexed strings       */
    int	            prev;		/* prev token returned by lex_next()  */
    int	            line;		/* current line number in buffer      */
    int	            gotEOL;		/* true if next token is on new line  */
    unsigned char   magic;		/* sentinel for asserting validity    */
};


/****************\
**  Prototypes  **
\****************/

static int lookup_token(char *str, char *toks[]);


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

    assert(buf);

    if (!(l = (Lex) malloc(sizeof(struct lexer_state))))
        return(out_of_memory());
    l->pos = buf;
    l->toks = toks;
    l->text[0] = '\0';
    l->prev = 0;
    l->line = 0;
    l->gotEOL = 1;
    l->magic = LEX_MAGIC;
    return(l);
}


void lex_destroy(Lex l)
{
    assert(l);
    assert(l->magic == LEX_MAGIC);

    l->magic = 0;
    free(l);
    return;
}


int lex_next(Lex l)
{
    char *p;
    int len;

    assert(l);
    assert(l->magic == LEX_MAGIC);

    if (l->gotEOL) {			/* deferred line count increment */
        l->line++;
        l->gotEOL = 0;
    }

    for (;;) {
        switch (*l->pos) {
        case '\0':			/* EOF */
            l->text[0] = '\0';
            return(l->prev = LEX_EOF);
            break;
        case ' ':			/* ignore whitespace */
        case '\t':
        case '\v':
        case '\f':
            l->pos++;
            break;
        case '#':			/* ignore comments */
            do {
                l->pos++;
            } while (*l->pos && (*l->pos != '\n') && (*l->pos != '\r'));
            break;
        case '\r':			/* EOL: CR, LF, CR/LF */
            if (*(l->pos+1) == '\n')
                l->pos++; 
            /* fall-thru... whee! */
        case '\n':
            l->text[0] = *l->pos++;
            l->text[1] = '\0';
            l->gotEOL = 1;		/* do not back up; severe tire damage */
            return(l->prev = LEX_EOL);
        case '"':
        case '\'':
            for (p=l->pos+1; *p && *p!=*l->pos && *p!='\r' && *p!='\n'; p++) {;}
            if (*p == *l->pos) {	/* valid string */
                len = MIN(p - l->pos - 1, LEX_MAX_STR - 1);
                memcpy(l->text, l->pos + 1, len);
                l->text[len] = '\0';
                l->pos = p + 1;
                return(l->prev = LEX_STR);
            }
            else {			/* unmatched quote */
                l->text[0] = '\0';
                l->pos = p;
                return(l->prev = LEX_ERR);
            }
        case '\\':
            if (*(l->pos+1) == '\n') {	/* ignore EOL, continue to next line */
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
                for (p=l->pos+1; *p && (isalnum((int)*p) || *p == '_'); p++) {;}
                len = MIN(p - l->pos, LEX_MAX_STR - 1);
                memcpy(l->text, l->pos, len);
                l->text[len] = '\0';
                l->pos = p;
                return(l->prev = lookup_token(l->text, l->toks));
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
            l->text[0] = *l->pos++;	/* single-character token */
            l->text[1] = '\0';
            return(l->prev = l->text[0]);
        }
    }
}


int lex_prev(Lex l)
{
    assert(l);
    assert(l->magic == LEX_MAGIC);
    return(l->prev);
}


int lex_line(Lex l)
{
    assert(l);
    assert(l->magic == LEX_MAGIC);
    return(l->line);
}


const char * lex_text(Lex l)
{
    assert(l);
    assert(l->magic == LEX_MAGIC);
    return(l->text);
}


static int lookup_token(char *str, char *toks[])
{
/*  Internal function to determine whether the string (str) is in
 *    the NULL-terminated array of strings (toks).
 *  Returns the token corresponding to the matched string in the
 *    array (toks), or the generic string token if no match is found.
 */
    int i;
    char *p, *q;

    if (toks) {
        for (i=0; (q=toks[i]) != NULL; i++) {
            for (p=str; *p && toupper(*p) == toupper(*q); p++, q++) {;}
            if (!*p && !*q)		/* token found, whoohoo! */
                return(i + LEX_TOK_OFFSET);
        }
    }
    return(LEX_STR);			/* token not found; doh! */
}


char * lex_encode(char *str)
{
    char *p;

    if (!str)
        return(NULL);
    for (p=str; *p; p++) {
        assert(!(*p & 0x80));		/* assert all high bits are cleared */
        if (*p == '\'' || *p == '"')
            *p |= 0x80;			/* set high bit to encode funky char */
    }
    return(str);
}


char * lex_decode(char *str)
{
    char *p;

    if (!str)
        return(NULL);
    for (p=str; *p; p++) {
        *p &= 0x7F;			/* clear all high bits */
    }
    return(str);
}


void lex_parse_test(char *buf, char *toks[])
{
    Lex l;
    int tok;
    int newline = 1;

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
            else if (toks)
                printf("TOK(%d:%s) ", tok, toks[LEX_UNTOK(tok)]);
            else
                printf("\nINTERNAL ERROR: line=%d, tok=%d, str=\"%s\"\n",
                    lex_line(l), lex_prev(l), lex_text(l));
            break;
        }
    }
    lex_destroy(l);
    return;
}
