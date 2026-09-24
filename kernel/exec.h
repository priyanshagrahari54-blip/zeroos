#ifndef ZEROOS_EXEC_H
#define ZEROOS_EXEC_H

#include "types.h"

struct process;

#define ZEROOS_EXEC_MAX_IMAGE (64U * 1024U)
#define ZEROOS_EXEC_MAX_ARGUMENTS 16U
#define ZEROOS_EXEC_MAX_STRING 128U
#define ZEROOS_EXEC_MAX_STRING_BYTES 2048U

struct zeroos_exec_spawn_result {
    uint64_t pid;
    uint64_t tid;
};

int exec_system_init(void);
/* Copy a validated static ELF and bounded argv/envp vectors from parent,
 * construct the initial user stack, and publish one child thread. */
int exec_spawn(struct process *parent, uint64_t user_image,
               uint64_t image_size, uint64_t user_argv, uint64_t argc,
               uint64_t user_envp, uint64_t envc,
               struct zeroos_exec_spawn_result *result);
int exec_debug_validate(void);

#endif
