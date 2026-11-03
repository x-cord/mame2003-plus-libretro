/***************************************************************************
    Qualcomm BREW application environment, hosted on an ARM7TDMI interpreter.
    ABI references: BREW SDK AEE.h, AEEShell.h, AEEStdLib.h and AEEDisp.h.
    Guest modules execute their own code; system interfaces use ARM trampolines.
***************************************************************************/
#include "cpu/arm7be/arm7core.h"
#include "driver.h"
#include "sound/brew_smaf.h"
#include "state.h"
#include "vidhrdw/brew_font.h"
#include <formats/image.h>
#include <formats/rpng.h>
#include <formats/rjpeg.h>
#include <zlib.h>
#include <stdint.h>
/* rjpeg.c asks libretro-common for host CPU features; the native MAME build
   does not link features_cpu.c, and the decoder works without SIMD. The
   EmulatorJS link already provides features_cpu.o from RetroArch. */
#ifndef __EMSCRIPTEN__
uint64_t cpu_features_get(void) { return 0; }
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BREW_RAM_SIZE 0x08000000
#define BREW_MODULE 0x00010000
#define BREW_HELPERS 0x00300000
#define BREW_VTABLES 0x00301000
#define BREW_OUTPUT 0x00308000
#define BREW_DISPLAY_STATE (BREW_OUTPUT + 0x40)
#define BREW_MEDIA_STATE (BREW_OUTPUT + 0x80)
#define BREW_RESOURCES 0x00400000
#define BREW_EXTRA_BARS 0x00600000
#define BREW_EXTRA_BARS_CAPACITY 0x00200000
#define BREW_EXT_BASE 0x07c00000
#define BREW_EXT_SPACING 0x00100000
#define BREW_EXT_MAX 2
#define BREW_TRAPS 0x10000000
#define BREW_RETURN 0x100ff000
#define BREW_WAKE 0x42524557
#define BREW_WIDTH 240
#define BREW_HEIGHT 400

enum
{
    B_HELPERS,
    B_SHELL,
    B_DISPLAY,
    B_FILEMGR,
    B_FILE,
    B_IMAGE,
    B_BITMAP,
    B_SOUND,
    B_WEB,
    B_HEAP,
    B_FONT,
    B_MEDIA,
    B_GRAPHICS,
    B_STREAM,
    B_WEBUTIL,
    B_RANDOM,
    B_SOUND10,
    B_RAMCACHE,
    B_STUB,
    B_SOURCE,
    B_SOURCEUTIL,
    B_GENERIC,
    B_DIB,
    B_WEBRESP,
    B_NETERROR,
    B_TYPES
};
static UINT32* brew_ram;
static UINT32 brew_heap, brew_shell, brew_module, brew_applet, brew_phase, brew_clock;
static UINT32 brew_timer_fn[64], brew_timer_user[64], brew_timer_due[64], brew_timer_callback[64];
static UINT32 brew_timer_arg[64], brew_timer_owned[64], brew_active_allocation;
static UINT32 brew_media_objects[32];
static UINT32 brew_stream_objects[16];
static UINT32 brew_source_cb[8];
static UINT32 brew_bar_size, brew_bars_size;
struct brew_ext { UINT32 image, entry, module; };
static struct brew_ext brew_exts[BREW_EXT_MAX];
static unsigned brew_ext_count;
static int brew_ext_loading = -1;
struct brew_saved_call { UINT32 regs[16]; UINT32 cpsr, out, lr, classid, extension; int pending; };
static struct brew_saved_call brew_saved_calls[8];
static int brew_call_depth;
static int brew_nested_started;
static void brew_cache_clear_object(UINT32 object);
static UINT32 brew_extension_create(UINT32 classid, UINT32 out);
static UINT32 brew_mod_entry;
static UINT32 brew_device_bitmap;
static UINT32 brew_display_destination;
/* Keep device pixels addressable by IDIB clients as well as host renderers. */
#define BREW_FRAMEBUFFER 0x00380000
#define brew_framebuffer (brew_ram + BREW_FRAMEBUFFER / 4)
static UINT8 brew_seen[B_TYPES][256];
static UINT32 brew_seen_class[256];
static unsigned brew_seen_class_count;
static UINT32 brew_classid;
static UINT32 brew_applet_enum;
static UINT32 brew_keys;
/* Host key-repeat deadlines are transient input state, not guest state. */
static UINT32 brew_direction_repeat[4];
static UINT32 brew_random = 1;
static UINT32 brew_colors[16];
static int brew_enqueue(UINT16 code, UINT16 key, UINT32 data);
enum
{
    BREW_EFAILED = 1, BREW_ENOMEMORY = 2, BREW_ECLASSNOTSUPPORT = 3,
    BREW_EBADSTATE = 13, BREW_EBADPARM = 14, BREW_EUNSUPPORTED = 20,
    BREW_EFILEEXISTS = 0x100, BREW_EFILENOEXISTS = 0x101, BREW_EBADFILENAME = 0x103,
    BREW_EBADSEEKPOS = 0x104, BREW_EFILEEOF = 0x105, BREW_EFSFULL = 0x106,
    BREW_EFILEOPEN = 0x107, BREW_EINVALIDOPERATION = 0x10a
};

static UINT8* brew_ptr(UINT32 p, UINT32 n)
{
    return p <= BREW_RAM_SIZE && n <= BREW_RAM_SIZE - p ? (UINT8*)brew_ram + p : NULL;
}
static const char* brew_string(UINT32 p)
{
    const char* text = (const char*)brew_ptr(p, 1);
    return p && text && memchr(text, 0, BREW_RAM_SIZE - p) ? text : NULL;
}
static UINT32 brew_u32(UINT32 p)
{
    UINT8* b = brew_ptr(p, 4);
    return b ? b[0] | (b[1] << 8) | (b[2] << 16) | ((UINT32)b[3] << 24) : 0;
}
static UINT32 brew_le32(const UINT8* p)
{
    return p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}
static UINT16 brew_le16(const UINT8* p)
{
    return p[0] | (p[1] << 8);
}
static UINT16 brew_u16(UINT32 p)
{
    UINT8* b = brew_ptr(p, 2);
    return b ? b[0] | (b[1] << 8) : 0;
}
static void brew_w32(UINT32 p, UINT32 v)
{
    UINT8* b = brew_ptr(p, 4);
    if (b)
    {
        b[0] = v;
        b[1] = v >> 8;
        b[2] = v >> 16;
        b[3] = v >> 24;
    }
}
static void brew_w16(UINT32 p, UINT16 v)
{
    UINT8* b = brew_ptr(p, 2);
    if (b)
    {
        b[0] = v;
        b[1] = v >> 8;
    }
}
static UINT32 brew_arg(int n)
{
    return n < 4 ? activecpu_get_reg(ARM7_R0 + n) : brew_u32(activecpu_get_reg(ARM7_R13) + (n - 4) * 4);
}
#define BREW_HEAP_BASE 0x00800000
#define BREW_HEAP_LIMIT 0x07c00000
#define BREW_BLOCK_MAGIC 0x42524557
static UINT32 brew_alloc(UINT32 n)
{
    UINT32 block = BREW_HEAP_BASE, bytes;
    if (n > BREW_HEAP_LIMIT - BREW_HEAP_BASE - 16)
        return 0;
    bytes = ((n ? n : 1) + 7) & ~7;
    while (block < brew_heap)
    {
        UINT32 size = brew_u32(block);
        if (brew_u32(block + 8) != BREW_BLOCK_MAGIC || size > brew_heap - block - 16)
            return 0;
        if (!brew_u32(block + 4) && size >= bytes)
        {
            if (size >= bytes + 24)
            {
                UINT32 next = block + 16 + bytes;
                brew_w32(next, size - bytes - 16);
                brew_w32(next + 4, 0);
                brew_w32(next + 8, BREW_BLOCK_MAGIC);
                brew_w32(block, bytes);
            }
            brew_w32(block + 4, 1);
            memset(brew_ptr(block + 16, bytes), 0, bytes);
            return block + 16;
        }
        block += 16 + size;
    }
    if (bytes + 16 > BREW_HEAP_LIMIT - brew_heap)
        return 0;
    brew_w32(block, bytes);
    brew_w32(block + 4, 1);
    brew_w32(block + 8, BREW_BLOCK_MAGIC);
    brew_heap += bytes + 16;
    memset(brew_ptr(block + 16, bytes), 0, bytes);
    return block + 16;
}
static void brew_free(UINT32 p)
{
    UINT32 block = BREW_HEAP_BASE;
    if (p < BREW_HEAP_BASE + 16 || p >= brew_heap || brew_u32(p - 8) != BREW_BLOCK_MAGIC)
        return;
    brew_w32(p - 12, 0);
    while (block < brew_heap)
    {
        UINT32 size = brew_u32(block), next = block + 16 + size;
        if (next < brew_heap && !brew_u32(block + 4) && !brew_u32(next + 4))
        {
            brew_w32(block, size + 16 + brew_u32(next));
            continue;
        }
        if (next == brew_heap && !brew_u32(block + 4))
        {
            brew_heap = block;
            break;
        }
        block = next;
    }
}
static UINT32 brew_realloc(UINT32 p, UINT32 size)
{
    UINT32 result, old;
    if (!p)
        return brew_alloc(size);
    if (!size)
    {
        brew_free(p);
        return 0;
    }
    if (p < BREW_HEAP_BASE + 16 || p >= brew_heap || brew_u32(p - 8) != BREW_BLOCK_MAGIC)
        return 0;
    old = brew_u32(p - 16);
    if (old >= size)
        return p;
    result = brew_alloc(size);
    if (!result)
        return 0;
    memcpy(brew_ptr(result, old), brew_ptr(p, old), old);
    brew_free(p);
    return result;
}
/* Count live blocks rather than the high-water mark: freed interior blocks
   remain available for reuse and must not inflate GetMemStats(). */
static UINT32 brew_heap_used(void)
{
    UINT32 block = BREW_HEAP_BASE, used = 0;
    while (block < brew_heap)
    {
        UINT32 size = brew_u32(block);
        if (brew_u32(block + 8) != BREW_BLOCK_MAGIC || size > brew_heap - block - 16)
            return brew_heap - BREW_HEAP_BASE;
        if (brew_u32(block + 4)) used += size + 16;
        block += size + 16;
    }
    return used;
}
static void brew_timer_clear(unsigned i)
{
    UINT32 callback = brew_timer_callback[i];
    if (callback && brew_u32(callback + 12) == i + 1)
    {
        brew_w32(callback + 8, 0);
        brew_w32(callback + 12, 0);
    }
    if (brew_timer_owned[i])
        brew_free(brew_timer_owned[i]);
    brew_timer_fn[i] = brew_timer_user[i] = brew_timer_due[i] = 0;
    brew_timer_callback[i] = brew_timer_owned[i] = brew_timer_arg[i] = 0;
}
static void brew_cache_clear(void);
static void brew_cache_clear_object(UINT32 object);
static UINT8* brew_bitmap_pixels(UINT32 object, UINT32* width, UINT32* height, UINT32* pitch);
static UINT32 brew_bitmap_new(UINT32 width, UINT32 height, UINT32 device);
static void brew_dib_sync(UINT32 dib);
static UINT32 brew_dib_new(UINT32 width, UINT32 height, UINT32 depth);
static UINT32 brew_bitmap_resolve(UINT32 object);
static UINT32 brew_bitmap_get(UINT32 object, UINT32 x, UINT32 y);
static void brew_bitmap_put(UINT32 object, UINT32 x, UINT32 y, UINT32 color);
static int brew_inflate(const UINT8* input, UINT32 input_size, UINT8** output, UINT32* output_size);
static UINT32 brew_bitmap_device(void);
static UINT32 brew_bitmap_rop(UINT32 old, UINT32 src, UINT32 rop, UINT32 transparent, UINT32 use_transparent);
static void brew_media_cancel_notifications(UINT32 object)
{
    unsigned i;
    for (i = 0; i < 64; i++)
        if (brew_timer_owned[i] && brew_u32(brew_timer_arg[i] + 4) == object)
            brew_timer_clear(i);
}
static UINT32 brew_object(int type)
{
    UINT32 p = brew_alloc(64);
    if (p)
    {
        memset(brew_ptr(p, 64), 0, 64);
        brew_w32(p, BREW_VTABLES + type * 1024);
        brew_w32(p + 60, 1);
    }
    return p;
}
/* Display font slots occupy the previously reserved object words 24..56.
   Font objects retain their class at +4; +8 identifies default Unicode fonts. */
#define BREW_FONT_FIRST 0x8000
#define BREW_FONT_COUNT 9
static int brew_is_object(UINT32 object, unsigned type)
{
    return object >= BREW_HEAP_BASE + 16 && object < brew_heap
        && brew_ptr(object, 64) && brew_u32(object - 8) == BREW_BLOCK_MAGIC
        && brew_u32(object - 12) && brew_u32(object + 60)
        && brew_u32(object) == BREW_VTABLES + type * 1024;
}
static UINT32 brew_ref(UINT32 object, unsigned type, unsigned slot)
{
    UINT32 refs = brew_u32(object + 60);
    if (!brew_is_object(object, type))
        return 0;
    refs = slot == 0 ? refs + 1 : refs ? refs - 1 : 0;
    brew_w32(object + 60, refs);
    if (!refs)
    {
        if (type == B_DISPLAY)
        {
            unsigned i;
            if (brew_u32(BREW_DISPLAY_STATE) == object)
                brew_w32(BREW_DISPLAY_STATE, 0);
            for (i = 0; i < BREW_FONT_COUNT; i++)
                brew_ref(brew_u32(object + 24 + i * 4), B_FONT, 1);
        }
        if (type == B_IMAGE)
            brew_free(brew_u32(object + 16));
        if (type == B_DIB)
        {
            brew_ref(brew_u32(object + 40), B_BITMAP, 1);
            if (brew_display_destination == object)
                brew_display_destination = 0;
        }
        if (type == B_BITMAP)
        {
            if (!brew_u32(object + 20) && brew_u32(object + 12))
                brew_free(brew_u32(object + 12));
            if (brew_u32(object + 36))
                brew_free(brew_u32(object + 36));
            if (brew_device_bitmap == object)
                brew_device_bitmap = 0;
            if (brew_display_destination == object)
                brew_display_destination = 0;
        }
        if (type == B_MEDIA)
        {
            unsigned i;
            for (i = 0; i < 32; i++)
                if (brew_media_objects[i] == object)
                {
                    brew_smaf_stop(i);
                    brew_media_objects[i] = 0;
                }
            brew_media_cancel_notifications(object);
        }
        if (type == B_RAMCACHE)
            brew_cache_clear_object(object);
        if (type == B_STREAM)
        {
            unsigned i;
            if (brew_u32(object + 36) == 1)
            {
                UINT32 source = brew_u32(object + 40);
                if (brew_u32(object + 8))
                    brew_free(brew_u32(object + 8));
                if (source)
                    brew_ref(source, brew_u32(object + 44) == 1 ? B_FILE : B_STREAM, 1);
            }
            else
            {
                UINT32 source = brew_u32(object + 40);
                if (brew_u32(object + 4))
                    brew_free(brew_u32(object + 4));
                if (source)
                    brew_ref(source, brew_u32(object + 44) == 1 ? B_FILE : B_STREAM, 1);
            }
            for (i = 0; i < 16; i++)
                if (brew_stream_objects[i] == object)
                    brew_stream_objects[i] = 0;
        }
        brew_free(object);
    }
    return refs;
}

static UINT32 brew_reference(unsigned type, unsigned slot)
{
    return brew_ref(brew_arg(0), type, slot);
}
static UINT32 brew_display_font(UINT32 display, UINT32 id)
{
    UINT32 at, font;
    if (id < BREW_FONT_FIRST || id - BREW_FONT_FIRST >= BREW_FONT_COUNT)
        return 0;
    at = display + 24 + (id - BREW_FONT_FIRST) * 4;
    font = brew_u32(at);
    if (!brew_is_object(font, B_FONT))
    {
        font = brew_object(B_FONT);
        if (font)
        {
            brew_w32(font + 4, 0x01001022); /* IFont / default Unicode font. */
            brew_w32(font + 8, 1);
            brew_w32(at, font);
        }
    }
    return font;
}

/* The reserved HLE area is already included in the guest RAM save state. */
static void brew_get_clip(UINT32 display, int* left, int* top, int* right, int* bottom)
{
    if (!display)
        display = brew_u32(BREW_DISPLAY_STATE);
    if (!display || !brew_ptr(display, 64) || brew_u32(display) != BREW_VTABLES + B_DISPLAY * 1024)
    {
        *left = *top = 0;
        *right = BREW_WIDTH;
        *bottom = BREW_HEIGHT;
        return;
    }
    if (brew_u32(display + 20) != 0x434c4950)
    {
        brew_w32(display + 4, 0);
        brew_w32(display + 8, 0);
        brew_w32(display + 12, BREW_WIDTH);
        brew_w32(display + 16, BREW_HEIGHT);
        brew_w32(display + 20, 0x434c4950);
    }
    *left = brew_u32(display + 4);
    *top = brew_u32(display + 8);
    *right = brew_u32(display + 12);
    *bottom = brew_u32(display + 16);
}

static void brew_prepare_call(UINT32 fn, UINT32 a, UINT32 b, UINT32 c, UINT32 d)
{
    activecpu_set_reg(ARM7_R0, a);
    activecpu_set_reg(ARM7_R1, b);
    activecpu_set_reg(ARM7_R2, c);
    activecpu_set_reg(ARM7_R3, d);
    activecpu_set_reg(ARM7_R12, fn);
    activecpu_set_reg(ARM7_R14, BREW_RETURN);
}
static int brew_unzip_prepare(UINT32 object);
static UINT32 brew_bitmap_new(UINT32 width, UINT32 height, UINT32 device);
static UINT32 brew_resource_in(UINT32 base, UINT32 length, UINT32 id, UINT32 type, UINT32* size)
{
    UINT32 i, count = brew_u16(base + 6);
    UINT32 index = brew_u32(base + 8), table = brew_u32(base + 16);
    if (size)
        *size = 0;
    if (index > length || count > (length - index) / 8 || table > length)
        return 0;
    for (i = 0; i < count; i++)
    {
        UINT32 p = base + index + i * 8, first = brew_u16(p + 2);
        if (brew_u16(p) == type && id >= first && id - first <= brew_u16(p + 4))
        {
            UINT32 section = brew_u16(p + 6) + id - first;
            UINT32 start, end;
            if ((length - table) / 4 < 2 || section >= (length - table) / 4 - 1)
                return 0;
            start = brew_u32(base + table + section * 4);
            end = brew_u32(base + table + (section + 1) * 4);
            if (end < start || end > length)
                return 0;
            if (size)
                *size = end - start;
            return base + start;
        }
    }
    return 0;
}
static const char* brew_path_basename(const char* path)
{
    const char* base = path;
    for (; path && *path; path++)
        if (*path == '/' || *path == '\\')
            base = path + 1;
    return base;
}
static UINT32 brew_extra_bar(const char* name, UINT32* length)
{
    UINT32 count, table, i;
    const char* base;
    if (!brew_bars_size || brew_bars_size < 12 || brew_le32(brew_ptr(BREW_EXTRA_BARS, 4)) != 0x53524142)
        return 0;
    base = brew_path_basename(name);
    count = brew_u32(BREW_EXTRA_BARS + 4);
    table = brew_u32(BREW_EXTRA_BARS + 8);
    if (table > brew_bars_size || count > (brew_bars_size - table) / 12)
        return 0;
    for (i = 0; i < count; i++)
    {
        UINT32 row = BREW_EXTRA_BARS + table + i * 12;
        UINT32 name_off = brew_u32(row), data_off = brew_u32(row + 4), size = brew_u32(row + 8);
        const char* entry = brew_string(BREW_EXTRA_BARS + name_off);
        if (!entry || !brew_ptr(BREW_EXTRA_BARS + data_off, size))
            continue;
        if (!strcasecmp(entry, base))
        {
            *length = size;
            return BREW_EXTRA_BARS + data_off;
        }
    }
    return 0;
}
static UINT32 brew_resource(const char* file, UINT32 id, UINT32 type, UINT32* size)
{
    if (file && *file)
    {
        UINT32 length = 0, base = brew_extra_bar(file, &length);
        if (base)
            return brew_resource_in(base, length, id, type, size);
    }
    return brew_resource_in(BREW_RESOURCES, brew_bar_size, id, type, size);
}

static int brew_decode_bmp(const UINT8* data, UINT32 size, UINT32* out_width, UINT32* out_height, UINT32* out_pixels)
{
    UINT32 offset, width, height, bpp, header, rowbytes, x, y, stride;
    UINT32 pixels;
    if (!data || size < 54 || data[0] != 'B' || data[1] != 'M')
        return 0;
    offset = brew_le32(data + 10);
    header = brew_le32(data + 14);
    width = brew_le32(data + 18);
    height = (INT32)brew_le32(data + 22);
    bpp = brew_le16(data + 28);
    if (width == 0 || width > 4096 || height == 0 || (height < 0 ? -height : height) > 4096
        || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) || header < 40 || offset >= size)
        return 0;
    stride = height < 0 ? -height : height;
    rowbytes = ((width * bpp + 31) / 32) * 4;
    if (rowbytes > size || offset > size - rowbytes * stride)
        return 0;
    pixels = brew_alloc(width * stride * 4);
    if (!pixels)
        return 0;
    for (y = 0; y < stride; y++)
    {
        const UINT8* row = data + offset + (height > 0 ? stride - 1 - y : y) * rowbytes;
        for (x = 0; x < width; x++)
        {
            UINT32 color;
            if (bpp <= 8)
            {
                UINT32 index = bpp == 8 ? row[x]
                    : bpp == 4 ? ((row[x / 2] >> (x & 1 ? 0 : 4)) & 15)
                               : ((row[x / 8] >> (7 - (x & 7))) & 1);
                UINT32 palette = 14 + header + index * 4;
                color = palette + 4 <= size ? (0xff000000 | (data[palette + 2] << 16)
                        | (data[palette + 1] << 8) | data[palette])
                                            : 0xff000000;
            }
            else if (bpp == 24)
                color = 0xff000000 | (row[x * 3 + 2] << 16) | (row[x * 3 + 1] << 8) | row[x * 3];
            else
                color = (row[x * 4 + 3] << 24) | (row[x * 4 + 2] << 16) | (row[x * 4 + 1] << 8) | row[x * 4];
            brew_w32(pixels + (y * width + x) * 4, color);
        }
    }
    *out_width = width;
    *out_height = stride;
    *out_pixels = pixels;
    return 1;
}
static UINT32 brew_load_image(const char* file, UINT32 id)
{
    UINT32 size, resource = brew_resource(file, id, 6, &size), object = 0, at;
    UINT32* pixels = NULL;
    unsigned width = 0, height = 0;
    int result;
    rpng_t* decoder = NULL;
    if (!resource || size < 20)
        return 0;
    at = brew_u16(resource);
    if (at > size || size - at < 54)
        return 0;
    {
        const UINT8* head = (const UINT8*)brew_ptr(resource + at, 4);
        if (head && head[0] == 0xff && head[1] == 0xd8)
        {
            rjpeg_t* jpeg = rjpeg_alloc();
            if (jpeg && rjpeg_set_buf_ptr(jpeg, brew_ptr(resource + at, size - at))
                && rjpeg_process_image(jpeg, (void**)&pixels, size - at, &width, &height) && pixels && width
                && height && width <= 4096 && height <= 4096)
            {
                at = brew_alloc(width * height * 4);
                if (at)
                {
                    memcpy(brew_ptr(at, width * height * 4), pixels, width * height * 4);
                    free(pixels);
                    pixels = NULL;
                    rjpeg_free(jpeg);
                    goto make_object;
                }
            }
            free(pixels);
            rjpeg_free(jpeg);
            goto done;
        }
    }
    if (brew_decode_bmp(brew_ptr(resource + at, size - at), size - at, &width, &height, &at))
        goto make_object;
    decoder = rpng_alloc();
    if (!decoder || !rpng_set_buf_ptr(decoder, brew_ptr(resource + at, size - at), size - at)
        || !rpng_start(decoder))
        goto done;
    while (rpng_iterate_image(decoder))
        ;
    if (!rpng_is_valid(decoder))
        goto done;
    do
    {
        result = rpng_process_image(decoder, (void**)&pixels, size - at, &width, &height);
    } while (result == IMAGE_PROCESS_NEXT);
    if (result == IMAGE_PROCESS_ERROR || result == IMAGE_PROCESS_ERROR_END || !pixels || !width || !height
        || width > 4096 || height > 4096)
        goto done;
    at = brew_alloc(width * height * 4);
    if (!at)
        goto done;
    memcpy(brew_ptr(at, width * height * 4), pixels, width * height * 4);
make_object:
    object = brew_object(B_IMAGE);
    if (!object)
    {
        brew_free(at);
        goto done;
    }
    brew_w32(object + 4, id);
    brew_w32(object + 8, width);
    brew_w32(object + 12, height);
    brew_w32(object + 16, at);
    brew_w32(object + 28, width);
    brew_w32(object + 32, height);
    brew_w32(object + 36, width);
done:
    rpng_free(decoder);
    free(pixels);
    return object;
}
static UINT32 brew_decode_stream_image(UINT32 object, UINT32 source)
{
    UINT32 buffer = 0, size = 0, start = 0, width = 0, height = 0, at, classid;
    void* pixels = NULL;
    int ok = 0;
    if (!source || !brew_is_object(source, B_STREAM))
        return 0;
    if (brew_u32(source + 36) == 1)
    {
        if (!brew_unzip_prepare(source))
            return 0;
        buffer = brew_u32(source + 8);
        size = brew_u32(source + 12);
    }
    else
    {
        buffer = brew_u32(source + 4);
        size = brew_u32(source + 8);
        start = brew_u32(source + 16);
    }
    if (!buffer || start >= size || !brew_ptr(buffer + start, size - start))
        return 0;
    classid = brew_u32(object + 4);
    if (classid == 0x01004004 || (classid != 0x01004005 && size - start >= 8
        && brew_ptr(buffer + start, 8)[0] == 0x89 && brew_ptr(buffer + start, 8)[1] == 'P'))
    {
        rpng_t* decoder = rpng_alloc();
        int result;
        if (decoder && rpng_set_buf_ptr(decoder, brew_ptr(buffer + start, size - start), size - start)
            && rpng_start(decoder))
        {
            while (rpng_iterate_image(decoder))
                ;
            if (rpng_is_valid(decoder))
            {
                do
                {
                    result = rpng_process_image(decoder, &pixels, size - start, &width, &height);
                } while (result == IMAGE_PROCESS_NEXT);
                ok = result != IMAGE_PROCESS_ERROR && result != IMAGE_PROCESS_ERROR_END && pixels && width && height;
            }
        }
        rpng_free(decoder);
    }
    else
    {
        rjpeg_t* decoder = rjpeg_alloc();
        if (decoder && rjpeg_set_buf_ptr(decoder, brew_ptr(buffer + start, size - start)))
            ok = rjpeg_process_image(decoder, &pixels, size - start, &width, &height) != 0 && pixels && width && height;
        rjpeg_free(decoder);
    }
    if (!ok || width > 4096 || height > 4096)
    {
        free(pixels);
        return 0;
    }
    at = brew_alloc(width * height * 4);
    if (!at)
    {
        free(pixels);
        return 0;
    }
    memcpy(brew_ptr(at, width * height * 4), pixels, width * height * 4);
    free(pixels);
    if (brew_u32(object + 16))
        brew_free(brew_u32(object + 16));
    brew_w32(object + 8, width);
    brew_w32(object + 12, height);
    brew_w32(object + 16, at);
    brew_w32(object + 20, 0);
    brew_w32(object + 24, 0);
    brew_w32(object + 28, width);
    brew_w32(object + 32, height);
    brew_w32(object + 36, width);
    return object;
}
static UINT32 brew_load_bitmap(const char* file, UINT32 id)
{
    UINT32 image = brew_load_image(file, id), bitmap, width, height, pitch, x, y;
    UINT8* pixels;
    UINT32* source;
    if (!image)
        return 0;
    width = brew_u32(image + 8);
    height = brew_u32(image + 12);
    source = (UINT32*)brew_ptr(brew_u32(image + 16), width * height * 4);
    bitmap = brew_bitmap_new(width, height, 0);
    if (!bitmap || !source)
    {
        if (bitmap)
            brew_ref(bitmap, B_BITMAP, 1);
        brew_ref(image, B_IMAGE, 1);
        return 0;
    }
    pixels = brew_bitmap_pixels(bitmap, &width, &height, &pitch);
    if (pixels)
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                ((UINT32*)(pixels + y * pitch))[x] = source[y * width + x] & 0xffffff;
    brew_ref(image, B_IMAGE, 1);
    return bitmap;
}
static UINT32 brew_image_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), c = brew_arg(3);
    UINT32 width = brew_u32(object + 8), height = brew_u32(object + 12), pixels = brew_u32(object + 16);
    if (slot < 2)
        return 1;
    if (slot == 4)
    {
        brew_w16(a, width);
        brew_w16(a + 2, height);
        brew_w16(a + 4, 0);
        brew_w16(a + 6, 0);
        brew_w16(a + 8, brew_u32(object + 36));
    }
    else if (slot == 5)
    {
        if (a == 0)
        {
            brew_w32(object + 28, b);
            brew_w32(object + 32, c);
        }
        if (a == 1)
        {
            brew_w32(object + 20, b);
            brew_w32(object + 24, c);
        }
        if (a == 2)
            brew_w32(object + 36, b);
        if (a == 4 && b)
            brew_w32(object + 36, width / b);
        if (a == 8)
            brew_w32(object + 40, b);
    }
    else if (slot == 8)
    {
        if (a && brew_is_object(a, B_STREAM))
            brew_decode_stream_image(object, a);
    }
    else if (slot == 2 || slot == 3)
    {
        int x0 = slot == 2 ? (INT32)a : (INT32)b, y0 = slot == 2 ? (INT32)b : (INT32)c;
        int frame = slot == 3 ? a : 0, x, y;
        int left, top, right, bottom;
        int sx = (INT32)brew_u32(object + 20) + frame * brew_u32(object + 36), sy = brew_u32(object + 24);
        int w = brew_u32(object + 28), h = brew_u32(object + 32);
        if (w > 4096 || h > 4096)
            return 14;
        brew_get_clip(brew_u32(object + 40), &left, &top, &right, &bottom);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
            {
                int dx = x0 + x, dy = y0 + y;
                UINT32 color;
                if (dx < left || dy < top || dx >= right || dy >= bottom || sx + x < 0 || sx + x >= width
                    || sy + y < 0 || sy + y >= height)
                    continue;
                color = brew_u32(pixels + ((sy + y) * width + sx + x) * 4);
                if ((color >> 24) == 255)
                    brew_framebuffer[dy * BREW_WIDTH + dx] = color & 0xffffff;
                else if (color >> 24)
                {
                    UINT32 alpha = color >> 24, old = brew_framebuffer[dy * BREW_WIDTH + dx];
                    UINT32 red = (((color >> 16) & 255) * alpha + ((old >> 16) & 255) * (255 - alpha) + 127) / 255;
                    UINT32 green = (((color >> 8) & 255) * alpha + ((old >> 8) & 255) * (255 - alpha) + 127) / 255;
                    UINT32 blue = ((color & 255) * alpha + (old & 255) * (255 - alpha) + 127) / 255;
                    brew_framebuffer[dy * BREW_WIDTH + dx] = (red << 16) | (green << 8) | blue;
                }
            }
    }
    return 0;
}

#define BREW_FILES 16
#define BREW_FILE_CAPACITY (2 * 1024 * 1024)
static char brew_file_names[BREW_FILES][64];
static UINT32 brew_file_sizes[BREW_FILES];
static UINT8 brew_file_data[BREW_FILES][BREW_FILE_CAPACITY];
static UINT32 brew_file_error;
static int brew_file_find(const char* name)
{
    int i;
    for (i = 0; i < BREW_FILES; i++)
        if (brew_file_names[i][0] && !strcmp(name, brew_file_names[i]))
            return i;
    return -1;
}
static int brew_file_opened(unsigned index)
{
    UINT32 block = BREW_HEAP_BASE;
    while (block < brew_heap)
    {
        UINT32 size = brew_u32(block), object = block + 16;
        if (brew_u32(block + 8) != BREW_BLOCK_MAGIC || size > brew_heap - block - 16)
            break;
        if (brew_u32(block + 4) && size >= 64 && brew_u32(object) == BREW_VTABLES + B_FILE * 1024
            && brew_u32(object + 4) == index && brew_u32(object + 60))
            return 1;
        block += 16 + size;
    }
    return 0;
}
static UINT32 brew_file_info(unsigned index, UINT32 output)
{
    UINT8* info = output ? brew_ptr(output, 76) : NULL;
    if (!info)
        return brew_file_error = BREW_EBADPARM;
    memset(info, 0, 76);
    info[0] = index == 0 ? 0x14 : 0;
    brew_w32(output + 8, brew_file_sizes[index]);
    memcpy(info + 12, brew_file_names[index], 64);
    return brew_file_error = 0;
}
static UINT32 brew_file_fill_ex(unsigned index, UINT32 ex)
{
    UINT32 name_ptr, max, n;
    if (!ex || !brew_ptr(ex, 40))
        return BREW_EBADPARM;
    brew_w32(ex + 8, 0);
    brew_w32(ex + 12, brew_file_sizes[index]);
    brew_ptr(ex, 40)[4] = index == 0 ? 0x14 : 0;
    name_ptr = brew_u32(ex + 16);
    max = brew_u32(ex + 20);
    if (name_ptr && max && brew_ptr(name_ptr, max))
    {
        n = strlen(brew_file_names[index]);
        if (n >= max)
            n = max - 1;
        memset(brew_ptr(name_ptr, max), 0, max);
        memcpy(brew_ptr(name_ptr, max), brew_file_names[index], n);
    }
    return 0;
}
static UINT32 brew_filemgr_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), p;
    const char* name = brew_string(a);
    int i = name ? brew_file_find(name) : -1;
    switch (slot)
    {
    case 0:
    case 1:
        return 1;
    case 5: /* MkDir: flat VFS has no directories. */
    case 6: /* RmDir */
        return brew_file_error = 0;
    case 10: /* EnumInit: enumerate the flat VFS files. */
        brew_w32(object + 4, 1);
        brew_w32(object + 8, 1);
        return brew_file_error = 0;
    case 2:
        b &= ~0x8000;
        if (!name || !*name || strlen(name) >= 64)
        {
            brew_file_error = BREW_EBADFILENAME;
            return 0;
        }
        if (b != 1 && b != 2 && b != 4 && b != 8)
        {
            brew_file_error = BREW_EBADPARM;
            return 0;
        }
        if (i == 0 && b != 1)
        {
            brew_file_error = BREW_EINVALIDOPERATION;
            return 0;
        }
        if (b == 4 && i >= 0)
        {
            brew_file_error = BREW_EFILEEXISTS;
            return 0;
        }
        if (i < 0 && b != 4)
        {
            brew_file_error = BREW_EFILENOEXISTS;
            return 0;
        }
        if (i < 0)
        {
            for (i = 1; i < BREW_FILES; i++)
                if (!brew_file_names[i][0])
                    break;
            if (i == BREW_FILES)
            {
                brew_file_error = BREW_EFSFULL;
                return 0;
            }
        }
        p = brew_object(B_FILE);
        if (!p)
        {
            brew_file_error = BREW_ENOMEMORY;
            return 0;
        }
        if (b == 4)
        {
            strcpy(brew_file_names[i], name);
            brew_file_sizes[i] = 0;
            memset(brew_file_data[i], 0, BREW_FILE_CAPACITY);
        }
        brew_w32(p + 4, i);
        brew_w32(p + 8, (b & 8) ? brew_file_sizes[i] : 0);
        brew_w32(p + 12, b);
        brew_file_error = 0;
        return p;
    case 3:
        if (i < 0)
            return brew_file_error = BREW_EFILENOEXISTS;
        return brew_file_info(i, b);
    case 4:
        if (i == 0)
            return brew_file_error = BREW_EINVALIDOPERATION;
        if (i >= 0)
        {
            if (brew_file_opened(i))
                return brew_file_error = BREW_EFILEOPEN;
            brew_file_names[i][0] = 0;
            brew_file_sizes[i] = 0;
            memset(brew_file_data[i], 0, BREW_FILE_CAPACITY);
            return brew_file_error = 0;
        }
        return brew_file_error = BREW_EFILENOEXISTS;
    case 7:
        brew_file_error = i < 0 ? 0x101 : 0;
        return brew_file_error;
    case 8:
    {
        UINT32 free_space = 0, total = (BREW_FILES - 1) * BREW_FILE_CAPACITY;
        for (i = 1; i < BREW_FILES; i++)
            free_space += BREW_FILE_CAPACITY - brew_file_sizes[i];
        if (a)
            brew_w32(a, total);
        return free_space;
    }
    case 9:
        return brew_file_error;
    case 11: /* EnumNext */
    {
        unsigned cursor = brew_u32(object + 4);
        if (!brew_u32(object + 8))
            return 0;
        while (cursor < BREW_FILES && (!brew_file_names[cursor][0] || cursor == 0))
            cursor++;
        if (cursor >= BREW_FILES)
        {
            brew_w32(object + 8, 0);
            return 0;
        }
        brew_w32(object + 4, cursor + 1);
        if (brew_file_info(cursor, a))
            return 0;
        return 1;
    }
    case 13: /* EnumNextEx */
    {
        unsigned cursor = brew_u32(object + 4);
        if (!brew_u32(object + 8))
            return 0;
        while (cursor < BREW_FILES && (!brew_file_names[cursor][0] || cursor == 0))
            cursor++;
        if (cursor >= BREW_FILES)
        {
            brew_w32(object + 8, 0);
            return 0;
        }
        brew_w32(object + 4, cursor + 1);
        if (brew_file_fill_ex(cursor, a))
            return 0;
        return 1;
    }
    case 14: /* SetDescription: stored nowhere. */
        return brew_file_error = 0;
    case 15: /* GetInfoEx */
        if (i < 0)
            return brew_file_error = BREW_EFILENOEXISTS;
        return brew_file_error = brew_file_fill_ex(i, b);
    case 16: /* Use */
        return brew_file_error = 0;
    case 18: /* ResolvePath */
    {
        UINT32 out_len = brew_arg(3), n;
        const char* in = brew_string(a);
        if (!in || !b || !brew_ptr(b, 1))
            return brew_file_error = BREW_EBADPARM;
        n = strlen(in) + 1;
        if (!brew_ptr(b, n))
            return brew_file_error = BREW_EBADPARM;
        memcpy(brew_ptr(b, n), in, n);
        if (out_len && brew_ptr(out_len, 4))
            brew_w32(out_len, n);
        return brew_file_error = 0;
    }
    case 19: /* CheckPathAccess */
    {
        UINT32 actual = brew_arg(3);
        if (actual && brew_ptr(actual, 4))
            brew_w32(actual, b);
        return brew_file_error = 0;
    }
    case 20: /* GetFreeSpaceEx */
    {
        UINT32 total = b, free_space = 0;
        UINT32 limit = (BREW_FILES - 1) * BREW_FILE_CAPACITY;
        for (i = 1; i < BREW_FILES; i++)
            free_space += BREW_FILE_CAPACITY - brew_file_sizes[i];
        if (total && brew_ptr(total, 4))
            brew_w32(total, limit);
        return free_space;
    }
    case 12:
    {
        const char* dest = brew_string(b);
        if (i < 0)
            return brew_file_error = BREW_EFILENOEXISTS;
        if (!dest || !*dest || strlen(dest) >= 64)
            return brew_file_error = BREW_EBADFILENAME;
        if (i == 0)
            return brew_file_error = BREW_EINVALIDOPERATION;
        if (!strcmp(name, dest))
            return brew_file_error = 0;
        if (brew_file_find(dest) >= 0)
            return brew_file_error = BREW_EFILEEXISTS;
        if (brew_file_opened(i))
            return brew_file_error = BREW_EFILEOPEN;
        memset(brew_file_names[i], 0, 64);
        strcpy(brew_file_names[i], dest);
        return brew_file_error = 0;
    }
    default:
        return BREW_EUNSUPPORTED;
    }
}
static UINT32 brew_file_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), pos = brew_u32(object + 8),
           index = brew_u32(object + 4), n;
    if (index >= BREW_FILES || !brew_file_names[index][0])
    {
        brew_file_error = BREW_EFILENOEXISTS;
        return 0;
    }
    switch (slot)
    {
    case 0:
    case 1:
        return 1;
    case 3:
        n = pos < brew_file_sizes[index] ? brew_file_sizes[index] - pos : 0;
        if (n > b)
            n = b;
        if (b && (!a || !brew_ptr(a, n)))
        {
            brew_file_error = BREW_EBADPARM;
            return 0;
        }
        if (n)
            memcpy(brew_ptr(a, n), brew_file_data[index] + pos, n);
        brew_w32(object + 8, pos + n);
        brew_file_error = b && !n ? BREW_EFILEEOF : 0;
        return n;
    case 5:
        if (index == 0 || !(brew_u32(object + 12) & 14))
        {
            brew_file_error = BREW_EINVALIDOPERATION;
            return 0;
        }
        if (b && (!a || !brew_ptr(a, b)))
        {
            brew_file_error = BREW_EBADPARM;
            return 0;
        }
        if (pos > BREW_FILE_CAPACITY || b > BREW_FILE_CAPACITY - pos)
        {
            brew_file_error = 0x106;
            return 0;
        }
        if (pos > brew_file_sizes[index])
            memset(brew_file_data[index] + brew_file_sizes[index], 0, pos - brew_file_sizes[index]);
        memcpy(brew_file_data[index] + pos, brew_ptr(a, b), b);
        pos += b;
        brew_w32(object + 8, pos);
        if (pos > brew_file_sizes[index])
            brew_file_sizes[index] = pos;
        brew_file_error = 0;
        return b;
    case 6:
        return brew_file_info(index, a);
    case 7:
    {
        INT64 next = (INT64)(a == 1 ? brew_file_sizes[index] : a == 2 ? pos : 0) + (INT32)b;
        if (a > 2 || next < 0 || next > BREW_FILE_CAPACITY
            || (!(brew_u32(object + 12) & 14) && next > brew_file_sizes[index]))
        {
            brew_file_error = BREW_EBADSEEKPOS;
            return BREW_EFAILED;
        }
        if (a == 2 && !b)
        {
            brew_file_error = 0;
            return pos;
        }
        pos = next;
        if (pos > brew_file_sizes[index])
        {
            memset(brew_file_data[index] + brew_file_sizes[index], 0, pos - brew_file_sizes[index]);
            brew_file_sizes[index] = pos;
        }
        brew_w32(object + 8, pos);
        brew_file_error = 0;
        return 0;
    }
    case 8:
        if (!index || !(brew_u32(object + 12) & 14))
        {
            brew_file_error = BREW_EINVALIDOPERATION;
            return BREW_EFAILED;
        }
        if (a > brew_file_sizes[index])
        {
            brew_file_error = BREW_EBADSEEKPOS;
            return BREW_EFAILED;
        }
        memset(brew_file_data[index] + a, 0, brew_file_sizes[index] - a);
        brew_file_sizes[index] = a;
        brew_w32(object + 8, a);
        brew_file_error = 0;
        return 0;
    case 11:
        brew_file_error = BREW_EUNSUPPORTED;
        return 0;
    default:
        return BREW_EUNSUPPORTED;
    }
}

/* ARM varargs remain guest words; host printf must never interpret a guest pointer.
   A non-zero `list` is a guest pointer to a packed AEEOldVaList word array. */
static UINT32 brew_format_impl(UINT32 dest, UINT32 format, int argument, UINT32 list)
{
    const char* fmt = brew_string(format);
    UINT32 listcursor = list;
    UINT32 out = 0, limit = dest < BREW_RAM_SIZE ? BREW_RAM_SIZE - dest : 0;
    if (!fmt || !dest || !limit)
        return 0;
    if (limit > 65536)
        limit = 65536;
    while (*fmt && out + 1 < limit)
    {
        char temp[1024], spec[32];
        unsigned n = 0, length = 0;
        UINT32 value;
        if (*fmt != '%')
        {
            brew_ptr(dest, limit)[out++] = *fmt++;
            continue;
        }
        spec[n++] = *fmt++;
        if (*fmt == '%')
        {
            brew_ptr(dest, limit)[out++] = *fmt++;
            continue;
        }
        while (*fmt && strchr("-+ #0.123456789", *fmt) && n + 2 < sizeof(spec))
            spec[n++] = *fmt++;
        while (*fmt == 'l' || *fmt == 'h')
        {
            fmt++;
        }
        if (!*fmt || !strchr("diuxXocps", *fmt))
            return 0;
        if (listcursor)
        {
            value = brew_u32(listcursor);
            listcursor += 4;
        }
        else
            value = brew_arg(argument++);
        if (*fmt == 'p')
        {
            spec[n++] = 'x';
            fmt++;
        }
        else
            spec[n++] = *fmt++;
        spec[n] = 0;
        if (spec[n - 1] == 's')
        {
            const char* str = brew_string(value);
            snprintf(temp, sizeof(temp), spec, str ? str : "");
        }
        else if (spec[n - 1] == 'd' || spec[n - 1] == 'i')
            snprintf(temp, sizeof(temp), spec, (INT32)value);
        else
            snprintf(temp, sizeof(temp), spec, value);
        temp[sizeof(temp) - 1] = 0;
        length = strlen(temp);
        if (length > limit - out - 1)
            length = limit - out - 1;
        memcpy(brew_ptr(dest + out, length), temp, length);
        out += length;
    }
    brew_ptr(dest, limit)[out] = 0;
    return out;
}
static UINT32 brew_format(UINT32 dest, UINT32 format, int argument)
{
    return brew_format_impl(dest, format, argument, 0);
}
static UINT32 brew_vformat(UINT32 dest, UINT32 format, UINT32 list)
{
    return brew_format_impl(dest, format, 0, list);
}
static UINT32 brew_wlen(UINT32 p)
{
    UINT32 n = 0;
    if (p >= BREW_RAM_SIZE)
        return 0;
    while (p + n * 2 + 2 <= BREW_RAM_SIZE && brew_u16(p + n * 2))
        n++;
    return n;
}
static void brew_wcopy(UINT32 dest, UINT32 source, UINT32 count)
{
    UINT32 n;
    if (!dest || dest >= BREW_RAM_SIZE)
        return;
    for (n = 0; n < count; n++)
    {
        UINT16 ch = source && source + n * 2 + 2 <= BREW_RAM_SIZE ? brew_u16(source + n * 2) : 0;
        if (dest + n * 2 + 2 > BREW_RAM_SIZE)
            return;
        brew_w16(dest + n * 2, ch);
        if (!ch)
            return;
    }
    if (dest + count * 2 + 2 <= BREW_RAM_SIZE)
        brew_w16(dest + count * 2, 0);
}
static int brew_wcmp(UINT32 a, UINT32 b, UINT32 n, int ci)
{
    UINT32 k;
    for (k = 0; n == 0 || k < n; k++)
    {
        UINT16 x = brew_u16(a + k * 2), y = brew_u16(b + k * 2);
        if (ci)
        {
            if (x >= 'A' && x <= 'Z')
                x += 'a' - 'A';
            if (y >= 'A' && y <= 'Z')
                y += 'a' - 'A';
        }
        if (x != y)
            return (int)x - (int)y;
        if (!x)
            return 0;
    }
    return 0;
}
static UINT32 brew_str_alloc(const char* text)
{
    UINT32 n = text ? strlen(text) + 1 : 0, p;
    if (!n)
        return 0;
    p = brew_alloc(n);
    if (p)
        memcpy(brew_ptr(p, n), text, n);
    return p;
}
static INT32 brew_days_from_civil(int y, unsigned m, unsigned d)
{
    INT32 era;
    unsigned yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (INT32)doe - 719468;
}
static void brew_civil_from_days(INT32 z, int* year, unsigned* month, unsigned* day)
{
    INT32 era;
    unsigned doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp + (mp < 10 ? 3 : -9);
    *year = (int)(yoe + era * 400) + (*month <= 2);
}
/* BREW time is seconds since 1980-01-06 (Sunday) GMT. */
#define BREW_EPOCH_DAYS 3653
static UINT32 brew_julian_now(UINT32 seconds, UINT32 output)
{
    INT32 z;
    UINT32 days, rem;
    int year;
    unsigned month, day;
    if (!output || !brew_ptr(output, 14))
        return 0;
    if (!seconds)
        seconds = brew_clock / 1000;
    days = seconds / 86400;
    rem = seconds % 86400;
    z = (INT32)days + BREW_EPOCH_DAYS;
    brew_civil_from_days(z, &year, &month, &day);
    brew_w16(output, (UINT16)year);
    brew_w16(output + 2, (UINT16)month);
    brew_w16(output + 4, (UINT16)day);
    brew_w16(output + 6, (UINT16)(rem / 3600));
    brew_w16(output + 8, (UINT16)((rem / 60) % 60));
    brew_w16(output + 10, (UINT16)(rem % 60));
    brew_w16(output + 12, (UINT16)((days + 0) % 7));
    return 1;
}
static UINT32 brew_julian_seconds(UINT32 input)
{
    int year, month, day, hour, minute, second;
    INT32 days;
    if (!input || !brew_ptr(input, 14))
        return 0;
    year = brew_u16(input);
    month = brew_u16(input + 2);
    day = brew_u16(input + 4);
    hour = brew_u16(input + 6);
    minute = brew_u16(input + 8);
    second = brew_u16(input + 10);
    days = brew_days_from_civil(year, month ? month : 1, day ? day : 1) - BREW_EPOCH_DAYS;
    return (UINT32)(days * 86400 + hour * 3600 + minute * 60 + second);
}
static UINT32 brew_convert_bitmap(UINT32 address)
{
    UINT8* data = brew_ptr(address, 16);
    UINT32 size = 0, width = 0, height = 0, bitmap, pitch, x, y, guest_pixels = 0;
    UINT32* pixels = NULL;
    UINT8* dst;
    if (!data)
        return 0;
    if (data[0] == 0x1f && data[1] == 0x8b)
    {
        UINT32 limit = BREW_RAM_SIZE - address > 0x800000 ? 0x800000 : BREW_RAM_SIZE - address;
        UINT8* inflated = NULL;
        UINT32 inflated_size = 0;
        if (!brew_inflate(data, limit, &inflated, &inflated_size))
            return 0;
        if (inflated_size >= 54 && inflated[0] == 'B' && inflated[1] == 'M')
            brew_decode_bmp(inflated, inflated_size, &width, &height, &guest_pixels);
        free(inflated);
        if (!guest_pixels)
            return 0;
    }
    else if (data[0] == 'B' && data[1] == 'M')
    {
        size = brew_le32(data + 2);
        if (size < 54 || size > BREW_RAM_SIZE - address
            || !brew_decode_bmp(data, size, &width, &height, &guest_pixels))
            return 0;
    }
    else if (data[0] == 0x89 && data[1] == 'P')
    {
        UINT32 limit = BREW_RAM_SIZE - address > 0x800000 ? 0x800000 : BREW_RAM_SIZE - address, i;
        for (i = 8; i + 8 < limit; i++)
            if (!memcmp(data + i, "IEND", 4))
            {
                size = i + 8;
                break;
            }
        if (!size)
            return 0;
    }
    else if (data[0] == 0xff && data[1] == 0xd8)
    {
        UINT32 limit = BREW_RAM_SIZE - address > 0x800000 ? 0x800000 : BREW_RAM_SIZE - address, i;
        for (i = 2; i + 1 < limit; i++)
            if (data[i] == 0xff && data[i + 1] == 0xd9)
            {
                size = i + 2;
                break;
            }
        if (!size)
            return 0;
    }
    else
        return 0;
    if (!guest_pixels)
    {
        if (data[0] == 0x89)
        {
            rpng_t* decoder = rpng_alloc();
            int result;
            if (!decoder || !rpng_set_buf_ptr(decoder, data, size) || !rpng_start(decoder))
            {
                rpng_free(decoder);
                return 0;
            }
            while (rpng_iterate_image(decoder))
                ;
            if (!rpng_is_valid(decoder))
            {
                rpng_free(decoder);
                return 0;
            }
            do
            {
                result = rpng_process_image(decoder, (void**)&pixels, size, &width, &height);
            } while (result == IMAGE_PROCESS_NEXT);
            rpng_free(decoder);
            if (result == IMAGE_PROCESS_ERROR || result == IMAGE_PROCESS_ERROR_END)
                return 0;
        }
        else
        {
            rjpeg_t* decoder = rjpeg_alloc();
            if (!decoder || !rjpeg_set_buf_ptr(decoder, data)
                || !rjpeg_process_image(decoder, (void**)&pixels, size, &width, &height))
            {
                rjpeg_free(decoder);
                return 0;
            }
            rjpeg_free(decoder);
        }
    }
    if ((!pixels && !guest_pixels) || !width || !height || width > 4096 || height > 4096)
        return 0;
    bitmap = brew_bitmap_new(width, height, 0);
    if (!bitmap)
    {
        if (guest_pixels)
            brew_free(guest_pixels);
        else
            free(pixels);
        return 0;
    }
    dst = (UINT8*)brew_bitmap_pixels(bitmap, &width, &height, &pitch);
    if (dst)
        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                ((UINT32*)(dst + y * pitch))[x]
                    = (guest_pixels ? brew_u32(guest_pixels + (y * width + x) * 4) : pixels[y * width + x]) & 0xffffff;
    if (guest_pixels)
        brew_free(guest_pixels);
    else
        free(pixels);
    return bitmap;
}
static UINT32 brew_helper(unsigned slot)
{
    UINT32 a = brew_arg(0), b = brew_arg(1), c = brew_arg(2), i;
    UINT8 *p = brew_ptr(a, 1), *q = brew_ptr(b, 1);
    switch (slot)
    {
    case 0:
        if (brew_ptr(a, c) && brew_ptr(b, c))
            memmove(p, q, c);
        return a;
    case 1:
        if (brew_ptr(a, c))
            memset(p, b, c);
        return a;
    case 2:
    {
        const char* source = brew_string(b);
        if (source && brew_ptr(a, strlen(source) + 1))
            memcpy(p, source, strlen(source) + 1);
        return a;
    }
    case 3:
    {
        const char *source = brew_string(b), *dest = brew_string(a);
        if (source && dest && brew_ptr(a, strlen(dest) + strlen(source) + 1))
            strcat((char*)p, source);
        return a;
    }
    case 4:
        return brew_string(a) && brew_string(b) ? strcmp((char*)p, (char*)q) : 0;
    case 5:
        return brew_string(a) ? strlen((char*)p) : 0;
    case 6:
    case 7:
    {
        char* s = brew_string(a) ? (slot == 6 ? strchr((char*)p, b) : strrchr((char*)p, b)) : NULL;
        return s ? a + (s - (char*)p) : 0;
    }
    case 8:
        return brew_format(a, b, 2);
    case 9: /* wstrcpy */
        brew_wcopy(a, b, brew_wlen(b));
        return a;
    case 10: /* wstrcat */
        brew_wcopy(a + brew_wlen(a) * 2, b, brew_wlen(b));
        return a;
    case 11: /* wstrcmp */
        return (UINT32)brew_wcmp(a, b, 0, 0);
    case 13: /* wstrchr */
    {
        UINT32 n = brew_wlen(a);
        for (i = 0; i <= n; i++)
            if (brew_u16(a + i * 2) == (UINT16)b)
                return a + i * 2;
        return 0;
    }
    case 14: /* wstrrchr */
    {
        UINT32 n = brew_wlen(a);
        for (i = n + 1; i > 0; i--)
            if (brew_u16(a + (i - 1) * 2) == (UINT16)b)
                return a + (i - 1) * 2;
        return 0;
    }
    case 17: /* wstrtostr */
        if (!c || !brew_ptr(b, c) || a >= BREW_RAM_SIZE)
            return b;
        for (i = 0; i + 1 < c && brew_u16(a + i * 2); i++)
            brew_ptr(b, c)[i] = (UINT8)brew_u16(a + i * 2);
        brew_ptr(b, c)[i] = 0;
        return b;
    case 22: /* wstrlower */
        for (i = 0; a + i * 2 + 2 <= BREW_RAM_SIZE && brew_u16(a + i * 2); i++)
        {
            UINT16 ch = brew_u16(a + i * 2);
            if (ch >= 'A' && ch <= 'Z')
                brew_w16(a + i * 2, ch + ('a' - 'A'));
        }
        return a;
    case 23: /* wstrupper */
        for (i = 0; a + i * 2 + 2 <= BREW_RAM_SIZE && brew_u16(a + i * 2); i++)
        {
            UINT16 ch = brew_u16(a + i * 2);
            if (ch >= 'a' && ch <= 'z')
                brew_w16(a + i * 2, ch - ('a' - 'A'));
        }
        return a;
    case 28: /* wstrdup */
    {
        UINT32 n = brew_wlen(a) + 1, out = brew_alloc(n * 2);
        if (out)
            brew_wcopy(out, a, n);
        return out;
    }
    case 31: /* wstrsize */
        return (brew_wlen(a) + 1) * 2;
    case 32: /* wstrncopyn(dest, cbDest AECHARs, src, lenSource) */
    {
        UINT32 limit = (UINT32)c, n = (INT32)b < 0 ? brew_wlen(a) : (UINT32)b;
        if (limit && n > limit - 1)
            n = limit - 1;
        brew_wcopy(a, b, n);
        return n;
    }
    case 33: /* OEMStrLen */
        return brew_string(a) ? (UINT32)strlen((char*)p) : 0;
    case 34: /* OEMStrSize */
        return brew_string(a) ? (UINT32)strlen((char*)p) + 1 : 0;
    case 40: /* wstrcompress(src AECHARs, nChars, dest bytes, nSize) */
        if (brew_ptr(c, b ? b : 1) && a < BREW_RAM_SIZE)
        {
            UINT32 n = 0, k;
            for (k = 0; k < b && n + 1 < c; k++)
            {
                UINT16 ch = brew_u16(a + k * 2);
                if (ch <= 127)
                    brew_ptr(c, 1)[n++] = (UINT8)ch;
                else if (n + 2 < c)
                {
                    brew_ptr(c, 1)[n++] = (UINT8)ch;
                    brew_ptr(c, 1)[n++] = (UINT8)(ch >> 8);
                }
            }
            brew_ptr(c, 1)[n] = 0;
        }
        return 0;
    case 46: /* aee_GetJulianDate */
        return brew_julian_now(a, b);
    case 58: /* stristr */
    {
        const char *h = brew_string(a), *n = brew_string(b);
        if (!h || !n)
            return 0;
        for (; *h; h++)
        {
            const char *x = h, *y = n;
            while (*x && *y && tolower((unsigned char)*x) == tolower((unsigned char)*y))
                x++, y++;
            if (!*y)
                return a + (UINT32)(h - (const char*)p);
        }
        return 0;
    }
    case 59: /* memstr */
    {
        const char *h = brew_ptr(a, c) ? (const char*)p : NULL, *n = brew_string(b);
        UINT32 k;
        if (!h || !n)
            return 0;
        for (k = 0; k + strlen(n) <= c; k++)
            if (!memcmp(h + k, n, strlen(n)))
                return a + k;
        return 0;
    }
    case 60: /* wstrncmp */
        return (UINT32)brew_wcmp(a, b, c, 0);
    case 61: /* strdup */
        return brew_str_alloc(brew_string(a));
    case 62: /* strbegins */
    {
        const char *pre = brew_string(a), *str = brew_string(b);
        return pre && str && !strncmp(pre, str, strlen(pre));
    }
    case 63: /* strends */
    {
        const char *suf = brew_string(a), *str = brew_string(b);
        size_t ls, lstr;
        if (!suf || !str)
            return 0;
        ls = strlen(suf);
        lstr = strlen(str);
        return ls <= lstr && !strcmp(str + lstr - ls, suf);
    }
    case 64: /* strchrend */
    {
        const char *str = brew_string(a), *f;
        if (!str)
            return 0;
        f = strchr(str, (int)b);
        return f ? a + (UINT32)(f - (const char*)p) : a + (UINT32)strlen(str);
    }
    case 65: /* strchrsend */
    {
        const char *str = brew_string(a), *set = brew_string(b);
        UINT32 k;
        if (!str || !set)
            return 0;
        for (k = 0; str[k]; k++)
            if (strchr(set, str[k]))
                return a + k;
        return a + (UINT32)strlen(str);
    }
    case 66: /* memrchr */
    {
        const char *buf = brew_ptr(a, c) ? (const char*)p : NULL;
        if (!buf)
            return 0;
        for (i = c; i > 0; i--)
            if ((UINT8)buf[i - 1] == (UINT8)b)
                return a + i - 1;
        return 0;
    }
    case 67: /* memchrend */
    {
        const char *buf = brew_ptr(a, c) ? (const char*)p : NULL, *f;
        if (!buf)
            return a + c;
        f = memchr(buf, (int)b, c);
        return f ? a + (UINT32)(f - buf) : a + c;
    }
    case 68: /* memrchrbegin */
    {
        const char *buf = brew_ptr(a, c) ? (const char*)p : NULL;
        if (!buf)
            return a;
        for (i = c; i > 0; i--)
            if ((UINT8)buf[i - 1] == (UINT8)b)
                return a + i - 1;
        return a;
    }
    case 69: /* strlower */
        if (brew_string(a))
            for (i = 0; p[i]; i++)
                p[i] = tolower(p[i]);
        return a;
    case 70: /* strupper */
        if (brew_string(a))
            for (i = 0; p[i]; i++)
                p[i] = toupper(p[i]);
        return a;
    case 71: /* wstricmp */
        return (UINT32)brew_wcmp(a, b, 0, 1);
    case 72: /* wstrnicmp */
        return (UINT32)brew_wcmp(a, b, c, 1);
    case 73: /* inet_aton */
    {
        const char *text = brew_string(a);
        unsigned o1, o2, o3, o4;
        if (!text || sscanf(text, "%u.%u.%u.%u", &o1, &o2, &o3, &o4) != 4 || !b)
            return 0;
        brew_w32(b, (o1 << 24) | (o2 << 16) | (o3 << 8) | o4);
        return 1;
    }
    case 74: /* inet_ntoa */
    {
        UINT32 addr = a;
        if (!b || !c)
            return 0;
        snprintf((char*)q, c, "%u.%u.%u.%u", (addr >> 24) & 255, (addr >> 16) & 255,
            (addr >> 8) & 255, addr & 255);
        return 0;
    }
    case 75: /* swapl */
        return (a >> 24) | ((a >> 8) & 0xff00) | ((a << 8) & 0xff0000) | (a << 24);
    case 76: /* swaps */
        return ((a & 255) << 8) | ((a >> 8) & 255);
    case 77: /* GetFSFree */
        if (a)
            brew_w32(a, 128 * 1024 * 1024);
        return 64 * 1024 * 1024;
    case 78: /* GetRAMFree */
        if (a)
            brew_w32(a, BREW_HEAP_LIMIT - BREW_HEAP_BASE);
        if (b)
            brew_w32(b, BREW_HEAP_LIMIT - brew_heap);
        return BREW_HEAP_LIMIT - brew_heap;
    case 79: /* vsprintf */
        return brew_vformat(a, b, c);
    case 80: /* vsnprintf (size ignored for now) */
        return brew_vformat(a, c, brew_arg(4));
    case 81: /* snprintf */
        return brew_format(a, b, 2);
    case 82: /* aee_JulianToSeconds */
        return brew_julian_seconds(a);
    case 83: /* strlcpy */
    {
        const char *src = brew_string(b);
        UINT32 total, n;
        if (!src || !c)
            return src ? strlen(src) : 0;
        total = strlen(src);
        n = total < c ? total : c - 1;
        memcpy(p, src, n);
        p[n] = 0;
        return total;
    }
    case 84: /* strlcat */
    {
        const char *src = brew_string(b);
        UINT32 have, total, n;
        if (!src || !c || !brew_string(a))
            return 0;
        have = strlen((char*)p);
        total = strlen(src);
        n = total;
        if (have + n >= c)
            n = c > have + 1 ? c - have - 1 : 0;
        memcpy(p + have, src, n);
        p[have + n] = 0;
        return have + total;
    }
    case 85: /* wstrlcpy */
    {
        UINT32 n = brew_wlen(b);
        if (!c)
            return n;
        if (n >= c)
            n = c - 1;
        brew_wcopy(a, b, n);
        return n;
    }
    case 86: /* wstrlcat */
    {
        UINT32 have = brew_wlen(a), n = brew_wlen(b);
        if (!c)
            return have;
        if (have + n >= c)
            n = c > have + 1 ? c - have - 1 : 0;
        brew_wcopy(a + have * 2, b, n);
        return have + n;
    }
    case 87: /* setstaticptr */
        return 0;
    case 88: /* f_assignstr: software-float helpers are not implemented yet. */
    case 89: /* f_assignint */
    case 96: /* f_calc */
    case 107: /* f_toint */
    case 108: /* f_get */
        return 0;
    case 91: /* dbgheapmark */
        return a;
    case 98: /* getlasterror */
        return 0;
    case 90: /* wwritelong */
    {
        char temp[32];
        snprintf(temp, sizeof(temp), "%ld", (long)(INT32)b);
        for (i = 0; temp[i] && a + i * 2 + 2 <= BREW_RAM_SIZE; i++)
            brew_w16(a + i * 2, (UINT8)temp[i]);
        if (a + i * 2 + 2 <= BREW_RAM_SIZE)
            brew_w16(a + i * 2, 0);
        return a;
    }
    case 95: /* strtod */
    {
        char *end = NULL;
        double value = brew_string(a) ? strtod((char*)p, &end) : 0;
        if (b)
            brew_w32(b, end ? a + (UINT32)(end - (char*)p) : a);
        return (UINT32)value;
    }
    case 97: /* sleep */
        return 0;
    case 101: /* aee_IsBadPtr */
        return !brew_ptr(b, c);
    case 102: /* aee_basename */
    {
        UINT32 k = a, last = a;
        if (a >= BREW_RAM_SIZE)
            return 0;
        for (k = a; k < BREW_RAM_SIZE && brew_ptr(k, 1)[0]; k++)
            if (brew_ptr(k, 1)[0] == '/' || brew_ptr(k, 1)[0] == '\\')
                last = k + 1;
        return last;
    }
    case 103: /* aee_makepath(dir, file, out, len) */
    {
        const char *dir = brew_string(a), *file = brew_string(b);
        UINT32 need, dlen, k = 0, lengthptr = brew_arg(3);
        if (!file)
            return BREW_EBADPARM;
        dlen = dir ? strlen(dir) : 0;
        need = dlen + (dlen && dir[dlen - 1] != '/' ? 1 : 0) + strlen(file) + 1;
        if (c && lengthptr)
        {
            UINT32 cap = brew_u32(lengthptr);
            if (brew_ptr(c, need <= cap ? need : cap))
            {
                if (dir && dlen && brew_ptr(c, dlen))
                    for (k = 0; k < dlen; k++)
                        brew_ptr(c, 1)[k] = dir[k];
                if (dlen && dir[dlen - 1] != '/' && k < cap - 1)
                    brew_ptr(c, 1)[k++] = '/';
                for (i = 0; file[i] && k + 1 < cap; i++)
                    brew_ptr(c, 1)[k++] = file[i];
                brew_ptr(c, 1)[k] = 0;
            }
            brew_w32(lengthptr, need);
        }
        return need;
    }
    case 104: /* aee_splitpath(path, dir) */
    {
        UINT32 k, last = 0;
        if (a >= BREW_RAM_SIZE)
            return a;
        for (k = a; k < BREW_RAM_SIZE && brew_ptr(k, 1)[0]; k++)
            if (brew_ptr(k, 1)[0] == '/' || brew_ptr(k, 1)[0] == '\\')
                last = k + 1;
        if (b && last > a)
        {
            UINT32 n = last - a;
            if (brew_ptr(b, n + 1))
            {
                memcpy(brew_ptr(b, n + 1), brew_ptr(a, n), n);
                brew_ptr(b, n + 1)[n] = 0;
            }
        }
        return last > a ? last : a;
    }
    case 105: /* aee_stribegins */
    {
        const char *pre = brew_string(a), *str = brew_string(b);
        if (!pre || !str)
            return 0;
        while (*pre && *str)
            if (tolower((unsigned char)*pre++) != tolower((unsigned char)*str++))
                return 0;
        return !*pre;
    }
    case 106: /* aee_GetUTCSeconds */
        return brew_clock / 1000;
    case 112: /* err_realloc */
        return brew_realloc(a, b);
    case 113: /* err_strdup */
        return brew_str_alloc(brew_string(a));
    case 12:
        if (a > BREW_RAM_SIZE)
            return 0;
        for (i = 0; i < (BREW_RAM_SIZE - a) / 2 && brew_u16(a + i * 2); i++)
            ;
        return i;
    case 16:
        if (c < 2 || !b || !brew_ptr(b, c) || !brew_string(a))
            return b;
        for (i = 0; p[i] && i + 1 < c / 2; i++)
            brew_w16(b + i * 2, p[i]);
        brew_w16(b + i * 2, 0);
        return b;
    case 25: /* SetupNativeImage */
    {
        UINT32 bitmap = brew_convert_bitmap(b), width, height;
        if (!bitmap)
            return 0;
        width = brew_u32(bitmap + 4);
        height = brew_u32(bitmap + 8);
        if (c && brew_ptr(c, 10))
        {
            memset(brew_ptr(c, 10), 0, 10);
            brew_w16(c, (UINT16)width);
            brew_w16(c + 2, (UINT16)height);
            brew_w16(c + 4, 0);
            brew_ptr(c, 10)[6] = 0;
            brew_w16(c + 8, (UINT16)width);
        }
        if (brew_arg(3) && brew_ptr(brew_arg(3), 1))
            *(UINT8*)brew_ptr(brew_arg(3), 1) = 0;
        return bitmap;
    }
    case 26:
        return brew_alloc(a);
    case 27:
    case 47:
        brew_free(a);
        return 0;
    case 29:
        return brew_realloc(a, b);
    case 35:
        return 0x04000100;
    case 36:
        return brew_string(a) ? atoi((char*)p) : 0;
    case 39:
        return 0; /* Guest diagnostic output is optional. */
    case 42:
        if (brew_ptr(a, b))
            for (i = 0; i < b; i++)
            {
                brew_random = brew_random * 1664525 + 1013904223;
                p[i] = brew_random >> 24;
            }
        return 0;
    case 43:
    case 44:
        return brew_clock;
    case 45:
        return 1000000000 + brew_clock / 1000;
    case 48:
        /* During AEEClsCreateInstance the applet pointer already lives in the
           module output word; real AEE can answer GetAppInstance that early. */
        return brew_applet ? brew_applet : brew_u32(BREW_OUTPUT + 4);
    case 49:
    {
        const char* text = brew_string(a);
        const char* digits;
        UINT32 value = 0, base = c;
        int negative = 0, overflow = 0;
        if (!text || (base && (base < 2 || base > 36)))
        {
            if (b)
                brew_w32(b, a);
            return 0;
        }
        while (isspace((unsigned char)*text))
            text++;
        if (*text == '-' || *text == '+')
            negative = *text++ == '-';
        if ((!base || base == 16) && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')
            && isxdigit((unsigned char)text[2]))
        {
            base = 16;
            text += 2;
        }
        if (!base)
            base = *text == '0' ? 8 : 10;
        digits = text;
        while (*text)
        {
            unsigned ch = (unsigned char)*text;
            unsigned digit = ch >= '0' && ch <= '9' ? ch - '0'
                : ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10
                : ch >= 'a' && ch <= 'z' ? ch - 'a' + 10 : 36;
            if (digit >= base)
                break;
            if (value > (0xffffffffU - digit) / base)
                overflow = 1;
            if (!overflow)
                value = value * base + digit;
            text++;
        }
        if (b)
            brew_w32(b, text == digits ? a : a + (text - (const char*)p));
        return overflow ? 0xffffffffU : negative ? 0U - value : value;
    }
    case 50:
        if (brew_ptr(a, c) && brew_string(b))
            strncpy((char*)p, (char*)q, c);
        return a;
    case 51:
        return brew_ptr(a, c) && brew_ptr(b, c) ? strncmp((char*)p, (char*)q, c) : 0;
    case 52:
    case 53:
        if (!brew_string(a) || !brew_string(b))
            return 0;
        for (i = 0; slot == 52 || i < c; i++)
        {
            int left = tolower(p[i]), right = tolower(q[i]);
            if (left != right || !left)
                return left - right;
        }
        return 0;
    case 54:
    {
        char* s = brew_string(a) && brew_string(b) ? strstr((char*)p, (char*)q) : NULL;
        return s ? a + (s - (char*)p) : 0;
    }
    case 55:
        return brew_ptr(a, c) && brew_ptr(b, c) ? memcmp(p, q, c) : 0;
    case 56:
    {
        UINT8* found = brew_ptr(a, c) ? memchr(p, b, c) : NULL;
        return found ? a + (found - p) : 0;
    }
    case 57:
    {
        UINT32 src = 0, out = 0, limit = brew_arg(3) / 2;
        if (!limit || !c || !brew_ptr(c, limit * 2) || !a || !brew_ptr(a, b))
            return 0;
        while (src < b && out + 1 < limit && p[src])
        {
            UINT16 ch = p[src++];
            if (((ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc)) && src < b)
                ch |= (UINT16)p[src++] << 8;
            brew_w16(c + out++ * 2, ch);
        }
        brew_w16(c + out * 2, 0);
        return 0;
    }
    default:
        return 0;
    }
}
static int brew_media_notify(UINT32 object, UINT32 command, UINT32 sub, UINT32 status, UINT32 value)
{
    UINT32 fn = brew_u32(object + 4), note;
    unsigned i;
    if (!fn)
        return 0;
    for (i = 0; i < 64; i++)
        if (!brew_timer_fn[i])
            break;
    if (i == 64)
        return 2;
    note = brew_alloc(28);
    if (!note)
        return 2;
    brew_timer_clear(i);
    brew_w32(note, brew_u32(object + 48));
    brew_w32(note + 4, object);
    brew_w32(note + 8, command);
    brew_w32(note + 12, sub);
    brew_w32(note + 16, status);
    brew_w32(note + 20, value);
    brew_w32(note + 24, 0);
    brew_timer_fn[i] = fn;
    brew_timer_user[i] = brew_u32(object + 8);
    brew_timer_due[i] = brew_clock + 1;
    brew_timer_arg[i] = note;
    brew_timer_owned[i] = note;
    return 0;
}
static void brew_media_repeat_state(void)
{
    unsigned i;
    if (brew_u32(BREW_MEDIA_STATE + 128) == 0x52455054)
        return;
    for (i = 0; i < 32; i++)
        brew_w32(BREW_MEDIA_STATE + i * 4, brew_media_objects[i] ? brew_u32(brew_media_objects[i] + 56) : 0);
    brew_w32(BREW_MEDIA_STATE + 128, 0x52455054);
}
static UINT32 brew_media_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), state = brew_u32(object + 24), duration;
    unsigned audio = brew_u32(object + 40);
    if (audio >= 32 || brew_media_objects[audio] != object)
        return BREW_EBADPARM;
    brew_media_repeat_state();
    switch (slot)
    {
    case 2:
        if (!b || !brew_ptr(b, 4))
            return BREW_EBADPARM;
        brew_w32(b, 0);
        if (a == 0x01000001 || a == 0x01005500)
        {
            brew_reference(B_MEDIA, 0);
            brew_w32(b, object);
            return 0;
        }
        return BREW_ECLASSNOTSUPPORT;
    case 3:
        brew_media_cancel_notifications(object);
        brew_w32(object + 4, a);
        brew_w32(object + 8, b);
        if (a)
        {
            UINT32 pending = brew_u32(object + 52);
            if ((pending & 1) && !brew_media_notify(object, 3, 4, 2, brew_u32(object + 20)))
                pending &= ~1;
            if ((pending & 2) && !brew_media_notify(object, 6, 0, 2, brew_u32(object + 32)))
                pending &= ~2;
            brew_w32(object + 52, pending);
            if (pending)
                return BREW_ENOMEMORY;
        }
        return 0;
    case 4:
        if (a == 1)
        {
            UINT32 kind = brew_u32(b), data = brew_u32(b + 4), size = brew_u32(b + 8);
            if (state != 1 && state != 2)
                return BREW_EBADSTATE;
            if (!b || !brew_ptr(b, 12) || kind != 1 || !data || !size || !brew_ptr(data, size))
                return 14;
            duration = brew_smaf_load(audio, data, size);
            if (!duration)
                return 14;
            brew_w32(object + 12, data);
            brew_w32(object + 16, size);
            brew_w32(object + 32, duration);
            brew_w32(object + 24, 2);
            brew_smaf_volume(audio, brew_u32(object + 44) ? 0 : brew_u32(object + 20));
            return 0;
        }
        if (a == 4)
        {
            if (b > 100)
                return BREW_EBADPARM;
            brew_w32(object + 20, b);
            brew_smaf_volume(audio, brew_u32(object + 44) ? 0 : b);
            return 0;
        }
        if (a == 5)
        {
            brew_w32(object + 44, !!b);
            brew_smaf_volume(audio, b ? 0 : brew_u32(object + 20));
            return 0;
        }
        if (a == 11)
        {
            brew_w32(object + 56, b);
            brew_w32(BREW_MEDIA_STATE + audio * 4, b);
            return 0;
        }
        return BREW_EUNSUPPORTED;
    case 5:
        if (a == 4)
        {
            if (!b || !brew_ptr(b, 2))
                return BREW_EBADPARM;
            brew_w16(b, brew_u32(object + 20));
            if (brew_u32(object + 4))
            {
                if (brew_media_notify(object, 3, 4, 2, brew_u32(object + 20)))
                    return BREW_ENOMEMORY;
            }
            else
                brew_w32(object + 52, brew_u32(object + 52) | 1);
            return -2;
        }
        if (a == 1)
        {
            if (!b || !brew_ptr(b, 12))
                return BREW_EBADPARM;
            if (state == 1)
                return BREW_EBADSTATE;
            brew_w32(b, 1);
            brew_w32(b + 4, brew_u32(object + 12));
            brew_w32(b + 8, brew_u32(object + 16));
            return 0;
        }
        if (a == 5 || a == 11 || a == 13 || a == 14)
        {
            if (!b || !brew_ptr(b, 4))
                return BREW_EBADPARM;
            brew_w32(b, a == 5 ? brew_u32(object + 44) : a == 11 ? brew_u32(object + 56)
                          : a == 13 ? brew_u32(object + 48) : 1);
            return 0;
        }
        return BREW_EUNSUPPORTED;
    case 6:
        if (state != 2)
            return BREW_EBADSTATE;
        duration = brew_media_notify(object, 4, 0, 1, 0);
        if (duration)
            return duration;
        brew_w32(BREW_MEDIA_STATE + audio * 4, brew_u32(object + 56));
        brew_smaf_play(audio);
        brew_w32(object + 24, 3);
        brew_w32(object + 28, brew_clock + brew_u32(object + 32));
        return 0;
    case 8:
        if (state == 3 || state == 5)
        {
            duration = brew_media_notify(object, 4, 0, 3, 0);
            if (duration)
                return duration;
            brew_smaf_stop(audio);
            brew_w32(object + 24, 2);
            return 0;
        }
        return BREW_EBADSTATE;
    case 10:
        if (state != 3)
            return BREW_EBADSTATE;
        duration = brew_media_notify(object, 4, 0, 9, 0);
        if (duration)
            return duration;
        brew_w32(object + 36, (INT32)(brew_u32(object + 28) - brew_clock) > 0
                                 ? brew_u32(object + 28) - brew_clock : 0);
        brew_smaf_pause(audio, 1);
        brew_w32(object + 24, 5);
        return 0;
    case 11:
        if (state != 5)
            return BREW_EBADSTATE;
        duration = brew_media_notify(object, 4, 0, 11, 0);
        if (duration)
            return duration;
        brew_smaf_pause(audio, 0);
        brew_w32(object + 28, brew_clock + brew_u32(object + 36));
        brew_w32(object + 24, 3);
        return 0;
    case 12:
        if (state == 1)
            return BREW_EBADSTATE;
        if (brew_u32(object + 4))
        {
            if (brew_media_notify(object, 6, 0, 2, brew_u32(object + 32)))
                return BREW_ENOMEMORY;
        }
        else
            brew_w32(object + 52, brew_u32(object + 52) | 2);
        return -2;
    case 13:
        if (a && !brew_ptr(a, 1))
            return BREW_EBADPARM;
        if (a && brew_ptr(a, 1))
            *brew_ptr(a, 1) = 0;
        return state;
    default:
        return BREW_EUNSUPPORTED;
    }
}
/* IMemAStream (AEECLSID_MEMASTREAM): IBase + IAStream + Set/SetEx.
   Object words: +4 buffer, +8 size, +12 cursor, +16 start, +20 sysmem,
   +24 callback function, +28 callback user, +32 callback pending. */
static void brew_stream_set(UINT32 object, UINT32 buffer, UINT32 size, UINT32 offset, UINT32 sysmem)
{
    UINT32 old = brew_u32(object + 4);
    if (old && old != buffer)
        brew_free(old);
    brew_w32(object + 4, buffer);
    brew_w32(object + 8, size);
    brew_w32(object + 12, offset);
    brew_w32(object + 16, offset);
    brew_w32(object + 20, sysmem);
}
/* Inflate a host buffer. Tries zlib, gzip/zlib auto-detect and raw deflate. */
static int brew_inflate(const UINT8* input, UINT32 input_size, UINT8** output, UINT32* output_size)
{
    int window_bits[3] = { 15, 31, -15 }, attempt;
    *output = NULL;
    *output_size = 0;
    for (attempt = 0; attempt < 3; attempt++)
    {
        z_stream stream;
        UINT8* out = NULL;
        UINT32 cap = 0, used = 0;
        int status;
        memset(&stream, 0, sizeof(stream));
        if (inflateInit2(&stream, window_bits[attempt]) != Z_OK)
            continue;
        stream.next_in = (Bytef*)input;
        stream.avail_in = input_size;
        for (;;)
        {
            if (used == cap)
            {
                UINT32 next = cap ? cap * 2 : 65536;
                UINT8* grown = (UINT8*)realloc(out, next);
                if (!grown)
                {
                    inflateEnd(&stream);
                    free(out);
                    return 0;
                }
                out = grown;
                cap = next;
            }
            stream.next_out = out + used;
            stream.avail_out = cap - used;
            status = inflate(&stream, Z_NO_FLUSH);
            used = cap - stream.avail_out;
            if (status == Z_STREAM_END)
                break;
            if (status != Z_OK && status != Z_BUF_ERROR)
            {
                inflateEnd(&stream);
                free(out);
                used = 0;
                break;
            }
            if (stream.avail_in == 0 && status == Z_OK && stream.avail_out != 0)
            {
                inflateEnd(&stream);
                free(out);
                used = 0;
                break;
            }
        }
        inflateEnd(&stream);
        if (used)
        {
            *output = out;
            *output_size = used;
            return 1;
        }
    }
    return 0;
}
static UINT32 brew_source_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), i;
    switch (slot)
    {
    case 2: /* QueryInterface */
        if (a && brew_ptr(a, 4))
            brew_w32(a, 0);
        return BREW_EUNSUPPORTED;
    case 3: /* Read */
    {
        UINT32 buffer = brew_u32(object + 4), size = brew_u32(object + 8);
        UINT32 cursor = brew_u32(object + 12), want = b;
        if (brew_u32(object + 20) == 1)
        {
            UINT32 stream = brew_u32(object + 24);
            if (!stream || !brew_is_object(stream, B_STREAM))
                return 0;
            if (brew_u32(stream + 36) == 1)
            {
                if (!brew_unzip_prepare(stream))
                    return 0;
                buffer = brew_u32(stream + 8);
                size = brew_u32(stream + 12);
            }
            else
            {
                buffer = brew_u32(stream + 4);
                size = brew_u32(stream + 8);
            }
            cursor = brew_u32(object + 12);
        }
        if (!buffer || cursor >= size)
            return 0;
        if (want > size - cursor)
            want = size - cursor;
        if (!a || !brew_ptr(a, want) || !brew_ptr(buffer + cursor, want))
            return 0;
        memcpy(brew_ptr(a, want), brew_ptr(buffer + cursor, want), want);
        brew_w32(object + 12, cursor + want);
        return want;
    }
    case 4: /* Readable(pcb) */
        brew_w32(object + 16, a);
        for (i = 0; i < 8; i++)
            if (!brew_source_cb[i])
            {
                brew_source_cb[i] = object;
                break;
            }
        return 0;
    default:
        return 0;
    }
}
static UINT32 brew_sourceutil_method(unsigned slot)
{
    UINT32 a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), p;
    switch (slot)
    {
    case 2:
        if (a && brew_ptr(a, 4))
            brew_w32(a, 0);
        return BREW_EUNSUPPORTED;
    case 5: /* SourceFromMemory(buf, len, freeFn, freeData, pp) */
        if (!brew_arg(5) || !brew_ptr(brew_arg(5), 4))
            return BREW_EBADPARM;
        if (!a || !brew_ptr(a, b > 0 ? b : 1))
            return BREW_EBADPARM;
        p = brew_object(B_SOURCE);
        if (!p)
            return BREW_ENOMEMORY;
        brew_w32(p + 4, a);
        brew_w32(p + 8, b);
        brew_w32(p + 12, 0);
        brew_w32(p + 20, 0);
        brew_w32(brew_arg(5), p);
        return 0;
    case 6: /* SourceFromAStream(stream, pp) */
        if (!c || !brew_ptr(c, 4))
            return BREW_EBADPARM;
        p = brew_object(B_SOURCE);
        if (!p)
            return BREW_ENOMEMORY;
        brew_w32(p + 4, 0);
        brew_w32(p + 8, 0);
        brew_w32(p + 12, 0);
        brew_w32(p + 20, 1);
        brew_w32(p + 24, a);
        brew_w32(c, p);
        return 0;
    default:
        return BREW_EUNSUPPORTED;
    }
}
static void brew_source_poll(void)
{
    unsigned i;
    for (i = 0; i < 8; i++)
    {
        UINT32 object = brew_source_cb[i];
        if (!object)
            continue;
        brew_source_cb[i] = 0;
        if (brew_is_object(object, B_SOURCE))
        {
            UINT32 pcb = brew_u32(object + 16), fn, data;
            if (!pcb)
                continue;
            fn = brew_u32(pcb + 16);
            data = brew_u32(pcb + 20);
            brew_w32(pcb + 8, 0);
            brew_w32(pcb + 12, 0);
            if (!fn)
                continue;
            brew_prepare_call(fn, data, 0, 0, 0);
            activecpu_set_reg(ARM7_PC, fn & ~1);
            activecpu_set_reg(ARM7_CPSR, 0x1f | ((fn & 1) ? 0x20 : 0));
            return;
        }
    }
}
static UINT32 brew_stream_data(UINT32 object, const UINT8** data, UINT32* size, UINT8** owned)
{
    *owned = NULL;
    if (brew_is_object(object, B_FILE))
    {
        UINT32 index = brew_u32(object + 4), pos = brew_u32(object + 8), n;
        if (index >= BREW_FILES || !brew_file_names[index][0])
            return 0;
        n = pos < brew_file_sizes[index] ? brew_file_sizes[index] - pos : 0;
        if (!n)
            return 0;
        *data = brew_file_data[index] + pos;
        *size = n;
        brew_w32(object + 8, pos + n);
        return 1;
    }
    if (!brew_is_object(object, B_STREAM))
        return 0;
    if (brew_u32(object + 36) == 1)
    {
        if (!brew_unzip_prepare(object))
            return 0;
        *data = brew_ptr(brew_u32(object + 8), brew_u32(object + 12));
        *size = brew_u32(object + 12);
        return *data != NULL;
    }
    if (brew_u32(object + 44))
        return brew_stream_data(brew_u32(object + 40), data, size, owned);
    {
        UINT32 buffer = brew_u32(object + 4), total = brew_u32(object + 8), offset = brew_u32(object + 16);
        if (!buffer || offset >= total)
            return 0;
        *data = brew_ptr(buffer + offset, total - offset);
        *size = total - offset;
        return *data != NULL;
    }
}
static int brew_unzip_prepare(UINT32 object)
{
    UINT32 source = brew_u32(object + 40);
    const UINT8* input = NULL;
    UINT32 size = 0;
    UINT8* owned = NULL;
    UINT8* output = NULL;
    UINT32 output_size = 0, buffer;
    if (brew_u32(object + 8))
        return 1;
    if (!source || !brew_stream_data(source, &input, &size, &owned))
        return 0;
    if (!brew_inflate(input, size, &output, &output_size))
    {
        free(owned);
        return 0;
    }
    free(owned);
    buffer = brew_alloc(output_size);
    if (!buffer)
    {
        free(output);
        return 0;
    }
    memcpy(brew_ptr(buffer, output_size), output, output_size);
    free(output);
    brew_w32(object + 8, buffer);
    brew_w32(object + 12, output_size);
    brew_w32(object + 16, 0);
    brew_w32(object + 20, 1);
    return 1;
}
#define BREW_CACHE_MAX 32
static UINT32 brew_cache_obj[BREW_CACHE_MAX];
static UINT8* brew_cache_key[BREW_CACHE_MAX];
static UINT32 brew_cache_key_len[BREW_CACHE_MAX];
static UINT8* brew_cache_val[BREW_CACHE_MAX];
static UINT32 brew_cache_val_len[BREW_CACHE_MAX];
static void brew_cache_clear_object(UINT32 object)
{
    unsigned i;
    for (i = 0; i < BREW_CACHE_MAX; i++)
        if (brew_cache_obj[i] == object)
        {
            free(brew_cache_key[i]);
            free(brew_cache_val[i]);
            brew_cache_key[i] = brew_cache_val[i] = NULL;
            brew_cache_key_len[i] = brew_cache_val_len[i] = 0;
            brew_cache_obj[i] = 0;
        }
}
static void brew_cache_clear(void)
{
    unsigned i;
    for (i = 0; i < BREW_CACHE_MAX; i++)
    {
        free(brew_cache_key[i]);
        free(brew_cache_val[i]);
        brew_cache_key[i] = brew_cache_val[i] = NULL;
        brew_cache_key_len[i] = brew_cache_val_len[i] = 0;
        brew_cache_obj[i] = 0;
    }
}
static UINT32 brew_ramcache_method(unsigned slot)
{
    UINT32 a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), d = brew_arg(4);
    const UINT8* key = brew_ptr(a, b > 0 ? b : 1);
    unsigned i;
    switch (slot)
    {
    case 2: /* QueryInterface */
        if (c)
            brew_w32(c, 0);
        return BREW_EUNSUPPORTED;
    case 3: /* Add(key, keyLen, val, valLen) */
    {
        const UINT8* val = brew_ptr(c, d > 0 ? d : 1);
        if (!key || b < 0 || !val || d < 0)
            return BREW_EBADPARM;
        for (i = 0; i < BREW_CACHE_MAX; i++)
            if (brew_cache_obj[i] == brew_arg(0) && brew_cache_key[i] && brew_cache_key_len[i] == (UINT32)b
                && !memcmp(brew_cache_key[i], key, b))
                break;
        if (i == BREW_CACHE_MAX)
            for (i = 0; i < BREW_CACHE_MAX; i++)
                if (!brew_cache_key[i])
                    break;
        if (i == BREW_CACHE_MAX)
            return BREW_ENOMEMORY;
        free(brew_cache_key[i]);
        free(brew_cache_val[i]);
        brew_cache_key[i] = (UINT8*)malloc(b ? (size_t)b : 1);
        brew_cache_val[i] = (UINT8*)malloc(d ? (size_t)d : 1);
        if (!brew_cache_key[i] || !brew_cache_val[i])
        {
            free(brew_cache_key[i]);
            free(brew_cache_val[i]);
            brew_cache_key[i] = brew_cache_val[i] = NULL;
            return BREW_ENOMEMORY;
        }
        memcpy(brew_cache_key[i], key, b);
        memcpy(brew_cache_val[i], val, d);
        brew_cache_obj[i] = brew_arg(0);
        brew_cache_key_len[i] = b;
        brew_cache_val_len[i] = d;
        return 0;
    }
    case 4: /* Remove(key, keyLen) */
        if (b < 0)
        {
            brew_cache_clear();
            return 0;
        }
        for (i = 0; i < BREW_CACHE_MAX; i++)
            if (brew_cache_obj[i] == brew_arg(0) && brew_cache_key[i] && brew_cache_key_len[i] == (UINT32)b
                && !memcmp(brew_cache_key[i], key, b))
            {
                free(brew_cache_key[i]);
                free(brew_cache_val[i]);
                brew_cache_key[i] = brew_cache_val[i] = NULL;
                brew_cache_key_len[i] = brew_cache_val_len[i] = 0;
                brew_cache_obj[i] = 0;
                return 0;
            }
        return BREW_EFAILED;
    case 5: /* Find(key, keyLen, val, valLen) */
    {
        UINT8* val = brew_ptr(c, 1);
        UINT32 capacity = d ? brew_u32(d) : 0, copy;
        if (!key || b < 0 || !val || !d)
            return BREW_EBADPARM;
        for (i = 0; i < BREW_CACHE_MAX; i++)
            if (brew_cache_obj[i] == brew_arg(0) && brew_cache_key[i] && brew_cache_key_len[i] == (UINT32)b
                && !memcmp(brew_cache_key[i], key, b))
            {
                copy = brew_cache_val_len[i] < capacity ? brew_cache_val_len[i] : capacity;
                memcpy(val, brew_cache_val[i], copy);
                brew_w32(d, brew_cache_val_len[i]);
                return 0;
            }
        brew_w32(d, 0);
        return BREW_EFAILED;
    }
    case 6: /* SetTTL */
    case 7: /* SetMaxSize */
    case 8: /* SetMaxEntries */
        return 0;
    default:
        return 0;
    }
}
static const char brew_b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static UINT32 brew_b64_encode(UINT32 input, UINT32 length, UINT32 output)
{
    UINT32 i, j = 0;
    const UINT8* in = brew_ptr(input, length);
    UINT8* out;
    if (!in || !output || !brew_ptr(output, ((length + 2) / 3) * 4 + 1))
        return 0;
    out = brew_ptr(output, ((length + 2) / 3) * 4 + 1);
    for (i = 0; i + 2 < length; i += 3)
    {
        UINT32 v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out[j++] = brew_b64_chars[(v >> 18) & 63];
        out[j++] = brew_b64_chars[(v >> 12) & 63];
        out[j++] = brew_b64_chars[(v >> 6) & 63];
        out[j++] = brew_b64_chars[v & 63];
    }
    if (i < length)
    {
        UINT32 v = in[i] << 16;
        if (i + 1 < length)
            v |= in[i + 1] << 8;
        out[j++] = brew_b64_chars[(v >> 18) & 63];
        out[j++] = brew_b64_chars[(v >> 12) & 63];
        out[j++] = i + 1 < length ? brew_b64_chars[(v >> 6) & 63] : '=';
        out[j++] = '=';
    }
    out[j] = 0;
    return output;
}
static int brew_b64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}
static UINT32 brew_b64_decode(UINT32 input, UINT32 output)
{
    const char* in = brew_string(input);
    UINT8* out = output ? brew_ptr(output, 1) : NULL;
    UINT32 i = 0, j = 0;
    if (!in || !out)
        return 0;
    while (in[i])
    {
        int a = brew_b64_value(in[i]), b, c, d;
        if (a < 0)
            break;
        b = brew_b64_value(in[i + 1]);
        c = in[i + 2] && in[i + 2] != '=' ? brew_b64_value(in[i + 2]) : 0;
        d = in[i + 3] && in[i + 3] != '=' ? brew_b64_value(in[i + 3]) : 0;
        if (b < 0)
            break;
        out[j++] = (a << 2) | (b >> 4);
        if (in[i + 2] && in[i + 2] != '=')
            out[j++] = ((b & 15) << 4) | (c >> 2);
        if (in[i + 3] && in[i + 3] != '=')
            out[j++] = ((c & 3) << 6) | d;
        i += 4;
        if (!in[i - 1])
            break;
        if (!brew_ptr(output, j + 1))
            break;
    }
    if (brew_ptr(output, j + 1))
        brew_ptr(output, j + 1)[j] = 0;
    return j;
}
static UINT32 brew_webutil_method(unsigned slot)
{
    UINT32 a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), d = brew_arg(4);
    switch (slot)
    {
    case 2: /* QueryInterface */
        if (c)
            brew_w32(c, 0);
        return BREW_EUNSUPPORTED;
    case 3: /* ParseUrl */
        if (b)
        {
            UINT32 k;
            for (k = 0; k < 32 && brew_ptr(b, 32); k++)
                brew_w32(b + k * 4, 0);
        }
        return BREW_EUNSUPPORTED;
    case 4: /* MakeUrl(base, relative, result, resultLen) */
    {
        const char *base = brew_string(a), *rel = brew_string(b);
        UINT32 need, k = 0;
        if (!rel || !c || !d)
            return BREW_EBADPARM;
        need = (base ? strlen(base) : 0) + strlen(rel) + 1;
        if (brew_u32(d) < need)
        {
            brew_w32(d, need);
            return BREW_ENOMEMORY;
        }
        if (base && brew_ptr(c, need))
            for (; base[k]; k++)
                brew_ptr(c, need)[k] = base[k];
        if (brew_ptr(c, need))
            for (a = 0; rel[a] && k + 1 < need; a++)
                brew_ptr(c, need)[k++] = rel[a];
        if (brew_ptr(c, need))
            brew_ptr(c, need)[k] = 0;
        brew_w32(d, k + 1);
        return 0;
    }
    case 5: /* UrlEncode */
    {
        const char* in = brew_string(a);
        UINT32 inlen = b ? (INT32)brew_u32(b) : 0, k, j = 0;
        char* out;
        if (!in)
            return 0;
        if ((INT32)inlen < 0)
            inlen = strlen(in);
        if (!c || !brew_ptr(c, inlen * 3 + 1))
            return 0;
        out = (char*)brew_ptr(c, inlen * 3 + 1);
        for (k = 0; k < inlen; k++)
        {
            unsigned ch = (unsigned char)in[k];
            if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
                out[j++] = ch;
            else
            {
                static const char hex[] = "0123456789ABCDEF";
                out[j++] = '%';
                out[j++] = hex[ch >> 4];
                out[j++] = hex[ch & 15];
            }
        }
        out[j] = 0;
        if (b)
            brew_w32(b, inlen);
        if (d)
            brew_w32(d, j);
        return c;
    }
    case 6: /* UrlDecode */
    {
        const char* in = brew_string(a);
        UINT32 inlen = b ? (INT32)brew_u32(b) : 0, k, j = 0;
        char* out;
        if (!in)
            return 0;
        if ((INT32)inlen < 0)
            inlen = strlen(in);
        if (!c || !brew_ptr(c, inlen + 1))
            return 0;
        out = (char*)brew_ptr(c, inlen + 1);
        for (k = 0; k < inlen; k++)
        {
            if (in[k] == '%' && k + 2 < inlen && isxdigit((unsigned char)in[k + 1])
                && isxdigit((unsigned char)in[k + 2]))
            {
                char hex[3] = { in[k + 1], in[k + 2], 0 };
                out[j++] = (char)strtol(hex, NULL, 16);
                k += 2;
            }
            else if (in[k] == '+')
                out[j++] = ' ';
            else
                out[j++] = in[k];
        }
        out[j] = 0;
        if (b)
            brew_w32(b, inlen);
        if (d)
            brew_w32(d, j);
        return c;
    }
    case 7: /* EncodeBase64 */
        return brew_b64_encode(a, b, c);
    case 8: /* DecodeBase64 */
        return brew_b64_decode(a, b);
    case 9: /* ParseFormFields */
        return 0;
    default:
        return 0;
    }
}
static UINT32 brew_random_method(unsigned slot)
{
    UINT32 a = brew_arg(1), b = brew_arg(2);
    switch (slot)
    {
    case 2: /* QueryInterface */
        if (b)
            brew_w32(b, 0);
        return BREW_EUNSUPPORTED;
    case 3: /* Read */
        if (brew_ptr(a, b))
        {
            UINT32 k;
            for (k = 0; k < b; k++)
            {
                brew_random = brew_random * 1664525 + 1013904223;
                brew_ptr(a, b)[k] = brew_random >> 24;
            }
        }
        return b;
    case 4: /* Readable */
        return 0;
    default:
        return 0;
    }
}
static UINT32 brew_sound10_method(unsigned slot)
{
    UINT32 a = brew_arg(1), b = brew_arg(2);
    switch (slot)
    {
    case 3: /* Set */
        return 0;
    case 4: /* Get */
        if (a && brew_ptr(a, 32))
            memset(brew_ptr(a, 32), 0, 32);
        return 0;
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
        return 0;
    case 13: /* GetVolume */
        return 0;
    default:
        if (slot < 2)
            return 1;
        (void)b;
        return 0;
    }
}
static UINT32 brew_stream_read(UINT32 object, UINT32 dest, UINT32 want)
{
    UINT32 source = brew_u32(object + 40);
    if (brew_u32(object + 36) == 1)
    {
        UINT32 buffer, size, cursor;
        if (!brew_unzip_prepare(object))
            return 0;
        buffer = brew_u32(object + 8);
        size = brew_u32(object + 12);
        cursor = brew_u32(object + 16);
        if (cursor >= size)
            return 0;
        if (want > size - cursor)
            want = size - cursor;
        if (want && dest && brew_ptr(dest, want) && brew_ptr(buffer + cursor, want))
            memcpy(brew_ptr(dest, want), brew_ptr(buffer + cursor, want), want);
        else
            want = 0;
        brew_w32(object + 16, cursor + want);
        return want;
    }
    if (brew_u32(object + 44) == 1)
    {
        UINT32 index, pos, n;
        if (!brew_is_object(source, B_FILE))
            return 0;
        index = brew_u32(source + 4);
        pos = brew_u32(source + 8);
        if (index >= BREW_FILES || !brew_file_names[index][0])
            return 0;
        n = pos < brew_file_sizes[index] ? brew_file_sizes[index] - pos : 0;
        if (n > want)
            n = want;
        if (n && dest && brew_ptr(dest, n))
            memcpy(brew_ptr(dest, n), brew_file_data[index] + pos, n);
        else
            n = 0;
        brew_w32(source + 8, pos + n);
        return n;
    }
    if (brew_u32(object + 44) == 2)
        return source && brew_is_object(source, B_STREAM) ? brew_stream_read(source, dest, want) : 0;
    {
        UINT32 buffer = brew_u32(object + 4), size = brew_u32(object + 8);
        UINT32 cursor = brew_u32(object + 16);
        if (cursor >= size)
            return 0;
        if (want > size - cursor)
            want = size - cursor;
        if (want && dest && brew_ptr(dest, want) && brew_ptr(buffer + cursor, want))
            memcpy(brew_ptr(dest, want), brew_ptr(buffer + cursor, want), want);
        else
            want = 0;
        brew_w32(object + 16, cursor + want);
        return want;
    }
}
static UINT32 brew_stream_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), c = brew_arg(3);
    switch (slot)
    {
    case 2: /* Readable(po, pfn, pUser) */
        brew_w32(object + 24, a);
        brew_w32(object + 28, b);
        brew_w32(object + 32, a ? 1 : 0);
        return 0;
    case 3: /* Read(po, pDest, nWant) */
        return brew_stream_read(object, a, b);
    case 4: /* Cancel(po, pfn, pUser) */
        if (!a || (brew_u32(object + 24) == a && brew_u32(object + 28) == b))
        {
            brew_w32(object + 24, 0);
            brew_w32(object + 28, 0);
            brew_w32(object + 32, 0);
        }
        return 0;
    case 5: /* Set(po, pBuff, dwSize, dwOffset, bSysMem) or SetStream(po, pIAStream) */
        if (brew_u32(object + 36) == 1 || (a && brew_is_object(a, B_FILE)))
        {
            UINT32 old = brew_u32(object + 40);
            if (old != a)
            {
                if (old)
                    brew_ref(old, brew_u32(object + 44) == 1 ? B_FILE : B_STREAM, 1);
                brew_w32(object + 44, 0);
                if (a && brew_is_object(a, B_FILE))
                {
                    brew_ref(a, B_FILE, 0);
                    brew_w32(object + 44, 1);
                }
                else if (a && brew_is_object(a, B_STREAM))
                    brew_ref(a, B_STREAM, 0);
                brew_w32(object + 40, a);
            }
            if (brew_u32(object + 8))
                brew_free(brew_u32(object + 8));
            brew_w32(object + 8, 0);
            brew_w32(object + 12, 0);
            brew_w32(object + 16, 0);
            brew_w32(object + 20, 0);
            return 0;
        }
        brew_stream_set(object, a, b, c, brew_arg(4));
        return 0;
    case 6: /* SetEx(po, pBuff, dwSize, nOffset, pUserFreeFn, pUserFreeData) */
        if (brew_u32(object + 36) != 1)
            brew_stream_set(object, a, b, c, 0);
        return 0;
    default:
        return 0;
    }
}
static void brew_stream_poll(void)
{
    unsigned i;
    for (i = 0; i < 16; i++)
    {
        UINT32 object = brew_stream_objects[i], fn, user;
        if (!object || !brew_u32(object + 32))
            continue;
        fn = brew_u32(object + 24);
        user = brew_u32(object + 28);
        brew_w32(object + 24, 0);
        brew_w32(object + 28, 0);
        brew_w32(object + 32, 0);
        if (!fn)
            continue;
        brew_phase = 2;
        cpu_trigger(BREW_WAKE);
        brew_prepare_call(fn, user, 0, 0, 0);
        activecpu_set_reg(ARM7_PC, fn & ~1);
        activecpu_set_reg(ARM7_CPSR, 0x1f | ((fn & 1) ? 0x20 : 0));
        return;
    }
}
static void brew_media_poll(void)
{
    unsigned i;
    brew_media_repeat_state();
    brew_smaf_sync();
    for (i = 0; i < 32; i++)
    {
        UINT32 object = brew_media_objects[i];
        if (object && brew_u32(object + 24) == 3
            && (brew_smaf_finished(i)
                || (!Machine->sample_rate && (INT32)(brew_clock - brew_u32(object + 28)) >= 0)))
        {
            UINT32 repeat = brew_u32(BREW_MEDIA_STATE + i * 4);
            if (repeat != 1)
            {
                if (brew_media_notify(object, 4, 0, 13, 0))
                    continue;
                if (repeat)
                    brew_w32(BREW_MEDIA_STATE + i * 4, repeat - 1);
                brew_smaf_play(i);
                brew_w32(object + 28, brew_clock + brew_u32(object + 32));
            }
            else
            {
                if (brew_media_notify(object, 4, 0, 2, 0))
                    continue;
                brew_w32(object + 24, 2);
            }
        }
    }
}

static void brew_restore_call(struct brew_saved_call* ctx, UINT32 result)
{
    unsigned k;
    for (k = 0; k < 16; k++)
        activecpu_set_reg(k, ctx->regs[k]);
    activecpu_set_reg(ARM7_CPSR, ctx->cpsr);
    activecpu_set_reg(ARM7_R0, result);
    activecpu_set_reg(ARM7_R12, ctx->lr);
}
static UINT32 brew_extension_create(UINT32 classid, UINT32 out)
{
    unsigned i;
    struct brew_saved_call* ctx;
    UINT32 module, fn;
    if (!brew_ext_count || brew_call_depth >= 8)
        return 0;
    for (i = 0; i < brew_ext_count; i++)
        if (brew_exts[i].module)
            break;
    if (i == brew_ext_count)
        return 0;
    module = brew_exts[i].module;
    fn = brew_u32(brew_u32(module) + 8);
    if (!fn)
        return 0;
    ctx = &brew_saved_calls[brew_call_depth++];
    ctx->extension = i;
    for (i = 0; i < 16; i++)
        ctx->regs[i] = activecpu_get_reg(i);
    ctx->cpsr = activecpu_get_reg(ARM7_CPSR);
    ctx->out = out;
    ctx->lr = ctx->regs[ARM7_R14];
    ctx->pending = 1;
    ctx->classid = classid;
    brew_w32(out, 0);
    brew_prepare_call(fn, module, brew_shell, classid, out);
    activecpu_set_reg(ARM7_CPSR, 0x1f | ((fn & 1) ? 0x20 : 0));
    brew_nested_started = 1;
    return 1;
}
static UINT32 brew_resume_callback(UINT32 callback)
{
    unsigned i;
        if (!callback || !brew_ptr(callback, 28) || !brew_u32(callback + 16))
            return BREW_EBADPARM;
        for (i = 0; i < 64; i++)
            if (brew_timer_callback[i] == callback)
                brew_timer_clear(i);
        for (i = 0; i < 64; i++)
            if (!brew_timer_fn[i])
            {
                brew_timer_clear(i);
                brew_timer_arg[i] = brew_timer_owned[i] = 0;
                brew_timer_fn[i] = brew_u32(callback + 16);
                brew_timer_user[i] = brew_u32(callback + 20);
                brew_timer_due[i] = brew_clock + 1;
                brew_timer_callback[i] = callback;
                brew_w32(callback + 8, BREW_TRAPS + B_SHELL * 4096 + 250 * 4);
                brew_w32(callback + 12, i + 1);
                return 0;
            }
        return 2;
}
static UINT32 brew_web_method(unsigned slot)
{
    UINT32 output = brew_arg(1), callback = brew_arg(2), response;
    if (slot == 6 || slot == 7)
    {
        if (!output || !brew_ptr(output, 4) || !callback || !brew_ptr(callback, 24))
            return BREW_EBADPARM;
        brew_w32(output, 0);
        response = brew_object(B_WEBRESP);
        if (!response)
            return BREW_ENOMEMORY;
        /* Offline runtime: complete the real request with WEB_ERROR_CONNECT.
           Never invent a successful purchase/subscription response. */
        brew_w32(response + 4, (UINT32)-0x509);
        brew_w32(response + 12, (UINT32)-1);
        brew_w32(response + 24, (UINT32)-1);
        brew_w32(response + 28, (UINT32)-1);
        brew_w32(output, response);
        return brew_resume_callback(callback);
    }
    return slot == 3 || slot == 4 ? 0 : BREW_EUNSUPPORTED;
}
static UINT32 brew_shell_method(unsigned slot)
{
    UINT32 a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), i, p, size;
    switch (slot)
    {
    case 0:
    case 1:
        return 1;
    case 2:
        if (!b || !brew_ptr(b, 4))
            return BREW_EBADPARM;
        i = a == 0x01001001                                   ? B_DISPLAY
                : a == 0x01001003                                         ? B_FILEMGR
                : a == 0x01005000                                         ? B_WEB
                : a == 0x01001002                                         ? B_HEAP
                : (a == 0x010211af || a == 0x0101270e || a == 0x01029313 || a == 0x01001022) ? B_FONT
                : (a == 0x01005509 || a == 0x01005508 || a == 0x01005501 || a == 0x0100550a) ? B_MEDIA
                : a == 0x01002001                                         ? B_GRAPHICS
                : a == 0x01001056                                         ? B_SOUND
                : (a == 0x0100100c || a == 0x01001014)                    ? B_STREAM
                : a == 0x01005001                                         ? B_WEBUTIL
                : a == 0x0100103c                                         ? B_RANDOM
                : a == 0x0100100b                                         ? B_SOUND10
                : a == 0x0100103d                                         ? B_RAMCACHE
                : a == 0x010456de                                         ? B_NETERROR
                : a == 0x01001021                                         ? B_BITMAP
                : a == 0x01001011                                         ? B_SOURCEUTIL
                : (a == 0x0100102b || a == 0x010127d4)                    ? B_DISPLAY
                : (a == 0x0102f91c || a == 0x0100100f || a == 0x0100550d
                   || a == 0x01004007 || a == 0x01003109 || a == 0x01003009) ? B_STUB
                : (a == 0x01004004 || a == 0x01004005 || a == 0x01046da0
                   || a == 0x01057b58 || a == 0x0106e413 || a == 0x01046dac) ? B_IMAGE
                : (a == 0x01012786 || a == 0x01012787)                    ? B_FONT
                                                                          : B_GENERIC;
        brew_w32(b, 0);
        if (i == B_GENERIC)
        {
            unsigned k;
            for (k = 0; k < brew_seen_class_count; k++)
                if (brew_seen_class[k] == a)
                    break;
            if (k == brew_seen_class_count)
            {
                if (brew_seen_class_count < 256)
                    brew_seen_class[brew_seen_class_count++] = a;
            }
            if (brew_extension_create(a, b))
                return 0;
            return BREW_ECLASSNOTSUPPORT;
        }
        p = brew_object(i);
        if (!p)
            return BREW_ENOMEMORY;
        if (i == B_DISPLAY && (a == 0x0100102b || a == 0x010127d4))
        {
            UINT32 source = brew_u32(BREW_DISPLAY_STATE);
            if (source && brew_is_object(source, B_DISPLAY))
            {
                unsigned k;
                memcpy(brew_ptr(p + 4, 56), brew_ptr(source + 4, 56), 56);
                for (k = 0; k < BREW_FONT_COUNT; k++)
                    brew_ref(brew_u32(p + 24 + k * 4), B_FONT, 0);
            }
        }
        if (i == B_IMAGE)
            brew_w32(p + 4, a);
        if (i == B_FONT)
        {
            brew_w32(p + 4, a);
            brew_w32(p + 8, a == 0x01001022 || a == 0x01012786 || a == 0x01012787);
        }
        if (p && i == B_STREAM)
        {
            for (i = 0; i < 16; i++)
                if (!brew_stream_objects[i])
                    break;
            if (i == 16)
            {
                brew_free(p);
                p = 0;
            }
            else
            {
                brew_stream_objects[i] = p;
                if (a == 0x01001014)
                {
                    brew_w32(p + 36, 1);
                    brew_w32(p + 4, 0);
                }
            }
        }
        if (p && (a == 0x01005509 || a == 0x01005508 || a == 0x01005501 || a == 0x0100550a))
        {
            for (i = 0; i < 32; i++)
                if (!brew_media_objects[i])
                    break;
            if (i == 32)
            {
                brew_free(p);
                p = 0;
            }
            else
            {
                brew_media_objects[i] = p;
                brew_w32(p + 40, i);
                brew_w32(p + 48, a);
                brew_w32(p + 20, 100);
                brew_w32(p + 24, 1);
                brew_w32(p + 56, 1);
            }
        }
        brew_w32(b, p);
        if (p && a == 0x01001001 && !brew_u32(BREW_DISPLAY_STATE))
            brew_w32(BREW_DISPLAY_STATE, p);
        return p ? 0 : BREW_ENOMEMORY;
    case 4:
        p = a;
        if (!p || !brew_ptr(p, 48))
            return BREW_EBADPARM;
        memset(brew_ptr(p, 48), 0, 48);
        brew_w16(p, BREW_WIDTH);
        brew_w16(p + 2, BREW_HEIGHT);
        brew_w16(p + 8, 4);
        brew_w16(p + 10, 1);
        brew_w16(p + 14, 16);
        brew_w32(p + 24, BREW_HEAP_LIMIT - BREW_HEAP_BASE);
        brew_w32(p + 40, 0x411);
        return 0;
    case 8: /* ActiveApplet */
        return brew_classid;
    case 9: /* EnumAppletInit */
        brew_applet_enum = 0;
        return 0;
    case 10: /* EnumNextApplet(po, AEEAppInfo *pai) */
        if (brew_applet_enum++)
            return 0;
        if (a && brew_ptr(a, 20))
        {
            static const char mif_name[] = "app.mif";
            UINT32 name = BREW_OUTPUT + 0x340;
            memset(brew_ptr(a, 20), 0, 20);
            memcpy(brew_ptr(name, sizeof(mif_name)), mif_name, sizeof(mif_name));
            brew_w32(a, brew_classid);
            brew_w32(a + 4, name);
        }
        return brew_classid;
    case 11:
        if (!b)
            return BREW_EBADPARM;
        /* ISHELL_SetTimerEx(p,s,pcb) is a macro: SetTimer(p,s,(PFNNOTIFY)pcb,pcb).
           In that form the real shell fires pcb->pfnNotify(pcb->pNotifyData). */
        if (b == c)
        {
            UINT32 notify = brew_u32(b + 16), data = brew_u32(b + 20);
            if (!notify || !brew_ptr(b, 28))
                return BREW_EBADPARM;
            for (i = 0; i < 64; i++)
                if (brew_timer_callback[i] == b)
                    brew_timer_clear(i);
            for (i = 0; i < 64; i++)
                if (!brew_timer_fn[i])
                {
                    brew_timer_clear(i);
                    brew_timer_fn[i] = notify;
                    brew_timer_user[i] = data;
                    brew_timer_arg[i] = brew_timer_owned[i] = 0;
                    brew_timer_callback[i] = b;
                    brew_timer_due[i] = brew_clock + ((INT32)a > 0 ? a : 1);
                    brew_w32(b + 8, BREW_TRAPS + B_SHELL * 4096 + 250 * 4);
                    brew_w32(b + 12, i + 1);
                    return 0;
                }
            return 2;
        }
        for (i = 0; i < 64; i++)
            if (brew_timer_fn[i] == b && brew_timer_user[i] == c && !brew_timer_owned[i] && !brew_timer_callback[i])
                brew_timer_clear(i);
        for (i = 0; i < 64; i++)
            if (!brew_timer_fn[i])
            {
                brew_timer_clear(i);
                brew_timer_fn[i] = b;
                brew_timer_arg[i] = brew_timer_owned[i] = brew_timer_callback[i] = 0;
                brew_timer_user[i] = c;
                brew_timer_due[i] = brew_clock + ((INT32)a > 0 ? a : 1);
                return 0;
            }
        return 2;
    case 12:
        for (i = 0; i < 64; i++)
            if ((brew_timer_callback[i] && (a == brew_timer_callback[i] || (!a && b == brew_timer_callback[i])))
                || (brew_timer_fn[i] && (!a || brew_timer_fn[i] == a) && brew_timer_user[i] == b
                    && !brew_timer_owned[i] && !brew_timer_callback[i]))
                brew_timer_clear(i);
        return 0;
    case 17:
        return 0;
    case 18:
    {
        UINT32 source = brew_resource(brew_string(a), b, c, &size);
        if (!source)
            return 0;
        p = brew_alloc(size);
        if (p)
            memcpy(brew_ptr(p, size), brew_ptr(source, size), size);
        return p;
    }
    case 19:
        return c == 0x01001021 ? brew_load_bitmap(brew_string(a), b) : brew_load_image(brew_string(a), b);
    case 20:
        brew_free(a);
        return 0;
    case 41:
    {
        UINT32 source = brew_resource(brew_string(a), b, c, &size), buffer = brew_arg(4), length = brew_arg(5),
               capacity = brew_u32(length);
        if (!source || !length)
            return 0;
        brew_w32(length, size);
        if (buffer == 0xffffffff)
            return buffer;
        if (!buffer)
            buffer = brew_alloc(size);
        else if (capacity < size)
            return 0;
        if (!buffer || !brew_ptr(buffer, size))
            return 0;
        memcpy(brew_ptr(buffer, size), brew_ptr(source, size), size);
        return buffer;
    }
    case 21:
        if (b && b != brew_classid)
            return 0;
        return brew_enqueue(c, brew_arg(4), brew_arg(5));
    case 23: /* GetPrefs */
        return 0x0101; /* EFAILED: no stored preferences, caller uses defaults. */
    case 24: /* SetPrefs */
        return 0;
    case 26: /* Prompt */
        return 0;
    case 27: /* MessageBox */
    case 28: /* MessageBoxText */
        return 1;
    case 29:
        return 1;
    case 39: /* CheckPrivLevel */
        return 1;
    case 36:
        return brew_resume_callback(a);
    case 250:
        i = brew_u32(brew_arg(0) + 12);
        if (i && i <= 64 && brew_timer_callback[i - 1] == brew_arg(0))
            brew_timer_clear(i - 1);
        brew_w32(brew_arg(0) + 8, 0);
        return 0;
    case 44:
        if (a == 0x58f0 && c)
        {
            UINT32 length = memory_region_length(REGION_USER4);
            if (b && brew_u32(c) >= length + 1 && brew_ptr(b, length + 1))
            {
                memcpy(brew_ptr(b, length), memory_region(REGION_USER4), length);
                brew_ptr(b, length + 1)[length] = 0;
            }
            brew_w32(c, length + 1);
            return 0;
        }
        if (a == 0x58f1 && c)
        {
            UINT32 needed = 11;
            if (b && brew_u32(c) >= needed && brew_ptr(b, needed))
                memcpy(brew_ptr(b, needed), "0000000000", needed);
            brew_w32(c, needed);
            return 0;
        }
        return 1;
    default:
        return 0;
    }
}
/* All three KDDI font classes currently use the public-domain K12 face.
   Do not invent class-specific handset metrics without reference evidence. */
static const struct brew_glyph* brew_glyph_code(UINT16 code)
{
    UINT32 lo = 0, hi = sizeof(brew_glyphs) / sizeof(brew_glyphs[0]);
    while (lo < hi)
    {
        UINT32 mid = (lo + hi) / 2;
        if (brew_glyphs[mid].code < code)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < sizeof(brew_glyphs) / sizeof(brew_glyphs[0]) && brew_glyphs[lo].code == code
        ? &brew_glyphs[lo] : NULL;
}
static const struct brew_glyph* brew_glyph(UINT32 font, UINT16 character)
{
    UINT32 code = character;
    if (font && brew_u32(font + 8))
    {
        UINT32 lo = 0, hi = sizeof(brew_unicode_glyphs) / sizeof(brew_unicode_glyphs[0]);
        if (character < 128)
            return brew_glyph_code(character);
        while (lo < hi)
        {
            UINT32 mid = (lo + hi) / 2;
            if (brew_unicode_glyphs[mid].unicode < character)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == sizeof(brew_unicode_glyphs) / sizeof(brew_unicode_glyphs[0])
            || brew_unicode_glyphs[lo].unicode != character)
            return NULL;
        code = brew_unicode_glyphs[lo].jis;
    }
    else if (character > 127)
    {
        unsigned a = character & 255, b = character >> 8, row, col;
        /* KDDI AECHARs pack Shift JIS in byte order, not Unicode. */
        if (a >= 0xa1 && a <= 0xdf && !b)
            return brew_glyph_code(brew_halfwidth_jis[a - 0xa1]);
        if (a >= 0x81 && a <= 0x9f)
            row = (a - 0x81) * 2 + 0x21;
        else if (a >= 0xe0 && a <= 0xef)
            row = (a - 0xe0) * 2 + 0x5f;
        else
            return NULL;
        if (b < 0x40 || b == 0x7f || b > 0xfc)
            return NULL;
        if (b >= 0x9f)
        {
            row++;
            col = b - 0x7e;
        }
        else
            col = b - (b >= 0x80 ? 0x20 : 0x1f);
        code = (row << 8) | col;
    }
    return brew_glyph_code(code);
}
/* Decode an OEM byte string into packed Shift JIS characters. Counts in this
   mode are byte counts; normal IDisplay/IFont counts are AECHAR counts. */
static unsigned brew_text_next(UINT32 text, unsigned at, unsigned limit, int oem, UINT16* ch)
{
    if (at >= limit)
        return 0;
    if (!oem)
    {
        *ch = brew_u16(text + at * 2);
        return 1;
    }
    *ch = *brew_ptr(text + at, 1);
    if (((*ch >= 0x81 && *ch <= 0x9f) || (*ch >= 0xe0 && *ch <= 0xfc)) && at + 1 < limit)
    {
        unsigned trail = *brew_ptr(text + at + 1, 1);
        if (trail >= 0x40 && trail <= 0xfc && trail != 0x7f)
        {
            *ch |= trail << 8;
            return 2;
        }
    }
    return 1;
}
static int brew_text_limit(UINT32 text, INT32 count, int oem, unsigned* limit)
{
    unsigned capacity, n;
    if (count < -1 || !text || !brew_ptr(text, 1))
        return 0;
    capacity = (BREW_RAM_SIZE - text) / (oem ? 1 : 2);
    if (count >= 0)
    {
        if ((UINT32)count > capacity)
            return 0;
        *limit = count;
        return 1;
    }
    for (n = 0; n < capacity; n++)
        if (oem ? !*brew_ptr(text + n, 1) : !brew_u16(text + n * 2))
        {
            *limit = n;
            return 1;
        }
    return 0;
}
static unsigned brew_text_advance(UINT32 font, UINT16 ch, const struct brew_glyph* glyph)
{
    if ((!(font && brew_u32(font + 8)) && ch >= 0xa1 && ch <= 0xdf)
        || (font && brew_u32(font + 8) && ch >= 0xff61 && ch <= 0xff9f))
        return 6;
    return glyph ? glyph->advance : ch < 128 ? 6 : 12;
}
static UINT32 brew_text_measure(UINT32 font, UINT32 text, unsigned limit, INT32 max,
    int oem, UINT32* fits)
{
    unsigned n = 0, width = 0, step;
    UINT16 ch;
    if (oem)
        font = 0;
    while ((step = brew_text_next(text, n, limit, oem, &ch)))
    {
        const struct brew_glyph* glyph = brew_glyph(font, ch);
        unsigned advance = brew_text_advance(font, ch, glyph);
        if (max >= 0 && width + advance > (UINT32)max)
            break;
        width += advance;
        n += step;
    }
    if (fits)
        *fits = n;
    return width;
}
static void brew_text_draw(UINT32 font, UINT32 text, unsigned limit, int oem, int x, int y,
    UINT32 foreground, UINT32 background, int transparent, int left, int top, int right, int bottom,
    UINT32* pixels, UINT32 stride)
{
    unsigned n = 0, step;
    INT64 cursor = x;
    UINT16 ch;
    /* Geometry is computed wide before clipping; negative guest coordinates
       and very long strings must never overflow framebuffer indexing. */
    if (oem)
        font = 0;
    while ((step = brew_text_next(text, n, limit, oem, &ch)))
    {
        const struct brew_glyph* glyph = brew_glyph(font, ch);
        unsigned advance = brew_text_advance(font, ch, glyph);
        int xx, yy, half = advance == 6 && glyph && glyph->advance == 12;
        if (cursor >= right)
            break;
        for (yy = 0; yy < 12; yy++)
            for (xx = 0; xx < (int)advance; xx++)
                if (cursor + xx >= left && cursor + xx < right && (INT64)y + yy >= top
                    && (INT64)y + yy < bottom)
                {
                    /* Halfwidth kana reuse fullwidth glyphs. OR both source
                       columns so strokes in either half survive the reduction. */
                    int ink = glyph && (half
                        ? (glyph->rows[yy] & (0xC000 >> (xx * 2)))
                        : xx < 12 && (glyph->rows[yy] & (0x8000 >> xx)));
                    if (ink || !transparent)
                        pixels[((INT64)y + yy) * stride + cursor + xx]
                            = ink ? foreground : background;
                }
        cursor += advance;
        n += step;
    }
}

static UINT32 brew_display_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), d = brew_arg(4), i, n = 0;
    int clip_left, clip_top, clip_right, clip_bottom;
    UINT32 target_width, target_height, target_pitch;
    UINT32* target = (UINT32*)brew_bitmap_pixels(
        brew_bitmap_resolve(brew_display_destination ? brew_display_destination : brew_bitmap_device()),
        &target_width, &target_height, &target_pitch);
    if (!target) return BREW_EFAILED;
    target_pitch /= 4;
    if (!brew_u32(BREW_DISPLAY_STATE))
        brew_w32(BREW_DISPLAY_STATE, object);
    brew_get_clip(object, &clip_left, &clip_top, &clip_right, &clip_bottom);
    if (clip_right > (INT32)target_width) clip_right = target_width;
    if (clip_bottom > (INT32)target_height) clip_bottom = target_height;
    switch (slot)
    {
    case 0:
    case 1:
        return 1;
    case 2:
        if (!brew_display_font(object, a) || (b && !brew_ptr(b, 4)) || (c && !brew_ptr(c, 4)))
            return BREW_EFAILED;
        if (b)
            brew_w32(b, 10);
        if (c)
            brew_w32(c, 2);
        return 12;
    case 3:
    {
        unsigned limit;
        UINT32 font = brew_display_font(object, a), output = brew_arg(5);
        if (!font || !brew_text_limit(b, (INT32)c, 0, &limit) || (INT32)brew_arg(4) < -1
            || (output && !brew_ptr(output, 4)))
            return 0;
        n = brew_text_measure(font, b, limit, (INT32)brew_arg(4), 0, &i);
        if (output)
            brew_w32(output, i);
        return n;
    }
    case 4:
    {
        UINT32 rect = brew_arg(6), flags = brew_arg(7);
        int x = (INT32)brew_arg(4), y = (INT32)brew_arg(5), left = 0, top = 0, right = BREW_WIDTH,
            bottom = BREW_HEIGHT;
        UINT32 font = brew_display_font(object, a), foreground = brew_colors[1], background = brew_colors[2];
        unsigned limit;
        int oem = !!(flags & 0x10000), width, xx, yy;
        if (!font || !brew_text_limit(b, (INT32)c, oem, &limit) || (rect && !brew_ptr(rect, 8)))
            return BREW_EBADPARM;
        width = brew_text_measure(font, b, limit, -1, oem, NULL);
        if (rect)
        {
            left = (INT16)brew_u16(rect);
            top = (INT16)brew_u16(rect + 2);
            right = left + (INT16)brew_u16(rect + 4);
            bottom = top + (INT16)brew_u16(rect + 6);
        }
        if (flags & 0x10)
            x = left;
        if (flags & 0x20)
            x = left + (right - left - width) / 2;
        if (flags & 0x40)
            x = right - width;
        if (flags & 0x100)
            y = top;
        if (flags & 0x200)
            y = top + (bottom - top - 12) / 2;
        if (flags & 0x400)
            y = bottom - 12;
        if (left < clip_left)
            left = clip_left;
        if (top < clip_top)
            top = clip_top;
        if (right > clip_right)
            right = clip_right;
        if (bottom > clip_bottom)
            bottom = clip_bottom;
        if (flags & 0x4000)
        {
            foreground = brew_colors[2];
            background = brew_colors[1];
        }
        if (flags & 7)
            for (yy = top; yy < bottom; yy++)
                for (xx = left; xx < right; xx++)
                {
                    UINT32* pixel = &target[yy * target_pitch + xx];
                    int rx = rect ? (INT16)brew_u16(rect) : 0;
                    int ry = rect ? (INT16)brew_u16(rect + 2) : 0;
                    int rw = rect ? (INT16)brew_u16(rect + 4) : BREW_WIDTH;
                    int rh = rect ? (INT16)brew_u16(rect + 6) : BREW_HEIGHT;
                    if (flags & 4)
                        *pixel ^= 0xffffff;
                    else if ((flags & 1) && (xx == rx || yy == ry || xx == rx + rw - 1 || yy == ry + rh - 1))
                        *pixel = brew_colors[3];
                    else if (flags & 2)
                        *pixel = background;
                }
        brew_text_draw(font, b, limit, oem, x, y, foreground, background, flags & 0x8000,
            left, top, right, bottom, target, target_pitch);
        if ((flags & (0x2000 | 0x20000)) && (INT64)y + 11 >= top && (INT64)y + 11 < bottom)
            for (xx = x < left ? left : x; (INT64)xx < (INT64)x + width && xx < right; xx++)
                if (!(flags & 0x20000) || !(((INT64)xx - x) & 1))
                    target[((INT64)y + 11) * target_pitch + xx] = foreground;
        return 0;
    }
    case 5:
    {
        UINT32 flags = brew_arg(4);
        int x = a ? (INT16)brew_u16(a) : 0, y = a ? (INT16)brew_u16(a + 2) : 0;
        int w = a ? (INT16)brew_u16(a + 4) : BREW_WIDTH, h = a ? (INT16)brew_u16(a + 6) : BREW_HEIGHT, xx, yy;
        UINT32 frame = b == 0xffffffff ? brew_colors[3] : b >> 8;
        UINT32 fill = c == 0xffffffff ? brew_colors[2] : c >> 8;
        if (!a && !(flags & 2))
            return 0;
        for (yy = y < clip_top ? clip_top : y; yy < y + h && yy < clip_bottom; yy++)
            for (xx = x < clip_left ? clip_left : x; xx < x + w && xx < clip_right; xx++)
            {
                UINT32* pixel = &target[yy * target_pitch + xx];
                if (flags & 4)
                    *pixel ^= 0xffffff;
                else if ((flags & 1) && (xx == x || yy == y || xx == x + w - 1 || yy == y + h - 1))
                    *pixel = frame;
                else if (flags & 2)
                    *pixel = fill;
            }
        return 0;
    }
    case 10:
        if (a < 16)
        {
            UINT32 old = brew_colors[a];
            if (b != 0xffffffff)
                brew_colors[a] = b >> 8;
            return old << 8;
        }
        return 0;
    case 6: /* BitBlt */
    {
        int xDest = (INT32)a, yDest = (INT32)b, dx = (INT32)c, dy = (INT32)d;
        UINT32 source = brew_arg(5), rop = brew_arg(8);
        int xSrc = (INT32)brew_arg(6), ySrc = (INT32)brew_arg(7);
        UINT32 dest = brew_display_destination ? brew_display_destination : brew_bitmap_device();
        UINT32 dw, dh, dp, sw = 0, sh = 0, sp = 0, transparent, use;
        UINT8 *dd = brew_bitmap_pixels(dest, &dw, &dh, &dp), *sd = NULL;
        if (!dd)
            return 0;
        transparent = brew_u32(dest + 24);
        use = brew_u32(dest + 32);
        if (brew_is_object(source, B_IMAGE))
        {
            sw = brew_u32(source + 8);
            sh = brew_u32(source + 12);
            sp = sw * 4;
            sd = brew_ptr(brew_u32(source + 16), sw * sh * 4);
            xSrc += (INT32)brew_u32(source + 20);
            ySrc += (INT32)brew_u32(source + 24);
        }
        else if (brew_is_object(source, B_BITMAP) || brew_is_object(source, B_DIB))
        {
            UINT32 bitmap = source;
            if (brew_is_object(source, B_DIB))
            {
                brew_dib_sync(source);
                bitmap = brew_u32(source + 40);
            }
            sd = brew_bitmap_pixels(bitmap, &sw, &sh, &sp);
            transparent = brew_u32(bitmap + 24);
            use = brew_u32(bitmap + 32);
            if (sd && sw && sh)
            {
                int y, x;
                for (y = 0; y < dy; y++)
                {
                    int sy = ySrc + y, dyy = yDest + y;
                    if (sy < 0 || sy >= (INT32)sh || dyy < 0 || dyy >= (INT32)dh)
                        continue;
                    for (x = 0; x < dx; x++)
                    {
                        int sx = xSrc + x, dxx = xDest + x;
                        if (sx < 0 || sx >= (INT32)sw || dxx < 0 || dxx >= (INT32)dw)
                            continue;
                        brew_bitmap_put(dest, dxx, dyy,
                            brew_bitmap_rop(brew_bitmap_get(dest, dxx, dyy), brew_bitmap_get(bitmap, sx, sy),
                                rop, transparent, use));
                    }
                }
                return 0;
            }
        }
        if (dx < 0) dx = 0;
        if (dy < 0) dy = 0;
        if (sd && sw && sh)
        {
            int y, x;
            for (y = 0; y < dy; y++)
            {
                int sy = ySrc + y, dyy = yDest + y;
                if (sy < 0 || sy >= (INT32)sh || dyy < 0 || dyy >= (INT32)dh)
                    continue;
                for (x = 0; x < dx; x++)
                {
                    int sx = xSrc + x, dxx = xDest + x;
                    if (sx < 0 || sx >= (INT32)sw || dxx < 0 || dxx >= (INT32)dw)
                        continue;
                    ((UINT32*)(dd + dyy * dp))[dxx] = brew_bitmap_rop(
                        ((UINT32*)(dd + dyy * dp))[dxx], ((UINT32*)(sd + sy * sp))[sx], rop, transparent, use);
                }
            }
        }
        else if (source && brew_ptr(source, 54) && brew_ptr(source, 1)[0] == 'B' && brew_ptr(source, 1)[1] == 'M')
        {
            const UINT8* bmp = brew_ptr(source, 54);
            UINT32 offset = brew_le32(bmp + 10), width = (INT32)brew_le32(bmp + 18);
            INT32 height = (INT32)brew_le32(bmp + 22), bpp = brew_le16(bmp + 28);
            UINT32 stride = ((width * bpp + 31) / 32) * 4;
            int y, x;
            if (bpp == 1 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32)
                for (y = 0; y < dy; y++)
                {
                    int ry = height > 0 ? height - 1 - (ySrc + y) : ySrc + y;
                    int dyy = yDest + y;
                    const UINT8* row;
                    if (ry < 0 || ry >= (height > 0 ? height : -height) || dyy < 0 || dyy >= (INT32)dh)
                        continue;
                    row = brew_ptr(source + offset + (UINT32)ry * stride, stride);
                    if (!row)
                        continue;
                    for (x = 0; x < dx; x++)
                    {
                        int sx = xSrc + x, dxx = xDest + x;
                        UINT32 color;
                        if (sx < 0 || sx >= (INT32)width || dxx < 0 || dxx >= (INT32)dw)
                            continue;
                        if (bpp <= 8)
                        {
                            unsigned index = bpp == 8 ? row[sx] : bpp == 4
                                ? (row[sx / 2] >> ((1 - (sx & 1)) * 4)) & 15
                                : (row[sx / 8] >> (7 - (sx & 7))) & 1;
                            UINT32 palette = source + 14 + brew_le32(bmp + 14) + index * 4;
                            const UINT8* rgb = brew_ptr(palette, 4);
                            if (!rgb) continue;
                            color = (rgb[2] << 16) | (rgb[1] << 8) | rgb[0];
                        }
                        else color = bpp == 24 ? (row[sx * 3 + 2] << 16) | (row[sx * 3 + 1] << 8) | row[sx * 3]
                                          : (row[sx * 4 + 2] << 16) | (row[sx * 4 + 1] << 8) | row[sx * 4];
                        ((UINT32*)(dd + dyy * dp))[dxx] = brew_bitmap_rop(
                            ((UINT32*)(dd + dyy * dp))[dxx], color, rop, transparent, use);
                    }
                }
        }
        return 0;
    }
    case 12: /* DrawFrame(rect, frameType, rgbFill) */
    {
        int rx, ry, rw, rh, xx;
        UINT32 fill;
        if (!a || !brew_ptr(a, 8))
            return BREW_EBADPARM;
        rx = (INT16)brew_u16(a);
        ry = (INT16)brew_u16(a + 2);
        rw = (INT16)brew_u16(a + 4);
        rh = (INT16)brew_u16(a + 6);
        fill = c >> 8;
        if (rw <= 0 || rh <= 0)
            return 0;
        for (xx = 0; xx < rw; xx++)
        {
            int x0 = rx + xx;
            if (x0 >= clip_left && x0 < clip_right)
            {
                if (ry >= clip_top && ry < clip_bottom)
                    target[ry * target_pitch + x0] = fill;
                if (ry + rh - 1 >= clip_top && ry + rh - 1 < clip_bottom)
                    target[(ry + rh - 1) * target_pitch + x0] = fill;
            }
        }
        for (xx = 0; xx < rh; xx++)
        {
            int y0 = ry + xx;
            if (y0 >= clip_top && y0 < clip_bottom)
            {
                if (rx >= clip_left && rx < clip_right)
                    target[y0 * target_pitch + rx] = fill;
                if (rx + rw - 1 >= clip_left && rx + rw - 1 < clip_right)
                    target[y0 * target_pitch + rx + rw - 1] = fill;
            }
        }
        return 0;
    }
    case 13: /* CreateDIBitmap(IDIB **pp, colorDepth, w, h) */
    {
        UINT32 dib;
        if (!a || !brew_ptr(a, 4))
            return BREW_EBADPARM;
        dib = brew_dib_new(c, d, b);
        if (!dib)
            return BREW_ENOMEMORY;
        brew_w32(a, dib);
        return 0;
    }
    case 14: /* SetDestination */
        if (a && !brew_is_object(a, B_BITMAP) && !brew_is_object(a, B_DIB))
            return BREW_EUNSUPPORTED;
        brew_display_destination = a;
        return 0;
    case 15: /* GetDestination */
        return brew_display_destination ? brew_display_destination : brew_bitmap_device();
    case 16: /* GetDeviceBitmap */
        if (!a || !brew_ptr(a, 4))
            return BREW_EBADPARM;
        i = brew_bitmap_device();
        brew_w32(a, i);
        return i ? 0 : BREW_ENOMEMORY;
    case 24: /* CreateDIBitmapEx(pp, depth, height, width, paletteSize, cbExtra) */
    {
        UINT32 dib;
        if (!a || !brew_ptr(a, 4))
            return BREW_EBADPARM;
        dib = brew_dib_new(brew_arg(4), brew_arg(3), b);
        if (!dib)
            return BREW_ENOMEMORY;
        brew_w32(a, dib);
        return 0;
    }
    case 17:
        if (!brew_is_object(b, B_FONT))
            return 0;
        i = brew_display_font(object, a);
        if (!i)
            return 0;
        brew_w32(object + 24 + (a - BREW_FONT_FIRST) * 4, b);
        return i;
    case 18:
    {
        int left = a ? (INT16)brew_u16(a) : 0, top = a ? (INT16)brew_u16(a + 2) : 0;
        int right = a ? left + (INT16)brew_u16(a + 4) : BREW_WIDTH;
        int bottom = a ? top + (INT16)brew_u16(a + 6) : BREW_HEIGHT;
        if (a && !brew_ptr(a, 8))
            return BREW_EBADPARM;
        if (left < 0)
            left = 0;
        if (top < 0)
            top = 0;
        if (right > BREW_WIDTH)
            right = BREW_WIDTH;
        if (bottom > BREW_HEIGHT)
            bottom = BREW_HEIGHT;
        if (right <= left || bottom <= top)
            left = top = right = bottom = 0;
        brew_w32(object + 4, left);
        brew_w32(object + 8, top);
        brew_w32(object + 12, right);
        brew_w32(object + 16, bottom);
        return 0;
    }
    case 19:
        if (!a || !brew_ptr(a, 8))
            return BREW_EBADPARM;
        brew_w16(a, clip_left);
        brew_w16(a + 2, clip_top);
        brew_w16(a + 4, clip_right - clip_left);
        brew_w16(a + 6, clip_bottom - clip_top);
        return 0;
    case 20:
    {
        UINT32 clone;
        if (!a || !brew_ptr(a, 4))
            return BREW_EBADPARM;
        clone = brew_object(B_DISPLAY);
        brew_w32(a, clone);
        if (!clone)
            return BREW_ENOMEMORY;
        memcpy(brew_ptr(clone + 4, 56), brew_ptr(object + 4, 56), 56);
        for (i = 0; i < BREW_FONT_COUNT; i++)
            brew_ref(brew_u32(clone + 24 + i * 4), B_FONT, 0);
        return 0;
    }
    case 22:
        return 1;
    default:
        return 0;
    }
}
static UINT32 brew_font_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), c = brew_arg(3);
    if (slot == 2)
    {
        if (!b || !brew_ptr(b, 4))
            return BREW_EBADPARM;
        brew_w32(b, 0);
        if (a != 0x01000001 && a != 0x01001022)
            return BREW_ECLASSNOTSUPPORT;
        brew_ref(object, B_FONT, 0);
        brew_w32(b, object);
        return 0;
    }
    if (slot == 3)
    {
        UINT32 text = brew_arg(4), count = brew_arg(5), rect = brew_arg(8), flags = brew_arg(9);
        unsigned limit;
        UINT32 w, h, pitch;
        UINT32* pixels = (UINT32*)brew_bitmap_pixels(a, &w, &h, &pitch);
        int left = 0, top = 0, right = BREW_WIDTH, bottom = BREW_HEIGHT;
        /* Direct IFont is strict: nChars is explicit, and pDst must be an
           actual bitmap. It uses the bitmap bounds, not a display's clip. */
        if (!pixels)
            return BREW_EUNSUPPORTED;
        if ((INT32)count < 0 || !brew_text_limit(text, count, 0, &limit)
            || !rect || !brew_ptr(rect, 8))
            return BREW_EBADPARM;
        left = (INT16)brew_u16(rect);
        top = (INT16)brew_u16(rect + 2);
        right = left + (INT16)brew_u16(rect + 4);
        bottom = top + (INT16)brew_u16(rect + 6);
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > (INT32)w) right = w;
        if (bottom > (INT32)h) bottom = h;
        brew_text_draw(object, text, limit, 0, (INT32)b, (INT32)c,
            brew_arg(6) & 0xffffff, brew_arg(7) & 0xffffff, flags & 0x8000,
            left, top, right, bottom, pixels, pitch / 4);
        return 0;
    }
    if (slot == 4)
    {
        UINT32 fits = brew_arg(4), pixels = brew_arg(5), n, width;
        unsigned limit;
        if ((INT32)b < 0 || (INT32)c < 0 || !fits || !pixels
            || !brew_ptr(fits, 4) || !brew_ptr(pixels, 4)
            || !brew_text_limit(a, b, 0, &limit))
            return BREW_EBADPARM;
        width = brew_text_measure(object, a, limit, c, 0, &n);
        brew_w32(fits, n);
        brew_w32(pixels, width);
        return 0;
    }
    if (slot == 5)
    {
        UINT8 info[4] = { 10, 0, 2, 0 };
        if ((INT32)b < 0 || b > sizeof(info))
            return BREW_EUNSUPPORTED;
        if (!a || !brew_ptr(a, b))
            return BREW_EBADPARM;
        memcpy(brew_ptr(a, b), info, b);
        return 0;
    }
    return BREW_EUNSUPPORTED;
}
/* Device and offscreen bitmaps share the same pixel access path. IDIB views
   expose their backing storage directly to guest code. */
static UINT8* brew_bitmap_pixels(UINT32 object, UINT32* width, UINT32* height, UINT32* pitch)
{
    UINT32 w, h, p;
    if (brew_is_object(object, B_DIB))
        object = brew_u32(object + 40);
    if (!brew_is_object(object, B_BITMAP))
        return NULL;
    w = brew_u32(object + 4);
    h = brew_u32(object + 8);
    if (!w || !h)
        return NULL;
    if (brew_u32(object + 20))
    {
        *width = BREW_WIDTH;
        *height = BREW_HEIGHT;
        *pitch = BREW_WIDTH * 4;
        return (UINT8*)brew_framebuffer;
    }
    p = brew_u32(object + 12);
    if (!p || !brew_ptr(p, w * h * 4))
        return NULL;
    *width = w;
    *height = h;
    *pitch = brew_u32(object + 16) ? brew_u32(object + 16) : w * 4;
    return brew_ptr(p, h * (brew_u32(object + 16) ? brew_u32(object + 16) : w * 4));
}
static UINT32 brew_bitmap_new(UINT32 width, UINT32 height, UINT32 device)
{
    UINT32 object, pixels;
    if (!width || !height || width > 4096 || height > 4096)
        return 0;
    object = brew_object(B_BITMAP);
    if (!object)
        return 0;
    brew_w32(object + 4, width);
    brew_w32(object + 8, height);
    brew_w32(object + 16, width * 4);
    brew_w32(object + 20, device ? 1 : 0);
    brew_w32(object + 28, 32);
    brew_w32(object + 24, 0xff00ff); /* SDK default color bitmap mask. */
    if (!device)
    {
        pixels = brew_alloc(width * height * 4);
        if (!pixels)
        {
            brew_free(object);
            return 0;
        }
        memset(brew_ptr(pixels, width * height * 4), 0, width * height * 4);
        brew_w32(object + 12, pixels);
    }
    else
    {
        brew_w32(object + 12, BREW_FRAMEBUFFER);
        brew_w32(object + 60, 0x1000); /* permanent device bitmap */
    }
    return object;
}
static UINT32 brew_bitmap_device(void)
{
    if (!brew_device_bitmap || !brew_is_object(brew_device_bitmap, B_BITMAP))
        brew_device_bitmap = brew_bitmap_new(BREW_WIDTH, BREW_HEIGHT, 1);
    return brew_device_bitmap;
}
static UINT32 brew_bitmap_rop(UINT32 old, UINT32 src, UINT32 rop, UINT32 transparent, UINT32 use_transparent)
{
    if ((rop & 15) == 7 && use_transparent && src == transparent)
        return old;
    switch (rop & 15)
    {
    case 0: /* AEE_RO_OR */
        return old | src;
    case 1: /* AEE_RO_XOR */
        return old ^ src;
    case 2: /* AEE_RO_COPY */
        return src;
    case 3: /* AEE_RO_NOT */
        return (~src) & 0xffffff;
    case 5: /* AEE_RO_MERGENOT */
        return (~(old | src)) & 0xffffff;
    case 6: /* AEE_RO_ANDNOT */
        return old & ~src;
    case 7: /* AEE_RO_TRANSPARENT */
        return src == transparent ? old : src;
    case 8: /* AEE_RO_AND */
        return old & src;
    case 9: /* AEE_RO_BLEND */
    {
        UINT32 a = (src >> 24) & 255;
        if (!a)
            return old;
        return (((src >> 16) & 255) * a + ((old >> 16) & 255) * (255 - a)) / 255 << 16
            | (((src >> 8) & 255) * a + ((old >> 8) & 255) * (255 - a)) / 255 << 8
            | ((src & 255) * a + (old & 255) * (255 - a)) / 255;
    }
    default:
        return src;
    }
}
static void brew_dib_sync(UINT32 dib)
{
    UINT32 bitmap = brew_u32(dib + 40);
    if (!brew_is_object(bitmap, B_BITMAP))
        return;
    brew_w32(bitmap + 4, brew_u16(dib + 20));
    brew_w32(bitmap + 8, brew_u16(dib + 22));
    brew_w32(bitmap + 12, brew_u32(dib + 8));
    brew_w32(bitmap + 16, (UINT32)(INT32)(INT16)brew_u16(dib + 24));
    brew_w32(bitmap + 24, brew_u32(dib + 16));
    brew_w32(bitmap + 28, *brew_ptr(dib + 28, 1));
    brew_w32(bitmap + 32, brew_u32(dib + 16) != 0);
    brew_w32(bitmap + 36, brew_u32(dib + 12));
}
static UINT32 brew_dib_new(UINT32 width, UINT32 height, UINT32 depth)
{
    UINT32 bitmap, dib, pitch, pixels, palette = 0, total;
    if (!width || !height || width > 4096 || height > 4096)
        return 0;
    if (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32)
        depth = 32;
    pitch = ((width * depth + 31) / 32) * 4;
    total = pitch * height;
    bitmap = brew_object(B_BITMAP);
    if (!bitmap)
        return 0;
    pixels = brew_alloc(total);
    if (!pixels)
    {
        brew_free(bitmap);
        return 0;
    }
    memset(brew_ptr(pixels, total), 0, total);
    if (depth <= 8)
    {
        palette = brew_alloc(256 * 4);
        if (!palette)
        {
            brew_free(pixels);
            brew_free(bitmap);
            return 0;
        }
        memset(brew_ptr(palette, 256 * 4), 0, 256 * 4);
    }
    brew_w32(bitmap + 4, width);
    brew_w32(bitmap + 8, height);
    brew_w32(bitmap + 12, pixels);
    brew_w32(bitmap + 16, pitch);
    brew_w32(bitmap + 24, 0xff00ff);
    brew_w32(bitmap + 28, depth);
    brew_w32(bitmap + 36, palette);
    dib = brew_object(B_DIB);
    if (!dib)
    {
        if (palette)
            brew_free(palette);
        brew_free(pixels);
        brew_free(bitmap);
        return 0;
    }
    brew_w32(dib + 8, pixels);
    brew_w32(dib + 12, palette);
    brew_w32(dib + 16, 0xff00ff);
    brew_w16(dib + 20, width);
    brew_w16(dib + 22, height);
    brew_w16(dib + 24, pitch);
    brew_w16(dib + 26, depth <= 8 ? (1 << depth) : 0);
    *brew_ptr(dib + 28, 1) = depth;
    *brew_ptr(dib + 29, 1) = depth <= 8 ? 0 : depth == 16 ? 16 : 24;
    brew_w32(dib + 40, bitmap);
    return dib;
}
static UINT32 brew_bitmap_resolve(UINT32 object)
{
    if (brew_is_object(object, B_DIB))
    {
        brew_dib_sync(object);
        return brew_u32(object + 40);
    }
    return object;
}
static UINT32 brew_bitmap_get(UINT32 object, UINT32 x, UINT32 y)
{
    UINT32 w = brew_u32(object + 4), h = brew_u32(object + 8), pitch = brew_u32(object + 16);
    UINT32 depth = brew_u32(object + 28), palette = brew_u32(object + 36), index;
    const UINT8* data;
    if (x >= w || y >= h)
        return 0;
    data = brew_ptr(brew_u32(object + 12) + y * pitch, pitch);
    if (!data)
        return 0;
    if (depth == 32)
        return brew_le32(data + x * 4) & 0xffffff;
    if (depth == 24)
        return (data[x * 3 + 2] << 16) | (data[x * 3 + 1] << 8) | data[x * 3];
    if (depth == 16)
    {
        UINT32 v = data[x * 2] | (data[x * 2 + 1] << 8);
        return ((v & 0xf800) << 8) | ((v & 0x7e0) << 5) | ((v & 0x1f) << 3);
    }
    if (depth == 8)
        index = data[x];
    else if (depth == 4)
        index = (data[x / 2] >> ((1 - (x & 1)) * 4)) & 15;
    else
        index = (data[x / 8] >> (7 - (x & 7))) & 1;
    return palette ? brew_u32(palette + index * 4) & 0xffffff
                   : (depth == 8 ? index * 0x010101 : depth == 4 ? index * 0x111111 : index * 0xffffff);
}
static void brew_bitmap_put(UINT32 object, UINT32 x, UINT32 y, UINT32 color)
{
    UINT32 w = brew_u32(object + 4), h = brew_u32(object + 8), pitch = brew_u32(object + 16);
    UINT32 depth = brew_u32(object + 28), palette = brew_u32(object + 36), index;
    UINT8* data;
    if (x >= w || y >= h)
        return;
    data = brew_ptr(brew_u32(object + 12) + y * pitch, pitch);
    if (!data)
        return;
    if (depth == 32)
    {
        brew_w32(brew_u32(object + 12) + y * pitch + x * 4, color & 0xffffff);
        return;
    }
    if (depth == 24)
    {
        UINT8* p = data + x * 3;
        p[0] = color;
        p[1] = color >> 8;
        p[2] = color >> 16;
        return;
    }
    if (depth == 16)
    {
        UINT32 v = ((color >> 8) & 0xf800) | ((color >> 5) & 0x7e0) | ((color >> 3) & 0x1f);
        data[x * 2] = v;
        data[x * 2 + 1] = v >> 8;
        return;
    }
    index = 0;
    if (palette)
    {
        UINT32 best = 0xffffffff, i, count = depth == 8 ? 256 : depth == 4 ? 16 : 2;
        for (i = 0; i < count; i++)
        {
            UINT32 entry = brew_u32(palette + i * 4), dr, dg, db, distance;
            dr = ((entry >> 16) & 0xff) - ((color >> 16) & 0xff);
            dg = ((entry >> 8) & 0xff) - ((color >> 8) & 0xff);
            db = (entry & 0xff) - (color & 0xff);
            distance = dr * dr + dg * dg + db * db;
            if (distance < best)
            {
                best = distance;
                index = i;
                if (!distance)
                    break;
            }
        }
    }
    if (depth == 8)
        data[x] = index;
    else if (depth == 4)
        data[x / 2] = (data[x / 2] & (x & 1 ? 0xf0 : 0x0f)) | (index << ((1 - (x & 1)) * 4));
    else
        data[x / 8] = (data[x / 8] & ~(0x80 >> (x & 7))) | (index << (7 - (x & 7)));
}
static void brew_bitmap_set(UINT32 object, UINT32 x, UINT32 y, UINT32 color, UINT32 rop)
{
    UINT32 transparent = brew_u32(object + 24), use = brew_u32(object + 32);
    brew_bitmap_put(object, x, y, brew_bitmap_rop(brew_bitmap_get(object, x, y), color, rop, transparent, use));
}
static void brew_bitmap_blit(UINT32 dst, int xD, int yD, int dx, int dy,
    UINT32 src, int xS, int yS, UINT32 rop)
{
    UINT32 dw, dh, dp, sw, sh, sp, transparent, use, sx, sy;
    UINT8 *dd, *sd;
    if (brew_is_object(src, B_DIB))
    {
        brew_dib_sync(src);
        src = brew_u32(src + 40);
    }
    if (brew_is_object(dst, B_DIB))
    {
        brew_dib_sync(dst);
        dst = brew_u32(dst + 40);
    }
    dd = brew_bitmap_pixels(dst, &dw, &dh, &dp);
    sd = brew_bitmap_pixels(src, &sw, &sh, &sp);
    if (!dd || !sd)
        return;
    transparent = brew_u32(src + 24);
    use = brew_u32(src + 32);
    if (dx < 0)
        dx = 0;
    if (dy < 0)
        dy = 0;
    if (dx > (int)dw)
        dx = dw;
    if (dy > (int)dh)
        dy = dh;
    for (sy = 0; sy < (UINT32)dy; sy++)
    {
        int syy = yS + (int)sy;
        if (syy < 0 || syy >= (int)sh || yD + (int)sy < 0 || yD + (int)sy >= (int)dh)
            continue;
        for (sx = 0; sx < (UINT32)dx; sx++)
        {
            int sxx = xS + (int)sx;
            if (sxx < 0 || sxx >= (int)sw || xD + (int)sx < 0 || xD + (int)sx >= (int)dw)
                continue;
            brew_bitmap_put(dst, xD + sx, yD + sy,
                brew_bitmap_rop(brew_bitmap_get(dst, xD + sx, yD + sy), brew_bitmap_get(src, sxx, syy), rop,
                    transparent, use));
        }
    }
}
static UINT32 brew_bitmap_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), d = brew_arg(4);
    switch (slot)
    {
    case 2: /* QueryInterface(IBitmap*, iid, void**) */
        if (!b || !brew_ptr(b, 4))
            return BREW_EBADPARM;
        brew_w32(b, 0);
        if (a == 0x01001045 || a == 0x0100102c) /* IDIB / DIB 2.0: public SDK structure. */
        {
            UINT32 dib = brew_object(B_DIB), pixels = brew_u32(object + 12);
            UINT32 width = brew_u32(object + 4), height = brew_u32(object + 8);
            UINT32 depth = brew_u32(object + 28), palette = brew_u32(object + 36);
            if (!dib)
                return BREW_ENOMEMORY;
            if (!pixels)
            {
                pixels = brew_alloc(width * height * 4);
                if (!pixels) { brew_free(dib); return BREW_ENOMEMORY; }
                brew_w32(object + 12, pixels);
            }
            brew_w32(dib + 8, pixels);
            brew_w32(dib + 12, palette);
            brew_w32(dib + 16, brew_u32(object + 24));
            brew_w16(dib + 20, width);
            brew_w16(dib + 22, height);
            brew_w16(dib + 24, brew_u32(object + 16));
            brew_w16(dib + 26, depth <= 8 ? (1 << depth) : 0);
            *brew_ptr(dib + 28, 1) = depth;
            *brew_ptr(dib + 29, 1) = depth <= 8 ? 0 : depth == 16 ? 16 : 24;
            brew_w32(dib + 40, object);
            brew_ref(object, B_BITMAP, 0);
            brew_w32(b, dib);
            return 0;
        }
        if (a != 0x01001021 && a != 0x01000001)
            return BREW_EUNSUPPORTED;
        brew_ref(object, B_BITMAP, 0);
        brew_w32(b, object);
        return 0;
    case 3: /* RGBVAL is 0xBBGGRR00; native pixels are 0x00RRGGBB. */
        return ((a & 0xff00) << 8) | ((a >> 8) & 0xff00) | (a >> 24);
    case 4: /* NativeToRGB */
        return ((a & 0xff) << 24) | ((a & 0xff00) << 8) | ((a >> 8) & 0xff00);
    case 5: /* DrawPixel */
        brew_bitmap_set(object, a, b, c, d);
        return 0;
    case 6: /* GetPixel */
    {
        UINT32 w = brew_u32(object + 4), h = brew_u32(object + 8);
        if (!c || !brew_ptr(c, 4))
            return BREW_EBADPARM;
        if (a >= w || b >= h)
            return BREW_EBADPARM;
        brew_w32(c, brew_bitmap_get(object, a, b));
        return 0;
    }
    case 7: /* SetPixels */
    {
        UINT32 k;
        if (!b || !brew_ptr(b, a * 4))
            return BREW_EBADPARM;
        for (k = 0; k < a; k++)
            brew_bitmap_set(object, (INT16)brew_u16(b + k * 4), (INT16)brew_u16(b + k * 4 + 2), c, d);
        return 0;
    }
    case 8: /* DrawHScanline(y, xMin, xMax, color, rop) */
    {
        UINT32 w = brew_u32(object + 4), h = brew_u32(object + 8), x;
        UINT32 rop = brew_arg(5), transparent = brew_u32(object + 24), use = brew_u32(object + 32);
        if (a >= h)
            return BREW_EBADPARM;
        if (b < 0)
            b = 0;
        if (c > w)
            c = w;
        if (c <= b)
            return 0;
        for (x = b; x < c; x++)
            brew_bitmap_put(object, x, a,
                brew_bitmap_rop(brew_bitmap_get(object, x, a), d, rop, transparent, use));
        return 0;
    }
    case 9: /* FillRect(rect, color, rop) */
    {
        int rx, ry, rw, rh, x, y;
        UINT32 rop = brew_arg(3), transparent = brew_u32(object + 24), use = brew_u32(object + 32);
        UINT32 w = brew_u32(object + 4), h = brew_u32(object + 8);
        if (!a || !brew_ptr(a, 8))
            return BREW_EBADPARM;
        rx = (INT16)brew_u16(a);
        ry = (INT16)brew_u16(a + 2);
        rw = (INT16)brew_u16(a + 4);
        rh = (INT16)brew_u16(a + 6);
        for (y = ry; y < ry + rh; y++)
            for (x = rx; x < rx + rw; x++)
                if (x >= 0 && y >= 0 && x < (int)w && y < (int)h)
                    brew_bitmap_put(object, x, y,
                        brew_bitmap_rop(brew_bitmap_get(object, x, y), b, rop, transparent, use));
        return 0;
    }
    case 10: /* BltIn(dst x,y,dx,dy, src, xSrc, ySrc, rop) */
        brew_bitmap_blit(object, a, b, c, d, brew_arg(5), brew_arg(6), brew_arg(7), brew_arg(8));
        return 0;
    case 11: /* BltOut(src x,y,dx,dy, dst, xSrc, ySrc, rop) */
        brew_bitmap_blit(brew_arg(5), a, b, c, d, object, brew_arg(6), brew_arg(7), brew_arg(8));
        return 0;
    case 12: /* GetInfo */
        if (!a || !brew_ptr(a, 12) || (INT32)b < 12)
            return BREW_EBADPARM;
        brew_w32(a, brew_u32(object + 4));
        brew_w32(a + 4, brew_u32(object + 8));
        brew_w32(a + 8, brew_u32(object + 28));
        return 0;
    case 13: /* CreateCompatibleBitmap(pp, w, h) */
    {
        UINT32 bitmap;
        if (!a || !brew_ptr(a, 4))
            return BREW_EBADPARM;
        bitmap = brew_bitmap_new(b, c, 0);
        if (!bitmap)
            return BREW_ENOMEMORY;
        brew_w32(a, bitmap);
        return 0;
    }
    case 14: /* SetTransparencyColor */
        brew_w32(object + 24, a);
        brew_w32(object + 32, 1);
        return 0;
    case 15: /* GetTransparencyColor */
        if (!a || !brew_ptr(a, 4))
            return BREW_EBADPARM;
        brew_w32(a, brew_u32(object + 24));
        return 0;
    default:
        return BREW_EUNSUPPORTED;
    }
}

static void brew_line(int x0, int y0, int x1, int y1, UINT32 color)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, error = dx + dy;
    int left, top, right, bottom;
    brew_get_clip(0, &left, &top, &right, &bottom);
    for (;;)
    {
        int twice = 2 * error;
        if (x0 >= left && x0 < right && y0 >= top && y0 < bottom)
            brew_framebuffer[y0 * BREW_WIDTH + x0] = color;
        if (x0 == x1 && y0 == y1)
            break;
        if (twice >= dy)
        {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx)
        {
            error += dx;
            y0 += sy;
        }
    }
}
static UINT32 brew_graphics_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), old;
    if (slot == 4 || slot == 8 || slot == 2)
    {
        unsigned field = slot == 4 ? 4 : slot == 8 ? 8 : 16;
        old = brew_u32(object + field);
        brew_w32(object + field, ((a & 255) << 16) | ((b & 255) << 8) | (c & 255));
        return old << 8;
    }
    if (slot == 6)
    {
        old = brew_u32(object + 12);
        brew_w32(object + 12, a);
        return old;
    }
    if (slot == 20) /* DrawPoint(AEEPoint) */
    {
        int x, y;
        if (!a || !brew_ptr(a, 4))
            return 14;
        x = (INT16)brew_u16(a);
        y = (INT16)brew_u16(a + 2);
        if (x >= 0 && y >= 0 && x < BREW_WIDTH && y < BREW_HEIGHT)
            brew_framebuffer[y * BREW_WIDTH + x] = brew_u32(object + 4);
        return 0;
    }
    if (slot == 21) /* DrawLine(AEELine) */
    {
        if (!a || !brew_ptr(a, 8))
            return 14;
        brew_line((INT16)brew_u16(a), (INT16)brew_u16(a + 2), (INT16)brew_u16(a + 4),
            (INT16)brew_u16(a + 6), brew_u32(object + 4));
        return 0;
    }
    if (slot == 22) /* DrawRect(AEERect) */
    {
        int x, y, w, h, xx, yy;
        UINT32 color = brew_u32(object + 4), fill = brew_u32(object + 8), mode = brew_u32(object + 12);
        int left, top, right, bottom;
        if (!a || !brew_ptr(a, 8))
            return 14;
        x = (INT16)brew_u16(a);
        y = (INT16)brew_u16(a + 2);
        w = (INT16)brew_u16(a + 4);
        h = (INT16)brew_u16(a + 6);
        brew_get_clip(0, &left, &top, &right, &bottom);
        if (mode)
        {
            for (yy = y; yy < y + h; yy++)
                for (xx = x; xx < x + w; xx++)
                    if (xx >= left && xx < right && yy >= top && yy < bottom)
                        brew_framebuffer[yy * BREW_WIDTH + xx] = fill;
        }
        else
        {
            for (xx = x; xx < x + w; xx++)
                if (xx >= left && xx < right)
                {
                    if (y >= top && y < bottom)
                        brew_framebuffer[y * BREW_WIDTH + xx] = color;
                    if (y + h - 1 >= top && y + h - 1 < bottom)
                        brew_framebuffer[(y + h - 1) * BREW_WIDTH + xx] = color;
                }
            for (yy = y; yy < y + h; yy++)
                if (yy >= top && yy < bottom)
                {
                    if (x >= left && x < right)
                        brew_framebuffer[yy * BREW_WIDTH + x] = color;
                    if (x + w - 1 >= left && x + w - 1 < right)
                        brew_framebuffer[yy * BREW_WIDTH + x + w - 1] = color;
                }
        }
        return 0;
    }
    if (slot == 30) /* ClearRect(AEERect) */
    {
        int x, y, w, h, xx, yy, left, top, right, bottom;
        UINT32 fill = brew_u32(object + 16);
        if (!a || !brew_ptr(a, 8))
            return 14;
        x = (INT16)brew_u16(a);
        y = (INT16)brew_u16(a + 2);
        w = (INT16)brew_u16(a + 4);
        h = (INT16)brew_u16(a + 6);
        brew_get_clip(0, &left, &top, &right, &bottom);
        for (yy = y; yy < y + h; yy++)
            for (xx = x; xx < x + w; xx++)
                if (xx >= left && xx < right && yy >= top && yy < bottom)
                    brew_framebuffer[yy * BREW_WIDTH + xx] = fill;
        return 0;
    }
    if (slot == 32 || slot == 31 || slot == 16) /* Update/DoubleBuffer/ClearViewport */
        return 0;
    if (slot == 27)
    {
        int x[3], y[3], i, xx, yy, minx = BREW_WIDTH - 1, miny = BREW_HEIGHT - 1, maxx = 0, maxy = 0;
        int left, top, right, bottom;
        UINT32 color = brew_u32(object + 8);
        if (!brew_ptr(a, 12))
            return 14;
        for (i = 0; i < 3; i++)
        {
            x[i] = (INT16)brew_u16(a + i * 4);
            y[i] = (INT16)brew_u16(a + i * 4 + 2);
            if (x[i] < minx)
                minx = x[i];
            if (x[i] > maxx)
                maxx = x[i];
            if (y[i] < miny)
                miny = y[i];
            if (y[i] > maxy)
                maxy = y[i];
        }
        brew_get_clip(0, &left, &top, &right, &bottom);
        if (minx < left)
            minx = left;
        if (maxx >= right)
            maxx = right - 1;
        if (miny < top)
            miny = top;
        if (maxy >= bottom)
            maxy = bottom - 1;
        if (brew_u32(object + 12))
            for (yy = miny; yy <= maxy; yy++)
                for (xx = minx; xx <= maxx; xx++)
                {
                    INT64 e0 = (INT64)(x[1] - x[0]) * (yy - y[0]) - (INT64)(y[1] - y[0]) * (xx - x[0]);
                    INT64 e1 = (INT64)(x[2] - x[1]) * (yy - y[1]) - (INT64)(y[2] - y[1]) * (xx - x[1]);
                    INT64 e2 = (INT64)(x[0] - x[2]) * (yy - y[2]) - (INT64)(y[0] - y[2]) * (xx - x[2]);
                    if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0))
                        brew_framebuffer[yy * BREW_WIDTH + xx] = color;
                }
        for (i = 0; i < 3; i++)
            brew_line(x[i], y[i], x[(i + 1) % 3], y[(i + 1) % 3], brew_u32(object + 4));
        return 0;
    }
    return 0;
}
static READ32_HANDLER(brew_trap_r)
{
    UINT32 address = BREW_TRAPS + offset * 4, result = 0;
    unsigned type = offset / 1024, slot = offset % 1024;
    if (address == BREW_RETURN)
    {
        if (brew_active_allocation)
        {
            brew_free(brew_active_allocation);
            brew_active_allocation = 0;
        }
        if (brew_ext_loading >= 0)
        {
            brew_exts[brew_ext_loading].module = brew_u32(BREW_OUTPUT + 0x300);
            brew_ext_loading++;
            if (brew_ext_loading < (int)brew_ext_count)
            {
                struct brew_ext* ext = &brew_exts[brew_ext_loading];
                brew_w32(BREW_OUTPUT + 0x300, 0);
                brew_prepare_call(ext->entry, brew_shell, 0, BREW_OUTPUT + 0x300, 0);
                return 0xe12fff1c;
            }
            brew_ext_loading = -1;
            brew_phase = 0;
            brew_prepare_call(BREW_MODULE + brew_mod_entry, brew_shell, 0, BREW_OUTPUT, 0);
            return 0xe12fff1c;
        }
        if (brew_call_depth > 0)
        {
            struct brew_saved_call* ctx = &brew_saved_calls[brew_call_depth - 1];
            UINT32 ret = activecpu_get_reg(ARM7_R0);
            UINT32 object = ctx->out ? brew_u32(ctx->out) : 0;
            brew_call_depth--;
            if (ctx->pending)
            {
                if (ret == 0 && object)
                {
                    brew_restore_call(ctx, 0);
                }
                else
                {
                    unsigned next = ctx->extension + 1;
                    while (next < brew_ext_count && !brew_exts[next].module)
                        next++;
                    if (next < brew_ext_count)
                    {
                        UINT32 module = brew_exts[next].module;
                        UINT32 fn = brew_u32(brew_u32(module) + 8);
                        ctx->extension = next;
                        brew_call_depth++;
                        brew_w32(ctx->out, 0);
                        brew_prepare_call(fn, module, brew_shell, ctx->classid, ctx->out);
                        activecpu_set_reg(ARM7_CPSR, 0x1f | ((fn & 1) ? 0x20 : 0));
                    }
                    else
                        brew_restore_call(ctx, BREW_ECLASSNOTSUPPORT);
                }
            }
            else
                brew_restore_call(ctx, ret);
            return 0xe12fff1c;
        }
        if (brew_phase == 0)
        {
            brew_module = brew_u32(BREW_OUTPUT);
            brew_phase = 1;
            if (!brew_module || !brew_ptr(brew_module, 4) || !brew_u32(brew_u32(brew_module) + 8))
            {
                brew_phase = 3;
                cpu_spinuntil_trigger(BREW_WAKE);
                return 0xeafffffe;
            }
            brew_prepare_call(
                brew_u32(brew_u32(brew_module) + 8), brew_module, brew_shell, brew_classid, BREW_OUTPUT + 4);
            return 0xe12fff1c;
        }
        if (brew_phase == 1)
        {
            UINT32 start = BREW_OUTPUT + 0x200;
            UINT32 display = brew_u32(BREW_DISPLAY_STATE);
            brew_applet = brew_u32(BREW_OUTPUT + 4);
            brew_phase = 2;
            if (!display)
            {
                display = brew_object(B_DISPLAY);
                if (display)
                    brew_w32(BREW_DISPLAY_STATE, display);
            }
            /* EVT_APP_START (0) carries AEEAppStart: error, clsApp, pDisplay,
               rc (4 x int16), pszArgs. */
            if (brew_ptr(start, 24))
            {
                memset(brew_ptr(start, 24), 0, 24);
                brew_w32(start + 4, brew_classid);
                brew_w32(start + 8, display);
                brew_w16(start + 12, 0);
                brew_w16(start + 14, 0);
                brew_w16(start + 16, BREW_WIDTH);
                brew_w16(start + 18, BREW_HEIGHT);
                brew_w32(start + 20, 0);
            }
            else
                start = 0;
            if (!brew_applet || !brew_ptr(brew_applet, 4) || !brew_u32(brew_u32(brew_applet) + 8))
            {
                brew_phase = 3;
                cpu_spinuntil_trigger(BREW_WAKE);
                return 0xeafffffe;
            }
            brew_prepare_call(brew_u32(brew_u32(brew_applet) + 8), brew_applet, 0, 0, start);
            return 0xe12fff1c;
        }
        brew_phase = 3;
        cpu_spinuntil_trigger(BREW_WAKE);
        return 0xeafffffe;
    }
    if (type == B_HELPERS)
        result = brew_helper(slot);
    else if (type < B_TYPES && slot < 2)
        result = brew_reference(type, slot);
    else if (type == B_SHELL)
        result = brew_shell_method(slot);
    else if (type == B_DISPLAY)
        result = brew_display_method(slot);
    else if (type == B_IMAGE)
        result = brew_image_method(slot);
    else if (type == B_FILEMGR)
        result = brew_filemgr_method(slot);
    else if (type == B_FILE)
        result = brew_file_method(slot);
    else if (type == B_FONT)
        result = brew_font_method(slot);
    else if (type == B_BITMAP)
        result = brew_bitmap_method(slot);
    else if (type == B_DIB)
    {
        UINT32 dib = brew_arg(0);
        brew_dib_sync(dib);
        activecpu_set_reg(ARM7_R0, brew_u32(dib + 40));
        result = brew_bitmap_method(slot);
    }
    else if (type == B_MEDIA)
        result = brew_media_method(slot);
    else if (type == B_GRAPHICS)
        result = brew_graphics_method(slot);
    else if (type == B_STREAM)
        result = brew_stream_method(slot);
    else if (type == B_NETERROR)
    {
        /* KDDI network error dialog: slot 2 is HandleEvent, slot 3 opens it.
           The caller releases the dialog after HandleEvent returns TRUE. */
        if (slot == 2)
            result = brew_arg(1) == 0x100;
        else if (slot == 3)
            result = 0;
        else
            result = BREW_EUNSUPPORTED;
    }
    else if (type == B_WEB)
        result = brew_web_method(slot);
    else if (type == B_WEBRESP)
        result = slot == 6 ? brew_arg(0) + 4 : BREW_EUNSUPPORTED;
    else if (type == B_WEBUTIL)
        result = brew_webutil_method(slot);
    else if (type == B_RANDOM)
        result = brew_random_method(slot);
    else if (type == B_SOUND10)
        result = brew_sound10_method(slot);
    else if (type == B_RAMCACHE)
        result = brew_ramcache_method(slot);
    else if (type == B_SOURCE)
        result = brew_source_method(slot);
    else if (type == B_SOURCEUTIL)
        result = brew_sourceutil_method(slot);
    else if (type == B_STUB)
    {
        if (slot == 2)
        {
            if (brew_arg(3) && brew_ptr(brew_arg(3), 4))
                brew_w32(brew_arg(3), 0);
            result = BREW_EUNSUPPORTED;
        }
        else
            result = 0;
    }
    else if (type == B_HEAP)
    {
        switch (slot)
        {
        case 0:
        case 1:
            result = 1;
            break;
        case 2:
            result = brew_alloc(brew_arg(1));
            break;
        case 3:
            result = brew_realloc(brew_arg(1), brew_arg(2));
            break;
        case 4:
            brew_free(brew_arg(1));
            result = 0;
            break;
        case 5:
        {
            const char* source = brew_string(brew_arg(1));
            unsigned n = source ? (strlen(source) + 1) * 2 : 0, j;
            result = n ? brew_alloc(n) : 0;
            if (result)
                for (j = 0; j < n / 2; j++)
                    brew_w16(result + j * 2, (UINT8)source[j]);
            break;
        }
        case 6:
            result = brew_arg(1) < BREW_HEAP_LIMIT - brew_heap;
            break;
        case 7:
            /* SDK: total used memory, subtracted from AEEDeviceInfo.dwRAM. */
            result = brew_heap_used();
            break;
        case 8:
        {
            UINT32 maximum = brew_arg(2), current = brew_arg(3);
            if (maximum)
                brew_w32(maximum, BREW_HEAP_LIMIT - BREW_HEAP_BASE);
            if (current)
                brew_w32(current, brew_heap_used());
            result = 0;
            break;
        }
        default:
            result = 0;
            break;
        }
    }
    else if (slot < 2)
        result = 1;
    if (brew_nested_started)
    {
        brew_nested_started = 0;
        return 0xe12fff1c;
    }
    activecpu_set_reg(ARM7_R0, result);
    return 0xe12fff1e;
}
static void brew_boot(int param)
{
    cpuintrf_push_context(0);
    activecpu_set_reg(ARM7_CPSR, 0x1f);
    activecpu_set_reg(ARM7_R13, BREW_RAM_SIZE - 256);
    if (brew_ext_count)
    {
        brew_ext_loading = 0;
        brew_w32(BREW_OUTPUT + 0x300, 0);
        brew_prepare_call(brew_exts[0].entry, brew_shell, 0, BREW_OUTPUT + 0x300, 0);
        activecpu_set_reg(ARM7_PC, brew_exts[0].entry);
    }
    else
    {
        brew_prepare_call(BREW_MODULE + brew_mod_entry, brew_shell, 0, BREW_OUTPUT, 0);
        activecpu_set_reg(ARM7_PC, BREW_MODULE + brew_mod_entry);
    }
    cpuintrf_pop_context();
}
static UINT16 brew_event_code[64], brew_event_key[64];
static UINT32 brew_event_data[64], brew_event_head, brew_event_tail;
static UINT32 brew_clock_fraction;
static int brew_enqueue(UINT16 code, UINT16 key, UINT32 data)
{
    unsigned next = (brew_event_tail + 1) % 64;
    if (next == brew_event_head)
        return 0;
    brew_event_code[brew_event_tail] = code;
    brew_event_key[brew_event_tail] = key;
    brew_event_data[brew_event_tail] = data;
    brew_event_tail = next;
    return 1;
}
static void brew_poll_keys(void)
{
    static const UINT16 keys[]
        = { 0xe035, 0xe031, 0xe032, 0xe033, 0xe034, 0xe036, 0xe037, 0xe030, 0xe021, 0xe022, 0xe023, 0xe024,
              0xe025, 0xe026, 0xe027, 0xe028, 0xe029, 0xe02a, 0xe02b, 0xe02c, 0xe02e, 0xe02f };
    UINT32 state = readinputport(0) | (readinputport(1) << 8), changed = state ^ brew_keys;
    unsigned i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        int direction = i >= 1 && i <= 4;
        if (changed & (1 << i))
        {
            if (state & (1 << i))
            {
                brew_enqueue(0x101, keys[i], 0);
                if (direction)
                {
                    brew_enqueue(0x100, keys[i], 0);
                    brew_direction_repeat[i - 1] = brew_clock + 350;
                }
            }
            else
            {
                brew_enqueue(0x102, keys[i], 0);
                if (!direction)
                    brew_enqueue(0x100, keys[i], 0);
            }
        }
        if (direction)
        {
            UINT32* due = &brew_direction_repeat[i - 1];
            if (!(state & (1 << i)))
                *due = 0;
            else if (!*due)
                *due = brew_clock + 350;
            else if ((INT32)(brew_clock - *due) >= 0)
            {
                /* BREW EVT_KEY with SDK KB_AUTOREPEAT, at 100 ms intervals. */
                if (brew_enqueue(0x100, keys[i], 0x40))
                    *due = brew_clock + 100;
            }
        }
    }
    brew_keys = state;
}

static INTERRUPT_GEN(brew_tick)
{
    unsigned i;
    brew_clock_fraction += 1000;
    brew_clock += brew_clock_fraction / 60;
    brew_clock_fraction %= 60;
    brew_poll_keys();
    brew_media_poll();
    if (brew_phase != 3)
        return;
    brew_stream_poll();
    if (brew_phase != 3)
        return;
    brew_source_poll();
    if (brew_phase != 3)
        return;
    if (brew_event_head != brew_event_tail)
    {
        unsigned event = brew_event_head;
        UINT32 handler = brew_u32(brew_u32(brew_applet) + 8);
        brew_event_head = (brew_event_head + 1) % 64;
        if (!handler)
            return;
        cpu_trigger(BREW_WAKE);
        brew_phase = 2;
        brew_prepare_call(
            handler, brew_applet, brew_event_code[event], brew_event_key[event], brew_event_data[event]);
        activecpu_set_reg(ARM7_PC, handler & ~1);
        activecpu_set_reg(ARM7_CPSR, 0x1f | ((handler & 1) ? 0x20 : 0));
        return;
    }
    for (i = 0; i < 64; i++)
        if (brew_timer_fn[i] && (INT32)(brew_clock - brew_timer_due[i]) >= 0)
        {
            UINT32 fn = brew_timer_fn[i], user = brew_timer_user[i], argument = brew_timer_arg[i];
            brew_active_allocation = brew_timer_owned[i];
            brew_timer_owned[i] = 0;
            brew_timer_clear(i);
            cpu_trigger(BREW_WAKE);
            brew_phase = 2;
            brew_prepare_call(fn, user, argument, 0, 0);
            activecpu_set_reg(ARM7_PC, fn & ~1);
            activecpu_set_reg(ARM7_CPSR, 0x1f | ((fn & 1) ? 0x20 : 0));
            break;
        }
}
/* clang-format off */
static MEMORY_READ32_START(brew_readmem)
    { 0x00000000, BREW_RAM_SIZE - 1, MRA32_RAM },
    { BREW_TRAPS, BREW_TRAPS + 0xfffff, brew_trap_r },
MEMORY_END
static MEMORY_WRITE32_START(brew_writemem)
    { 0x00000000, BREW_RAM_SIZE - 1, MWA32_RAM, &brew_ram },
MEMORY_END
    /* clang-format on */

    static NVRAM_HANDLER(brew)
{
    unsigned i;
    UINT8 header[8], length[4];
    if (read_or_write)
    {
        mame_fwrite(file, "BREWHLE1", 8);
        for (i = 1; i < BREW_FILES; i++)
        {
            UINT32 n = brew_file_sizes[i];
            length[0] = n;
            length[1] = n >> 8;
            length[2] = n >> 16;
            length[3] = n >> 24;
            mame_fwrite(file, brew_file_names[i], 64);
            mame_fwrite(file, length, 4);
            if (n)
                mame_fwrite(file, brew_file_data[i], n);
        }
    }
    else
    {
        memset(brew_file_names + 1, 0, sizeof(brew_file_names) - sizeof(brew_file_names[0]));
        memset(brew_file_sizes + 1, 0, sizeof(brew_file_sizes) - sizeof(brew_file_sizes[0]));
        memset(brew_file_data + 1, 0, sizeof(brew_file_data) - sizeof(brew_file_data[0]));
        if (!file || mame_fread(file, header, 8) != 8 || memcmp(header, "BREWHLE1", 8))
            return;
        for (i = 1; i < BREW_FILES; i++)
        {
            UINT32 n;
            if (mame_fread(file, brew_file_names[i], 64) != 64 || mame_fread(file, length, 4) != 4)
                break;
            n = length[0] | (length[1] << 8) | (length[2] << 16) | ((UINT32)length[3] << 24);
            if (n > BREW_FILE_CAPACITY || !memchr(brew_file_names[i], 0, 64))
            {
                brew_file_names[i][0] = 0;
                break;
            }
            if (n && mame_fread(file, brew_file_data[i], n) != n)
            {
                brew_file_names[i][0] = 0;
                break;
            }
            brew_file_sizes[i] = n;
        }
    }
}
static void brew_postload(void)
{
    memset(brew_direction_repeat, 0, sizeof(brew_direction_repeat));
    cpu_trigger(BREW_WAKE);
}
static DRIVER_INIT(brew)
{
    state_save_register_UINT32("brew", 0, "heap", &brew_heap, 1);
    state_save_register_UINT32("brew", 0, "shell", &brew_shell, 1);
    state_save_register_UINT32("brew", 0, "module", &brew_module, 1);
    state_save_register_UINT32("brew", 0, "applet", &brew_applet, 1);
    state_save_register_UINT32("brew", 0, "phase", &brew_phase, 1);
    state_save_register_UINT32("brew", 0, "classid", &brew_classid, 1);
    state_save_register_UINT32("brew", 0, "clock", &brew_clock, 1);
    state_save_register_UINT32("brew", 0, "clock_fraction", &brew_clock_fraction, 1);
    state_save_register_UINT32("brew", 0, "random", &brew_random, 1);
    state_save_register_UINT32("brew", 0, "keys", &brew_keys, 1);
    state_save_register_UINT32("brew", 0, "media_objects", brew_media_objects, 32);
    state_save_register_UINT32("brew", 0, "active_allocation", &brew_active_allocation, 1);
    state_save_register_UINT32("brew", 0, "timer_arg", brew_timer_arg, 64);
    state_save_register_UINT32("brew", 0, "timer_owned", brew_timer_owned, 64);
    state_save_register_UINT32("brew", 0, "timer_fn", brew_timer_fn, 64);
    state_save_register_UINT32("brew", 0, "timer_user", brew_timer_user, 64);
    state_save_register_UINT32("brew", 0, "timer_due", brew_timer_due, 64);
    state_save_register_UINT32("brew", 0, "timer_callback", brew_timer_callback, 64);
    state_save_register_UINT16("brew", 0, "event_code", brew_event_code, 64);
    state_save_register_UINT16("brew", 0, "event_key", brew_event_key, 64);
    state_save_register_UINT32("brew", 0, "event_data", brew_event_data, 64);
    state_save_register_UINT32("brew", 0, "event_head", &brew_event_head, 1);
    state_save_register_UINT32("brew", 0, "event_tail", &brew_event_tail, 1);
    state_save_register_UINT32("brew", 0, "framebuffer", brew_framebuffer, BREW_WIDTH * BREW_HEIGHT);
    state_save_register_UINT32("brew", 0, "colors", brew_colors, 16);
    state_save_register_UINT8("brew", 0, "file_names", (UINT8*)brew_file_names, sizeof(brew_file_names));
    state_save_register_UINT32("brew", 0, "file_sizes", brew_file_sizes, BREW_FILES);
    state_save_register_UINT32("brew", 0, "file_error", &brew_file_error, 1);
    state_save_register_func_postload(brew_postload);
}

static void brew_vfs_add_packaged(const char* name, const UINT8* data, UINT32 size)
{
    int slot;
    if (!name || !*name || !data || !size || size > BREW_FILE_CAPACITY)
        return;
    for (slot = 1; slot < BREW_FILES; slot++)
        if (!brew_file_names[slot][0])
            break;
    if (slot == BREW_FILES)
        return;
    strncpy(brew_file_names[slot], name, sizeof(brew_file_names[slot]) - 1);
    brew_file_names[slot][sizeof(brew_file_names[slot]) - 1] = 0;
    brew_file_sizes[slot] = size;
    memcpy(brew_file_data[slot], data, size);
}
static void brew_vfs_add_data(UINT32 size)
{
    const UINT8* data;
    UINT32 count, table, i;
    if (!size || size < 12)
        return;
    data = memory_region(REGION_USER6);
    if (!data || size > memory_region_length(REGION_USER6) || brew_le32(data) != 0x41544144)
        return;
    count = brew_le32(data + 4);
    table = brew_le32(data + 8);
    if (table > size || count > (size - table) / 12)
        return;
    for (i = 0; i < count; i++)
    {
        UINT32 row = table + i * 12;
        UINT32 name_off = brew_le32(data + row), data_off = brew_le32(data + row + 4);
        UINT32 length = brew_le32(data + row + 8);
        const char* name;
        if (name_off >= size || data_off > size || length > size - data_off)
            continue;
        name = (const char*)data + name_off;
        if (!memchr(name, 0, size - name_off))
            continue;
        brew_vfs_add_packaged(brew_path_basename(name), data + data_off, length);
    }
}
static void brew_vfs_add_bars(void)
{
    UINT32 count, table, i, slot = 1;
    if (!brew_bars_size || brew_bars_size < 12 || brew_le32(brew_ptr(BREW_EXTRA_BARS, 4)) != 0x53524142)
        return;
    count = brew_u32(BREW_EXTRA_BARS + 4);
    table = brew_u32(BREW_EXTRA_BARS + 8);
    if (table > brew_bars_size || count > (brew_bars_size - table) / 12)
        return;
    for (i = 0; i < count && slot < BREW_FILES; i++)
    {
        UINT32 row = BREW_EXTRA_BARS + table + i * 12;
        UINT32 name_off = brew_u32(row), data_off = brew_u32(row + 4), size = brew_u32(row + 8);
        const char* name = brew_string(BREW_EXTRA_BARS + name_off);
        const char* base = brew_path_basename(name);
        if (!name || !base || size > BREW_FILE_CAPACITY || !brew_ptr(BREW_EXTRA_BARS + data_off, size))
            continue;
        strncpy(brew_file_names[slot], base, sizeof(brew_file_names[slot]) - 1);
        brew_file_names[slot][sizeof(brew_file_names[slot]) - 1] = 0;
        brew_file_sizes[slot] = size;
        memcpy(brew_file_data[slot], brew_ptr(BREW_EXTRA_BARS + data_off, size), size);
        slot++;
    }
}
static void brew_load_extensions(UINT32 size)
{
    const UINT8* data = memory_region(REGION_USER7);
    UINT32 count, table, i;
    brew_ext_count = 0;
    if (!size || !data || size < 12 || size > memory_region_length(REGION_USER7)
        || brew_le32(data) != 0x53545845)
        return;
    count = brew_le32(data + 4);
    table = brew_le32(data + 8);
    if (table > size || count > (size - table) / 12)
        return;
    for (i = 0; i < count && brew_ext_count < BREW_EXT_MAX; i++)
    {
        UINT32 row = table + i * 12;
        UINT32 name_off = brew_le32(data + row), data_off = brew_le32(data + row + 4);
        UINT32 length = brew_le32(data + row + 8), base, entry = 0;
        const char* name = name_off < size ? (const char*)data + name_off : NULL;
        if (!name || !memchr(name, 0, size - name_off))
            continue;
        if (strlen(name) < 4 || strcasecmp(name + strlen(name) - 4, ".mod"))
            continue;
        if (data_off > size || length > size - data_off || length > BREW_EXT_SPACING)
            continue;
        base = BREW_EXT_BASE + brew_ext_count * BREW_EXT_SPACING;
        if (!brew_ptr(base, length))
            continue;
        memcpy(brew_ptr(base, length), data + data_off, length);
        if (length >= 0x44 && !memcmp(brew_ptr(base + 8, 4), "BREW", 4))
        {
            UINT32 code = brew_le32(brew_ptr(base + 0x10, 4));
            if (code && code < length)
                entry = code;
        }
        /* The relocation stub reads the prefix before the whole image and
           copies it to the relocated AEEMod_Load, not entry-8 in the header. */
        brew_w32(base - 8, 0x04000100);
        brew_w32(base - 4, BREW_HELPERS);
        brew_exts[brew_ext_count].image = base;
        brew_exts[brew_ext_count].entry = base + entry;
        brew_exts[brew_ext_count].module = 0;
        brew_ext_count++;
    }
}
static MACHINE_INIT(brew)
{
    unsigned type, slot;
    UINT8* mif = memory_region(REGION_USER3);
    UINT8* param = memory_region(REGION_USER4);
    UINT32 mif_size = memory_region_length(REGION_USER3), table = 0, count = 0, i;
    UINT32 mod_size = memory_region_length(REGION_USER1);
    UINT32 bar_size = memory_region_length(REGION_USER2);
    UINT32 param_size = memory_region_length(REGION_USER4);
    UINT32 bars_size = 0, data_size = 0, ext_size = 0;
    brew_bar_size = bar_size;
    brew_bars_size = bars_size;
    if (bars_size && bars_size <= BREW_EXTRA_BARS_CAPACITY && memory_region_length(REGION_USER5) >= bars_size)
        memcpy(brew_ptr(BREW_EXTRA_BARS, bars_size), memory_region(REGION_USER5), bars_size);
    brew_vfs_add_bars();
    brew_vfs_add_data(data_size);
    if (mif_size >= 32)
    {
        table = brew_le32(mif + 16);
        count = brew_le32(mif + 20);
    }
    if (table > mif_size || count >= (mif_size - table) / 4)
        count = 0;
    memset(brew_ram, 0, BREW_RAM_SIZE);
    brew_heap = BREW_HEAP_BASE;
    memcpy(brew_ptr(BREW_MODULE, mod_size), memory_region(REGION_USER1), mod_size);
    memcpy(brew_ptr(BREW_RESOURCES, bar_size), memory_region(REGION_USER2), bar_size);
    brew_mod_entry = 0;
    if (mod_size >= 0x44 && !memcmp(brew_ptr(BREW_MODULE + 8, 4), "BREW", 4))
    {
        UINT32 code = brew_le32(brew_ptr(BREW_MODULE + 0x10, 4));
        if (code && code < mod_size)
            brew_mod_entry = code;
    }
    brew_w32(0, 0xeafffffe);
    /* The elf2mod stub reads the helper table pointer immediately before
       AEEMod_Load.  For a BREW-header module that is inside the header at
       code_offset-4, not at the start of the image. */
    brew_w32(BREW_MODULE - 8, 0x04000100);
    brew_w32(BREW_MODULE - 4, BREW_HELPERS);
    for (type = 0; type < B_TYPES; type++)
        for (slot = 0; slot < 256; slot++)
        {
            UINT32 trap = BREW_TRAPS + type * 4096 + slot * 4;
            brew_w32(BREW_VTABLES + type * 1024 + slot * 4, trap);
            if (type == 0)
                brew_w32(BREW_HELPERS + slot * 4, trap);
        }
    brew_classid = 0;
    for (i = 0; i < count; i++)
    {
        UINT32 a = brew_le32(mif + table + i * 4), b = brew_le32(mif + table + (i + 1) * 4);
        if (a <= mif_size && b <= mif_size && b >= a && b - a == 20 && !brew_le32(mif + a + 4)
            && !brew_le32(mif + a + 12))
        {
            brew_classid = brew_le32(mif + a);
            break;
        }
    }
    brew_shell = brew_object(B_SHELL);
    if (brew_shell)
        brew_w32(brew_shell + 60, 0x1000); /* shell is a process-wide singleton */
    brew_phase = 0;
    brew_clock = 0;
    brew_file_error = 0;
    memset(brew_colors, 0, sizeof(brew_colors));
    brew_colors[1] = 0xffffff;
    brew_colors[2] = 0;
    brew_random = 1;
    brew_keys = 0;
    memset(brew_direction_repeat, 0, sizeof(brew_direction_repeat));
    brew_event_head = brew_event_tail = brew_clock_fraction = 0;
    strcpy(brew_file_names[0], "appparam.txt");
    brew_file_sizes[0] = param_size;
    if (param_size)
        memcpy(brew_file_data[0], param, param_size);
    brew_smaf_reset(brew_ptr);
    memset(brew_media_objects, 0, sizeof(brew_media_objects));
    memset(brew_stream_objects, 0, sizeof(brew_stream_objects));
    memset(brew_source_cb, 0, sizeof(brew_source_cb));
    brew_device_bitmap = 0;
    brew_display_destination = 0;
    brew_ext_count = 0;
    brew_ext_loading = -1;
    brew_call_depth = 0;
    brew_nested_started = 0;
    brew_cache_clear();
    brew_active_allocation = 0;
    memset(brew_timer_arg, 0, sizeof(brew_timer_arg));
    memset(brew_timer_owned, 0, sizeof(brew_timer_owned));
    memset(brew_timer_fn, 0, sizeof(brew_timer_fn));
    memset(brew_timer_callback, 0, sizeof(brew_timer_callback));
    memset(brew_framebuffer, 0, BREW_WIDTH * BREW_HEIGHT * 4);
    brew_load_extensions(ext_size);
    timer_set(TIME_NOW, 0, brew_boot);
}
static VIDEO_START(brew) { return 0; }
static VIDEO_UPDATE(brew)
{
    int x, y;
    for (y = 0; y < BREW_HEIGHT; y++)
        for (x = 0; x < BREW_WIDTH; x++)
        {
            UINT32 c = brew_framebuffer[y * BREW_WIDTH + x];
            *BITMAP_ADDR16(bitmap, y, x) = ((c >> 9) & 0x7c00) | ((c >> 6) & 0x3e0) | ((c >> 3) & 31);
        }
}
/* clang-format off */
INPUT_PORTS_START(brew)
PORT_START
PORT_BITX(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Select", KEYCODE_ENTER, KEYCODE_SPACE)
PORT_BITX(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Up", KEYCODE_UP, IP_JOY_NONE)
PORT_BITX(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Down", KEYCODE_DOWN, IP_JOY_NONE)
PORT_BITX(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Left", KEYCODE_LEFT, IP_JOY_NONE)
PORT_BITX(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Right", KEYCODE_RIGHT, IP_JOY_NONE)
PORT_BITX(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Left Softkey", KEYCODE_F1, IP_JOY_NONE)
PORT_BITX(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Right Softkey", KEYCODE_F2, IP_JOY_NONE)
PORT_BITX(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Clear", KEYCODE_BACKSPACE, IP_JOY_NONE)
PORT_START
PORT_BITX(0x0001, IP_ACTIVE_HIGH, IPT_KEYBOARD, "0", KEYCODE_0, KEYCODE_0_PAD)
PORT_BITX(0x0002, IP_ACTIVE_HIGH, IPT_KEYBOARD, "1", KEYCODE_1, KEYCODE_1_PAD)
PORT_BITX(0x0004, IP_ACTIVE_HIGH, IPT_KEYBOARD, "2", KEYCODE_2, KEYCODE_2_PAD)
PORT_BITX(0x0008, IP_ACTIVE_HIGH, IPT_KEYBOARD, "3", KEYCODE_3, KEYCODE_3_PAD)
PORT_BITX(0x0010, IP_ACTIVE_HIGH, IPT_KEYBOARD, "4", KEYCODE_4, KEYCODE_4_PAD)
PORT_BITX(0x0020, IP_ACTIVE_HIGH, IPT_KEYBOARD, "5", KEYCODE_5, KEYCODE_5_PAD)
PORT_BITX(0x0040, IP_ACTIVE_HIGH, IPT_KEYBOARD, "6", KEYCODE_6, KEYCODE_6_PAD)
PORT_BITX(0x0080, IP_ACTIVE_HIGH, IPT_KEYBOARD, "7", KEYCODE_7, KEYCODE_7_PAD)
PORT_BITX(0x0100, IP_ACTIVE_HIGH, IPT_KEYBOARD, "8", KEYCODE_8, KEYCODE_8_PAD)
PORT_BITX(0x0200, IP_ACTIVE_HIGH, IPT_KEYBOARD, "9", KEYCODE_9, KEYCODE_9_PAD)
PORT_BITX(0x0400, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Star", KEYCODE_Z, IP_JOY_NONE)
PORT_BITX(0x0800, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Pound", KEYCODE_X, IP_JOY_NONE)
PORT_BITX(0x1000, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Send", KEYCODE_F3, IP_JOY_NONE)
PORT_BITX(0x2000, IP_ACTIVE_HIGH, IPT_KEYBOARD, "End", KEYCODE_F4, IP_JOY_NONE)
INPUT_PORTS_END
static MACHINE_DRIVER_START(brew)
    MDRV_CPU_ADD(ARM7LE,48000000)
    MDRV_CPU_MEMORY(brew_readmem,brew_writemem)
    MDRV_CPU_VBLANK_INT(brew_tick,1)
    MDRV_FRAMES_PER_SECOND(60)
    MDRV_VBLANK_DURATION(DEFAULT_60HZ_VBLANK_DURATION)
    MDRV_MACHINE_INIT(brew)
    MDRV_NVRAM_HANDLER(brew)
    MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER|VIDEO_RGB_DIRECT)
    MDRV_SCREEN_SIZE(BREW_WIDTH,BREW_HEIGHT)
    MDRV_VISIBLE_AREA(0,BREW_WIDTH-1,0,BREW_HEIGHT-1)
    MDRV_ASPECT_RATIO(BREW_WIDTH,BREW_HEIGHT)
    MDRV_PALETTE_LENGTH(256)
    MDRV_VIDEO_START(brew)
    MDRV_VIDEO_UPDATE(brew)
    MDRV_SOUND_ATTRIBUTES(SOUND_SUPPORTS_STEREO)
    MDRV_SOUND_ADD(CUSTOM,brew_smaf_interface)
MACHINE_DRIVER_END
ROM_START(brew_conan)
    ROM_REGION(73140,REGION_USER1,0)
    ROM_LOAD("ink1.mod",0,73140,CRC(ee742a34))
    ROM_REGION(377540,REGION_USER2,0)
    ROM_LOAD("ink1.bar",0,377540,CRC(5d6713f5))
    ROM_REGION(17747,REGION_USER3,0)
    ROM_LOAD("ink1.mif",0,17747,CRC(42830ad2))
    ROM_REGION(512,REGION_USER4,0)
    ROM_LOAD("appparam.txt",0,512,CRC(2aacdf4b))
    ROM_REGION(BREW_RAM_SIZE,REGION_CPU1,0)
ROM_END
GAME(2009,brew_conan,0,brew,brew,brew,ROT0,"D3 Publisher","Detective Conan: Escape from the Mansion (BREW)")

/* clang-format on */
