#ifndef ZEROOS_DRIVER_CORE_H
#define ZEROOS_DRIVER_CORE_H
#include "types.h"
#define DRIVER_MAX_RESOURCES 32
enum driver_state { DRIVER_NEW,DRIVER_DISCOVERED,DRIVER_MATCHED,DRIVER_PROBED,DRIVER_ACQUIRED,DRIVER_CONFIGURED,DRIVER_REGISTERED,DRIVER_SERVING,DRIVER_FAILED,DRIVER_SUSPENDED,DRIVER_REMOVED };
typedef int (*driver_release_fn)(void *context,uint32_t resource);
struct driver_instance { enum driver_state state; uint32_t resources; void *context; driver_release_fn release; };
int driver_transition(struct driver_instance *d,enum driver_state next);
int driver_resource_acquire(struct driver_instance *d,uint32_t resource);
int driver_resource_release(struct driver_instance *d,uint32_t resource);
int driver_cleanup(struct driver_instance *d);
#endif
