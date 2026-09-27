#include <assert.h>
#include "../kernel/driver_core.h"
static int released;static int release(void*c,uint32_t r){(void)c;(void)r;released++;return 0;}
int main(void){struct driver_instance d={0};d.release=release;assert(driver_transition(&d,DRIVER_DISCOVERED)==0);assert(driver_transition(&d,DRIVER_MATCHED)==0);assert(driver_transition(&d,DRIVER_PROBED)==0);assert(driver_resource_acquire(&d,3)==0);assert(driver_resource_acquire(&d,3)==-1);assert(driver_cleanup(&d)==0&&released==1);assert(driver_cleanup(&d)==0&&released==1);return 0;}
