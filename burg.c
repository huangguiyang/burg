#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>
#include "burg.h"

/*
  The following symbols must be defined in the
  prologue section of the specification file:
  
  NODE_TYPE: the node type, for example:
      struct node {
          int op;
          struct node *kids[2];
          void *state;
          // other fields ...
      };
      typedef struct node NODE_TYPE;
  
  LEFT_KID(p): left kid of 'p'

  RIGHT_KID(p): right kid of 'p'

  NODE_OP(p): op of 'p'

  NODE_STATE(p): state of 'p'
 */

#define STR_HASH_INIT       5381
#define STR_HASH_STEP(h, c) (((h) << 5) + (h) + (c))
#define ARRAY_SIZE(array)   (sizeof(array) / sizeof(array)[0])
#define NEWS(st)            malloc(sizeof(st))
#define NEWS0(st)           memset(NEWS(st), 0, sizeof(st))
#define NEWARRAY(size, n)   calloc(n, size)
#define NODE_TYPE           "NODE_TYPE"
#define NODE_OP             "NODE_OP"
#define LEFT_KID            "LEFT_KID"
#define RIGHT_KID           "RIGHT_KID"
#define NODE_STATE          "NODE_STATE"
#define MAX_COST            SHRT_MAX

enum { TERM, NONTERM };

/* terminal table */
struct entry {
    union {
        const char *name;
        struct term t;
        struct nonterm nt;
    } sym;
    struct entry *link;
};

static const char progname[] = "burg";
static const char version[] = "1.0";
static char *prefix = "_";
static int trace;
static struct entry *tokens[512];
static struct nonterm *start;
static unsigned int num_rules;    /* count of rules */
static struct rule *rules;        /* all rules */
static unsigned int num_nonterms; /* count of nonterms */
static struct nonterm *nonterms;  /* all nonterms */
static unsigned int num_terms;    /* count of terms */
static struct term *terms;        /* all terms */

static void fprint(FILE *fp, const char *fmt, ...);

static void vfprint(FILE *fp, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt == '%') {
            switch (*++fmt) {
            case 'd':
                fprintf(fp, "%d", va_arg(ap, int));
                break;
            case 'u':
                fprintf(fp, "%u", va_arg(ap, unsigned int));
                break;
            case 'x':
                fprintf(fp, "%x", va_arg(ap, int));
                break;
            case 'X':
                fprintf(fp, "%X", va_arg(ap, int));
                break;
            case 's':
                fputs(va_arg(ap, char *), fp);
                break;
            case 'p':
                fprintf(fp, "%p", va_arg(ap, void *));
                break;
                /* pattern */
            case 'P':
                {
                    struct pattern *p = va_arg(ap, struct pattern *);
                    fprint(fp, "%K", p->op);
                    if (p->left && p->right)
                        fprint(fp, "(%P, %P)", p->left, p->right);
                    else if (p->left)
                        fprint(fp, "(%P)", p->left);
                }
                break;
                /* rule */
            case 'R':
                {
                    struct rule *r = va_arg(ap, struct rule *);
                    fprint(fp, "%K: %P", r->nterm, r->pattern);
                }
                break;
                /* token */
            case 'K':
                {
                    struct term *t = va_arg(ap, struct term *);
                    fputs(t->name, fp);
                }
                break;
                /* prefix */
            case '?':
                fputs(prefix, fp);
                break;
                /* tab indent */
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                {
                    int n = *fmt - '0';
                    while (n-- > 0)
                        putc('\t', fp);
                }
                break;
            default:
                putc(*fmt, fp);
                break;
            }
        } else {
            putc(*fmt, fp);
        }
    }
}

static void fprint(FILE *fp, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprint(fp, fmt, ap);
    va_end(ap);
}

static void print(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprint(stdout, fmt, ap);
    va_end(ap);
}

static void die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("error: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
    va_end(ap);
    exit(EXIT_FAILURE);
}

char *xstrdup(const char *s)
{
    return strcpy(malloc(strlen(s) + 1), s);
}

char *xstrndup(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    strncpy(d, s, n);
    d[n] = '\0';
    return d;
}

/* 'i' can be represented in 'n' bits. */
static int bits(int i)
{
    int n = 1;
    while ((i >>= 1) != 0)
        n++;
    return n;
}

static char *format(char *fmt, ...)
{
    va_list ap;
    char buf[1024];

    va_start(ap, fmt);
    vsnprintf(buf, ARRAY_SIZE(buf), fmt, ap);
    va_end(ap);
    return xstrdup(buf);
}

static unsigned int strhash(const char *s)
{
    unsigned int hash = STR_HASH_INIT;
    int c;

    while ((c = *s++))
        hash = STR_HASH_STEP(hash, c);

    return hash & (ARRAY_SIZE(tokens) - 1);
}

static void *install(char *name)
{
    unsigned int h = strhash(name);
    struct entry *p = NEWS0(struct entry);

    p->sym.name = name;
    p->link = tokens[h];
    tokens[h] = p;

    return &p->sym;
}

static void *lookup(char *name)
{
    unsigned int h = strhash(name);

    for (struct entry *p = tokens[h]; p; p = p->link)
        if (!strcmp(name, p->sym.name))
            return &p->sym;

    return NULL;
}

struct nonterm *nonterm(char *name)
{
    struct nonterm **p = &nonterms;
    struct nonterm *nt;

    nt = lookup(name);
    if (nt) {
        if (nt->kind != NONTERM)
            yyerror("corrupted nonterm: '%s'", name);
        return nt;
    }

    nt = install(name);
    nt->kind = NONTERM;
    nt->number = ++num_nonterms;

    /* start symbol */
    if (nt->number == 1)
        start = nt;

    while (*p && (*p)->number < nt->number)
        p = &(*p)->link;

    assert(*p == NULL || (*p)->number != nt->number);

    nt->link = *p;
    *p = nt;

    return nt;
}

struct term *term(char *name, int val)
{
    struct term **p = &terms;
    struct term *t;

    t = lookup(name);
    if (t)
        yyerror("redefinition of terminal '%s'", name);

    t = install(name);
    t->kind = TERM;
    t->id = val;
    t->nkids = -1;
    num_terms++;

    while (*p && (*p)->id < t->id)
        p = &(*p)->link;

    t->link = *p;
    *p = t;

    return t;
}

struct pattern *pattern(char *name, struct pattern *l, struct pattern *r)
{
    struct term *t = lookup(name); /* term or nonterm */
    int nkids;
    struct pattern *p;

    if (l && r)
        nkids = 2;
    else if (l)
        nkids = 1;
    else
        nkids = 0;

    if (t == NULL && nkids == 0)
        t = (struct term *)nonterm(name);
    else if (t == NULL && nkids > 0)
        yyerror("undefined terminal '%s'", name);
    else if (t && t->kind == NONTERM && nkids > 0)
        yyerror("non-terminal has kids");

    if (t->kind == TERM && t->nkids == -1)
        t->nkids = nkids;
    else if (t->kind == TERM && t->nkids != nkids)
        yyerror("inconsistent kids in termial '%s' (%d != %d)",
                name, t->nkids, nkids);

    p = NEWS0(struct pattern);
    p->op = t;
    p->left = l;
    p->right = r;
    /* count terms in the pattern */
    p->nterms = t->kind == TERM;
    if (p->left)
        p->nterms += p->left->nterms;
    if (p->right)
        p->nterms += p->right->nterms;
    return p;
}

/* cost may be code or digits. */
struct rule *rule(char *name, struct pattern *pattern, char *template, char *cost)
{
    struct term *op = pattern->op;
    struct nonterm *nt;
    struct rule *r;
    struct rule **p;
    char *endptr;

    nt = nonterm(name);

    r = NEWS0(struct rule);
    r->nterm = nt;
    r->pattern = pattern;
    r->template = template;
    r->code = cost;
    r->cost = strtol(cost, &endptr, 10);
    if (*endptr) {
        /* invalid */
        r->cost = -1;
        r->code = format("(%s)", cost);
    }
    r->ern = ++num_rules;
    r->irn = ++nt->nrules;

    for (p = &nt->rules; *p && (*p)->irn < r->irn; p = &(*p)->nlink)
        ;                       /* continue next */

    r->nlink = *p;
    *p = r;

    if (op->kind == TERM) {
        r->tlink = op->rules;
        op->rules = r;
    } else if (pattern->left == NULL && pattern->right == NULL) {
        struct nonterm *nterm = (struct nonterm *)op;
        r->chain = nterm->chain;
        nterm->chain = r;
    }

    for (p = &rules; *p && (*p)->ern < r->ern; p = &(*p)->link)
        ;                       /* continue next */

    r->link = *p;
    *p = r;

    return r;
}

/* See also: compute_nts */
static char *compute_kids(struct pattern *p, char *sub, char *bp, int *idx)
{
    struct term *t = p->op;
    
    if (t->kind == TERM) {
        if (p->left)
            bp = compute_kids(p->left,
                              format("%s(%s)", LEFT_KID, sub), bp, idx);
        if (p->right)
            bp = compute_kids(p->right,
                              format("%s(%s)", RIGHT_KID, sub), bp, idx);
    } else {
        sprintf(bp, "\t\tkids[%d] = %s;\n", (*idx)++, sub);
        bp += strlen(bp);
    }
    return bp;
}

/*
  Function: ?kids(NODE_TYPE *p, int ruleno, NODE_TYPE *kids[])

  This function calculates all nonterm kids for the rule `ruleno'.

  @param p         The node.
  @param ruleno    The rule number.
  @param kids      The passed-in kids array to be written.
  
  Generated code overview:
  
  static void ?kids(NODE_TYPE *p, int ruleno, NODE_TYPE *kids[])
  {
      assert(p && "null tree");
      assert(kids && "null kids for writing");
 
      switch (ruleno) {
      ... cases (compute kids) ...
      default:
          abort();
      }
  }
 */
static void emit_func_kids(void)
{
    int i;
    struct rule *r;
    int *nts = NEWARRAY(sizeof(int), num_rules);
    char **str = NEWARRAY(sizeof(char *), num_rules);
    
    for (i = 0, r = rules; r; r = r->link, i++) {
        char buf[1024];
        int j = 0;
        *compute_kids(r->pattern, "p", buf, &j) = 0;
        /* lookup */
        for (j = 0; str[j] && strcmp(str[j], buf); j++)
            ;                   /* continue next */
        if (str[j] == NULL)
            str[j] = xstrdup(buf);
        nts[i] = j;
    }

    print("static void %?kids(%s *p, int ruleno, %s *kids[])\n",
          NODE_TYPE, NODE_TYPE);
    print("{\n");
    print("%1assert(p && \"%s\");\n", "null tree");
    print("%1assert(kids && \"%s\");\n\n", "null kids for writing");
    print("%1switch (ruleno) {\n");
    /* cases */
    for (int j = 0; str[j] && j < num_rules; j++) {
        for (i = 0, r = rules; r; r = r->link, i++)
            if (nts[i] == j)
                print("%1case %d: /* %R */\n", r->ern, r);
        print("%s%2break;\n", str[j]);
    }
    /* default */
    print("%1default:\n");
    print("%2abort();\n");
    print("%1}\n");
    print("}\n\n");
}

/*
  Function: ?rule(void *state, int nt)

  This function returns the rule number of a node to be reduced to the nonterm `nt'.

  @param state    The `state' field of a node.
  @param nt       The nonterm number.

  @return         If the node can be reduced to the nonterm `nt', the best matched
                  rule number (greater than 0) will be returned. Otherwise it will
                  return 0.

  Generated code overview:
                  
  static int ?rule(void *state, int nt)
  {
      if (!state)
          return 0;
      switch (nt) {
      case ?xx_NT:
          return ?xx_rules[((struct ?state *)state)->rule.xx];
      ...
      default:
          abort();
      }
  }
 */
static void emit_func_rule(void)
{
    print("static int %?rule(void *state, int nt)\n");
    print("{\n");
    print("%1if (!state)\n");
    print("%2return 0;\n");
    print("%1switch (nt) {\n");
    for (struct nonterm *nt = nonterms; nt; nt = nt->link) {
        print("%1case %?%K_NT: /* %d */\n", nt, nt->number);
        print("%2return %?%K_rules[((struct %?state *)state)->rule.%K];\n",
              nt, nt);
    }
    print("%1default:\n");
    print("%2abort();\n");
    print("%1}\n");
    print("}\n\n");
}

/*
  ?trace(t, ruleno, cost, bestcost);
  if (c + cost < p->costs[?xx_NT]) {
      p->costs[?xx_NT] = c + cost;
      p->rule.xx = r->irn;
      ?closure_xx(t, c + cost);
  }
 */
static void emit_record(char *tabs, struct rule *r, char *c, int cost)
{
    if (trace)
        print("%s%?trace(t, %d, %s + %d, p->costs[%?%K_NT]);\n",
              tabs, r->ern, c, cost, r->nterm);

    print("%sif (%s + %d < p->costs[%?%K_NT]) {\n", tabs, c, cost, r->nterm);
    print("%s%1p->costs[%?%K_NT] = %s + %d;\n", tabs, r->nterm, c, cost);
    print("%s%1p->rule.%K = %d;\n", tabs, r->nterm, r->irn);
    if (r->nterm->chain)
        print("%s%1%?closure_%K(t, %s + %d);\n", tabs, r->nterm, c, cost);
    print("%s}\n", tabs);
}

/*
  static void ?closure_xx(NODE_TYPE *t, int c)
  {
      struct ?state *p = (struct ?state *)NODE_STATE(t);
      ... emit closure part ...
  }
 */
static void emit_func_closure(struct nonterm *nt)
{
    assert(nt->chain);

    print("static void %?closure_%K(%s *t, int c)\n", nt, NODE_TYPE);
    print("{\n");
    print("%1struct %?state *p = (struct %?state *)%s(t);\n", NODE_STATE);
    for (struct rule *r = nt->chain; r; r = r->chain) {
        print("%1/* %d. %R */\n", r->ern, r);
        if (r->cost == -1) {
            print("%1c += %s;\n", r->code);
            emit_record("\t", r, "c", 0);
        } else {
            emit_record("\t", r, "c", r->cost);
        }
    }
    print("}\n\n");
}

/* emit the matched condition */
static void emit_cond(struct pattern *p, char *var, char *suffix)
{
    struct term *t = p->op;
    
    if (t->kind == TERM) {
        print("%3%s(%s) == %d%s/* %K */",
              NODE_OP, var, t->id, p->nterms > 1 ? " && " : suffix, t);

        if (p->left)
            emit_cond(p->left, format("%s(%s)", LEFT_KID, var),
                      p->right && p->right->nterms ? " && " : suffix);
        if (p->right)
            emit_cond(p->right, format("%s(%s)", RIGHT_KID, var), suffix);
    }
}

static void emit_cost(struct pattern *p, char *var)
{
    struct term *t = p->op;
    
    if (t->kind == TERM) {
        if (p->left)
            emit_cost(p->left, format("%s(%s)", LEFT_KID, var));
        if (p->right)
            emit_cost(p->right, format("%s(%s)", RIGHT_KID, var));
    } else {
        print("((struct %?state *)(%s(%s)))->costs[%?%K_NT] + ",
              NODE_STATE, var, t);
    }
}

static void emit_case(struct term *t)
{
    /* case op: */
    print("%1case %d: /* %K */\n", t->id, t);
    switch (t->nkids) {
    case 0:
    case -1:
        /*
          do nothing
          terminals never appeared in a pattern
          would have the default nkids -1.
         */
        break;
    case 1:
        print("%2assert(l);\n");
        print("%2%?label(l);\n");
        break;
    case 2:
        print("%2assert(l && r);\n");
        print("%2%?label(l);\n");
        print("%2%?label(r);\n");
        break;
    default:
        assert(0 && "illegal nkids");
    }
    /* walk terminal links */
    for (struct rule *r = t->rules; r; r = r->tlink) {
        char *tabs = "\t\t";
        print("%2/* %d. %R */\n", r->ern, r);
        switch (t->nkids) {
        case 0:
        case -1:
            if (r->cost == -1) {
                print("%2c = %s;\n", r->code);
                emit_record(tabs, r, "c", 0);
            } else {
                emit_record(tabs, r, r->code, 0);
            }
            break;
        case 1:
            if (r->pattern->nterms > 1) {
                /* sub-tree pattern is a terminal */
                print("%2if (\n");
                emit_cond(r->pattern->left, "l", " ");
                print("\n%2) {\n");
                print("%3c = ");
                tabs = "\t\t\t";
            } else {
                print("%2c = ");
            }
            emit_cost(r->pattern->left, "l");
            print("%s;\n", r->code);
            emit_record(tabs, r, "c", 0);
            if (tabs[2])        /* end if */
                print("%2}\n");
            break;
        case 2:
            if (r->pattern->nterms > 1) {
                /* sub-tree patterns have terminal */
                print("%2if (\n");
                emit_cond(r->pattern->left, "l",
                          r->pattern->right->nterms ? " && " : " ");
                emit_cond(r->pattern->right, "r", " ");
                print("\n%2) {\n");
                print("%3c = ");
                tabs = "\t\t\t";
            } else {
                print("%2c = ");
            }
            emit_cost(r->pattern->left, "l");
            emit_cost(r->pattern->right, "r");
            print("%s;\n", r->code);
            emit_record(tabs, r, "c", 0);
            if (tabs[2])        /* end if */
                print("%2}\n");
            break;
        default:
            assert(0 && "illegal nkids");
        }
    }
    print("%2break;\n");
}

/*
  Function: ?label(NODE_TYPE *t)

  This function labels the node with dynamic programming. It labels the node
  recursively, selects the matched rule with a best cost, then records the
  rule numbers and costs in the state field.

  The costs for all nonterms in the state field are initialized to MAX_COST,
  and the rule numbers of all nonterms are initialized to zero.

  @param t    The node to be labeled.
  
  Generated code overview:
  
  static void ?label(NODE_TYPE *t)
  {
      int c;
      NODE_TYPE *l, *r;
      struct ?state *p;
 
      assert(t && "null tree");
 
      l = LEFT_KID(t);
      r = RIGHT_KID(t);
      NODE_STATE(t) = p = ?ZNEW(sizeof(struct ?state));
 
      p->costs[1] =
      p->costs[2] =
      ...
      p->costs[num_nonterms] =
          MAX_COST;
 
      switch (NODE_OP(t)) {
      ... emit cases ...
      default:
          abort();
      }
  }
 */
static void emit_func_label(void)
{
    print("static void %?label(%s *t)\n", NODE_TYPE);
    print("{\n");

    print("%1int c;\n");
    print("%1%s *l, *r;\n", NODE_TYPE);
    print("%1struct %?state *p;\n\n");
    print("%1assert(t && \"%s\");\n\n", "null tree");
    print("%1l = %s(t);\n", LEFT_KID);
    print("%1r = %s(t);\n", RIGHT_KID);
    print("%1%s(t) = p = %?ZNEW(sizeof(struct %?state));\n\n", NODE_STATE);

    /* initialize the cost to max */
    for (int i = 1; i <= num_nonterms; i++)
        print("%1p->costs[%d] =\n", i);
    print("%20x%x;\n\n", MAX_COST);

    print("%1switch (%s(t)) {\n", NODE_OP);
    /* cases */
    for (struct term *t = terms; t; t = t->link)
        emit_case(t);
    print("%1default:\n");
    print("%2abort();\n");
    print("%1}\n");
    print("}\n\n");
}

static void emit_functions(void)
{
    emit_func_rule();
    for (struct nonterm *nt = nonterms; nt; nt = nt->link)
        if (nt->chain)          /* has closure */
            emit_func_closure(nt);
    emit_func_label();
    emit_func_kids();
}

/* static void ?closure_xx(NODE_TYPE *t, int c) */
static void emit_forwards(void)
{
    for (struct nonterm *nt = nonterms; nt; nt = nt->link)
        if (nt->chain)          /* has closure */
            print("static void %?closure_%K(%s *t, int c);\n", nt, NODE_TYPE);
    print("\n");
}

/* See also: compute_kids */
static char *compute_nts(struct pattern *p, char *bp, int *j)
{
    struct term *t = p->op;
    
    if (t->kind == TERM) {
        if (p->left)
            bp = compute_nts(p->left, bp, j);
        if (p->right)
            bp = compute_nts(p->right, bp, j);
    } else {
        sprintf(bp, "%s%s_NT, ", prefix, t->name);
        bp += strlen(bp);
        (*j)++;
    }
    return bp;
}

/*
  see also: emit_func_kids
  
  static short ?nts_%d[] = { ... };
  static short *?nts[] = { ... };
 */

static void emit_var_nts(void)
{
    int i, j, max = 0;
    struct rule *r;
    int *nts = NEWARRAY(sizeof(int), num_rules);
    char **str = NEWARRAY(sizeof(char *), num_rules);

    for (i = 0, r = rules; r; r = r->link, i++) {
        char buf[1024];
        j = 0;
        *compute_nts(r->pattern, buf, &j) = 0;
        max = j > max ? j : max;
        /* lookup */
        for (j = 0; str[j] && strcmp(str[j], buf); j++)
            ;                   /* continue next */
        if (str[j] == NULL) {
            /* if _NOT_ found */
            /* static short ?nts_j[] = { buf, 0 }; */
            print("static short %?nts_%d[] = { %s0 };\n", j, buf);
            str[j] = xstrdup(buf);
        }
        nts[i] = j;
    }
    /* static short * ?nts[] */
    print("\nstatic short *%?nts[] = {\n");
    print("%10,\n");
    for (i = 0, r = rules; r; r = r->link, i++)
        print("%1%?nts_%d, // %d. %R\n", nts[i], r->ern, r);
    print("};\n");
    print("#define %?MAX_NTS %d\n\n", max);
}

/* indexed by ?xxx_NT */
static void emit_var_nt_names(void)
{
    print("static const char *%?nt_names[] = {\n");
    print("%10,\n");
    for (struct nonterm *nt = nonterms; nt; nt = nt->link)
        print("%1\"%K\",\n", nt);
    print("%10,\n");
    print("};\n\n");
}

static void emit_var_rule_names(void)
{
    print("static const char *%?rule_names[] = {\n%10,\n");
    for (struct rule *r = rules; r; r = r->link)
        print("%1\"%R\", // %d\n", r, r->ern);
    print("};\n\n");
}

/* nonterm rule numbers (indexed by inner ruleno) */
static void emit_var_nt_rules(void)
{
    for (struct nonterm *nt = nonterms; nt; nt = nt->link) {
        print("static short %?%K_rules[] = {\n", nt);
        print("%10,\n");
        for (struct rule *rule = nt->rules; rule; rule = rule->nlink)
            print("%1%d, // %R\n", rule->ern, rule);
        print("};\n\n");
    }
}

static void emit_var_templates(void)
{
    print("static const char *%?templates[] = {\n");
    print("%1\"\",\n");
    for (struct rule *rule = rules; rule; rule = rule->link) {
        if (rule->template)
            print("%1\"%s\", // %d\n", rule->template, rule->ern);
        else
            print("%1\"\", // %d\n", rule->ern);
    }
    print("};\n\n");
}

static void emit_var_is_instruction(void)
{
    print("static char %?is_instruction[] = {\n");
    print("%10,\n");
    for (struct rule *rule = rules; rule; rule = rule->link) {
        if (rule->template) {
            int len = strlen(rule->template);
            print("%1%d, // %d. \"%s\"\n",
                  len >= 2 &&
                  rule->template[len - 2] == '\\' &&
                  rule->template[len - 1] == 'n',
                  rule->ern, rule->template);
        } else {
            print("%10,\n");
        }
    }
    print("};\n\n");
}

static void emit_variables(void)
{
    emit_var_nts();
    emit_var_nt_names();
    emit_var_rule_names();
    emit_var_templates();
    emit_var_is_instruction();
    emit_var_nt_rules();
}

/*
  struct ?state {
      short costs[nts_cnt+1];
      struct {
          unsigned int nt: nt->nrules;
          ...
      } rule;
  };
 */
static void emit_types(void)
{
    print("struct %?state {\n");
    print("%1short costs[%d];\n", num_nonterms + 1);
    print("%1// indexed by inner rule number\n");
    print("%1struct {\n");
    for (struct nonterm *nt = nonterms; nt; nt = nt->link)
        print("%2unsigned int %K: %d;\n", nt, bits(nt->nrules));
    print("%1} rule;\n");
    print("};\n\n");
}

static void emit_macros(void)
{
    /* ?ZNEW */
    print("#ifndef %?ZNEW\n");
    print("#define %?ZNEW(size) memset(malloc(size), 0, (size))\n");
    print("#endif\n\n");
    /* xx_NT */
    for (struct nonterm *nt = nonterms; nt; nt = nt->link)
        print("#define %?%K_NT %d\n", nt, nt->number);
    print("#define %?NUM_NTS %d\n", num_nonterms);
    print("\n");
}

static void emit_includes(void)
{
    print("#include <assert.h>\n");
    print("#include <stdlib.h>\n");
    print("#include <string.h>\n");
    print("\n");
}

static void usage(void)
{
    fprintf(stderr,
            "usage: %s [options] <file>\n\n"
            "options:\n"
            "  -o <file>             Write output to <file>\n"
            "  -prefix <prefix>      Using <prefix> as prefix for generated names\n"
            "  -T                    Generate trace function calls\n"
            "  --help                Display available options\n"
            "  --version             Display version number\n",
            progname);
    exit(0);
}

int main(int argc, char *argv[])
{
    char *ifile = NULL;
    char *ofile = NULL;
    int ret;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!strcmp(arg, "-o")) {
            if (++i >= argc)
                die("missing output file while -o specified");
            ofile = argv[i];
        } else if (!strcmp(arg, "-prefix")) {
            if (++i >= argc)
                die("missing prefix while -prefix specified");
            if (!isalpha(argv[i][0]) && argv[i][0] != '_')
                die("prefix must start with alpha or '_'");
            prefix = argv[i];
        } else if (!strcmp(arg, "-T")) {
            trace = 1;
        } else if (!strcmp(arg, "--help")) {
            usage();
        } else if (!strcmp(arg, "--version")) {
            fprintf(stderr, "%s\n", version);
            exit(0);
        } else if (!strcmp(arg, "-")) {
            ifile = NULL;
        } else if (arg[0] == '-') {
            die("unknown option '%s' specified\n"
                "Try typing \"%s --help\" for help.", arg, progname);
        } else {
            ifile = arg;
        }
    }

    if (ifile && !freopen(ifile, "r", stdin)) {
        perror("can't read input file");
        exit(EXIT_FAILURE);
    }
    if (ofile && !freopen(ofile, "w", stdout)) {
        perror("can't open file for output");
        exit(EXIT_FAILURE);
    }

    if ((ret = yyparse()))
        die("parser failed with code: %d", ret);

    /* check start symbol */
    if (!start || !start->rules)
        die("missing 'start' rule");

    print("\n/* [BEGIN] Code generated automatically. */\n\n");

    emit_includes();
    emit_macros();
    emit_types();
    emit_variables();
    emit_forwards();
    emit_functions();

    print("\n/* [END] Code generated automatically. */\n\n");

    /* emit text left */
    if (!feof(stdin)) {
        int c;
        while ((c = getc(stdin)) != EOF)
            putc(c, stdout);
    }

    return 0;
}
