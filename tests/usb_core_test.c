#include <assert.h>
#include <string.h>
#include "../kernel/usb_core.h"

static void test_usb_null_and_bounds(void) {
    struct usb_descriptor_summary s;
    uint8_t desc[] = {18, 1, 0, 2, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    assert(usb_validate_descriptors(0, sizeof(desc), &s) == -1);
    assert(usb_validate_descriptors(desc, sizeof(desc), 0) == -1);
    assert(usb_validate_descriptors(desc, 0, &s) == -1);
    assert(usb_validate_descriptors(desc, USB_DESC_LIMIT + 1, &s) == -1);
}

static void test_usb_malformed(void) {
    struct usb_descriptor_summary s;

    /* Too short to contain length and type */
    uint8_t short_desc[] = {1};
    assert(usb_validate_descriptors(short_desc, sizeof(short_desc), &s) == -1);

    /* Descriptor length byte specifies < 2 */
    uint8_t zero_len[] = {0, 1};
    assert(usb_validate_descriptors(zero_len, sizeof(zero_len), &s) == -1);
    uint8_t one_len[] = {1, 1};
    assert(usb_validate_descriptors(one_len, sizeof(one_len), &s) == -1);

    /* Descriptor length extends past buffer */
    uint8_t overflow_desc[] = {10, 1, 0, 0};
    assert(usb_validate_descriptors(overflow_desc, sizeof(overflow_desc), &s) == -1);

    /* Device descriptor (type 1) with length < 18 */
    uint8_t short_dev[] = {17, 1, 0, 2, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    assert(usb_validate_descriptors(short_dev, sizeof(short_dev), &s) == -1);

    /* Config descriptor (type 2) with length < 9 */
    uint8_t short_cfg[] = {8, 2, 8, 0, 0, 1, 0, 0x80};
    assert(usb_validate_descriptors(short_cfg, sizeof(short_cfg), &s) == -1);

    /* Interface descriptor (type 4) with length < 9 */
    uint8_t short_iface[] = {8, 4, 0, 0, 1, 3, 1, 1};
    assert(usb_validate_descriptors(short_iface, sizeof(short_iface), &s) == -1);

    /* Endpoint descriptor (type 5) with length < 7 */
    uint8_t short_ep[] = {6, 5, 0x81, 3, 8, 0};
    assert(usb_validate_descriptors(short_ep, sizeof(short_ep), &s) == -1);

    /* HID descriptor (type 0x21) with length < 6 */
    uint8_t short_hid[] = {5, 0x21, 0x10, 1, 0};
    assert(usb_validate_descriptors(short_hid, sizeof(short_hid), &s) == -1);
}

static void test_usb_duplicate_descriptors(void) {
    struct usb_descriptor_summary s;

    /* Two device descriptors in one stream */
    uint8_t dup_dev[] = {
        18, 1, 0, 2, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        18, 1, 0, 2, 0, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    assert(usb_validate_descriptors(dup_dev, sizeof(dup_dev), &s) == -1);

    /* Config descriptor nested inside config descriptor without completion */
    uint8_t nested_cfg[] = {
        9, 2, 18, 0, 0, 1, 0, 0x80, 50,
        9, 2, 9, 0, 0, 1, 0, 0x80, 50
    };
    assert(usb_validate_descriptors(nested_cfg, sizeof(nested_cfg), &s) == -1);
}

static void test_usb_valid_hierarchy(void) {
    struct usb_descriptor_summary s;

    /* Full standard USB hierarchy:
     * - Device descriptor (18 bytes)
     * - Configuration descriptor (9 bytes, wTotalLength = 9 + 9 + 7 = 25)
     * - Interface descriptor (9 bytes)
     * - Endpoint descriptor (7 bytes)
     * Total = 43 bytes
     */
    uint8_t valid_stream[] = {
        /* Device */
        18, 1, 0x00, 0x02, 0x00, 0x00, 0x00, 64,
        0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 1, 2, 0, 1,
        /* Configuration (wTotalLength = 25) */
        9, 2, 25, 0, 1, 1, 0, 0x80, 50,
        /* Interface */
        9, 4, 0, 0, 1, 0x03, 0x01, 0x01, 0,
        /* Endpoint */
        7, 5, 0x81, 0x03, 8, 0, 10
    };

    memset(&s, 0, sizeof(s));
    assert(usb_validate_descriptors(valid_stream, sizeof(valid_stream), &s) == 0);
    assert(s.count == 4);
    assert(s.device_seen == 1);
    assert(s.configuration_seen == 1);
}

int main(void) {
    test_usb_null_and_bounds();
    test_usb_malformed();
    test_usb_duplicate_descriptors();
    test_usb_valid_hierarchy();
    return 0;
}
