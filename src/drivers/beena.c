/***************************************************************************

    Sega Toys "Advanced Pico Beena" (Sega/Applause 9H0-0008 SoC, AP2010)

    Ported to MAME 0.78 / mame2003-plus from the modern MAME driver
    (src/mame/sega/sega_beena.cpp, copyright-holders:R. Belmont, QUFB).

    ARM7TDMI (big-endian, Thumb) core plus the SoC memory map, input sensors,
    RTC and RWebAudio-style I/O registers. Cartridges are exposed as individual
    machines (ROM set bundles the base unit BIOS and the cartridge ROM).

***************************************************************************/

#include "driver.h"
#include "state.h"
#include "cpu/arm7be/arm7be.h"

#include <time.h>

#define ROM_MASK_BASE  0x80000000
#define ROM_FLASH_BASE 0xa0000000

#define BEENA_IRQ_LINE  0
#define BEENA_FIRQ_LINE 1

#define SCREEN_W 704
#define SCREEN_H 480

/* ----------------------------------------------------------------------- */
/* state                                                                    */
/* ----------------------------------------------------------------------- */

static UINT32 *beena_bitmap;
static UINT32 *beena_tilemap_bg;
static UINT32 *beena_tilemap_fg;
static UINT32 *beena_tilemap_scroll_x;
static UINT32 *beena_tilemap_scroll_y;
static UINT32 *beena_tilemap_sprites;
static UINT32 *beena_paletteram;
static UINT32 *beena_sensor_regs;
static UINT32 *beena_aux_regs;

static UINT32 beena_video_regs[0x100/4];
static UINT16 beena_rtc[0x100/2];
static UINT32 beena_midi_regs[0x20/4];
static UINT32 beena_midi_busy;

/* Palette: source is BGR555; render output is RGB555 (0RGB1555). */
static UINT16 beena_pal_base[0x100];
static UINT16 beena_pal_cache[480 * 0x100];

/* Cache layer for x-scrolling scanlines (1024x512, mirrors upstream m_cache_layer). */
static UINT16 beena_cache_layer[1024 * 512];
#define BGR555_TO_RGB555(c)  (UINT16)((((c) & 0x1f) << 10) | ((c) & 0x3e0) | (((c) >> 10) & 0x1f))

static UINT8 *beena_tiles_layers;
static UINT8 *beena_tiles_sprites;

static UINT8  beena_scale;
static UINT16 beena_scanline;
static UINT32 beena_pen_target;
static UINT32 beena_effective_page;
static UINT8 beena_selected_page, beena_page_keys;
static UINT8 beena_pen_keys;
static INT32 beena_book_x = -1, beena_book_y = -1;
static int beena_book_contact;

static int beena_book_positions(void)
{
	return !strcmp(Machine->gamedrv->name, "beena_conan") ? 6 : 7;
}

static void beena_update_pen_target(void)
{
	UINT8 key = readinputport(4) & 2;
	if (key != beena_pen_keys) beena_pen_target ^= 1;
	beena_pen_keys = key;
}


static void beena_release_book_pen(void)
{
	beena_book_x = beena_book_y = -1;
	beena_book_contact = 0;
}

static void beena_restore_book_pen(void)
{
	beena_book_contact = 0;
	beena_selected_page %= beena_book_positions();
}

/* Page and pen keyboard input share the positions used by the frontend. */
static void beena_update_page(void)
{
	UINT8 keys = readinputport(5);
	UINT8 pressed = keys & ~beena_page_keys;
	int positions = beena_book_positions();
	UINT8 arrows = readinputport(11);
	if (pressed & 1) beena_selected_page = (beena_selected_page + positions - 1) % positions;
	if (pressed & 2) beena_selected_page = (beena_selected_page + 1) % positions;
	beena_page_keys = keys;
	if (arrows) {
		if (beena_book_x < 0) beena_book_x = readinputport(6) & 0xff;
		if (beena_book_y < 0) beena_book_y = readinputport(7) & 0xff;
		beena_book_x += ((arrows & 2) ? 4 : 0) - ((arrows & 1) ? 4 : 0);
		beena_book_y += ((arrows & 8) ? 4 : 0) - ((arrows & 4) ? 4 : 0);
		if (beena_book_x < 0) beena_book_x = 0;
		if (beena_book_x > 255) beena_book_x = 255;
		if (beena_book_y < 0) beena_book_y = 0;
		if (beena_book_y > 255) beena_book_y = 255;
	}
}
static UINT8  beena_requested_fiq;
static UINT32 beena_irq_wait_start_addr;
static UINT32 beena_vbl_count;

static UINT8 *beena_cart_base;
static UINT32 *beena_cart_ram;
static UINT32 *beena_ram_cc;
static UINT32 *beena_bios_ram;
static UINT32 beena_cart_size;

/* memcache */
#define MEMCACHE_FIFO_MAX 0x100
static UINT8 beena_memcache[0x800];
static UINT32 beena_mc_seq, beena_mc_addr;
static UINT8 beena_mc_data, beena_mc_i, beena_mc_state;

enum { BITS_EMPTY = 0, BITS_0 = 0x0000ff00, BITS_0_1 = 0x00ff0001,
       BITS_0_1_7 = 0xff000107, BITS_6 = 0x0000ff06, BITS_6_7 = 0x00ff0607,
       BITS_7 = 0x0000ff07, BITS_7_1 = 0x00ff0701, BITS_ERR = 0xffffffff };
enum { IDLE = 0, READ_COMMAND, READ_ADDRESS_BYTE, READ_ACK, READ_RAM_BYTE,
       WRITE_RAM_BYTE, CONSUME_UNTIL_IDLE };

static UINT8 beena_fsa_data[MEMCACHE_FIFO_MAX]; static UINT16 beena_fsa_size, beena_fsa_head, beena_fsa_tail;
static UINT8 beena_fev_data[MEMCACHE_FIFO_MAX]; static UINT16 beena_fev_size, beena_fev_head, beena_fev_tail;

static UINT8 fifo_fsa_pop(void) { UINT8 v = beena_fsa_data[beena_fsa_head]; beena_fsa_head = (beena_fsa_head + 1) & (MEMCACHE_FIFO_MAX - 1); beena_fsa_size--; return v; }
static UINT8 fifo_fev_pop(void) { UINT8 v = beena_fev_data[beena_fev_head]; beena_fev_head = (beena_fev_head + 1) & (MEMCACHE_FIFO_MAX - 1); beena_fev_size--; return v; }
static void fifo_fsa_push(UINT8 s) { if (beena_fsa_size > MEMCACHE_FIFO_MAX - 1) fifo_fsa_pop(); beena_fsa_data[beena_fsa_tail] = s; beena_fsa_tail = (beena_fsa_tail + 1) & (MEMCACHE_FIFO_MAX - 1); beena_fsa_size++; }
static void fifo_fev_push(UINT8 s) { if (beena_fev_size > MEMCACHE_FIFO_MAX - 1) fifo_fev_pop(); beena_fev_data[beena_fev_tail] = s; beena_fev_tail = (beena_fev_tail + 1) & (MEMCACHE_FIFO_MAX - 1); beena_fev_size++; }

static UINT32 beena_cart_base_addr;

static READ32_HANDLER( beena_cart_r )
{
	/* Cartridge data is pre-converted to the CPU's big-endian view in
	   beena_init() (load16_word_swap), so hand the word back directly.
	   Only the region selected by the ROM header's base address is mapped;
	   the other reads as open bus (0xffffffff). */
	if (!beena_cart_ram || beena_cart_base_addr != ROM_MASK_BASE)
		return 0xffffffff;
	(void)mem_mask;
	return beena_cart_ram[offset];
}

static READ32_HANDLER( beena_cart_flash_r )
{
	if (!beena_cart_ram || beena_cart_base_addr != ROM_FLASH_BASE)
		return 0xffffffff;
	(void)mem_mask;
	return beena_cart_ram[offset];
}

static READ32_HANDLER( beena_tiles_r )
{
	return ((UINT32 *)beena_tiles_layers)[offset];
}
static WRITE32_HANDLER( beena_tiles_w )
{
	COMBINE_DATA(&((UINT32 *)beena_tiles_layers)[offset]);
}
static READ32_HANDLER( beena_sprites_r )
{
	return ((UINT32 *)beena_tiles_sprites)[offset];
}
static WRITE32_HANDLER( beena_sprites_w )
{
	COMBINE_DATA(&((UINT32 *)beena_tiles_sprites)[offset]);
}

/* ----------------------------------------------------------------------- */
/* video                                                                    */
/* ----------------------------------------------------------------------- */

static READ32_HANDLER( beena_video_reg_r ) { return beena_video_regs[offset]; }

static WRITE32_HANDLER( beena_video_reg_w )
{
	COMBINE_DATA(&beena_video_regs[offset]);
	if (offset == 0x10/4)
		beena_scale = ((data & 0x200) == 0) ? 2 : 1;
}

static WRITE32_HANDLER( beena_pal_w );
static WRITE32_HANDLER( beena_pal_w )
{
	/* The palette region is 0x100 16-bit BGR555 entries; each 32-bit word
	   covers two entries, big-endian halves (high half = even entry). */
	UINT32 e0 = (offset * 2) & 0xff;
	UINT16 hi = (UINT16)(data >> 16);
	UINT16 lo = (UINT16)(data & 0xffff);
	if (mem_mask != 0x0000ffff) {
		UINT16 rgb = BGR555_TO_RGB555(hi);
		beena_pal_base[e0] = rgb;
		if (beena_scanline < 480) beena_pal_cache[beena_scanline * 0x100 + e0] = rgb;
	}
	if (mem_mask != 0xffff0000) {
		UINT32 e1 = (e0 + 1) & 0xff;
		UINT16 rgb = BGR555_TO_RGB555(lo);
		beena_pal_base[e1] = rgb;
		if (beena_scanline < 480) beena_pal_cache[beena_scanline * 0x100 + e1] = rgb;
	}
	COMBINE_DATA(&beena_paletteram[offset]);
	beena_requested_fiq = 1;
}

static UINT32 beena_dbg_px;
static int beena_frame;

static void beena_put(struct mame_bitmap *bitmap, const struct rectangle *clip, int x, int y, UINT16 color)
{
	if (x < 0 || x >= bitmap->width || y < 0 || y >= bitmap->height) return;
	if (clip && (x < clip->min_x || x > clip->max_x || y < clip->min_y || y > clip->max_y)) return;
	*BITMAP_ADDR16(bitmap, y, x) = color;
	beena_dbg_px++;
}

/* One 16x16 8bpp tile (linear, 256 bytes), scaled by beena_scale, with a
   per-scanline palette lookup (falls back to the base palette for pen 0).
   The ARM7 is big-endian, so a logical byte address `a` of this 32-bit RAM
   region lives at native byte `a ^ 3` (BYTE4_XOR_BE); tile buffers are read
   directly as bytes and must apply that mapping.
   use_scanline_pal selects the per-scanline palette (tile layers, matching
   upstream draw_layer_tiles) vs the static palette (sprites, matching
   upstream draw_sprites which uses the gfx element's palette directly). */
static void beena_draw_tile(struct mame_bitmap *bitmap, const struct rectangle *clip,
		const UINT8 *tiledata, int tile_idx, int fx, int fy, int dx, int dy, int use_scanline_pal)
{
	const UINT8 *src = tiledata + tile_idx * 256;
	int x, y, px, py;

	for (y = 0; y < 16; y++) {
		int ry = fy ? (15 - y) : y;
		for (x = 0; x < 16; x++) {
			UINT8 p;
			UINT16 color;
			int rx = fx ? (15 - x) : x;
			p = src[ry * 16 + (rx ^ 3)];
			if (p == 0) continue;
			/* per-scanline palette cache (tile layers only) */
			if (use_scanline_pal) {
				color = beena_pal_cache[((dy / beena_scale) & 0x1ff) * 0x100 + p];
				if (color == 0) color = beena_pal_base[p];
			} else {
				color = beena_pal_base[p];
			}
			for (py = 0; py < beena_scale; py++)
				for (px = 0; px < beena_scale; px++)
					beena_put(bitmap, clip,
							dx + x * beena_scale + px, dy + y * beena_scale + py, color);
		}
	}
}

static INT32 beena_signed10(UINT32 n) { INT32 v = n & 0x3ff; return (v & 0x200) ? (v - 0x400) : v; }

static INT32 beena_scroll_x(INT32 x, UINT16 i)
{
	UINT16 *sd = (UINT16 *)beena_tilemap_scroll_x;
	x += -beena_signed10(sd[BYTE_XOR_BE(i)]);
	if (x > 0x2c0 + 0xa0) x -= (0x2c0 + 0xa0 * 2);
	if (x < -0xa0) x += (0x2c0 + 0xa0 * 2);
	return x;
}

static INT32 beena_scroll_y(INT32 y, UINT16 i)
{
	UINT16 *sd = (UINT16 *)beena_tilemap_scroll_y;
	UINT32 e = sd[BYTE_XOR_BE(i)];
	if (((e & 0xfc00) == 0xfc00) && (y <= 224)) return y;
	y += beena_signed10(e);
	if (y > (0x400 - 0x20) - 0x10) y -= 0x400;
	if (y < -0x20) y += 0x400;
	return y;
}

/* Draw one 16x16 tile at 1x into beena_cache_layer (pen 0 transparent),
   used by beena_draw_layer_scanlines(). Mirrors upstream's gfx zoom_alpha
   pass which renders tiles with the (static) gfx-element palette. */
static void beena_draw_tile_cache(const UINT8 *tiledata, int tile_idx, int fx, int fy, int dx, int dy)
{
	const UINT8 *src = tiledata + tile_idx * 256;
	int x, y;
	for (y = 0; y < 16; y++) {
		int ry = fy ? (15 - y) : y;
		int py = dy + y;
		if (py < 0 || py >= 512) continue;
		for (x = 0; x < 16; x++) {
			UINT8 p;
			int rx = fx ? (15 - x) : x;
			int px = dx + x;
			if (px < 0 || px >= 1024) continue;
			p = src[ry * 16 + (rx ^ 3)];
			if (p == 0) continue;
			beena_cache_layer[py * 1024 + px] = beena_pal_base[p];
		}
	}
}

static void beena_draw_layer_tiles(struct mame_bitmap *bitmap, const struct rectangle *clip,
		const UINT32 *tilemap, int scroll_idx, int is_overlay_rendered)
{
	UINT16 *tm = (UINT16 *)tilemap;
	size_t offset;
	for (offset = 0; offset < 0x2000 / 2; offset++) {
		INT32 y = ((INT32)(offset / 64) - 2) * 16;
		INT32 x = ((INT32)(offset % 64) - 10) * 16;
		UINT16 sx_idx = scroll_idx, sy_idx = scroll_idx;
		UINT16 tile, transform, tile_idx;
		int flip_x, flip_y, overlay, use_sprites;
		if (y >= 0 && y < (0x90 / 4) * 16 && (beena_video_regs[0x10/4] & 0x20) == 0x20)
			sx_idx += y / 16 * 2;
		if (x >= 0 && x < (0xb4 / 4) * 16 && (beena_video_regs[0x10/4] & 0x40) == 0x40)
			sy_idx += x / 16 * 2;
		x = beena_scroll_x(x, sx_idx);
		y = beena_scroll_y(y, sy_idx);
		tile = tm[BYTE_XOR_BE(offset)];
		transform = (tile & 0xf000) >> 12;
		flip_x = (transform & 1) != 0;
		flip_y = (transform & 2) != 0;
		overlay = (transform & 4) != 0;
		if (overlay != is_overlay_rendered) continue;
		tile_idx = tile & 0x7ff;
		use_sprites = (tile & 0x800) != 0;
		beena_draw_tile(bitmap, clip, use_sprites ? beena_tiles_sprites : beena_tiles_layers,
				tile_idx, flip_x, flip_y, x * beena_scale, y * beena_scale, 1);
	}
}

/* Mirrors upstream draw_layer_scanlines(): draw all tiles (y-scroll applied
   per tile row) into a 1024x512 cache, then blit the cache back to the screen
   applying x-scroll per scanline. Used when (video_regs[0x10/4] & 0x30)==0x30. */
static void beena_draw_layer_scanlines(struct mame_bitmap *bitmap, const struct rectangle *clip,
		const UINT32 *tilemap, int scroll_idx, int is_overlay_rendered)
{
	UINT16 *tm = (UINT16 *)tilemap;
	size_t offset;
	int yi, xi, line_yi, line_xi;

	memset(beena_cache_layer, 0, sizeof(beena_cache_layer));

	/* Apply y-scrolling + wrap-around on each tile row and draw to cache. */
	for (offset = 0; offset < 0x2000 / 2; offset++) {
		INT32 y = ((INT32)(offset / 64) - 2) * 16;
		INT32 x = ((INT32)(offset % 64) - 10) * 16;
		UINT16 sy_idx = scroll_idx;
		UINT16 tile, transform, tile_idx;
		int flip_x, flip_y, overlay, use_sprites;
		tile = tm[BYTE_XOR_BE(offset)];
		transform = (tile & 0xf000) >> 12;
		flip_x = (transform & 1) != 0;
		flip_y = (transform & 2) != 0;
		overlay = (transform & 4) != 0;
		if (overlay != is_overlay_rendered) continue;
		if (x >= 0 && x < (0xb4 / 4) * 16 && (beena_video_regs[0x10/4] & 0x40) == 0x40)
			sy_idx += x / 16 * 2;
		y = beena_scroll_y(y, sy_idx);
		/* Undo the x-axis visible-area adjustment so x-scrolling can use
		   tiles that would otherwise be clipped away. */
		x += 10 * 16;
		tile_idx = tile & 0x7ff;
		use_sprites = (tile & 0x800) != 0;
		beena_draw_tile_cache(use_sprites ? beena_tiles_sprites : beena_tiles_layers,
				tile_idx, flip_x, flip_y, x, y);
	}

	/* Apply x-scrolling + wrap-around on each cached scanline and blit. */
	for (yi = 0; yi < 32 / beena_scale; yi++) {
		for (xi = 0; xi < 64; xi++) {
			INT32 y = yi * 16;
			INT32 x = xi * 16;
			for (line_yi = 0; line_yi < 16; line_yi++) {
				INT32 dst_y = y + line_yi;
				INT32 src_y = dst_y;
				UINT16 sx_idx = scroll_idx;
				if (y >= 0 && y < (0x90 / 4) * 16)
					sx_idx += (y + line_yi) * 2;
				for (line_xi = 0; line_xi < 16; line_xi++) {
					INT32 dst_x = x + line_xi;
					INT32 src_x = dst_x;
					UINT16 color;
					dst_x = beena_scroll_x(dst_x, sx_idx);
					dst_x -= 10 * 16;
					if (src_x < 0 || src_x >= 1024 || src_y < 0 || src_y >= 512) continue;
					color = beena_cache_layer[src_y * 1024 + src_x];
					if (color == 0) continue;
					{
						int dyi, dxi;
						for (dyi = 0; dyi < beena_scale; dyi++)
							for (dxi = 0; dxi < beena_scale; dxi++)
								beena_put(bitmap, clip, dst_x * beena_scale + dxi,
										dst_y * beena_scale + dyi, color);
					}
				}
			}
		}
	}
}

static void beena_draw_layer(struct mame_bitmap *bitmap, const struct rectangle *clip,
		const UINT32 *tilemap, int scroll_idx, int is_active, int is_overlay_rendered)
{
	if (!is_active) return;
	if ((beena_video_regs[0x10/4] & 0x30) == 0x30)
		beena_draw_layer_scanlines(bitmap, clip, tilemap, scroll_idx, is_overlay_rendered);
	else
		beena_draw_layer_tiles(bitmap, clip, tilemap, scroll_idx, is_overlay_rendered);
}

static void beena_draw_sprites(struct mame_bitmap *bitmap, const struct rectangle *clip, int is_overlay_rendered)
{
	UINT16 *tm = (UINT16 *)beena_tilemap_sprites;
	INT32 offset;
	INT16 delta_factor = beena_video_regs[0x38/4] & 0xff;
	for (offset = 0x400 / 2 - 4; offset >= 0; offset -= 4) {
		UINT16 priority = tm[BYTE_XOR_BE(offset + 1)] & 0xff;
		UINT32 tile_pos;
		UINT16 y, x;
		int flip_x, flip_y, pattern_x_len, pattern_y_len, pi, ix, iy;
		UINT8 pxv[4], pyv[4];
		INT16 dx = 0, dy = 0;
		if ((priority > 0 && !is_overlay_rendered) || (priority == 0 && is_overlay_rendered)) continue;
		tile_pos = ((UINT32)tm[BYTE_XOR_BE(offset)] << 16) | tm[BYTE_XOR_BE(offset + 1)];
		y = (tile_pos >> 20) & 0x7ff;
		x = (tile_pos >> 8) & 0x7ff;
		flip_y = ((tile_pos >> 20) & 0x800) != 0;
		flip_x = ((tile_pos >> 8) & 0x800) != 0;
		if (delta_factor == 0x2c) { dx = 14 * 16; dy = 7 * 16; }
		else if (delta_factor == 0x20) { dx = 6 * 16; dy = -1 * 16; }
		{
			UINT16 tile_idx = tm[BYTE_XOR_BE(offset + 2)] & 0x7ff;
			UINT16 plen = (tm[BYTE_XOR_BE(offset + 2)] & 0xf000) >> 12;
			if (tile_idx == 0 && x == 0 && y == 0) continue;
			pattern_x_len = (plen % 4) + 1;
			pattern_y_len = (plen / 4) + 1;
			/* Upstream builds iota vectors and reverses them on flip; the tile
			   index still advances in iteration order, so a flipped sprite is
			   laid out (and mirrored) as a whole. */
			for (ix = 0; ix < pattern_x_len; ix++) pxv[ix] = flip_x ? (pattern_x_len - 1 - ix) : ix;
			for (iy = 0; iy < pattern_y_len; iy++) pyv[iy] = flip_y ? (pattern_y_len - 1 - iy) : iy;
			pi = 0;
			for (iy = 0; iy < pattern_y_len; iy++) {
				for (ix = 0; ix < pattern_x_len; ix++) {
					beena_draw_tile(bitmap, clip, beena_tiles_sprites, tile_idx + pi, flip_x, flip_y,
							(x + pxv[ix] * 16) * beena_scale - (0x2c0 / 2) + dx,
							(y + pyv[iy] * 16) * beena_scale - (0x1e0 / 2) + dy, 0);
					pi++;
				}
			}
		}
	}
}

static void beena_draw_bitmap(struct mame_bitmap *bitmap, const struct rectangle *cliprect)
{
	UINT16 *bmp;
	UINT32 i;
	UINT16 w, h, ox, oy, sx, sy, cw, ch;
	UINT32 y, x;

	if ((beena_video_regs[0x20/4] & 1) == 0)
		return;

	bmp = (UINT16 *)beena_bitmap;
	w  = ((beena_video_regs[0x20/4] >> 8)  & 0xff) << 4;
	h  = ((beena_video_regs[0x20/4] >> 16) & 0xff) << 4;
	ox = (beena_video_regs[0x24/4] >> 0)  & 0x3ff;
	oy = (beena_video_regs[0x24/4] >> 16) & 0x3ff;
	sx = (beena_video_regs[0x28/4] >> 0)  & 0x3ff;
	sy = (beena_video_regs[0x28/4] >> 16) & 0x3ff;
	cw = (beena_video_regs[0x2c/4] >> 0)  & 0x3ff;
	ch = (beena_video_regs[0x2c/4] >> 16) & 0x3ff;

	i = oy * w + ox;
	for (y = 0; y < (UINT32)h; y++) {
		for (x = 0; x < (UINT32)w; x++) {
			UINT16 color = bmp[BYTE_XOR_BE(i)];
			int dx, dy, yy, xx;
			i++;
			if (color == 0 || y >= ch || x >= cw)
				continue;
			dy = ((int)y + sy) * beena_scale;
			dx = ((int)x + sx) * beena_scale;
			for (yy = 0; yy < beena_scale; yy++)
				for (xx = 0; xx < beena_scale; xx++)
					beena_put(bitmap, cliprect,
							dx + xx, dy + yy, BGR555_TO_RGB555(color));
		}
	}
}

static UINT8 beena_rescale_alpha_step(UINT8 step)
{
	/* mirrors upstream rescale_alpha_step(): step 0..0x1f fades 255->0,
	   step 0x20..0x3f fades 0->255. */
	if (step < 0x20)
		return (UINT8)(255 - (step * 255) / 0x1f);
	else
		return (UINT8)(((step - 0x20) * 255) / 0x1f);
}

static UINT16 beena_blend_px(UINT16 dst, int bl_r, int bl_g, int bl_b, int a_r, int a_g, int a_b)
{
	int dr = (dst >> 10) & 0x1f, dg = (dst >> 5) & 0x1f, db = dst & 0x1f;
	int r, g, b;
	dr = (dr << 3) | (dr >> 2);
	dg = (dg << 3) | (dg >> 2);
	db = (db << 3) | (db >> 2);
	r = (dr * a_b + bl_r * (256 - a_b)) >> 8;
	g = (dg * a_r + bl_g * (256 - a_r)) >> 8;
	b = (db * a_g + bl_b * (256 - a_g)) >> 8;
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (UINT16)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

static void beena_screen_blend(struct mame_bitmap *bitmap, const struct rectangle *clip)
{
	UINT32 r30 = beena_video_regs[0x30/4];
	int a_r = beena_rescale_alpha_step((r30 >> 0)  & 0x3f);
	int a_g = beena_rescale_alpha_step((r30 >> 8)  & 0x3f);
	int a_b = beena_rescale_alpha_step((r30 >> 16) & 0x3f);
	int bl_r = (((r30 >> 0)  & 0x3f) < 0x20) ? 0xff : 0;
	int bl_g = (((r30 >> 8)  & 0x3f) < 0x20) ? 0xff : 0;
	int bl_b = (((r30 >> 16) & 0x3f) < 0x20) ? 0xff : 0;
	int y, x;
	for (y = clip->min_y; y <= clip->max_y; y++) {
		UINT16 *line = (UINT16 *)bitmap->line[y];
		for (x = clip->min_x; x <= clip->max_x; x++)
			line[x] = beena_blend_px(line[x], bl_r, bl_g, bl_b, a_r, a_g, a_b);
	}
}

static VIDEO_START( beena )
{
	/* VIDEO_CONFIG: 0 = video output connected, non-zero = disconnected
	   (upstream sets video_regs[0] bit 0x200 when disconnected). This runs
	   after input port settings are loaded, unlike MACHINE_INIT. */
	if (readinputport(8) != 0)
		beena_video_regs[0] |= 0x00000200;
	return 0;
}

static VIDEO_UPDATE( beena )
{
	UINT16 *line;
	int i, y;

	beena_frame++;
	beena_pal_base[0] = 0;
	fillbitmap(bitmap, 0, cliprect);
	beena_dbg_px = 0;

	if ((beena_video_regs[0x10/4] & 1) == 0) {
		beena_screen_blend(bitmap, cliprect);
		for (i = 0; i < 480; i++) { line = (UINT16 *)beena_pal_cache + i * 0x100; for (y = 0; y < 0x100; y++) line[y] = 0; }
		return;
	}

	if ((beena_video_regs[0x10/4] & 0x3000) == 0x3000)
		beena_draw_bitmap(bitmap, cliprect);
	beena_draw_layer(bitmap, cliprect, beena_tilemap_bg, 1, (beena_video_regs[0x10/4] & 4) != 0, 0);
	if ((beena_video_regs[0x10/4] & 0x3000) == 0x2000)
		beena_draw_bitmap(bitmap, cliprect);
	beena_draw_layer(bitmap, cliprect, beena_tilemap_fg, 0, (beena_video_regs[0x10/4] & 2) != 0, 0);
	{
		int after = (beena_video_regs[0x20/4] & 0x10) != 0;
		if ((beena_video_regs[0x10/4] & 0x3000) == 0x1000 && !after)
			beena_draw_bitmap(bitmap, cliprect);
		beena_draw_sprites(bitmap, cliprect, 0);
		beena_draw_layer(bitmap, cliprect, beena_tilemap_bg, 1, (beena_video_regs[0x10/4] & 4) != 0, 1);
		if ((beena_video_regs[0x10/4] & 0x3000) == 0 && !after)
			beena_draw_bitmap(bitmap, cliprect);
		beena_draw_layer(bitmap, cliprect, beena_tilemap_fg, 0, (beena_video_regs[0x10/4] & 2) != 0, 1);
		if ((beena_video_regs[0x10/4] & 0x3000) == 0x1000 && after)
			beena_draw_bitmap(bitmap, cliprect);
		beena_draw_sprites(bitmap, cliprect, 1);
		if ((beena_video_regs[0x10/4] & 0x3000) == 0 && after)
			beena_draw_bitmap(bitmap, cliprect);
	}
	beena_screen_blend(bitmap, cliprect);

	for (i = 0; i < 480; i++) {
		line = (UINT16 *)beena_pal_cache + i * 0x100;
		for (y = 0; y < 0x100; y++) line[y] = 0;
	}
}

/* ----------------------------------------------------------------------- */
/* I/O: sensors, RTC, MIDI, expansion, memcache                             */
/* ----------------------------------------------------------------------- */

static void beena_update_sensors(UINT32 offset)
{
	int16_t io_pen_x, io_pen_y, io_pen_x_min = 0, io_pen_x_max = 255, io_pen_y_min = 0, io_pen_y_max = 255;
	UINT32 pen_pressed, pen_data;
	UINT32 io_page;
	int is_tablet;
	int is_test_mode = (readinputport(10) & 0x80) == 0; /* PAGE_CONFIG */

	switch (offset) {
		case 0x8/4: case 0xc/4: case 0x10/4:
			io_pen_x = beena_book_x >= 0 ? beena_book_x : (readinputport(6) & 0xff);
			io_pen_y = beena_book_y >= 0 ? beena_book_y : (readinputport(7) & 0xff);
			beena_update_pen_target();
			is_tablet = (beena_pen_target == 0);
			if (is_tablet) {
				io_pen_x = 0x1b8 + (((io_pen_x - io_pen_x_min) * (0x58 - 0x1b8)) / (io_pen_x_max - io_pen_x_min));
				io_pen_y = 0x338 + (((io_pen_y - io_pen_y_min) * (0x248 - 0x338)) / (io_pen_y_max - io_pen_y_min));
			} else {
				io_pen_x = 0x1b6 + (((io_pen_x - (io_pen_x_min + 8)) * (0x109 - 0x1b6)) / ((io_pen_x_max - 8) - (io_pen_x_min + 8)));
				io_pen_y = 0x3b8 + (((io_pen_y - (io_pen_y_min + 8)) * (0x342 - 0x3b8)) / (io_pen_y_max - (io_pen_y_min + 8)));
			}
			pen_pressed = (beena_book_contact || (readinputport(4) & 1)) ? 0x80000 : 0;
			pen_data = (io_pen_y << 9) | io_pen_x | pen_pressed;
			beena_sensor_regs[offset] = pen_data;
			break;
		case 0x2c/4:
			io_page = beena_selected_page;
			if (is_test_mode) {
				/* Forced pages 1/3/5 covered, others exposed. */
				beena_sensor_regs[offset] = 0xff00ff00;
				beena_effective_page = 6;
			} else if ((beena_effective_page ^ io_page) != 0) {
				beena_effective_page = io_page;
				beena_sensor_regs[offset] = (UINT32)((UINT64)0xffffffff >> (beena_effective_page * 8));
			}
			break;
		case 0x30/4:
			if (is_test_mode) {
				beena_sensor_regs[offset] = 0xffff00ff;
			} else {
				switch (beena_effective_page) {
					case 5: beena_sensor_regs[offset] = 0x0000ff00; break;
					case 6: beena_sensor_regs[offset] = 0x00000000; break;
					default: beena_sensor_regs[offset] = 0x00ffff00;
				}
			}
			break;
		case 0x34/4:
			beena_sensor_regs[offset] = ((readinputport(0) << 5) | readinputport(2)) & 0x3ff;
			break;
		case 0x38/4:
			beena_sensor_regs[offset] = 3;
			break;
	}
}

static READ32_HANDLER( beena_sensors_r ) { beena_update_sensors(offset); return beena_sensor_regs[offset]; }

static READ32_HANDLER( beena_rtc_r ) { return beena_rtc[offset]; }
static WRITE32_HANDLER( beena_rtc_w ) { COMBINE_DATA(&beena_rtc[offset]); }

/* Refresh the RTC from the host clock (mirrors upstream update_rtc(), which
   is called every frame from the scanline callback). */
static void beena_update_rtc(void)
{
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);
	beena_rtc[0] = lt->tm_sec;
	beena_rtc[1] = lt->tm_min;
	beena_rtc[2] = lt->tm_hour;
	beena_rtc[3] = lt->tm_mday;
	beena_rtc[4] = lt->tm_wday;
	beena_rtc[5] = lt->tm_mon + 1;
	beena_rtc[6] = lt->tm_year % 100;
}

static READ32_HANDLER( beena_midi_reg_r )
{
	if (offset == 0 && beena_midi_busy > 0) { beena_midi_busy--; return 0x2; }
	return beena_midi_regs[offset];
}
static WRITE32_HANDLER( beena_midi_reg_w )
{
	if (offset == 0x8/4) beena_midi_busy = 1;
	COMBINE_DATA(&beena_midi_regs[offset]);
}

static void beena_memcache_advance(UINT32 *status)
{
	UINT8 event = fifo_fev_pop();
	*status = (event == 6) ? 1 : 0;
	switch (beena_mc_seq) {
		case BITS_0:   beena_mc_seq = (event == 1) ? BITS_0_1 : BITS_ERR; break;
		case BITS_0_1: beena_mc_seq = (event == 7) ? BITS_0_1_7 : ((event == 6) ? BITS_6 : BITS_ERR); break;
		case BITS_6:   beena_mc_seq = (event == 7) ? BITS_6_7 : ((event == 0) ? BITS_0 : BITS_ERR); break;
		case BITS_7:   beena_mc_seq = (event == 1) ? BITS_7_1 : BITS_ERR; break;
		case BITS_EMPTY: beena_mc_seq = (event == 7) ? BITS_7 : ((event == 6) ? BITS_6 : ((event == 0) ? BITS_0 : BITS_ERR)); break;
		default: beena_mc_seq = BITS_ERR;
	}
}

static void beena_memcache_parse_bit(UINT32 *status)
{
	if (beena_mc_seq == BITS_0_1) {
		if (beena_fev_size && beena_fev_data[beena_fev_head] == 7) return;
		beena_mc_seq = BITS_EMPTY; beena_mc_data = (beena_mc_data << 1) | 0; beena_mc_i++;
	} else if (beena_mc_seq == BITS_6_7) {
		beena_mc_seq = BITS_EMPTY; beena_mc_data = (beena_mc_data << 1) | 1; beena_mc_i++;
	} else if (beena_mc_seq == BITS_0_1_7) {
		*status = 1; beena_mc_seq = BITS_EMPTY; beena_mc_state = IDLE;
		beena_fsa_size = 0; beena_fsa_head = 0; beena_fsa_tail = 0;
	}
}

static UINT32 beena_io_memcache_r(void)
{
	UINT32 status = 0;
	while (beena_fev_size) {
		beena_memcache_advance(&status);
		/* CPU_CONFIG bit 0x40: memory cache disabled (advance only). */
		if ((readinputport(9) & 0x40) == 0x40) continue;
		switch (beena_mc_state) {
			case IDLE:
				if (beena_mc_seq == BITS_6) { beena_mc_seq = BITS_EMPTY; beena_mc_state = CONSUME_UNTIL_IDLE; }
				else if (beena_mc_seq == BITS_7_1) { beena_mc_seq = BITS_EMPTY; beena_mc_state = READ_COMMAND; }
				break;
			case READ_COMMAND:
				beena_memcache_parse_bit(&status);
				if (beena_mc_i == 8) {
					beena_mc_state = READ_ACK;
					beena_mc_addr = (((beena_mc_data & 0xe) >> 1) << 8) | (beena_mc_addr & 0xff);
					if ((beena_mc_data & 0xa1) == 0xa0) {
						fifo_fsa_push(READ_ADDRESS_BYTE); fifo_fsa_push(READ_ACK); fifo_fsa_push(READ_RAM_BYTE);
					} else if ((beena_mc_data & 0xa1) == 0xa1) {
						fifo_fsa_push(WRITE_RAM_BYTE);
					} else {
						beena_mc_state = CONSUME_UNTIL_IDLE; beena_fsa_size = 0; beena_fsa_head = 0; beena_fsa_tail = 0;
					}
					beena_mc_data = 0; beena_mc_i = 0;
				}
				break;
			case READ_ACK:
				if (beena_mc_seq == BITS_6_7) { beena_mc_seq = BITS_EMPTY; beena_mc_state = fifo_fsa_pop(); }
				break;
			case READ_ADDRESS_BYTE:
				beena_memcache_parse_bit(&status);
				if (beena_mc_i == 8) { beena_mc_addr |= beena_mc_data; beena_mc_data = 0; beena_mc_i = 0; beena_mc_state = fifo_fsa_pop(); }
				break;
			case READ_RAM_BYTE:
				beena_memcache_parse_bit(&status);
				if (beena_mc_seq == BITS_7_1) { beena_mc_seq = BITS_EMPTY; beena_mc_state = READ_COMMAND; beena_mc_data = 0; beena_mc_i = 0; }
				if (beena_mc_i == 8) {
					beena_memcache[beena_mc_addr] = beena_mc_data; beena_mc_data = 0; beena_mc_i = 0;
					beena_mc_state = READ_ACK; beena_fsa_size = 0; beena_fsa_head = 0; beena_fsa_tail = 0;
					fifo_fsa_push(READ_RAM_BYTE); beena_mc_addr++;
				}
				break;
			case WRITE_RAM_BYTE:
				if (beena_mc_i == 8 && beena_mc_seq == BITS_6_7) { beena_mc_seq = BITS_EMPTY; beena_mc_i = 0; }
				if (beena_mc_seq == BITS_0_1) {
					if (beena_fev_size && beena_fev_data[beena_fev_head] == 7) continue;
					beena_mc_seq = BITS_EMPTY; beena_mc_addr++; beena_mc_i = 0;
				} else if (beena_mc_seq == BITS_6_7) {
					status = (beena_memcache[beena_mc_addr] >> (7 - beena_mc_i)) & 1;
					beena_mc_seq = BITS_EMPTY; beena_mc_i++;
				} else if (beena_mc_seq == BITS_0_1_7) {
					status = 1; beena_mc_addr = 0; beena_mc_data = 0; beena_mc_i = 0; beena_mc_seq = BITS_EMPTY;
					beena_mc_state = IDLE; beena_fsa_size = 0; beena_fsa_head = 0; beena_fsa_tail = 0;
				}
				break;
			case CONSUME_UNTIL_IDLE:
				if (beena_mc_seq == BITS_0_1_7) {
					status = 1; beena_mc_addr = 0; beena_mc_data = 0; beena_mc_i = 0; beena_mc_seq = BITS_EMPTY; beena_mc_state = IDLE;
				}
				break;
		}
		if (beena_mc_seq == BITS_ERR) {
			beena_mc_addr = 0; beena_mc_data = 0; beena_mc_i = 0; beena_mc_seq = BITS_EMPTY; beena_mc_state = CONSUME_UNTIL_IDLE;
			beena_fsa_size = 0; beena_fsa_head = 0; beena_fsa_tail = 0;
		}
	}
	return status;
}

static READ32_HANDLER( beena_aux_r )
{
	if (offset == 0x0c/4) return 0;            /* io_expansion_r */
	if (offset == 0xbc/4) return beena_io_memcache_r();
	return beena_aux_regs[offset];
}
static WRITE32_HANDLER( beena_aux_w )
{
	COMBINE_DATA(&beena_aux_regs[offset]);
	if (offset == 0xc0/4) fifo_fev_push((UINT8)data);
}

/* ----------------------------------------------------------------------- */
/* interrupts                                                               */
/* ----------------------------------------------------------------------- */

static UINT32 beena_bios_sr[0x100 / 4];
static READ32_HANDLER( beena_bios_sr_r )
{
	return beena_bios_sr[offset & 0xff];
}
static WRITE32_HANDLER( beena_bios_sr_w )
{
	COMBINE_DATA(&beena_bios_sr[offset & 0xff]);
}

static WRITE32_HANDLER( beena_ramcc_w )
{
	COMBINE_DATA(&beena_ram_cc[offset]);
}

static WRITE32_HANDLER( beena_biosram_w )
{
	COMBINE_DATA(&beena_bios_ram[offset]);
}

static void beena_request_irq(void) { cpu_set_irq_line(0, BEENA_IRQ_LINE, PULSE_LINE); }
static void beena_request_fiq(void)
{
	if (beena_requested_fiq) {
		cpu_set_irq_line(0, BEENA_FIRQ_LINE, PULSE_LINE);
		beena_requested_fiq = 0;
	}
}

/* Upstream sega_beena_state::irq_wait_speedup: the game copies this five
   instruction busy wait into work RAM. Locate it by its instruction signature
   and let the AP2010 CPU spend most of a matching timeslice waiting for IRQ. */
static void beena_irq_wait_speedup(void)
{
	static const UINT32 signature[5] = {
		0xe3a03001, 0xe5c03000, 0xe5d03000, 0xe3530000, 0x1afffffc
	};
	UINT32 pc, addr, i, j, offset;
	if ((readinputport(9) & 0x10) || beena_irq_wait_start_addr != 0xffffffff || !beena_ram_cc)
		return;
	pc = cpunum_get_pc(0);
	if (pc < 0xc00cc000 || pc >= 0xc00fff80)
		return;
	for (i = 0; i <= 16; i += 4) {
		if (pc < 0xc00cc000 + i) break;
		addr = pc - i;
		offset = (addr - 0xc00cc000) / 4;
		if (offset + 5 > 0x34000 / 4) continue;
		for (j = 0; j < 5 && beena_ram_cc[offset + j] == signature[j]; j++) {}
		if (j == 5) {
			beena_irq_wait_start_addr = addr;
			for (j = 0; j < 5; j++) arm7be_add_hotspot(addr + j * 4);
			break;
		}
	}
}

static void beena_scanline_callback(int param)
{
	UINT8 irq_frequency = readinputport(9) & 0xf; /* CPU_CONFIG */
	if (param == 0) beena_update_page();
	beena_irq_wait_speedup();
	if (irq_frequency == 0) irq_frequency = 1;
	beena_scanline = param / beena_scale;
	beena_video_regs[0x4/4] = 0x10000 * (20 + beena_scanline);
	if ((beena_scanline % (SCREEN_H / irq_frequency)) == 0) {
		UINT8 vs = (beena_video_regs[0] & 0x10) ^ 0x10;
		beena_vbl_count++;
		beena_video_regs[0] = (beena_video_regs[0] & 0xffffffef) | vs;
		beena_update_rtc();
		beena_request_irq();
		beena_requested_fiq = 1;
	}
	beena_request_fiq();
	param++;
	if (param >= SCREEN_H) param = 0;
	timer_set(cpu_getscanlinetime(param), param, beena_scanline_callback);
}

/* ----------------------------------------------------------------------- */
/* AP2010PCM                                                                */
/* Ported from modern MAME src/devices/sound/ap2010pcm.cpp. The device is a */
/* pair of FIFOs (normal + "fast") of signed 16-bit big-endian PCM fed by   */
/* the BIOS (decoded .ogg/.speex output), played at 8 kHz or 16 kHz with a  */
/* software volume. The 0.78 streams API is used in place of MAME's sound   */
/* streams; the sample rate can be changed at runtime (stream_set_sample_rate). */
/* ----------------------------------------------------------------------- */

#define PCM_FIFO_MAX 0x800

static UINT32 beena_pcm_regs[0x40 / 4];
static float  beena_pcm_volume;
static UINT32 beena_pcm_sample_rate;
static UINT16 beena_pcm_fifo_data[PCM_FIFO_MAX];
static UINT16 beena_pcm_fifo_size, beena_pcm_fifo_head, beena_pcm_fifo_tail;
static UINT16 beena_pcm_fifo_fast_data[PCM_FIFO_MAX];
static UINT16 beena_pcm_fifo_fast_size, beena_pcm_fifo_fast_head, beena_pcm_fifo_fast_tail;
static int    beena_pcm_stream = -1;

static UINT16 beena_pcm_fifo_pop(void)
{
	UINT16 sample = beena_pcm_fifo_data[beena_pcm_fifo_head];
	beena_pcm_fifo_head = (beena_pcm_fifo_head + 1) & (PCM_FIFO_MAX - 1);
	beena_pcm_fifo_size--;
	return sample;
}
static UINT16 beena_pcm_fifo_fast_pop(void)
{
	UINT16 sample = beena_pcm_fifo_fast_data[beena_pcm_fifo_fast_head];
	beena_pcm_fifo_fast_head = (beena_pcm_fifo_fast_head + 1) & (PCM_FIFO_MAX - 1);
	beena_pcm_fifo_fast_size--;
	return sample;
}
static void beena_pcm_fifo_push(UINT16 sample)
{
	if (sample == 0) return;
	if (beena_pcm_fifo_size > PCM_FIFO_MAX - 1) beena_pcm_fifo_pop();
	beena_pcm_fifo_data[beena_pcm_fifo_tail] = sample;
	beena_pcm_fifo_tail = (beena_pcm_fifo_tail + 1) & (PCM_FIFO_MAX - 1);
	beena_pcm_fifo_size++;
}
static void beena_pcm_fifo_fast_push(UINT16 sample)
{
	if (sample == 0) return;
	if (beena_pcm_fifo_fast_size > PCM_FIFO_MAX - 1) beena_pcm_fifo_fast_pop();
	beena_pcm_fifo_fast_data[beena_pcm_fifo_fast_tail] = sample;
	beena_pcm_fifo_fast_tail = (beena_pcm_fifo_fast_tail + 1) & (PCM_FIFO_MAX - 1);
	beena_pcm_fifo_fast_size++;
}

static void beena_pcm_stream_callback(int param, INT16 *buffer, int length)
{
	int i;
	(void)param;
	for (i = 0; i < length; i++) {
		INT16 sample;
		if (beena_pcm_fifo_fast_size)
			sample = (INT16)beena_pcm_fifo_fast_pop();
		else if (beena_pcm_fifo_size)
			sample = (INT16)beena_pcm_fifo_pop();
		else
			sample = 0;
		buffer[i] = (INT16)(sample * beena_pcm_volume);
	}
}

/* The stream can only be allocated once the OSD/mixer is up, which happens
   after MACHINE_INIT, so allocate lazily on the first PCM access. */
static void beena_pcm_init_stream(void)
{
	if (beena_pcm_stream >= 0) return;
	beena_pcm_sample_rate = 8000;
	beena_pcm_volume = 0.0f;
	beena_pcm_stream = stream_init("AP2010PCM", 100, beena_pcm_sample_rate, 0,
			beena_pcm_stream_callback);
}

static READ32_HANDLER( beena_pcm_r )
{
	UINT32 o = offset & 0x3f;
	if (o == 0) {
		/* PCM data (0x5001000c) only received when 0x50010000 & 1 != 0;
		   PCM parameters (0x50010010/18) only when 0x50010000 & 4 != 0; */
		return (beena_pcm_regs[0x4/4] != 0) ? 0x0f : 0;
	} else if (o == 0x4/4 && beena_pcm_fifo_size > 0x1ff) {
		return beena_pcm_regs[o] & 0xfffffffe;
	} else if (o == 0x1c/4) {
		UINT32 fifo_size = beena_pcm_fifo_size;
		if (fifo_size > 0x1ff) fifo_size = 0x1ff;
		else if (fifo_size > 1) fifo_size -= 2;
		return fifo_size;
	}
	return beena_pcm_regs[o];
}
static WRITE32_HANDLER( beena_pcm_w )
{
	UINT32 o = offset & 0x3f;
	beena_pcm_init_stream();
	COMBINE_DATA(&beena_pcm_regs[o]);
	if (beena_pcm_stream >= 0) stream_update(beena_pcm_stream, 0);
	switch (o) {
		case 0x4/4:
			if ((data & 0x78) == 0x78) {
				beena_pcm_sample_rate = 8000 * (1 + ((data >> 1) & 1));
				if (beena_pcm_stream >= 0)
					stream_set_sample_rate(beena_pcm_stream, beena_pcm_sample_rate);
				/* When a new stream starts, stop playback of the previous one. */
				beena_pcm_fifo_size = 0; beena_pcm_fifo_head = 0; beena_pcm_fifo_tail = 0;
			}
			break;
		case 0xc/4:
			if (ACCESSING_MSW32) beena_pcm_fifo_push((data & 0xffff0000U) >> 16);
			if (ACCESSING_LSW32)  beena_pcm_fifo_push(data & 0x0000ffffU);
			break;
		case 0x10/4:
			/* Fast samples are always played first; also serve an FIQ so
			   .au data processing does not hang. */
			if (data != 0) beena_requested_fiq = 1;
			if (ACCESSING_MSW32) beena_pcm_fifo_fast_push((data & 0xffff0000U) >> 16);
			if (ACCESSING_LSW32)  beena_pcm_fifo_fast_push(data & 0x0000ffffU);
			break;
		case 0x14/4:
			/* Panning (bits not identified). */
			break;
		case 0x18/4:
			{
				float v = ((data & 0x1ff00000U) >> 20) / 345.0f;
				beena_pcm_volume = (v < 1.0f) ? v : 1.0f;
			}
			break;
	}
}

/* ----------------------------------------------------------------------- */
/* memory maps                                                              */
/* ----------------------------------------------------------------------- */

static MEMORY_READ32_START( beena_readmem )
	{ 0x00000000, 0x0001ffff, MRA32_ROM },
	{ 0x20003e00, 0x20003eff, beena_bios_sr_r },
	{ 0x20000000, 0x20003fff, MRA32_RAM, &beena_bios_ram },
	{ 0x40000000, 0x400000ff, beena_video_reg_r },
	{ 0x40010000, 0x4001ffff, MRA32_RAM },
	{ 0x40020000, 0x400201ff, MRA32_RAM },
	{ 0x40020200, 0x4002ffff, MRA32_RAM },
	{ 0x40030000, 0x4003ffff, MRA32_RAM },
	{ 0x50000000, 0x5000000f, MRA32_RAM },
	{ 0x50010000, 0x5001003f, beena_pcm_r },
	{ 0x50020000, 0x5002007f, beena_sensors_r },
	{ 0x50030000, 0x500300ff, beena_aux_r },
	{ 0x60000000, 0x600000ff, beena_rtc_r },
	{ 0x60010000, 0x6003ffff, MRA32_RAM },
	{ 0x70000000, 0x7000001f, beena_midi_reg_r },
	{ 0x70010000, 0x7003ffff, MRA32_RAM },
	{ 0x80000000, 0x807fffff, beena_cart_r },
	{ 0xa0000000, 0xa07fffff, beena_cart_flash_r },
	{ 0xc0000000, 0xc00c5fff, MRA32_RAM },
	{ 0xc00c6000, 0xc00c68ff, MRA32_RAM },
	{ 0xc00c6900, 0xc00c7fff, MRA32_RAM },
	{ 0xc00c8000, 0xc00c9fff, MRA32_RAM },
	{ 0xc00ca000, 0xc00cbfff, MRA32_RAM },
	{ 0xc00cc000, 0xc00fffff, MRA32_RAM, &beena_ram_cc },
	{ 0xc0100000, 0xc017ffff, beena_tiles_r },
	{ 0xc0180000, 0xc01fffff, beena_sprites_r },
MEMORY_END

static MEMORY_WRITE32_START( beena_writemem )
	{ 0x00000000, 0x0001ffff, MWA32_NOP },
	{ 0x20003e00, 0x20003eff, beena_bios_sr_w },
	{ 0x20000000, 0x20003fff, beena_biosram_w, &beena_bios_ram },
	{ 0x40000000, 0x400000ff, beena_video_reg_w },
	{ 0x40010000, 0x4001ffff, MWA32_RAM, &beena_tilemap_sprites },
	{ 0x40020000, 0x400201ff, beena_pal_w, &beena_paletteram },
	{ 0x40020200, 0x4002ffff, MWA32_RAM },
	{ 0x40030000, 0x4003ffff, MWA32_RAM, &beena_tilemap_scroll_y },
	{ 0x50000000, 0x5000000f, MWA32_RAM },
	{ 0x50010000, 0x5001003f, beena_pcm_w },
	{ 0x50020000, 0x5002007f, MWA32_RAM, &beena_sensor_regs },
	{ 0x50030000, 0x500300ff, beena_aux_w, &beena_aux_regs },
	{ 0x60000000, 0x600000ff, beena_rtc_w },
	{ 0x60010000, 0x6003ffff, MWA32_RAM },
	{ 0x70000000, 0x7000001f, beena_midi_reg_w },
	{ 0x70010000, 0x7003ffff, MWA32_RAM },
	{ 0x80000000, 0x807fffff, MWA32_NOP },
	{ 0xa0000000, 0xa07fffff, MWA32_RAM },
	{ 0xc0000000, 0xc00c5fff, MWA32_RAM, &beena_bitmap },
	{ 0xc00c6000, 0xc00c68ff, MWA32_RAM, &beena_tilemap_scroll_x },
	{ 0xc00c6900, 0xc00c7fff, MWA32_RAM },
	{ 0xc00c8000, 0xc00c9fff, MWA32_RAM, &beena_tilemap_fg },
	{ 0xc00ca000, 0xc00cbfff, MWA32_RAM, &beena_tilemap_bg },
	{ 0xc00cc000, 0xc00fffff, beena_ramcc_w, &beena_ram_cc },
	{ 0xc0100000, 0xc017ffff, beena_tiles_w },
	{ 0xc0180000, 0xc01fffff, beena_sprites_w },
MEMORY_END

/* ----------------------------------------------------------------------- */
/* input ports                                                              */
/* ----------------------------------------------------------------------- */

INPUT_PORTS_START( beena )
	PORT_START /* PAD_LEFT (bits 0..4) */
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_UP )
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_DOWN )
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_LEFT )
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_JOYSTICKLEFT_RIGHT )
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON1 )

	PORT_START /* (unused) */
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START /* PAD_RIGHT (bits 0..4) */
	PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_UP )
	PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_DOWN )
	PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_LEFT )
	PORT_BIT( 0x0008, IP_ACTIVE_LOW, IPT_JOYSTICKRIGHT_RIGHT )
	PORT_BIT( 0x0010, IP_ACTIVE_LOW, IPT_BUTTON2 )

	PORT_START /* (unused) */
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START /* PEN_LEFT: bit0 = pen button, bit1 = pen target toggle */
	PORT_BITX( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON3, "Pen Button", KEYCODE_SPACE, JOYCODE_1_BUTTON3 )
	PORT_BITX( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON4 | IPF_TOGGLE, "Pen Target (Tablet / Book)", KEYCODE_LSHIFT, JOYCODE_1_BUTTON4 )

	PORT_START /* PAGE: 7-position, Home/End */
	PORT_BITX( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Previous Book Page", KEYCODE_HOME, IP_JOY_NONE )
	PORT_BITX( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Next Book Page", KEYCODE_END, IP_JOY_NONE )

	/* Keep pointer and keyboard coordinates in the same screen units. */
	PORT_START /* PENX */
	PORT_ANALOGX( 0xff, 0x80, IPT_LIGHTGUN_X, 100, 4, 0, 255, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE )

	PORT_START /* PENY */
	PORT_ANALOGX( 0xff, 0x80, IPT_LIGHTGUN_Y, 100, 4, 0, 255, IP_KEY_NONE, IP_KEY_NONE, IP_JOY_NONE, IP_JOY_NONE )

	/* Machine configurations, mirroring upstream's CPU_CONFIG /
	   VIDEO_CONFIG / PAGE_CONFIG (0.78 has no PORT_CONFNAME). */
	PORT_START /* 8: VIDEO_CONFIG */
	PORT_DIPNAME( 0x80, 0x00, "Video Output" )
	PORT_DIPSETTING( 0x00, DEF_STR( On ) )
	PORT_DIPSETTING( 0x80, DEF_STR( Off ) )

	PORT_START /* 9: CPU_CONFIG */
	PORT_DIPNAME( 0x0f, 0x01, "IRQ Frequency" )
	PORT_DIPSETTING( 0x01, "1 (Once every VBLANK)" )
	PORT_DIPSETTING( 0x02, "2" )
	PORT_DIPSETTING( 0x03, "3" )
	PORT_DIPSETTING( 0x04, "4" )
	PORT_DIPSETTING( 0x05, "5" )
	PORT_DIPNAME( 0x10, 0x00, "IRQ Wait Speedup" )
	PORT_DIPSETTING( 0x00, DEF_STR( On ) )
	PORT_DIPSETTING( 0x10, DEF_STR( Off ) )
	PORT_DIPNAME( 0x60, 0x20, "Memory Cache" )
	PORT_DIPSETTING( 0x20, DEF_STR( On ) )
	PORT_DIPSETTING( 0x40, DEF_STR( Off ) )

	PORT_START /* 10: PAGE_CONFIG */
	PORT_DIPNAME( 0x80, 0x80, "Test Mode Pages" )
	PORT_DIPSETTING( 0x00, DEF_STR( On ) )
	PORT_DIPSETTING( 0x80, DEF_STR( Off ) )

	PORT_START /* 11: keyboard pen movement */
	PORT_BITX( 0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Pen Left", KEYCODE_LEFT, IP_JOY_NONE )
	PORT_BITX( 0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Pen Right", KEYCODE_RIGHT, IP_JOY_NONE )
	PORT_BITX( 0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Pen Up", KEYCODE_UP, IP_JOY_NONE )
	PORT_BITX( 0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD, "Pen Down", KEYCODE_DOWN, IP_JOY_NONE )
INPUT_PORTS_END

/* ----------------------------------------------------------------------- */
/* ROM loading                                                              */
/* ----------------------------------------------------------------------- */

ROM_START( beena )
	ROM_REGION32_BE( 0x20000, REGION_CPU1, 0 )
	ROM_LOAD( "9h0-0008.bios.ic1", 0x000000, 0x20000, CRC(5471aaf8) SHA1(33d756b6c64afb0fa377b3f85ab74505e9ae2f9c) )
	ROM_REGION32_BE( 0x8000, REGION_SOUND1, 0 )
	ROM_LOAD( "9h0-0008.midipcm.ic1", 0x000000, 0x8000, CRC(ed336d29) SHA1(4af7b7cf7fc4fe8b7a514cde91f930a582742509) )
ROM_END

#define beena_cart_rom \
	ROM_REGION( 0x400000, REGION_USER1, 0 ) \
	ROM_LOAD( "S-100011-1000 - Meitantei Conan - Kanzen Suiri Kazu to Zukei no Nazo (Japan).bin", 0x000000, 0x400000, CRC(778d58f6) SHA1(985844af2626ebcea56a6e764493d023d6cec293) )

ROM_START( beena_conan )
	ROM_REGION32_BE( 0x20000, REGION_CPU1, 0 )
	ROM_LOAD( "9h0-0008.bios.ic1", 0x000000, 0x20000, CRC(5471aaf8) SHA1(33d756b6c64afb0fa377b3f85ab74505e9ae2f9c) )
	ROM_REGION32_BE( 0x8000, REGION_SOUND1, 0 )
	ROM_LOAD( "9h0-0008.midipcm.ic1", 0x000000, 0x8000, CRC(ed336d29) SHA1(4af7b7cf7fc4fe8b7a514cde91f930a582742509) )
	beena_cart_rom
ROM_END

/* ----------------------------------------------------------------------- */
/* driver                                                                   */
/* ----------------------------------------------------------------------- */

static void beena_init(void)
{
	int i;

	beena_cart_base = (UINT8 *)memory_region(REGION_USER1);
	beena_cart_size = memory_region_length(REGION_USER1);
	beena_cart_base_addr = ROM_MASK_BASE;
	if (beena_cart_base && beena_cart_size >= 0x400000) {
		beena_cart_ram = (UINT32 *)auto_malloc(0x400000);
		for (i = 0; i < 0x100000; i++) {
			/* Cartridge ROM is stored `load16_word_swap`: swap the bytes of
			   each 16-bit word (the BIOS itself is plain big-endian). */
			beena_cart_ram[i] =
				((UINT32)beena_cart_base[i*4+1] << 24) | ((UINT32)beena_cart_base[i*4]   << 16) |
				((UINT32)beena_cart_base[i*4+3] <<  8) |  (UINT32)beena_cart_base[i*4+2];
		}
		/* ROM header (0xdc) selects which address range the BIOS maps the
		   cart into (mask ROM vs NOR flash); invalid values default to the
		   mask-ROM base, matching upstream install_game_rom(). */
		beena_cart_base_addr = beena_cart_ram[0xdc/4];
		if (beena_cart_base_addr != ROM_MASK_BASE && beena_cart_base_addr != ROM_FLASH_BASE)
			beena_cart_base_addr = ROM_MASK_BASE;
	}

	beena_tiles_layers  = auto_malloc(0x80000);
	beena_tiles_sprites = auto_malloc(0x80000);
	memset(beena_tiles_layers, 0, 0x80000);
	memset(beena_tiles_sprites, 0, 0x80000);
	memset(beena_paletteram, 0, 0x200*4);

	beena_scale = 1;
	beena_irq_wait_start_addr = 0xffffffff;
	for (i = 0; i < 0x100/2; i++)
		beena_rtc[i] = 0;
}

static MACHINE_INIT( beena )
{
	beena_requested_fiq = 0;
	beena_irq_wait_start_addr = 0xffffffff;
	memset(beena_video_regs, 0, sizeof(beena_video_regs));
	beena_video_regs[0x10/4] = 0x200;
	beena_scale = 1;
	beena_scanline = 0;
	memset(beena_midi_regs, 0, sizeof(beena_midi_regs));
	beena_midi_busy = 0;
	beena_sensor_regs[0] = 7;
	beena_sensor_regs[0x2c/4] = 0xffffffff;
	beena_sensor_regs[0x30/4] = 0x00ffffff;
	beena_pen_target = 0;
	beena_pen_keys = 0;
	beena_release_book_pen();
	beena_effective_page = 0;
	beena_selected_page = 0;
	beena_page_keys = 0;
	beena_mc_addr = 0; beena_mc_data = 0; beena_mc_i = 0;
	beena_mc_seq = BITS_EMPTY; beena_mc_state = IDLE;
	beena_fsa_size = 0; beena_fsa_head = 0; beena_fsa_tail = 0;
	memset(beena_pcm_regs, 0, sizeof(beena_pcm_regs));
	beena_pcm_fifo_size = 0; beena_pcm_fifo_head = 0; beena_pcm_fifo_tail = 0;
	beena_pcm_fifo_fast_size = 0; beena_pcm_fifo_fast_head = 0; beena_pcm_fifo_fast_tail = 0;
	beena_pcm_volume = 0.0f;
	timer_set(cpu_getscanlinetime(0), 0, beena_scanline_callback);
}

static MACHINE_DRIVER_START( beena )
	MDRV_CPU_ADD(ARM7BE, 81000000)
	MDRV_CPU_MEMORY(beena_readmem, beena_writemem)

	MDRV_FRAMES_PER_SECOND(60)
	MDRV_VBLANK_DURATION(DEFAULT_60HZ_VBLANK_DURATION)
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_RGB_DIRECT | VIDEO_UPDATE_AFTER_VBLANK)

	MDRV_SCREEN_SIZE(SCREEN_W, SCREEN_H)
	MDRV_VISIBLE_AREA(0, SCREEN_W-1, 0, SCREEN_H-1)

	MDRV_PALETTE_LENGTH(0x100)

	MDRV_MACHINE_INIT(beena)
	MDRV_VIDEO_START(beena)
	MDRV_VIDEO_UPDATE(beena)
MACHINE_DRIVER_END

static DRIVER_INIT( beena )
{
	beena_init();
	state_save_register_UINT8("beena", 0, "selected_page", &beena_selected_page, 1);
	state_save_register_UINT8("beena", 0, "page_keys", &beena_page_keys, 1);
	state_save_register_UINT32("beena", 0, "effective_page", &beena_effective_page, 1);
	state_save_register_UINT32("beena", 0, "pen_target", &beena_pen_target, 1);
	state_save_register_UINT8("beena", 0, "pen_keys", &beena_pen_keys, 1);
	state_save_register_INT32("beena", 0, "pen_x", &beena_book_x, 1);
	state_save_register_INT32("beena", 0, "pen_y", &beena_book_y, 1);
	state_save_register_func_postload(beena_restore_book_pen);
}

GAME( 2005, beena,       0, beena, beena, beena, ROT0, "Sega Toys", "Advanced Pico BEENA" )
GAME( 2006, beena_conan, 0, beena, beena, beena, ROT0, "Sega Toys", "Meitantei Conan: Kanzen Suiri! Kazu to Zukei no Nazo (Sega Beena)" )
