%{
#include <stdio.h>
enum { MOVE=1, MEM=2, PLUS=3, NAME=4, CONST=6 };
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
%term MOVE=1 MEM=2 PLUS=3 NAME=4 CONST=6
%%
stm:    MOVE(MEM(loc),reg)      ""      4

reg:    PLUS(con,reg)           ""      3
reg:    PLUS(reg,reg)           ""      2
reg:    PLUS(MEM(loc),reg)      ""      4
reg:    MEM(loc)                ""      4
reg:    con                     ""      2

loc:    reg                     ""
loc:    NAME                    ""
loc:    PLUS(NAME,reg)          ""

con:    CONST                   ""
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

        t = tree(MOVE,
		tree(MEM, tree(NAME, 0, 0), 0),
		tree(PLUS,
			tree(MEM, tree(PLUS,
				tree(NAME, 0, 0),
				tree(MEM, tree(NAME, 0, 0), 0)), 0),
			tree(CONST, 0, 0) ) );
         walk(t);
}
