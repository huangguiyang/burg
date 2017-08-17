%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include "burg.h"
static int yylex(void);
%}

%union {
    int ival;
    char *sval;
    struct pattern *pval;
}
%token TERM PERCENT START
%token <sval> ID
%token <ival> NUMBER
%token <sval> TEMPLATE
%token <sval> COST
%type <pval> pattern
%type <sval> nonterm
%type <sval> cost

%%
start    : decls PERCENT rules
         | decls
         ;

decls    : /* empty */
         | decls decl
         ;

decl     : TERM idlist '\n'
         | START nonterm '\n'     { if (nonterm($2)->number != 1)
                                        yyerror("redeclaration of the start symbol"); }
         | '\n'
         | error '\n'             { yyerrok; }
         ;

idlist   : /* empty */
         | idlist ID '=' NUMBER   { term($2, $4); }
         ;

rules    : /* empty */
         | rules nonterm ':' pattern TEMPLATE cost '\n'
                                  { rule($2, $4, $5, $6); }
         | rules '\n'
         | rules error '\n'       { yyerrok; }
         ;

nonterm  : ID                     { nonterm($$ = $1); }
         ;

cost     : COST                   { if (*$1 == 0) $$ = "0"; }
         ;

pattern  : ID                                { $$ = pattern($1, NULL, NULL); }
         | ID '(' pattern ')'                { $$ = pattern($1, $3, NULL); }
         | ID '(' pattern ',' pattern ')'    { $$ = pattern($1, $3, $5); }
         ;

%%

#define BSIZE 1024
#define ISWHITESPACE(t) ((t) == ' ' || (t) == '\t' || (t) == '\r' ||    \
                         (t) == '\v' || (t) == '\f')
static char buf[BSIZE+1];
static char *bp = buf;
static int percent;
static int need_cost;
static unsigned int lineno;

static char *nextline(void)
{
    /* BSIZE contains the tailing '\0' */
    if(fgets(buf, BSIZE, stdin) == NULL) {
        if (ferror(stdin))
            yyerror("read error: %s", strerror(errno));
        *bp = 0;
        return NULL;
    }
    lineno++;
    if (strchr(buf, '\n') == NULL) {
        if (feof(stdin)) {
            int len = strlen(buf);
            buf[len] = '\n';
            buf[len + 1] = 0;
        } else {
            yyerror("line is too long");
        }
    }
    bp = buf;
    return bp;
}

static void handle_prologue(void)
{
start:
    switch (*bp) {
    case ' ': case '\t': case '\r': case '\v': case '\f':
        do {
            bp++;
        } while (ISWHITESPACE(*bp));
        goto start;

    case '\n':
        if (nextline() == NULL)
            break;
        goto start;

    case '/':
        if (bp[1] == '/'){
            while (*bp != '\n')
                bp++;
            goto start;
        }
        break;

    case '%':
        if (bp[1] == '{') {
            /* got prologue */
            bp += 2;
            while (1) {
                if (*bp == '%' && bp[1] == '}') {
                    bp += 2;
                    break;
                }
                if (*bp == '\n') {
                    putc(*bp, stdout);
                    if (nextline() == NULL)
                        break;
                } else {
                    putc(*bp++, stdout);
                }
            }
        }
        break;
    }
    /* do nothing if not found. */
}

static int yylex(void)
{
    static int once;
    int c;

rescan:
    if (*bp == 0 && nextline() == NULL)
        return EOF;

    if (!once) {
        handle_prologue();
        once++;
        goto rescan;
    }

    if (need_cost) {
        char *p;

        bp += strspn(bp, " \t\r\v\f");
        p = strchr(bp, '/');
        if (!(p && p[1] == '/'))
            p = strchr(bp, '\n');
        assert(p);
        while (p > bp && ISWHITESPACE(p[-1]))
            p--;
        yylval.sval = xstrndup(bp, p - bp);
        bp = p;
        need_cost--;
        return COST;
    }

start:
    c = *bp++;
    switch (c) {
    case '\n':
        return c;

    case ' ': case '\t': case '\r': case '\v': case '\f':
        while (ISWHITESPACE(*bp))
            bp++;
        goto start;

    case '/':
        if (*bp == '/') {
            while (*bp != '\n')
                bp++;
            goto start;
        } else {
            return c;
        }
        break;

    case '%':
        if (*bp == '%') {
            bp += 1;
            return percent++ ? 0 : PERCENT;
        } else if (isalpha(*bp)) {
            char *p = bp;
            while (isalpha(*p))
                p++;
            if (!strncmp(bp, "start", 5) && p - bp == 5) {
                bp += 5;
                return START;
            } else if (!strncmp(bp, "term", 4) && p - bp == 4) {
                bp += 4;
                return TERM;
            } else {
                return c;
            }
        } else {
            return c;
        }
        break;

    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
    case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
    case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
    case 's': case 't': case 'u': case 'v': case 'w': case 'x':
    case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
    case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
    case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
    case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
    case 'Y': case 'Z':
    case '_':
        {
            char *src = bp - 1;
            while (isalpha(*bp) || isdigit(*bp) || *bp == '_')
                bp++;
            yylval.sval = xstrndup(src, bp - src);
        }
        return ID;

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        {
            int n = c - '0';
            while (isdigit(*bp)) {
                n = n * 10 + (*bp - '0');
                bp++;
            }
            yylval.ival = n;
        }
        return NUMBER;

    case '"':
        {
            char *src = bp;
            while (*bp != '"' && *bp != '\n')
                bp++;
            if (*bp != '"')
                yyerror("unclosed string");
            yylval.sval = xstrndup(src, bp - src);
            bp++;
            need_cost++;
        }
        return TEMPLATE;
        
    default:
        return c;
    }
}

void yyerror(const char *msg, ...)
{
    fprintf(stderr, "L%d: %s\n", lineno, msg);
    exit(EXIT_FAILURE);
}
