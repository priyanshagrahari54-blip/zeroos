#include <assert.h>
#include "../kernel/display_core.h"
int main(void){struct display_caps c={0};struct display_mode m={1920,1080,60000,1};assert(display_caps_add(&c,&m,16*1024*1024)==0);assert(display_caps_add(&c,&m,16*1024*1024)==-1);m.width=0;assert(!display_mode_valid(&m,16*1024*1024));return 0;}
