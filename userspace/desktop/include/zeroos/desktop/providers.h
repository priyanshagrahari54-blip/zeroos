/* Built-in Universal Search providers (Stage 5 part A): commands and
 * diagnostics.  Apps/files/settings providers bind to their services
 * at the shell layer; these two are self-contained. */
#ifndef ZEROOS_DESKTOP_PROVIDERS_H
#define ZEROOS_DESKTOP_PROVIDERS_H

#include <zeroos/desktop/search.h>

#define ZD_CMD_MAX 16
#define ZD_CMD_NAME 24
#define ZD_CMD_DESC 64

struct zd_command {
    char name[ZD_CMD_NAME];
    char description[ZD_CMD_DESC];
    int (*run)(void *ctx);
    void *ctx;
    uint8_t in_use;
};

struct zd_cmd_provider {
    struct zd_search_provider provider; /* embeds as a provider */
    struct zd_command commands[ZD_CMD_MAX];
    uint32_t count;
};

/* Diagnostics provider: one health line per index — wired by the
 * shell to real counters.  Returns 1 while lines exist, 0 after. */
typedef uint32_t (*zd_diag_line_fn)(void *ctx, uint32_t index,
                                    char *out, uint32_t cap);
struct zd_diag_provider {
    struct zd_search_provider provider;
    zd_diag_line_fn line;   /* produce one health line per call index */
    void *ctx;
};

int zd_commands_init(struct zd_cmd_provider *cp);
int zd_commands_register(struct zd_cmd_provider *cp, const char *name,
                         const char *desc,
                         int (*run)(void *), void *ctx);
int zd_commands_execute(struct zd_cmd_provider *cp, const char *name);

void zd_diagnostics_init(struct zd_diag_provider *dp,
                         zd_diag_line_fn line, void *ctx);

#endif /* ZEROOS_DESKTOP_PROVIDERS_H */
