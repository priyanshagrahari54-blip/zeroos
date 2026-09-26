/* Formula engine.  See formula.h for the supported subset. */
#include <zeroos/desktop/formula.h>

void zd_formula_init(struct zd_formula *f) {
    uint32_t i;
    if (!f)
        return;
    f->input[0] = 0;
    f->node_count = 0;
    f->root = -1;
    f->err_pos = 0;
    for (i = 0; i < ZD_FORMULA_NODES; ++i) {
        f->nodes[i].type = ZD_FORM_NUM;
        f->nodes[i].num = 0;
        f->nodes[i].a = f->nodes[i].b = -1;
        f->nodes[i].name[0] = 0;
    }
    for (i = 0; i < ZD_FORMULA_VARS; ++i) {
        f->vars[i].name[0] = 0;
        f->vars[i].value = 0;
        f->vars[i].in_use = 0;
    }
    f->stats.parses = f->stats.parse_errors = 0;
    f->stats.evals = f->stats.eval_errors = 0;
    f->stats.var_sets = f->stats.var_rejected = 0;
}

/* ---- parser ---- */

struct f_parse {
    const char *s;
    uint32_t i;
    struct zd_formula *f;
    int err;                 /* 1 = failed */
    uint32_t err_pos;
    int depth;
};

static int fp_expr(struct f_parse *p);

static int fp_node(struct f_parse *p, int type, int a, int b) {
    struct zd_formula *f = p->f;
    if (f->node_count >= ZD_FORMULA_NODES) {
        p->err = 1;
        p->err_pos = p->i;
        return -1;
    }
    f->nodes[f->node_count].type = type;
    f->nodes[f->node_count].num = 0;
    f->nodes[f->node_count].a = a;
    f->nodes[f->node_count].b = b;
    f->nodes[f->node_count].name[0] = 0;
    return (int)f->node_count++;
}

static void fp_ws(struct f_parse *p) {
    while (p->s[p->i] == ' ')
        p->i++;
}

static int fp_primary(struct f_parse *p) {
    fp_ws(p);
    if (p->err)
        return -1;
    if (p->depth >= ZD_FORMULA_DEPTH) {
        p->err = 1;
        p->err_pos = p->i;
        return -1;
    }
    /* \frac{a}{b} */
    if (p->s[p->i] == '\\' && p->s[p->i + 1] == 'f' &&
        p->s[p->i + 2] == 'r' && p->s[p->i + 3] == 'a' &&
        p->s[p->i + 4] == 'c') {
        int na, nb;
        p->i += 5;
        fp_ws(p);
        if (p->s[p->i] != '{') {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        p->i++;
        p->depth++;
        na = fp_expr(p);
        p->depth--;
        fp_ws(p);
        if (p->err || p->s[p->i] != '}') {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        p->i++;
        fp_ws(p);
        if (p->s[p->i] != '{') {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        p->i++;
        p->depth++;
        nb = fp_expr(p);
        p->depth--;
        fp_ws(p);
        if (p->err || p->s[p->i] != '}') {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        p->i++;
        if (p->err || na < 0 || nb < 0)
            return -1;
        return fp_node(p, ZD_FORM_FRAC, na, nb);
    }
    /* \sqrt{x} */
    if (p->s[p->i] == '\\' && p->s[p->i + 1] == 's' &&
        p->s[p->i + 2] == 'q' && p->s[p->i + 3] == 'r' &&
        p->s[p->i + 4] == 't') {
        int na;
        p->i += 5;
        fp_ws(p);
        if (p->s[p->i] != '{') {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        p->i++;
        p->depth++;
        na = fp_expr(p);
        p->depth--;
        fp_ws(p);
        if (p->err || p->s[p->i] != '}') {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        p->i++;
        if (na < 0)
            return -1;
        return fp_node(p, ZD_FORM_SQRT, na, -1);
    }
    if (p->s[p->i] == '(') {
        int na;
        p->i++;
        p->depth++;
        na = fp_expr(p);
        p->depth--;
        fp_ws(p);
        if (p->err || p->s[p->i] != ')') {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        p->i++;
        return na;
    }
    /* number: integer digits, optional single '.', fraction digits */
    if ((p->s[p->i] >= '0' && p->s[p->i] <= '9') ||
        p->s[p->i] == '.') {
        double v = 0;
        int any = 0, dot = 0;
        while (p->s[p->i] >= '0' && p->s[p->i] <= '9') {
            v = v * 10 + (double)(p->s[p->i] - '0');
            any = 1;
            p->i++;
        }
        if (p->s[p->i] == '.') {
            double scale = 0.1;
            dot = 1;
            p->i++;
            while (p->s[p->i] >= '0' && p->s[p->i] <= '9') {
                v += (double)(p->s[p->i] - '0') * scale;
                scale *= 0.1;
                any = 1;
                p->i++;
            }
        }
        (void)dot;
        if (!any) {
            p->err = 1;
            p->err_pos = p->i;
            return -1;
        }
        {
            int idx = fp_node(p, ZD_FORM_NUM, -1, -1);
            if (idx < 0)
                return -1;
            p->f->nodes[idx].num = v;
            return idx;
        }
    }
    /* variable: letter then alnum (bounded) */
    if ((p->s[p->i] >= 'a' && p->s[p->i] <= 'z') ||
        (p->s[p->i] >= 'A' && p->s[p->i] <= 'Z')) {
        char nm[ZD_FORMULA_NAME];
        uint32_t n = 0;
        int idx;
        while (p->s[p->i] && n + 1 < ZD_FORMULA_NAME &&
               ((p->s[p->i] >= 'a' && p->s[p->i] <= 'z') ||
                (p->s[p->i] >= 'A' && p->s[p->i] <= 'Z') ||
                (p->s[p->i] >= '0' && p->s[p->i] <= '9'))) {
            nm[n++] = p->s[p->i++];
        }
        nm[n] = 0;
        idx = fp_node(p, ZD_FORM_VAR, -1, -1);
        if (idx < 0)
            return -1;
        {
            uint32_t k = 0;
            while (nm[k] && k + 1 < ZD_FORMULA_NAME) {
                p->f->nodes[idx].name[k] = nm[k];
                ++k;
            }
            p->f->nodes[idx].name[k] = 0;
        }
        return idx;
    }
    p->err = 1;
    p->err_pos = p->i;
    return -1;
}

/* precedence (loose -> tight): unary -, then ^, then primary —
 * so -2^2 parses as -(2^2), matching standard math notation. */
static int fp_power(struct f_parse *p);

static int fp_unary(struct f_parse *p) {
    fp_ws(p);
    if (p->s[p->i] == '-') {
        int na;
        p->i++;
        p->depth++;
        na = fp_unary(p);
        p->depth--;
        if (p->err || na < 0)
            return -1;
        return fp_node(p, ZD_FORM_NEG, na, -1);
    }
    if (p->s[p->i] == '+') {
        p->i++; /* unary plus is legal */
        return fp_unary(p);
    }
    return fp_power(p);
}

static int fp_power(struct f_parse *p) {
    int base = fp_primary(p);
    fp_ws(p);
    if (p->err || base < 0)
        return -1;
    if (p->s[p->i] == '^') {
        int exp;
        p->i++;
        p->depth++;
        exp = fp_power(p); /* right associative */
        p->depth--;
        if (p->err || exp < 0)
            return -1;
        return fp_node(p, ZD_FORM_POW, base, exp);
    }
    return base;
}

static int fp_term(struct f_parse *p) {
    int left = fp_unary(p);
    if (p->err || left < 0)
        return -1;
    for (;;) {
        fp_ws(p);
        if (p->s[p->i] == '*') {
            int right;
            p->i++;
            p->depth++;
            right = fp_unary(p);
            p->depth--;
            if (p->err || right < 0)
                return -1;
            left = fp_node(p, ZD_FORM_MUL, left, right);
            if (left < 0)
                return -1;
        } else if (p->s[p->i] == '/') {
            int right;
            p->i++;
            p->depth++;
            right = fp_unary(p);
            p->depth--;
            if (p->err || right < 0)
                return -1;
            left = fp_node(p, ZD_FORM_DIV, left, right);
            if (left < 0)
                return -1;
        } else
            return left;
    }
}

static int fp_expr(struct f_parse *p) {
    int left = fp_term(p);
    if (p->err || left < 0)
        return -1;
    for (;;) {
        fp_ws(p);
        if (p->s[p->i] == '+') {
            int right;
            p->i++;
            p->depth++;
            right = fp_term(p);
            p->depth--;
            if (p->err || right < 0)
                return -1;
            left = fp_node(p, ZD_FORM_ADD, left, right);
            if (left < 0)
                return -1;
        } else if (p->s[p->i] == '-') {
            int right;
            p->i++;
            p->depth++;
            right = fp_term(p);
            p->depth--;
            if (p->err || right < 0)
                return -1;
            left = fp_node(p, ZD_FORM_SUB, left, right);
            if (left < 0)
                return -1;
        } else
            return left;
    }
}

int zd_formula_parse(struct zd_formula *f, const char *input) {
    struct f_parse p;
    uint32_t n = 0;
    int root;
    if (!f || !input) {
        if (f)
            f->stats.parse_errors++;
        return -22;
    }
    while (input[n]) {
        if (n >= ZD_FORMULA_INPUT - 1) {
            f->stats.parse_errors++;
            f->root = -1;
            f->err_pos = n;
            return -22;
        }
        ++n;
    }
    if (n == 0) {
        f->stats.parse_errors++;
        f->root = -1;
        f->err_pos = 0;
        return -22;
    }
    /* reset tree (vars persist across parses) */
    f->node_count = 0;
    f->root = -1;
    f->err_pos = 0;
    {
        uint32_t k = 0;
        while (k < n && k + 1 < ZD_FORMULA_INPUT) {
            f->input[k] = input[k];
            ++k;
        }
        f->input[k] = 0;
    }
    p.s = f->input;
    p.i = 0;
    p.f = f;
    p.err = 0;
    p.err_pos = 0;
    p.depth = 0;
    root = fp_expr(&p);
    fp_ws(&p);
    if (p.err || root < 0 || p.s[p.i] != 0) {
        f->stats.parse_errors++;
        f->root = -1;
        f->err_pos = p.err ? p.err_pos : p.i;
        return -22;
    }
    f->root = root;
    f->stats.parses++;
    return 0;
}

/* ---- variables ---- */

static int fv_name_ok(const char *name) {
    uint32_t n = 0;
    if (!name || !name[0])
        return 0;
    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z')))
        return 0;
    while (name[n]) {
        if (n + 1 >= ZD_FORMULA_NAME)
            return 0;
        if (!((name[n] >= 'a' && name[n] <= 'z') ||
              (name[n] >= 'A' && name[n] <= 'Z') ||
              (name[n] >= '0' && name[n] <= '9')))
            return 0;
        ++n;
    }
    return 1;
}

int zd_formula_set_var(struct zd_formula *f, const char *name,
                       double value) {
    uint32_t i, free_slot = ZD_FORMULA_VARS;
    if (!f || !fv_name_ok(name)) {
        if (f)
            f->stats.var_rejected++;
        return -22;
    }
    for (i = 0; i < ZD_FORMULA_VARS; ++i) {
        if (f->vars[i].in_use) {
            uint32_t k = 0;
            int same = 1;
            while (f->vars[i].name[k] && name[k]) {
                if (f->vars[i].name[k] != name[k]) {
                    same = 0;
                    break;
                }
                ++k;
            }
            if (same && f->vars[i].name[k] == 0 && name[k] == 0) {
                f->vars[i].value = value;
                f->stats.var_sets++;
                return 0;
            }
        } else if (free_slot == ZD_FORMULA_VARS) {
            free_slot = i;
        }
    }
    if (free_slot == ZD_FORMULA_VARS)
        return -28;
    {
        uint32_t k = 0;
        while (name[k] && k + 1 < ZD_FORMULA_NAME) {
            f->vars[free_slot].name[k] = name[k];
            ++k;
        }
        f->vars[free_slot].name[k] = 0;
    }
    f->vars[free_slot].value = value;
    f->vars[free_slot].in_use = 1;
    f->stats.var_sets++;
    return 0;
}

/* ---- evaluation ---- */

static double f_sqrt_approx(double x) {
    double r;
    int k;
    if (x <= 0)
        return 0;
    r = x > 1 ? x : 1;
    for (k = 0; k < 24; ++k)
        r = 0.5 * (r + x / r);
    return r;
}

static int feval(const struct zd_formula *f, int idx, double *out,
                 const char **bad_var) {
    const struct zd_formula_node *n;
    if (idx < 0 || (uint32_t)idx >= f->node_count)
        return -22;
    n = &f->nodes[idx];
    switch (n->type) {
    case ZD_FORM_NUM:
        *out = n->num;
        return 0;
    case ZD_FORM_VAR: {
        uint32_t i;
        for (i = 0; i < ZD_FORMULA_VARS; ++i)
            if (f->vars[i].in_use) {
                uint32_t k = 0;
                int same = 1;
                while (f->vars[i].name[k] && n->name[k]) {
                    if (f->vars[i].name[k] != n->name[k]) {
                        same = 0;
                        break;
                    }
                    ++k;
                }
                if (same && f->vars[i].name[k] == 0 &&
                    n->name[k] == 0) {
                    *out = f->vars[i].value;
                    return 0;
                }
            }
        *bad_var = n->name;
        return -2;
    }
    case ZD_FORM_ADD:
    case ZD_FORM_SUB:
    case ZD_FORM_MUL:
    case ZD_FORM_DIV:
    case ZD_FORM_POW: {
        double a, b;
        int r = feval(f, n->a, &a, bad_var);
        if (r < 0)
            return r;
        r = feval(f, n->b, &b, bad_var);
        if (r < 0)
            return r;
        if (n->type == ZD_FORM_ADD)
            *out = a + b;
        else if (n->type == ZD_FORM_SUB)
            *out = a - b;
        else if (n->type == ZD_FORM_MUL)
            *out = a * b;
        else if (n->type == ZD_FORM_DIV) {
            if (b == 0)
                return -22; /* explicit, never Inf */
            *out = a / b;
        } else { /* POW: integer exponents only */
            double e = b;
            double acc = 1;
            long times;
            long k;
            long ei = (long)e;
            if ((double)ei != e || e < -64 || e > 64)
                return -22; /* non-integer / out of range */
            times = (ei < 0) ? -ei : ei;
            for (k = 0; k < times; ++k)
                acc *= a;
            if (ei < 0) {
                if (acc == 0)
                    return -22;
                acc = 1.0 / acc;
            }
            *out = acc;
        }
        return 0;
    }
    case ZD_FORM_NEG: {
        double a;
        int r = feval(f, n->a, &a, bad_var);
        if (r < 0)
            return r;
        *out = -a;
        return 0;
    }
    case ZD_FORM_FRAC: {
        double a, b;
        int r = feval(f, n->a, &a, bad_var);
        if (r < 0)
            return r;
        r = feval(f, n->b, &b, bad_var);
        if (r < 0)
            return r;
        if (b == 0)
            return -22;
        *out = a / b;
        return 0;
    }
    case ZD_FORM_SQRT: {
        double a;
        int r = feval(f, n->a, &a, bad_var);
        if (r < 0)
            return r;
        if (a < 0)
            return -22; /* no NaN */
        *out = f_sqrt_approx(a);
        return 0;
    }
    default:
        return -22;
    }
}

int zd_formula_eval(struct zd_formula *f, double *out) {
    const char *bad = 0;
    int r;
    if (!f || !out)
        return -22;
    if (f->root < 0) {
        f->stats.eval_errors++;
        return -22;
    }
    f->stats.evals++;
    r = feval(f, f->root, out, &bad);
    if (r < 0) {
        f->stats.eval_errors++;
        *out = 0;
    }
    return r;
}
