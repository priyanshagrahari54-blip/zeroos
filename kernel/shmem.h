#ifndef ZEROOS_SHMEM_H
#define ZEROOS_SHMEM_H

#include "types.h"

struct process;

typedef uint64_t zeroos_shmem_handle_t;

#define ZEROOS_SHMEM_MAX_OBJECTS 16U
#define ZEROOS_SHMEM_MAX_CAPABILITIES 64U
#define ZEROOS_SHMEM_MAX_MAPPINGS 32U
#define ZEROOS_SHMEM_MAX_PAGES 16U
#define ZEROOS_SHMEM_MAX_SIZE (ZEROOS_SHMEM_MAX_PAGES * 4096ULL)

#define ZEROOS_SHMEM_RIGHT_MAP   (1U << 0)
#define ZEROOS_SHMEM_RIGHT_WRITE (1U << 1)
#define ZEROOS_SHMEM_RIGHT_GRANT (1U << 2)
#define ZEROOS_SHMEM_RIGHT_CLOSE (1U << 3)
#define ZEROOS_SHMEM_ALL_RIGHTS (ZEROOS_SHMEM_RIGHT_MAP | \
                                 ZEROOS_SHMEM_RIGHT_WRITE | \
                                 ZEROOS_SHMEM_RIGHT_GRANT | \
                                 ZEROOS_SHMEM_RIGHT_CLOSE)

#define ZEROOS_SHMEM_MAP_WRITE (1ULL << 0)
#define ZEROOS_SHMEM_VALID_MAP_FLAGS ZEROOS_SHMEM_MAP_WRITE

int shmem_system_init(void);
int shmem_create(struct process *owner, uint64_t size, uint64_t flags,
                 zeroos_shmem_handle_t *handle_out);
int shmem_grant(struct process *owner, zeroos_shmem_handle_t source,
                uint64_t target_pid, uint8_t rights,
                zeroos_shmem_handle_t *target_out);
int shmem_map(struct process *owner, zeroos_shmem_handle_t handle,
              uint64_t virtual_address, uint64_t flags,
              uint64_t *mapped_out);
int shmem_unmap(struct process *owner, zeroos_shmem_handle_t handle,
                uint64_t virtual_address);
int shmem_close(struct process *owner, zeroos_shmem_handle_t handle);
int shmem_process_revoke(struct process *owner);
int shmem_debug_validate(void);

#endif
