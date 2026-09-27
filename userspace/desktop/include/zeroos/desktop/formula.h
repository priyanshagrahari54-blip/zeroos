/* Formula engine (Stage 5 part G — Study Center): a bounded
 * recursive-descent parser + evaluator for a documented math subset.
 *
 * Supported: numbers (decimal), variables, + - * /, unary minus,
 * right-associative ^ with INTEGER exponents only (documented
 * limit), parentheses, \frac{a}{b}, \sqrt{x} (Newton iteration —
 * no libm dependency; negative argument is an explicit error).
 * Unknown variables, division by zero and non-integer exponents are
 * explicit errors, never silent NaN/Inf.  Bounded: input <= 128
 * chars, AST arena <= 64 nodes, nesting depth checked, vars <= 8.
 */
#ifndef ZEROOS_DESKTOP_FORMULA_H
#define ZEROOS_DESKTOP_FORMULA_H

#include <stdint.h>

#define ZD_FORMULA_INPUT 128
#define ZD_FORMULA_NODES 64
#define ZD_FORMULA_VARS 8
#define ZD_FORMULA_NAME 8
#define ZD_FORMULA_DEPTH 16

enum zd_formula_node_type {
    ZD_FORM_NUM = 0,
    ZD_FORM_VAR,
    ZD_FORM_ADD,
    ZD_FORM_SUB,
    ZD_FORM_MUL,
    ZD_FORM_DIV,
    ZD_FORM_POW,
    ZD_FORM_NEG,
    ZD_FORM_FRAC,
    ZD_FORM_SQRT
};

struct zd_formula_node {
    int type;                /* enum above */
    double num;              /* ZD_FORM_NUM value */
    char name[ZD_FORMULA_NAME]; /* ZD_FORM_VAR */
    int a, b;                /* child indices (-1 = none) */
};

struct zd_formula_var {
    char name[ZD_FORMULA_NAME];
    double value;
    uint8_t in_use;
};

struct zd_formula {
    char input[ZD_FORMULA_INPUT];
    struct zd_formula_node nodes[ZD_FORMULA_NODES];
    uint32_t node_count;
    int root;                       /* -1 when unparsed */
    uint32_t err_pos;               /* parse error position */
    struct zd_formula_var vars[ZD_FORMULA_VARS];
    struct {
        uint32_t parses, parse_errors, evals, eval_errors,
                 var_sets, var_rejected;
    } stats;
};

void zd_formula_init(struct zd_formula *f);
/* 0 parsed; -22 syntax/overlong/depth/arena (err_pos set). */
int zd_formula_parse(struct zd_formula *f, const char *input);
/* Bind or update a variable (name 1..7 chars letters/digits,
 * first char a letter).  Cap reached with a NEW name -> -28;
 * invalid name -> -22 +var_rejected. */
int zd_formula_set_var(struct zd_formula *f, const char *name,
                       double value);
/* Evaluate the parsed tree against bound variables.
 * -22 parse-error state / division by zero / non-integer exponent /
 *     negative sqrt / depth violation;
 * -2 unknown variable (name in stats via eval_errors). */
int zd_formula_eval(struct zd_formula *f, double *out);

#endif /* ZEROOS_DESKTOP_FORMULA_H */
