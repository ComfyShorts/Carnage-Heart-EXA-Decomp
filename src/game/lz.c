/* "ARZ" decoder for files stored in the game's pack (the game's own is in lz.c).
 *
 * Header (8 bytes): magic "ARZ" or " 3;", a method digit, then the unpacked size (u32).
 * The payload is XORed with 0x72. Method 0 stores the data as-is; method 1 is classic
 * LZSS with a 4 KiB ring buffer that starts zero-filled at position 0xFEE. Flag bytes are
 * read LSB first, with 1 meaning a literal byte. A match is two bytes: a 12-bit ring
 * position (low byte, then the high nibble of the second byte) and a 4-bit length + 3. */
#include "packfile.h"

#include <string.h>

long arz_unpacked_size(const uint8_t *src, size_t len)
{
    if (len < 8 || !(memcmp(src, "ARZ", 3) == 0 || memcmp(src, " 3;", 3) == 0))
        return -1;
    return (long)((uint32_t)src[4] | (uint32_t)src[5] << 8 | (uint32_t)src[6] << 16 |
                  (uint32_t)src[7] << 24);
}

long arz_decode(const uint8_t *src, size_t len, uint8_t *dst, size_t cap)
{
    long size = arz_unpacked_size(src, len);
    if (size < 0 || (size_t)size > cap)
        return -1;
    const uint8_t key = 0x72;
    size_t in = 8, out = 0;

    if (src[3] == '0') {
        if (len - 8 < (size_t)size)
            return -1;
        for (; out < (size_t)size; out++)
            dst[out] = src[in++] ^ key;
        return size;
    }
    if (src[3] != '1')
        return -1;

    uint8_t ring[0x1000];
    memset(ring, 0, sizeof ring);
    unsigned pos = 0xFEE;
    unsigned flags = 0;
    while (out < (size_t)size) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            if (in >= len)
                break;
            flags = (src[in++] ^ key) | 0xFF00;
        }
        if (flags & 1) {
            if (in >= len)
                break;
            uint8_t b = src[in++] ^ key;
            dst[out++] = b;
            ring[pos] = b;
            pos = (pos + 1) & 0xFFF;
        } else {
            if (in + 1 >= len)
                break;
            unsigned lo = src[in++] ^ key, hi = src[in++] ^ key;
            unsigned from = lo | (hi & 0xF0) << 4;
            unsigned count = (hi & 0x0F) + 3;
            for (unsigned k = 0; k < count && out < (size_t)size; k++) {
                uint8_t b = ring[(from + k) & 0xFFF];
                dst[out++] = b;
                ring[pos] = b;
                pos = (pos + 1) & 0xFFF;
            }
        }
    }
    return out == (size_t)size ? size : -1;
}
