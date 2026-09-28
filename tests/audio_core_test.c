#include <assert.h>
#include <string.h>
#include "../kernel/audio_core.h"

static void test_audio_ring_init(void) {
    int16_t store[8];
    struct audio_ring r;

    assert(audio_ring_init(0, store, 4) == -1);
    assert(audio_ring_init(&r, 0, 4) == -1);
    assert(audio_ring_init(&r, store, 0) == -1);
    assert(audio_ring_init(&r, store, AUDIO_RING_MAX_FRAMES + 1) == -1);

    assert(audio_ring_init(&r, store, 8) == 0);
    assert(r.capacity == 8);
    assert(r.read_at == 0);
    assert(r.write_at == 0);
    assert(r.used == 0);
    assert(r.underruns == 0);
    assert(r.overruns == 0);
}

static void test_audio_ring_null_and_zero_ops(void) {
    int16_t store[4];
    int16_t in[4] = {1, 2, 3, 4};
    int16_t out[4] = {0};
    struct audio_ring r;

    assert(audio_ring_init(&r, store, 4) == 0);
    assert(audio_ring_write(0, in, 1) == 0);
    assert(audio_ring_read(0, out, 1) == 0);
    assert(audio_ring_write(&r, 0, 2) == 0);
    assert(audio_ring_read(&r, 0, 2) == 0);
    assert(audio_ring_write(&r, in, 0) == 0);
    assert(audio_ring_read(&r, out, 0) == 0);
    assert(r.used == 0);
    assert(r.overruns == 0);
    assert(r.underruns == 0);
}

static void test_audio_ring_overruns_and_underruns(void) {
    int16_t store[4];
    int16_t in[6] = {10, 20, 30, 40, 50, 60};
    int16_t out[6] = {0};
    struct audio_ring r;

    assert(audio_ring_init(&r, store, 4) == 0);

    /* Write 6 samples into capacity 4: transfers 4, 1 overrun */
    assert(audio_ring_write(&r, in, 6) == 4);
    assert(r.used == 4);
    assert(r.overruns == 1);

    /* Further write when full: transfers 0, overruns becomes 2 */
    assert(audio_ring_write(&r, in, 2) == 0);
    assert(r.used == 4);
    assert(r.overruns == 2);

    /* Read 6 samples from used 4: transfers 4, 1 underrun */
    assert(audio_ring_read(&r, out, 6) == 4);
    assert(r.used == 0);
    assert(r.underruns == 1);
    assert(out[0] == 10 && out[1] == 20 && out[2] == 30 && out[3] == 40);

    /* Further read when empty: transfers 0, underruns becomes 2 */
    assert(audio_ring_read(&r, out, 2) == 0);
    assert(r.used == 0);
    assert(r.underruns == 2);
}

static void test_audio_ring_wraparound(void) {
    int16_t store[4];
    int16_t in1[3] = {1, 2, 3};
    int16_t in2[3] = {4, 5, 6};
    int16_t out[4] = {0};
    struct audio_ring r;

    assert(audio_ring_init(&r, store, 4) == 0);

    /* Write 3: slots 0, 1, 2 */
    assert(audio_ring_write(&r, in1, 3) == 3);
    assert(r.write_at == 3);
    assert(r.used == 3);

    /* Read 2: slots 0, 1 */
    assert(audio_ring_read(&r, out, 2) == 2);
    assert(out[0] == 1 && out[1] == 2);
    assert(r.read_at == 2);
    assert(r.used == 1);

    /* Write 3: slot 3, then wraps to slot 0, slot 1 */
    assert(audio_ring_write(&r, in2, 3) == 3);
    assert(r.write_at == 2);
    assert(r.used == 4);
    assert(r.overruns == 0);

    /* Read 4: reads slots 2, 3, 0 (wrapped), 1 (wrapped) */
    memset(out, 0, sizeof(out));
    assert(audio_ring_read(&r, out, 4) == 4);
    assert(out[0] == 3);
    assert(out[1] == 4);
    assert(out[2] == 5);
    assert(out[3] == 6);
    assert(r.read_at == 2);
    assert(r.used == 0);
    assert(r.underruns == 0);
}

static void test_audio_ring_stress(void) {
    int16_t store[16];
    int16_t block[7];
    int16_t out[7];
    struct audio_ring r;
    int16_t val = 0;

    assert(audio_ring_init(&r, store, 16) == 0);

    for (int cycle = 0; cycle < 100; ++cycle) {
        for (int i = 0; i < 7; ++i)
            block[i] = ++val;
        assert(audio_ring_write(&r, block, 7) == 7);
        memset(out, 0, sizeof(out));
        assert(audio_ring_read(&r, out, 7) == 7);
        for (int i = 0; i < 7; ++i)
            assert(out[i] == block[i]);
    }
    assert(r.used == 0);
    assert(r.overruns == 0);
    assert(r.underruns == 0);
}

int main(void) {
    test_audio_ring_init();
    test_audio_ring_null_and_zero_ops();
    test_audio_ring_overruns_and_underruns();
    test_audio_ring_wraparound();
    test_audio_ring_stress();
    return 0;
}
