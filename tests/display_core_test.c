#include <assert.h>
#include "../kernel/display_core.h"

int main(void) {
    struct display_caps c={0};
    struct display_mode m={1920,1080,60000,1};

    assert(display_caps_add(&c,&m,16*1024*1024)==0);
    assert(display_caps_add(&c,&m,16*1024*1024)==-1);
    m.width=0;
    assert(!display_mode_valid(&m,16*1024*1024));

    /* Pixel-mapping present validation: happy paths per depth. */
    assert(display_present_request_valid(1024,768,32,3,0,0,4,4,16));
    assert(display_present_request_valid(1024,768,32,3,1020,764,4,4,16));
    assert(display_present_request_valid(1024,768,16,1,0,0,4,4,8));
    assert(display_present_request_valid(1024,768,24,2,0,0,4,4,12));
    assert(display_present_request_valid(1024,768,32,3,0,0,1024,768,4096));
    /* Padded source stride above the minimum row is legal. */
    assert(display_present_request_valid(1024,768,32,3,0,0,4,4,4096));

    /* Bounds and overflow negatives. */
    assert(!display_present_request_valid(0,0,32,3,0,0,1,1,4));
    assert(!display_present_request_valid(1024,768,32,3,1023,0,4,4,16));
    assert(!display_present_request_valid(1024,768,32,3,0,767,4,4,16));
    assert(!display_present_request_valid(1024,768,32,3,0,0xffffffffU,4,4,16));
    assert(!display_present_request_valid(1024,768,32,3,0,0,0,4,16));
    assert(!display_present_request_valid(1024,768,32,3,0,0,4,0,16));

    /* Format/bpp agreement and stride floor. */
    assert(!display_present_request_valid(1024,768,32,1,0,0,4,4,16));
    assert(!display_present_request_valid(1024,768,16,3,0,0,4,4,8));
    assert(!display_present_request_valid(1024,768,32,3,0,0,4,4,12));
    assert(!display_present_request_valid(1024,768,32,3,0,0,4,4,0));
    assert(!display_present_request_valid(1024,768,8,0,0,0,4,4,4));
    /* Stride cap keeps height*stride arithmetic bounded. */
    assert(!display_present_request_valid(1024,768,32,3,0,0,4,4,(1U<<21)));
    /* A full-scanout present with exact pitch must remain valid. */
    assert(display_present_request_valid(1024,768,32,3,0,0,1024,768,4096));
    return 0;
}
