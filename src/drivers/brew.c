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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BREW_RAM_SIZE 0x02000000
#define BREW_MODULE 0x00010000
#define BREW_HELPERS 0x00070000
#define BREW_VTABLES 0x00080000
#define BREW_OUTPUT 0x00090000
#define BREW_DISPLAY_STATE (BREW_OUTPUT + 0x40)
#define BREW_MEDIA_STATE (BREW_OUTPUT + 0x80)
#define BREW_RESOURCES 0x00100000
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
    B_GENERIC,
    B_TYPES
};
static UINT32* brew_ram;
static UINT32 brew_heap, brew_shell, brew_module, brew_applet, brew_phase, brew_clock;
static UINT32 brew_timer_fn[64], brew_timer_user[64], brew_timer_due[64], brew_timer_callback[64];
static UINT32 brew_timer_arg[64], brew_timer_owned[64], brew_active_allocation;
static UINT32 brew_media_objects[32];
static UINT32 brew_framebuffer[BREW_WIDTH * BREW_HEIGHT];
static UINT8 brew_seen[B_TYPES][256];
static UINT32 brew_classid;
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
#define BREW_HEAP_BASE 0x00200000
#define BREW_HEAP_LIMIT 0x01e00000
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
static UINT32 brew_resource(UINT32 id, UINT32 type, UINT32* size)
{
    UINT32 i, count = brew_u16(BREW_RESOURCES + 6);
    UINT32 index = brew_u32(BREW_RESOURCES + 8), table = brew_u32(BREW_RESOURCES + 16);
    UINT32 length = memory_region_length(REGION_USER2);
    if (size)
        *size = 0;
    if (index > length || count > (length - index) / 8 || table > length)
        return 0;
    for (i = 0; i < count; i++)
    {
        UINT32 p = BREW_RESOURCES + index + i * 8, first = brew_u16(p + 2);
        if (brew_u16(p) == type && id >= first && id - first <= brew_u16(p + 4))
        {
            UINT32 section = brew_u16(p + 6) + id - first;
            UINT32 start, end;
            if ((length - table) / 4 < 2 || section >= (length - table) / 4 - 1)
                return 0;
            start = brew_u32(BREW_RESOURCES + table + section * 4);
            end = brew_u32(BREW_RESOURCES + table + (section + 1) * 4);
            if (end < start || end > memory_region_length(REGION_USER2))
                return 0;
            if (size)
                *size = end - start;
            return BREW_RESOURCES + start;
        }
    }
    if (size)
        *size = 0;
    return 0;
}
static UINT32 brew_load_image(UINT32 id)
{
    UINT32 size, resource = brew_resource(id, 6, &size), object = 0, at;
    UINT32* pixels = NULL;
    unsigned width = 0, height = 0;
    int result;
    rpng_t* decoder = NULL;
    if (!resource || size < 20)
        return 0;
    at = brew_u16(resource);
    if (at > size || size - at < 33)
        return 0;
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
    log_cb(RETRO_LOG_INFO, "BREW PNG %u %ux%u -> %08x\n", id, width, height, object);
done:
    rpng_free(decoder);
    free(pixels);
    return object;
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
#define BREW_FILE_CAPACITY 65536
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
static UINT32 brew_filemgr_method(unsigned slot)
{
    UINT32 a = brew_arg(1), b = brew_arg(2), p;
    const char* name = brew_string(a);
    int i = name ? brew_file_find(name) : -1;
    switch (slot)
    {
    case 0:
    case 1:
        return 1;
    case 2:
        log_cb(RETRO_LOG_INFO, "BREW OpenFile %s mode %x\n", name ? name : "(null)", b);
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
    case 11:
    case 13:
        return 0;
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

/* ARM varargs remain guest words; host printf must never interpret a guest pointer. */
static UINT32 brew_format(UINT32 dest, UINT32 format, int argument)
{
    const char* fmt = brew_string(format);
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
        return brew_applet;
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
            log_cb(RETRO_LOG_DEBUG, "BREW SMAF slot %u: %u bytes, %u ms\n", audio, size, duration);
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
                : a == 0x01005509                                         ? B_MEDIA
                : a == 0x01002001                                         ? B_GRAPHICS
                : a == 0x01001056                                         ? B_SOUND
                                                                          : B_GENERIC;
        brew_w32(b, 0);
        if (i == B_GENERIC)
            return BREW_ECLASSNOTSUPPORT;
        p = brew_object(i);
        if (!p)
            return BREW_ENOMEMORY;
        if (i == B_FONT)
        {
            brew_w32(p + 4, a);
            brew_w32(p + 8, a == 0x01001022);
        }
        if (p && a == 0x01005509)
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
        brew_w32(p + 24, 32 * 1024 * 1024);
        brew_w32(p + 40, 0x411);
        return 0;
    case 11:
        if (!b)
            return BREW_EBADPARM;
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
            if (brew_timer_fn[i] && (!a || brew_timer_fn[i] == a) && brew_timer_user[i] == b
                && !brew_timer_owned[i] && !brew_timer_callback[i])
                brew_timer_clear(i);
        return 0;
    case 17:
        return 0;
    case 18:
    {
        UINT32 source = brew_resource(b, c, &size);
        if (!source)
            return 0;
        p = brew_alloc(size);
        if (p)
            memcpy(brew_ptr(p, size), brew_ptr(source, size), size);
        return p;
    }
    case 19:
        return brew_load_image(b);
    case 20:
        brew_free(a);
        return 0;
    case 41:
    {
        UINT32 source = brew_resource(b, c, &size), buffer = brew_arg(4), length = brew_arg(5),
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
    case 23:
        return 1;
    case 24:
        return 0;
    case 36:
        if (!a || !brew_ptr(a, 28) || !brew_u32(a + 16))
            return BREW_EBADPARM;
        for (i = 0; i < 64; i++)
            if (brew_timer_callback[i] == a)
                brew_timer_clear(i);
        for (i = 0; i < 64; i++)
            if (!brew_timer_fn[i])
            {
                brew_timer_clear(i);
                brew_timer_arg[i] = brew_timer_owned[i] = 0;
                brew_timer_fn[i] = brew_u32(a + 16);
                brew_timer_user[i] = brew_u32(a + 20);
                brew_timer_due[i] = brew_clock + 1;
                brew_timer_callback[i] = a;
                brew_w32(a + 8, BREW_TRAPS + B_SHELL * 4096 + 250 * 4);
                brew_w32(a + 12, i + 1);
                return 0;
            }
        return 2;
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
    UINT32 foreground, UINT32 background, int transparent, int left, int top, int right, int bottom)
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
                        brew_framebuffer[((INT64)y + yy) * BREW_WIDTH + cursor + xx]
                            = ink ? foreground : background;
                }
        cursor += advance;
        n += step;
    }
}

static UINT32 brew_display_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2), c = brew_arg(3), i, n = 0;
    int clip_left, clip_top, clip_right, clip_bottom;
    if (!brew_u32(BREW_DISPLAY_STATE))
        brew_w32(BREW_DISPLAY_STATE, object);
    brew_get_clip(object, &clip_left, &clip_top, &clip_right, &clip_bottom);
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
                    UINT32* pixel = &brew_framebuffer[yy * BREW_WIDTH + xx];
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
            left, top, right, bottom);
        if ((flags & (0x2000 | 0x20000)) && (INT64)y + 11 >= top && (INT64)y + 11 < bottom)
            for (xx = x < left ? left : x; (INT64)xx < (INT64)x + width && xx < right; xx++)
                if (!(flags & 0x20000) || !(((INT64)xx - x) & 1))
                    brew_framebuffer[((INT64)y + 11) * BREW_WIDTH + xx] = foreground;
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
                UINT32* pixel = &brew_framebuffer[yy * BREW_WIDTH + xx];
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
    case 15:
        return brew_object(B_BITMAP); /* The physical framebuffer. */
    case 16:
        if (!a || !brew_ptr(a, 4))
            return BREW_EBADPARM;
        i = brew_object(B_BITMAP);
        brew_w32(a, i);
        return i ? 0 : BREW_ENOMEMORY;
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
        int left = 0, top = 0, right = BREW_WIDTH, bottom = BREW_HEIGHT;
        /* Direct IFont is strict: nChars is explicit, and pDst must be an
           actual bitmap. It uses the bitmap bounds, not a display's clip. */
        if (!brew_is_object(a, B_BITMAP))
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
        if (right > BREW_WIDTH) right = BREW_WIDTH;
        if (bottom > BREW_HEIGHT) bottom = BREW_HEIGHT;
        brew_text_draw(object, text, limit, 0, (INT32)b, (INT32)c,
            brew_arg(6) & 0xffffff, brew_arg(7) & 0xffffff, flags & 0x8000,
            left, top, right, bottom);
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
/* Minimal physical-bitmap interface for direct IFont clients. Offscreen
   bitmap allocation and unrelated blitters remain outside the font API. */
static UINT32 brew_bitmap_method(unsigned slot)
{
    UINT32 object = brew_arg(0), a = brew_arg(1), b = brew_arg(2);
    if (slot == 2)
    {
        if (!b || !brew_ptr(b, 4))
            return BREW_EBADPARM;
        brew_w32(b, 0);
        if (a != 0x01000001 && a != 0x01001021)
            return BREW_ECLASSNOTSUPPORT;
        brew_ref(object, B_BITMAP, 0);
        brew_w32(b, object);
        return 0;
    }
    if (slot == 3)
        return a >> 8;
    if (slot == 4)
        return a << 8;
    if (slot == 12)
    {
        if (b != 12)
            return BREW_EUNSUPPORTED;
        if (!a || !brew_ptr(a, 12))
            return BREW_EBADPARM;
        brew_w32(a, BREW_WIDTH);
        brew_w32(a + 4, BREW_HEIGHT);
        brew_w32(a + 8, 24);
        return 0;
    }
    return BREW_EUNSUPPORTED;
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
        if (brew_phase == 0)
        {
            brew_module = brew_u32(BREW_OUTPUT);
            brew_phase = 1;
            log_cb(RETRO_LOG_INFO, "BREW module %08x class %08x\n", brew_module, brew_classid);
            brew_prepare_call(
                brew_u32(brew_u32(brew_module) + 8), brew_module, brew_shell, brew_classid, BREW_OUTPUT + 4);
            return 0xe12fff1c;
        }
        if (brew_phase == 1)
        {
            brew_applet = brew_u32(BREW_OUTPUT + 4);
            brew_phase = 2;
            log_cb(
                RETRO_LOG_INFO, "BREW applet %08x handler=%08x\n", brew_applet, brew_u32(brew_applet + 0x18));
            brew_prepare_call(brew_u32(brew_u32(brew_applet) + 8), brew_applet, 0, 0, 0);
            return 0xe12fff1c;
        }
        brew_phase = 3;
        cpu_spinuntil_trigger(BREW_WAKE);
        return 0xeafffffe;
    }
    if (type < B_TYPES && slot < 256 && !brew_seen[type][slot])
    {
        brew_seen[type][slot] = 1;
        log_cb(RETRO_LOG_INFO, "BREW API type %u slot %u args %08x %08x %08x %08x LR %08x\n", type, slot,
            brew_arg(0), brew_arg(1), brew_arg(2), brew_arg(3), activecpu_get_reg(ARM7_R14));
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
    else if (type == B_MEDIA)
        result = brew_media_method(slot);
    else if (type == B_GRAPHICS)
        result = brew_graphics_method(slot);
    else if (type == B_HEAP)
    {
        switch (slot)
        {
        case 0:
        case 1:
            result = 1;
            break;
        case 6:
            result = brew_arg(1) < BREW_HEAP_LIMIT - brew_heap;
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
        default:
            result = 0;
            break;
        }
    }
    else if (slot < 2)
        result = 1;
    activecpu_set_reg(ARM7_R0, result);
    return 0xe12fff1e;
}
static void brew_boot(int param)
{
    log_cb(RETRO_LOG_INFO, "BREW boot RAM %p entry=%08x class=%08x\n", brew_ram, brew_u32(BREW_MODULE),
        brew_classid);
    cpuintrf_push_context(0);
    activecpu_set_reg(ARM7_CPSR, 0x1f);
    activecpu_set_reg(ARM7_R13, BREW_RAM_SIZE - 256);
    brew_prepare_call(BREW_MODULE, brew_shell, 0, BREW_OUTPUT, 0);
    activecpu_set_reg(ARM7_PC, BREW_MODULE);
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
    if (brew_event_head != brew_event_tail)
    {
        unsigned event = brew_event_head;
        UINT32 handler = brew_u32(brew_u32(brew_applet) + 8);
        brew_event_head = (brew_event_head + 1) % 64;
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
        memset(brew_file_names, 0, sizeof(brew_file_names));
        memset(brew_file_sizes, 0, sizeof(brew_file_sizes));
        memset(brew_file_data, 0, sizeof(brew_file_data));
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
    state_save_register_UINT8("brew", 0, "file_data", (UINT8*)brew_file_data, sizeof(brew_file_data));
    state_save_register_UINT32("brew", 0, "file_error", &brew_file_error, 1);
    state_save_register_func_postload(brew_postload);
}

static MACHINE_INIT(brew)
{
    unsigned type, slot;
    UINT8* mif = memory_region(REGION_USER3);
    UINT32 mif_size = memory_region_length(REGION_USER3), table = 0, count = 0, i;
    if (mif_size >= 32)
    {
        table = brew_le32(mif + 16);
        count = brew_le32(mif + 20);
    }
    if (table > mif_size || count >= (mif_size - table) / 4)
        count = 0;
    memset(brew_ram, 0, BREW_RAM_SIZE);
    brew_heap = 0x00200000;
    memcpy(brew_ptr(BREW_MODULE, memory_region_length(REGION_USER1)), memory_region(REGION_USER1),
        memory_region_length(REGION_USER1));
    memcpy(brew_ptr(BREW_RESOURCES, memory_region_length(REGION_USER2)), memory_region(REGION_USER2),
        memory_region_length(REGION_USER2));
    brew_w32(0, 0xeafffffe);
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
    brew_file_sizes[0] = memory_region_length(REGION_USER4);
    memcpy(brew_file_data[0], memory_region(REGION_USER4), brew_file_sizes[0]);
    brew_smaf_reset(brew_ptr);
    memset(brew_media_objects, 0, sizeof(brew_media_objects));
    brew_active_allocation = 0;
    memset(brew_timer_arg, 0, sizeof(brew_timer_arg));
    memset(brew_timer_owned, 0, sizeof(brew_timer_owned));
    memset(brew_timer_fn, 0, sizeof(brew_timer_fn));
    memset(brew_timer_callback, 0, sizeof(brew_timer_callback));
    memset(brew_framebuffer, 0, sizeof(brew_framebuffer));
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
