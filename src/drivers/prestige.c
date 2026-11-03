/***************************************************************************

    VTech PreComputer Prestige / Bandai Super Note Club

    Ported to MAME 0.78 / MAME2003-plus from the modern MAME driver
    (src/mame/vtech/prestige.cpp, copyright-holders:Sandro Ronco).

    The original driver supports a family of VTech/Bandai handhelds built
    around a Z80 and a custom LCD controller.  This port targets the
    "Genius Leader Color"-style 2bpp, 160x80 machines, in particular the
    Bandai Super Note Club cartridges (Detective Conan 1/2/3).

    Because the 0.78 core has no generic cartridge slot, each supported
    cartridge is exposed as its own machine whose ROM set contains both the
    base unit ROM and the cartridge ROM.

    PCB (PreComputer Prestige Elite)
    --------------------------------
    Z80 @ 8MHz, 32Kx8 SRAM, 512Kx8 mask ROM, LCD, keyboard, mouse, NVRAM.

    Memory map (base banks are 0x4000 each):
        0x0000-0x3fff  bank1  (base ROM or cart)
        0x4000-0x7fff  bank2  (base ROM or cart)
        0x8000-0xbfff  bank3  (base ROM or cart / RAM display window)
        0xc000-0xdfff  bank4  (RAM page)
        0xe000-0xffff  bank5  (RAM page)

    I/O:
        0x04-0x05  mouse
        0x30-0x3f  LCD controller
        0x40       keyboard matrix select
        0x41-0x42  keyboard matrix data
        0x50-0x56  bankswitch

***************************************************************************/

#include "driver.h"
#include "cpu/z80/z80.h"
#include "vidhrdw/generic.h"

/* ----------------------------------------------------------------------- */
/* machine state                                                           */
/* ----------------------------------------------------------------------- */

#define PRESTIGE_RAM_SIZE   0x8000
#define PRESTIGE_ROM_BANK   0x4000

static data8_t *prestige_ram;       /* 32Kb work + video RAM */
static data8_t *prestige_base;      /* base unit ROM        */
static data8_t *prestige_cart;      /* cartridge ROM        */

static UINT32 prestige_num_entries; /* number of base ROM banks */
static UINT32 prestige_rom_mask;

static UINT8 prestige_bank[7];
static data8_t *prestige_bank3_base;	/* current bank3 target (0x8000-0xbfff) */

static UINT8 prestige_kb_matrix;
static UINT8 prestige_irq_counter;

static UINT16 lcd_addr1;
static UINT16 lcd_addr2;
static UINT8  lcd_w;
static UINT8  lcd_h;
static UINT8  lcd_fb_width;
static UINT8  lcd_split_pos;

/* Normalize the offset passed to I/O handlers. MAME may pass either the full
   port address or the offset relative to the handler's range; handle both. */
static int rel_offset(offs_t offset, offs_t base)
{
	return (offset >= base) ? (offset - base) : (int)offset;
}

static READ_HANDLER( prestige_ram_r );
static WRITE_HANDLER( prestige_ram_w );

/* ----------------------------------------------------------------------- */
/* banking                                                                 */
/* ----------------------------------------------------------------------- */

static READ_HANDLER( bankswitch_r )
{
	return prestige_bank[rel_offset(offset, 0x50)];
}

static WRITE_HANDLER( bankswitch_w )
{
	int reg = rel_offset(offset, 0x50);

	switch (reg)
	{
		case 0:
			cpu_setbank(1, prestige_base + (data & prestige_rom_mask) * PRESTIGE_ROM_BANK);
			break;

		case 1:
			if (!(prestige_bank[5] & 0x01) && (prestige_bank[5] & 0x02))
				cpu_setbank(2, prestige_cart + (data & 0x1f) * PRESTIGE_ROM_BANK);
			else
				cpu_setbank(2, prestige_base + (data & prestige_rom_mask) * PRESTIGE_ROM_BANK);
			break;

		case 2:
			if (!(prestige_bank[5] & 0x01) && (prestige_bank[5] & 0x04))
				prestige_bank3_base = prestige_cart + (data & 0x1f) * PRESTIGE_ROM_BANK;
			else
				prestige_bank3_base = prestige_base + (data & prestige_rom_mask) * PRESTIGE_ROM_BANK;
			cpu_setbank(3, prestige_bank3_base);
			break;

		case 3:
			cpu_setbank(4, prestige_ram + (data & 0x03) * 0x2000);
			break;

		case 4:
			cpu_setbank(5, prestige_ram + (data & 0x03) * 0x2000);
			break;

		case 5:
			if (prestige_bank[5] == data)
				break;

			if (data & 0x20)
			{
				/* bit 0x20 selects the RAM display window at 0x8000-0xbfff */
				install_mem_read_handler(0, 0x8000, 0xbfff, prestige_ram_r);
				install_mem_write_handler(0, 0x8000, 0xbfff, prestige_ram_w);
			}
			else
			{
				/* cartridge memory is read-only */
				install_mem_read_handler(0, 0x8000, 0xbfff, MRA_BANK3);
				install_mem_write_handler(0, 0x8000, 0xbfff, MWA_NOP);

				/* MAME 0.78's install_mem_read_handler() with a static bank
				   resets the bank base pointer.  Restore the bank3 target that
				   the last write to port 0x52 selected, otherwise the
				   cartridge window at 0x8000-0xbfff is lost. */
				cpu_setbank(3, prestige_bank3_base);
			}
			break;

		case 6:
			break;
	}

	prestige_bank[reg] = data;
}

/* ----------------------------------------------------------------------- */
/* keyboard / mouse                                                        */
/* ----------------------------------------------------------------------- */

static WRITE_HANDLER( kb_w )
{
	prestige_kb_matrix = data;
}

static READ_HANDLER( kb_r )
{
	int offs = rel_offset(offset, 0x41);
	UINT8 data = 0xff;
	int line;

	for (line = 0; line < 8; line++)
		if (!(prestige_kb_matrix & (1 << line)))
			data &= readinputport(offs * 8 + line);

	/* The firmware's menu "select" is matrix KEY.6 bit 5 (glcolor "Button 2",
	   KEYCODE_END). Let the space bar and Enter trigger it as well. */
	if (offs == 0 && (readinputport(5) & 0x50) != 0x50)
	{
		/* Space / Enter act as the menu select key (KEY.6 bit 5). */
		if (!(prestige_kb_matrix & 0x40))
			data &= ~0x20;
		/* ...and are not reported as their own keys, so they don't
		   interfere with the selection. */
		data |= 0x50;
	}

	return data;
}

/* ----------------------------------------------------------------------- */
/* LCD controller                                                          */
/* ----------------------------------------------------------------------- */

static WRITE_HANDLER( lcdc_w )
{
	switch (offset & 0x0f)
	{
		case 0x02: lcd_w = data; break;
		case 0x04: lcd_h = data; break;
		case 0x06: lcd_addr1 = (lcd_addr1 & 0xff00) | data; break;
		case 0x07: lcd_addr1 = (lcd_addr1 & 0x00ff) | (data << 8); break;
		case 0x08: lcd_addr2 = (lcd_addr2 & 0xff00) | data; break;
		case 0x09: lcd_addr2 = (lcd_addr2 & 0x00ff) | (data << 8); break;
		case 0x0a: lcd_split_pos = data; break;
		case 0x0d: lcd_fb_width = data; break;
		default: break;
	}
}

/* ----------------------------------------------------------------------- */
/* memory / I/O maps                                                       */
/* ----------------------------------------------------------------------- */

static READ_HANDLER( prestige_ram_r )
{
	return prestige_ram[offset & 0x3fff];
}

static WRITE_HANDLER( prestige_ram_w )
{
	prestige_ram[offset & 0x3fff] = data;
}

static MEMORY_READ_START( prestige_readmem )
	{ 0x0000, 0x3fff, MRA_BANK1 },
	{ 0x4000, 0x7fff, MRA_BANK2 },
	{ 0x8000, 0xbfff, MRA_BANK3 },
	{ 0xc000, 0xdfff, MRA_BANK4 },
	{ 0xe000, 0xffff, MRA_BANK5 },
MEMORY_END

static MEMORY_WRITE_START( prestige_writemem )
	{ 0x0000, 0x3fff, MWA_NOP },
	{ 0x4000, 0x7fff, MWA_NOP },
	{ 0x8000, 0xbfff, MWA_NOP },
	{ 0xc000, 0xdfff, MWA_BANK4 },
	{ 0xe000, 0xffff, MWA_BANK5 },
MEMORY_END

static PORT_READ_START( prestige_readport )
	{ 0x41, 0x42, kb_r },
	{ 0x50, 0x56, bankswitch_r },
PORT_END

static PORT_WRITE_START( prestige_writeport )
	{ 0x30, 0x3f, lcdc_w },
	{ 0x40, 0x40, kb_w },
	{ 0x50, 0x56, bankswitch_w },
PORT_END

/* ----------------------------------------------------------------------- */
/* input ports                                                             */
/* ----------------------------------------------------------------------- */

INPUT_PORTS_START( prestige )

	/* The Super Note Club uses the "Genius Leader Color" keyboard matrix
	   (the machine reuses the glcolor input ports upstream). Matrix lines
	   0..7 are selected via the keyboard scan register; ports 0..15. */

	PORT_START	/* KEY.0 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "Esc", KEYCODE_ESC, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "1", KEYCODE_1, IP_JOY_NONE)
	PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD, "2", KEYCODE_2, IP_JOY_NONE)
	PORT_BITX(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD, "3", KEYCODE_3, IP_JOY_NONE)
	PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD, "4", KEYCODE_4, IP_JOY_NONE)
	PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD, "5", KEYCODE_5, IP_JOY_NONE)
	PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD, "6", KEYCODE_6, IP_JOY_NONE)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD, "7", KEYCODE_7, IP_JOY_NONE)

	PORT_START	/* KEY.1 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "Help", KEYCODE_F6, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "q", KEYCODE_Q, IP_JOY_NONE)
	PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD, "w", KEYCODE_W, IP_JOY_NONE)
	PORT_BITX(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD, "e", KEYCODE_E, IP_JOY_NONE)
	PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD, "r", KEYCODE_R, IP_JOY_NONE)
	PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD, "t", KEYCODE_T, IP_JOY_NONE)
	PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD, "equals", KEYCODE_EQUALS, IP_JOY_NONE)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD, "0", KEYCODE_0, IP_JOY_NONE)

	PORT_START	/* KEY.2 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "Caps Lock", KEYCODE_CAPSLOCK, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "a", KEYCODE_A, IP_JOY_NONE)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNKNOWN)
	PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD, "u", KEYCODE_U, IP_JOY_NONE)
	PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD, "z", KEYCODE_Z, IP_JOY_NONE)
	PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD, "i", KEYCODE_I, IP_JOY_NONE)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD, "o", KEYCODE_O, IP_JOY_NONE)

	PORT_START	/* KEY.3 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "Left Shift", KEYCODE_LSHIFT, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "s", KEYCODE_S, IP_JOY_NONE)
	PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD, "d", KEYCODE_D, IP_JOY_NONE)
	PORT_BITX(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD, "f", KEYCODE_F, IP_JOY_NONE)
	PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD, "g", KEYCODE_G, IP_JOY_NONE)
	PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD, "h", KEYCODE_H, IP_JOY_NONE)
	PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD, "j", KEYCODE_J, IP_JOY_NONE)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD, "k", KEYCODE_K, IP_JOY_NONE)

	PORT_START	/* KEY.4 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "Player 1", KEYCODE_F1, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "y", KEYCODE_Y, IP_JOY_NONE)
	PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD, "x", KEYCODE_X, IP_JOY_NONE)
	PORT_BITX(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD, "c", KEYCODE_C, IP_JOY_NONE)
	PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD, "v", KEYCODE_V, IP_JOY_NONE)
	PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD, "b", KEYCODE_B, IP_JOY_NONE)
	PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD, "n", KEYCODE_N, IP_JOY_NONE)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD, "m", KEYCODE_M, IP_JOY_NONE)

	PORT_START	/* KEY.5 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "Alt", KEYCODE_LALT, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "Ctrl", KEYCODE_LCONTROL, IP_JOY_NONE)
	PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD, "Players", KEYCODE_F5, IP_JOY_NONE)
	PORT_BITX(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD, "Level", KEYCODE_F3, IP_JOY_NONE)
	PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD, "Space", KEYCODE_SPACE, IP_JOY_NONE)
	PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD, "Symbols", KEYCODE_F4, IP_JOY_NONE)
	PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD, "Enter", KEYCODE_ENTER, IP_JOY_NONE)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD, ",", KEYCODE_COMMA, IP_JOY_NONE)

	PORT_START	/* KEY.6 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "Down", KEYCODE_DOWN, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "Left", KEYCODE_LEFT, IP_JOY_NONE)
	PORT_BITX(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD, "Up", KEYCODE_UP, IP_JOY_NONE)
	PORT_BITX(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD, "Right", KEYCODE_RIGHT, IP_JOY_NONE)
	PORT_BITX(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD, "Button 1", KEYCODE_DEL, IP_JOY_NONE)
	PORT_BITX(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD, "Button 2", KEYCODE_END, IP_JOY_NONE)
	PORT_BITX(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD, "Button 3", KEYCODE_HOME, IP_JOY_NONE)
	PORT_BITX(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD, "Button 4", KEYCODE_PGUP, IP_JOY_NONE)

	PORT_START	/* KEY.7 */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.8 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "8", KEYCODE_8, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "Backspace", KEYCODE_BACKSPACE, IP_JOY_NONE)
	PORT_BIT(0xfc, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.9 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "9", KEYCODE_9, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "umlaut-u", KEYCODE_OPENBRACE, IP_JOY_NONE)
	PORT_BIT(0xfc, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.10 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "p", KEYCODE_P, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "umlaut-a", KEYCODE_CLOSEBRACE, IP_JOY_NONE)
	PORT_BIT(0xfc, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.11 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "l", KEYCODE_L, IP_JOY_NONE)
	PORT_BIT(0xfe, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.12 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, "umlaut-o", KEYCODE_QUOTE, IP_JOY_NONE)
	PORT_BITX(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD, "Player 2", KEYCODE_F2, IP_JOY_NONE)
	PORT_BIT(0xfc, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.13 */
	PORT_BITX(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD, ".", KEYCODE_STOP, IP_JOY_NONE)
	PORT_BIT(0xfe, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.14 */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNKNOWN)

	PORT_START	/* KEY.15 */
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNKNOWN)

INPUT_PORTS_END

/* ----------------------------------------------------------------------- */
/* video                                                                   */
/* ----------------------------------------------------------------------- */

static VIDEO_START( prestige )
{
	return 0;
}

static PALETTE_INIT( prestige )
{
	palette_set_color(0, 0x3f, 0xbf, 0x3f);
	palette_set_color(1, 0xff, 0x3f, 0x5f);
	palette_set_color(2, 0x1f, 0x1f, 0x3f);
	palette_set_color(3, 0xff, 0xdf, 0x1f);
}

static VIDEO_UPDATE( prestige )
{
	int width = lcd_fb_width + lcd_w + 1;
	int y, sx, x, b, pix;
	UINT8 data;

	for (y = 0; y <= lcd_h; y++)
	{
		int line_start;
		if (y <= lcd_split_pos)
			line_start = lcd_addr1 + y * width;
		else
			line_start = lcd_addr2 + (y - lcd_split_pos - 1) * width;

		for (sx = 0; sx <= lcd_w; sx++)
		{
			data = prestige_ram[(line_start + sx) & 0x1fff];

			for (x = 0; x < 8 / 2; x++)
			{
				pix = 0;
				for (b = 0; b < 2; b++)
					pix |= ((data >> (7 - b)) & 1) << b;

				if (sx * 4 + x <= cliprect->max_x && sx * 4 + x >= cliprect->min_x &&
					y >= cliprect->min_y && y <= cliprect->max_y)
					plot_pixel(bitmap, sx * 4 + x, y, pix);

				data <<= 2;
			}
		}
	}
}

/* ----------------------------------------------------------------------- */
/* interrupts                                                              */
/* ----------------------------------------------------------------------- */

static int prestige_int_ack(int irqline)
{
	UINT32 vector;

	cpu_set_irq_line(0, 0, CLEAR_LINE);

	if (prestige_irq_counter == 0x04)
	{
		prestige_irq_counter = 0;
		vector = 0x0020;
	}
	else
	{
		prestige_irq_counter++;
		vector = 0x0030;
	}

	/* IM0: execute a CALL to the vector */
	return (0xcd << 16) | vector;
}

static void prestige_irq_timer(int param)
{
	cpu_set_irq_line(0, 0, ASSERT_LINE);
	timer_set(1.0 / 200.0, 0, prestige_irq_timer);
}

/* ----------------------------------------------------------------------- */
/* machine driver                                                          */
/* ----------------------------------------------------------------------- */

static MACHINE_INIT( prestige )
{
	int i;

	prestige_ram  = auto_malloc(PRESTIGE_RAM_SIZE);
	prestige_base = memory_region(REGION_CPU1);
	prestige_cart = memory_region(REGION_CPU2);

	memset(prestige_ram, 0x00, PRESTIGE_RAM_SIZE);

	/* Modern MAME zero-fills the unused tail of a ROM region; 0.78 does not.
	   The base unit ROM is 512K but the region is 1M, and the firmware can
	   bank the upper half into the address space (mask 0x3f).  Without this
	   the CPU executes garbage there.  Match upstream and zero it. */
	if (memory_region_length(REGION_CPU1) > 0x080000)
		memset(prestige_base + 0x080000, 0x00, memory_region_length(REGION_CPU1) - 0x080000);

	/* The firmware polls some unmapped ports (e.g. 0x10, 0x20) waiting for
	   bits to be set; upstream MAME uses unmap_value_high() so those reads
	   return 0xff. Match that behaviour. */
	memory_set_unmap_value(0xff);

	prestige_num_entries = memory_region_length(REGION_CPU1) / PRESTIGE_ROM_BANK;
	prestige_rom_mask    = prestige_num_entries - 1;

	cpu_setbank(1, prestige_base);
	cpu_setbank(2, prestige_base);
	cpu_setbank(3, prestige_base);
	cpu_setbank(4, prestige_ram);
	cpu_setbank(5, prestige_ram);

	prestige_bank3_base = prestige_base;

	for (i = 0; i < 7; i++)
		prestige_bank[i] = 0;

	prestige_kb_matrix = 0;
	prestige_irq_counter = 0;

	lcd_addr1 = 0;
	lcd_addr2 = 0;
	lcd_w = 0;
	lcd_h = 0;
	lcd_fb_width = 0;
	lcd_split_pos = 0;

	cpu_set_irq_callback(0, prestige_int_ack);
	timer_set(1.0 / 200.0, 0, prestige_irq_timer);
}

static MACHINE_DRIVER_START( prestige )

	/* basic machine hardware */
	MDRV_CPU_ADD(Z80, 8000000)		/* Z84C008 @ 8MHz */
	MDRV_CPU_MEMORY(prestige_readmem, prestige_writemem)
	MDRV_CPU_PORTS(prestige_readport, prestige_writeport)

	MDRV_FRAMES_PER_SECOND(50)
	/* The LCD controller registers (window base, size, split) are updated by
	   the firmware during the vertical blank; the screen must therefore be
	   rendered *after* that work, otherwise the frame is sampled mid-update
	   and tears when the display scrolls/changes.  Upstream expresses this as
	   screen.set_vblank_time(2500us); the 0.78 equivalent is a non-zero
	   vblank duration plus VIDEO_UPDATE_AFTER_VBLANK. */
	MDRV_VBLANK_DURATION(DEFAULT_REAL_60HZ_VBLANK_DURATION)

	MDRV_MACHINE_INIT(prestige)

	/* video hardware */
	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER | VIDEO_UPDATE_AFTER_VBLANK)
	MDRV_SCREEN_SIZE(160, 80)
	MDRV_VISIBLE_AREA(0, 160-1, 0, 80-1)
	MDRV_ASPECT_RATIO(160, 80)	/* square pixels (LCD) */
	MDRV_PALETTE_LENGTH(4)

	MDRV_PALETTE_INIT(prestige)
	MDRV_VIDEO_START(prestige)
	MDRV_VIDEO_UPDATE(prestige)

	/* sound hardware (none) */
MACHINE_DRIVER_END

/* ----------------------------------------------------------------------- */
/* ROMs                                                                    */
/*                                                                         */
/* Each ROM set contains the base unit ROM (27-5616-01.u6, Super Note Club)*/
/* followed by the cartridge ROM.                                          */
/* ----------------------------------------------------------------------- */

#define PRESTIGE_BASE_ROM \
	ROM_LOAD( "27-5616-01.u6", 0x000000, 0x080000, CRC(74093f5b) SHA1(3495b07e297315051888261d608680513a05c08b) )

ROM_START( snotec_conan )
	ROM_REGION( 0x100000, REGION_CPU1, 0 )	/* base unit ROM */
	PRESTIGE_BASE_ROM
	ROM_REGION( 0x080000, REGION_CPU2, 0 )	/* cartridge ROM */
	ROM_LOAD( "LH532KV6.IC1", 0x000000, 0x040000, CRC(edf3cc1e) SHA1(0b759ff7dd2bd6809e5634679f758de106fed654) )
ROM_END

ROM_START( snotec_conan2 )
	ROM_REGION( 0x100000, REGION_CPU1, 0 )
	PRESTIGE_BASE_ROM
	ROM_REGION( 0x080000, REGION_CPU2, 0 )
	ROM_LOAD( "SNC_Conan2.ic", 0x000000, 0x040000, CRC(47a3ce5e) SHA1(2f9653ef01026d8a8f8c3036bda533336ebc7e0a) )
ROM_END

ROM_START( snotec_conan3 )
	ROM_REGION( 0x100000, REGION_CPU1, 0 )
	PRESTIGE_BASE_ROM
	ROM_REGION( 0x080000, REGION_CPU2, 0 )
	ROM_LOAD( "SNC-CONAN3.IC1", 0x000000, 0x080000, CRC(f93d6149) SHA1(b624db8fd47cae79357de4d60d41d88ebe1f089d) )
ROM_END

/* ----------------------------------------------------------------------- */
/* game drivers                                                            */
/* ----------------------------------------------------------------------- */

GAMEX( 1996, snotec_conan,  0, prestige, prestige, 0, ROT0, "Bandai", "Super Note Club - Detective Conan", GAME_NO_SOUND )
GAMEX( 1996, snotec_conan2, 0, prestige, prestige, 0, ROT0, "Bandai", "Super Note Club - Detective Conan Episode 2", GAME_NO_SOUND )
GAMEX( 1999, snotec_conan3, 0, prestige, prestige, 0, ROT0, "Bandai", "Super Note Club - Detective Conan Episode 3", GAME_NO_SOUND )
