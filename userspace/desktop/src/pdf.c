/* PDF subset parser.  See pdf.h for the honest-support contract. */
#include <zeroos/desktop/pdf.h>

static int pd_find(const uint8_t *d, uint32_t n, const char *needle,
                   uint32_t from, uint32_t *out) {
    uint32_t i, j = 0;
    uint32_t nl = 0;
    while (needle[nl])
        ++nl;
    if (!nl)
        return -22;
    for (i = from; i < n; ++i) {
        if (d[i] == (uint8_t)needle[j]) {
            if (++j == nl) {
                *out = i + 1 - nl;
                return 0;
            }
        } else {
            j = (d[i] == (uint8_t)needle[0]) ? 1 : 0;
        }
    }
    return -1;
}
static int pd_match(const uint8_t *d, uint32_t n, uint32_t at,
                    const char *needle) {
    uint32_t i = 0;
    while (needle[i]) {
        if (at + i >= n || d[at + i] != (uint8_t)needle[i])
            return 0;
        ++i;
    }
    return 1;
}
/* object number of the nearest "<digits> <gen> obj" before `pos` */
static uint32_t pd_obj_before(const uint8_t *d, uint32_t pos) {
    uint32_t i = pos;
    uint32_t guard = 0;
    while (i > 0 && guard < 64) {
        uint32_t end_i, k, num = 0, t, g0;
        --i;
        ++guard;
        if (d[i] < '0' || d[i] > '9')
            continue;
        end_i = i;
        while (end_i > 0 && d[end_i - 1] >= '0' && d[end_i - 1] <= '9')
            --end_i;
        for (k = end_i; k <= i; ++k)
            num = num * 10u + (uint32_t)(d[k] - '0');
        /* forward: spaces, generation digits, spaces, "obj" */
        t = i + 1;
        while (t < pos && (d[t] == ' ' || d[t] == '\t' || d[t] == '\r' ||
                           d[t] == '\n'))
            ++t;
        g0 = t;
        while (t < pos && d[t] >= '0' && d[t] <= '9')
            ++t;
        if (t == g0)
            continue; /* no generation number after this run */
        while (t < pos && (d[t] == ' ' || d[t] == '\t' || d[t] == '\r' ||
                           d[t] == '\n'))
            ++t;
        if (t + 2 < pos && d[t] == 'o' && d[t + 1] == 'b' &&
            d[t + 2] == 'j') {
            uint32_t e = t + 3;
            if (e >= pos || d[e] == ' ' || d[e] == '<' || d[e] == '\r' ||
                d[e] == '\n' || d[e] == '\t')
                return num;
        }
    }
    return 0;
}
/* count "/Type" + optional spaces + "/Page" not followed by 's' */
static uint32_t pd_count_pages(const uint8_t *d, uint32_t n,
                               uint32_t *first_obj) {
    uint32_t at = 0, count = 0;
    while (pd_find(d, n, "/Type", at, &at) == 0) {
        uint32_t i = at + 5;
        uint32_t objpos;
        while (i < n && d[i] == ' ')
            ++i;
        if (!pd_match(d, n, i, "/Page")) {
            at = i + 1;
            continue;
        }
        i += 5;
        if (i < n && d[i] == 's') { /* /Pages node */
            at = i + 1;
            continue;
        }
        objpos = at;
        if (count == 0 && first_obj)
            *first_obj = pd_obj_before(d, objpos);
        ++count;
        at = i + 1;
    }
    return count;
}
static uint32_t pd_count_substr(const uint8_t *d, uint32_t n,
                                const char *needle) {
    uint32_t at = 0, c = 0;
    while (pd_find(d, n, needle, at, &at) == 0) {
        ++c;
        at += 1;
    }
    return c;
}

int zd_pdf_open(const uint8_t *data, uint32_t size, struct zd_pdf *out) {
    uint32_t i, first_obj = 0;
    if (!data || !out)
        return -22;
    for (i = 0; i < sizeof(*out); ++i)
        ((uint8_t *)out)[i] = 0;
    out->data = data;
    out->size = size;
    if (size < 16)
        return -22;
    if (data[0] != '%' || data[1] != 'P' || data[2] != 'D' ||
        data[3] != 'F' || data[4] != '-')
        return -22;
    /* version: %PDF-1.x */
    if (data[5] >= '0' && data[5] <= '9' && data[6] == '.' &&
        data[7] >= '0' && data[7] <= '9') {
        out->version[0] = (char)data[5];
        out->version[1] = '.';
        out->version[2] = (char)data[7];
    }
    out->page_count = pd_count_pages(data, size, &first_obj);
    if (out->page_count == 0 || out->page_count > ZD_PDF_MAX_PAGES)
        return -22;
    if (pd_find(data, size, "/Encrypt", 0, &(uint32_t){0}) == 0)
        out->encrypted = 1;
    out->filtered_streams = pd_count_substr(data, size, "/Filter");
    if (out->encrypted)
        return -95;
    if (out->filtered_streams)
        return -95;
    return 0;
}

/* append a literal string (already unescaped) with cap accounting */
static void pd_emit(struct zd_pdf_page *pg, const char *s, uint32_t len,
                    uint32_t *warn) {
    uint32_t i;
    for (i = 0; i < len; ++i) {
        if (pg->text_len + 1 >= ZD_PDF_TEXT_CAP) {
            ++(*warn);
            return;
        }
        pg->text[pg->text_len++] = s[i];
    }
    pg->text[pg->text_len] = 0;
}

/* extract show-text operators from one content stream.
 * Strings stream straight into the page buffer (no intermediate cap);
 * an ignored string rolls the buffer back to its mark. */
static void pd_extract_stream(struct zd_pdf_page *pg,
                              const uint8_t *s, uint32_t n,
                              uint32_t *warn) {
    uint32_t i = 0, depth = 0, mark = 0;
    int in_str = 0, esc = 0;
    pg->text_len = 0;
    pg->text[0] = 0;
    while (i < n) {
        uint8_t c = s[i];
        if (in_str) {
            if (esc) {
                /* PDF simple escapes; unknown -> literal char */
                pd_emit(pg, (const char *)&c, 1, warn);
                esc = 0;
            } else if (c == '\\') {
                esc = 1;
            } else if (c == ')') {
                uint32_t j = i + 1;
                in_str = 0;
                while (j < n && (s[j] == ' ' || s[j] == '\t' ||
                                 s[j] == '\r' || s[j] == '\n'))
                    ++j;
                if (j + 1 < n && s[j] == 'T' && s[j + 1] == 'j') {
                    pd_emit(pg, "\n", 1, warn);
                } else if (j < n && s[j] == '\'') {
                    pd_emit(pg, "\n", 1, warn);
                } else if (depth > 0) {
                    /* array element (TJ): keep as-is */
                } else {
                    /* not a show-text operand: roll back */
                    pg->text_len = mark;
                    pg->text[mark] = 0;
                }
            } else {
                pd_emit(pg, (const char *)&c, 1, warn);
            }
        } else if (c == '(') {
            in_str = 1;
            esc = 0;
            mark = pg->text_len;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            if (depth)
                --depth;
        }
        ++i;
    }
    if (in_str) {
        /* truncated literal: drop the partial string */
        pg->text_len = mark;
        pg->text[mark] = 0;
        ++(*warn);
    }
}

/* locate "/Contents N 0 R" for a page object, then its stream */
static int pd_page_content(const uint8_t *d, uint32_t n,
                           uint32_t page_obj, uint32_t *stream_at,
                           uint32_t *stream_end) {
    uint32_t at = 0, span_start = 0, i;
    char num[16];
    uint32_t num_len = 0;
    /* find "<page_obj> 0 obj" */
    if (page_obj == 0)
        return -1;
    /* build "N 0 obj" needle */
    {
        uint32_t v = page_obj;
        char rev[16];
        uint32_t rl = 0;
        while (v && rl < sizeof(rev)) {
            rev[rl++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (rl && num_len < sizeof(num))
            num[num_len++] = rev[--rl];
    }
    if (num_len == 0 || num_len + 6 >= sizeof(num))
        return -1;
    num[num_len++] = ' ';
    num[num_len++] = '0';
    num[num_len++] = ' ';
    num[num_len++] = 'o';
    num[num_len++] = 'b';
    num[num_len++] = 'j';
    num[num_len] = 0;
    while (pd_find(d, n, num, at, &span_start) == 0) {
        /* confirm boundary before the number */
        if (span_start == 0 ||
            (d[span_start - 1] < '0' || d[span_start - 1] > '9')) {
            break;
        }
        at = span_start + 1;
        span_start = 0;
    }
    if (!span_start)
        return -1;
    /* contents reference inside this object */
    {
        uint32_t obj_at = span_start, end_at = obj_at;
        uint32_t contents_val = 0;
        if (pd_find(d, n, "endobj", obj_at, &end_at) != 0)
            end_at = n;
        if (pd_find(d, n, "/Contents", obj_at, &at) != 0 || at > end_at)
            return -1;
        /* parse N 0 R */
        i = at + 9;
        while (i < n && (d[i] == ' ' || d[i] == '\r' || d[i] == '\n'))
            ++i;
        {
            uint32_t digits = 0;
            while (i < n && d[i] >= '0' && d[i] <= '9' && digits < 10) {
                contents_val = contents_val * 10 +
                               (uint32_t)(d[i] - '0');
                ++i;
                ++digits;
            }
            if (!digits)
                return -1;
        }
        /* resolve "contents_val 0 obj" -> stream ... endstream */
        {
            char cn[16];
            uint32_t cl = 0, v = contents_val;
            char rev[16];
            uint32_t rl = 0;
            while (v && rl < sizeof(rev)) {
                rev[rl++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (rl && cl < sizeof(cn))
                cn[cl++] = rev[--rl];
            if (cl == 0 || cl + 6 >= sizeof(cn))
                return -1;
            cn[cl++] = ' ';
            cn[cl++] = '0';
            cn[cl++] = ' ';
            cn[cl++] = 'o';
            cn[cl++] = 'b';
            cn[cl++] = 'j';
            cn[cl] = 0;
            if (pd_find(d, n, cn, 0, &at) != 0)
                return -1;
            /* after "obj": find "stream" before "endobj" */
            {
                uint32_t e2 = at;
                if (pd_find(d, n, "endobj", at, &e2) != 0)
                    e2 = n;
                if (pd_find(d, n, "stream", at, stream_at) != 0 ||
                    *stream_at > e2)
                    return -1;
                *stream_at += 6;
                if (*stream_at < n && d[*stream_at] == '\r')
                    ++*stream_at;
                if (*stream_at < n && d[*stream_at] == '\n')
                    ++*stream_at;
                if (pd_find(d, n, "endstream", *stream_at,
                            stream_end) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

int zd_pdf_extract(struct zd_pdf *doc) {
    uint32_t p, first_obj = 0;
    if (!doc || !doc->data)
        return -22;
    if (doc->page_count == 0 || doc->page_count > ZD_PDF_MAX_PAGES)
        return -22;
    if (doc->encrypted || doc->filtered_streams)
        return -95;
    /* recover first object number for the page-object walk */
    (void)pd_count_pages(doc->data, doc->size, &first_obj);
    for (p = 0; p < doc->page_count; ++p) {
        uint32_t st = 0, en = 0;
        /* each /Type /Page occurrence's object number */
        uint32_t at = 0, seen = 0, obj = 0;
        while (pd_find(doc->data, doc->size, "/Type", at, &at) == 0) {
            uint32_t i = at + 5;
            while (i < doc->size && doc->data[i] == ' ')
                ++i;
            if (!pd_match(doc->data, doc->size, i, "/Page")) {
                at = i + 1;
                continue;
            }
            i += 5;
            if (i < doc->size && doc->data[i] == 's') {
                at = i + 1;
                continue;
            }
            if (seen == p) {
                obj = pd_obj_before(doc->data, at);
                break;
            }
            ++seen;
            at = i + 1;
        }
        doc->pages[p].obj_num = obj;
        if (obj &&
            pd_page_content(doc->data, doc->size, obj, &st, &en) == 0 &&
            en > st) {
            pd_extract_stream(&doc->pages[p], doc->data + st, en - st,
                              &doc->parse_warnings);
        } else {
            doc->pages[p].text_len = 0;
            doc->pages[p].text[0] = 0;
            ++doc->parse_warnings; /* missing content stream */
        }
    }
    return 0;
}

const char *zd_pdf_status_name(int rc) {
    if (rc == 0)
        return "ok";
    if (rc == -95)
        return "unsupported-feature";
    if (rc == -22)
        return "malformed";
    return "error";
}
