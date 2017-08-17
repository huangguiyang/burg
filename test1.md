%{
#include <stdio.h>
enum {
     ASGNI = 53,
     CNSTI = 21,
     ADDI = 309,
     ADDRLP = 295,
     INDIRC = 67,
     CVCI = 85,
     I0I = 661,
};
struct tree {
       int op;
       struct tree *kids[2];
       void *state;
};
typedef struct tree NODE_TYPE;
#define LEFT_KID(p)  ((p)->kids[0])
#define RIGHT_KID(p)  ((p)->kids[1])
#define NODE_OP(p)  ((p)->op)
#define NODE_STATE(p)  ((p)->state)

static void _trace(struct tree *t, int ruleno, int cost, int bestcost);
%}
%term ASGNI = 53
%term CNSTI = 21
%term ADDI = 309
%term ADDRLP = 295
%term INDIRC = 67
%term CVCI = 85
%term I0I = 661
%start stmt
%%
stmt: ASGNI(disp, reg)   "mov #reg, #disp"      1
stmt: reg                ""
reg: ADDI(reg, rc)       "add #reg, #rc"        1
reg: CVCI(INDIRC(disp))  "cvci [disp]"          1
reg: I0I                 ""
reg: disp                ""                     1
disp: ADDI(reg, con)     "add #reg, #con"
disp: ADDRLP             ""
rc: con                  ""
rc: reg                  ""
con: CNSTI               ""
con: I0I                 ""
%%

static struct tree *tree(int op, struct tree *l, struct tree *r)
{
        struct tree *p = malloc(sizeof(struct tree));
        p->op = op;
        p->kids[0] = l;
        p->kids[1] = r;
        p->state = 0;
        return p;
}

static void _trace(struct tree *t, int ruleno, int cost, int bestcost)
{
        fprintf(stderr, "Trace: %p, %d, %d. %s with %d vs. %d\n",
                t, NODE_OP(t), ruleno, _rule_names[ruleno], cost, bestcost);
}

// print the matched pattern for p
// p - the tree
// nt - the nonterm at the lhs of the pattern
// level - indent level for output
static void dump_match(struct tree *p, int nt, int level)
{
        int ruleno = _rule(NODE_STATE(p), nt);
        short *nts = _nts[ruleno];
        struct tree *kids[_MAX_NTS];

        for (int i = 0; i < level; i++)
            fprintf(stderr, " ");

        fprintf(stderr, "%s\n", _rule_names[ruleno]);
        _kids(p, ruleno, kids);
        //NOTE: kids and nts _MUST_ have equal length.
        for (int i = 0; nts[i]; i++)
            dump_match(kids[i], nts[i], level + 1);
}

static void walk(struct tree *p)
{
        _label(p);
        if (_rule(NODE_STATE(p), 1))
           dump_match(p, 1, 0);
        else
           fprintf(stderr, "Error: no match found.\n");
}

int main(int argc, char *argv[])
{
        struct tree *t;

        // int i; char c; i = c + 4;
        t = tree(ASGNI,
                tree(ADDRLP, NULL, NULL),
                tree(ADDI,
                     tree(CVCI,
                          tree(INDIRC,
                                tree(ADDRLP, NULL, NULL),
                                NULL),
                          NULL),
                     tree(CNSTI, NULL, NULL)));
         walk(t);
}
