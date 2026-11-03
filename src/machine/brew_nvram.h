#ifndef BREW_NVRAM_H
#define BREW_NVRAM_H
#include <stddef.h>
#include <string.h>

static int brew_native_save_valid(const unsigned char *data, size_t size)
{
    size_t offset = 8;
    unsigned slot;
    if (!data || size < 8 || size > 8 + 15 * (68 + 65536) || memcmp(data, "BREWHLE1", 8))
        return 0;
    for (slot = 0; slot < 15; slot++)
    {
        unsigned length;
        if (offset + 68 > size || !memchr(data + offset, 0, 64))
            return 0;
        length = data[offset + 64] | (data[offset + 65] << 8) |
            (data[offset + 66] << 16) | ((unsigned)data[offset + 67] << 24);
        if (length > 65536 || length > size - offset - 68)
            return 0;
        offset += 68 + length;
    }
    return offset == size;
}
#endif
