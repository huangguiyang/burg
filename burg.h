#ifndef BURG_H
#define BURG_H

#include <stddef.h>             /* for size_t */

struct term {
    /* dont't touch `name' and `kind' ! */
    char *name;
    int kind;
    int id;
    int nkids;
    struct rule *rules;         /* rules whose pattern starts with term */
    struct term *link;          /* next term (sorted by id) */
};

struct nonterm {
    /* dont't touch `name' and `kind' ! */
    char *name;
    int kind;
    int number;
    unsigned int nrules;
    struct rule *rules;         /* rules with the same nonterm on lhs */
    struct rule *chain;         /* rules with the same nonterm on rhs */
    struct nonterm *link;       /* next nonterm (sorted by number) */
};

struct pattern {
    void *op;                   /* a term or nonterm */
    struct pattern *left;
    struct pattern *right;
    int nterms;                 /* number of terms */
};

struct rule {
    struct nonterm *nterm;      /* lhs */
    struct pattern *pattern;
    char *template;
    char *code;            /* cost code */
    int cost;              /* -1 if cost is not integer literal */
    int ern;               /* external rule number (in all rules) */
    int irn;               /* internal rule number (in the same nonterm) */
    struct rule *tlink;    /* next rule with the same pattern root (term) */
    struct rule *nlink;    /* next rule with the same lhs (nonterm) (sorted by irn) */
    struct rule *chain;    /* next rule with the same rhs (nonterm) */
    struct rule *link;     /* next rule (sorted by ern) */
};

extern int yyparse(void);
extern void yyerror(const char *, ...);
extern char *xstrdup(const char *);
extern char *xstrndup(const char *, size_t);
extern struct nonterm *nonterm(char *);
extern struct term *term(char *, int);
extern struct pattern *pattern(char *, struct pattern *, struct pattern *);
extern struct rule *rule(char *, struct pattern *, char *, char *);

#endif
