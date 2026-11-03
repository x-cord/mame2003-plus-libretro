/* license:BSD-3-Clause
 * XaviX e-kara / Super XaviX Super TV-PC, adapted from current MAME.
 * Upstream copyright-holders: David Haywood, Olivier Galibert, ramacat.
 */
#include "driver.h"
#include "state.h"
#include "cpu/xavix/xavix.h"
#include "xavix_port.h"

static mame_timer *xavix_timers[7];
static int xavix_stream = -1;
static double xavix_remaining[7], xavix_period[7];
static int xavix_super;
static UINT32 *xavix_pixels;
static void xavix_timer_callback(int id) { xavix_tv_timer(id); }
void xavix_host_timer(int id, double delay, double period) {
 xavix_period[id] = period;
 if (!xavix_timers[id]) return;
 timer_adjust(xavix_timers[id], delay > 1e20 ? TIME_NEVER : delay, id, period);
}
void xavix_host_audio_sync(void) { if (xavix_stream >= 0) stream_update(xavix_stream, 0); }
void xavix_host_video_sync(void) { if (xavix_pixels) force_partial_update(cpu_getscanline()); }
double xavix_raster_delay(int y, int x) {
 int scale = xavix_super ? 2 : 1;
 double delay = cpu_getscanlinetime((y & 255) * scale);
 delay += cpu_getscanlineperiod() * scale * (x & 255) / 256.0;
 if (delay <= 0) delay += TIME_IN_HZ(60);
 return delay;
}
UINT8 xavix_host_input(int port) { return readinputport(port); }
int xavix_host_pointer(int *x, int *y) {
 int px, py;
 if (!osd_pointer_position(&px, &py)) return 0;
 *x = ((px + 32767) * 255 + 32767) / 65534;
 /* Firmware mouse Y increases upward; the visible screen spans rows 16-239. */
 *y = 240 - ((py + 32767) * 223 + 32767) / 65534;
 return 1;
}
void xavix_host_save(const char *name, void *data, unsigned bytes) { state_save_register_UINT8("xavix", 0, name, data, bytes); }
static void xavix_audio_callback(int param, INT16 **out, int samples) { xavix_tv_audio(out, samples); }
static int xavix_sound_start(const struct MachineSound *msound) {
 static const char *names[] = {"XaviX left", "XaviX right"};
 static int levels[] = {MIXER(100, MIXER_PAN_LEFT), MIXER(100, MIXER_PAN_RIGHT)};
 xavix_stream = stream_init_multi(2, names, levels, 21477272/128, 0, xavix_audio_callback);
 return xavix_stream < 0;
}
static void xavix_sound_stop(void) { xavix_stream = -1; }
static struct CustomSound_interface xavix_sound = {xavix_sound_start, xavix_sound_stop, 0};
static READ_HANDLER(xavix_read) { return xavix_tv_read(offset); }
static WRITE_HANDLER(xavix_write) { xavix_tv_write(offset, data); }
static MEMORY_READ_START(xavix_readmem)
 {0x000000,0xffffff,xavix_read},
MEMORY_END
static MEMORY_WRITE_START(xavix_writemem)
 {0x000000,0xffffff,xavix_write},
MEMORY_END
static INTERRUPT_GEN(xavix_vblank) { xavix_tv_vblank(); }
static void xavix_presave(void) {
 int i;
 xavix_host_audio_sync();
 for(i=0;i<7;i++)xavix_remaining[i]=timer_timeleft(xavix_timers[i]);
}
static void xavix_postload(void) {
 int i;
 xavix_tv_input_sync();
 for(i=0;i<7;i++)timer_adjust(xavix_timers[i],xavix_remaining[i],i,xavix_period[i]);
}
static MACHINE_INIT(xavixtv) {
 int i;
 for(i=0;i<7;i++)xavix_timers[i]=timer_alloc(xavix_timer_callback);
 xavix_tv_set_clock(timer_get_time);
 xavix_tv_reset(xavix_super,memory_region(REGION_USER1),memory_region(REGION_USER2));
 xavix_tv_register_state();
 state_save_register_double("xavix",0,"timer remaining",xavix_remaining,xavix_super?7:6);
 state_save_register_double("xavix",0,"timer period",xavix_period,xavix_super?7:6);
 state_save_register_func_presave(xavix_presave);
 state_save_register_func_postload(xavix_postload);
}
static VIDEO_START(xavixtv) {
 int height=xavix_super?512:256;
 xavix_pixels=auto_malloc(512*height*sizeof(UINT32));
 memset(xavix_pixels,0,512*height*sizeof(UINT32));
 return 0;
}
static VIDEO_UPDATE(xavixtv) {
 int y,x;
 if(xavix_super)xavix_tv_render_progressive(xavix_pixels,cliprect->min_y,cliprect->max_y);
 else xavix_tv_render(xavix_pixels,cliprect->min_y,cliprect->max_y);
 if(bitmap->depth==32) {
  for(y=cliprect->min_y;y<=cliprect->max_y;y++)
   memcpy((UINT32*)bitmap->line[y]+cliprect->min_x,xavix_pixels+y*512+cliprect->min_x,(cliprect->max_x-cliprect->min_x+1)*sizeof(UINT32));
 } else {
  for(y=cliprect->min_y;y<=cliprect->max_y;y++)
   for(x=cliprect->min_x;x<=cliprect->max_x;x++) {
    UINT32 c=xavix_pixels[y*512+x];
    ((UINT16*)bitmap->line[y])[x]=((c>>9)&0x7c00)|((c>>6)&0x03e0)|((c>>3)&0x001f);
   }
 }
}
static DRIVER_INIT(ekara) {xavix_super=0;}
static DRIVER_INIT(stvpc) {
 xavix_super=1;
 /* Same EEPROM-error bypass as upstream super_tv_pc_cart.xml. */
 memory_region(REGION_USER2)[0xd5fa]=0xea;
 memory_region(REGION_USER2)[0xd5fb]=0xea;
}

const char *xavix_info(void *context,int regnum) {
 static char buffer[64];
 switch(regnum) {
  case CPU_INFO_NAME:return "XaviX";
  case CPU_INFO_FAMILY:return "XaviX / Super XaviX";
  case CPU_INFO_VERSION:return "MAME 0.78 port";
  case CPU_INFO_FILE:return __FILE__;
  case CPU_INFO_CREDITS:return "David Haywood, Olivier Galibert";
  case CPU_INFO_FLAGS:return "";
  default: if(regnum>=CPU_INFO_REG&&regnum<CPU_INFO_REG+7){sprintf(buffer,"R%d:%06X",regnum-CPU_INFO_REG,xavix_get_reg(regnum-CPU_INFO_REG));return buffer;}return "";
 }
}

INPUT_PORTS_START(ekara)
 PORT_START
 PORT_BIT(0xff,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_BIT(0xff,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Echo",KEYCODE_E,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Effects",KEYCODE_F,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Key Down",KEYCODE_DOWN,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Key Up",KEYCODE_UP,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"BGM Down",KEYCODE_A,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"BGM Up",KEYCODE_S,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Tempo Down",KEYCODE_LEFT,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Tempo Up",KEYCODE_RIGHT,IP_JOY_NONE)
 PORT_START
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Select",KEYCODE_ENTER,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Cancel",KEYCODE_BACKSPACE,IP_JOY_NONE)
 PORT_BIT(0xfc,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_BIT(0xff,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_BIT(0xff,IP_ACTIVE_HIGH,IPT_UNUSED)
INPUT_PORTS_END
INPUT_PORTS_START(stvpc)
 PORT_START
 /* Conan polls IO0 bit 4 for mouse 1's left button. */
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_BUTTON1,"Left mouse",IP_KEY_NONE,JOYCODE_MOUSE_1_BUTTON1)
 PORT_BIT(0xef,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 /* IO1 bit 3 is the right button; both inputs are active high. */
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_BUTTON2,"Right mouse",IP_KEY_NONE,JOYCODE_MOUSE_1_BUTTON2)
 PORT_BIT(0xf7,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Cursor left",KEYCODE_LEFT,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Cursor right",KEYCODE_RIGHT,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Cursor up",KEYCODE_UP,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Cursor down",KEYCODE_DOWN,IP_JOY_NONE)
 PORT_BIT(0xf0,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START /* Serial keyboard scan codes 00-07. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Space",KEYCODE_SPACE,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Super",KEYCODE_LCONTROL,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Shift",KEYCODE_LSHIFT,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Input mode",KEYCODE_F1,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Hiragana / Katakana / Alphanumeric (英数)",KEYCODE_F2,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Back (Modoru)",KEYCODE_F3,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Symbols",KEYCODE_F4,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Emoji",KEYCODE_F5,IP_JOY_NONE)
 PORT_START /* Serial keyboard scan codes 08-0f. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Back Space",KEYCODE_BACKSPACE,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Mail",KEYCODE_TAB,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Enter",KEYCODE_ENTER,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Esc",KEYCODE_ESC,IP_JOY_NONE)
 PORT_BIT(0x10,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_BIT(0x20,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_BIT(0x40,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_BIT(0x80,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START /* Serial keyboard scan codes 10-17. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"0",KEYCODE_0,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"1",KEYCODE_1,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"2",KEYCODE_2,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"3",KEYCODE_3,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"4",KEYCODE_4,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"5",KEYCODE_5,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,"6",KEYCODE_6,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"7",KEYCODE_7,IP_JOY_NONE)
 PORT_START /* Serial keyboard scan codes 18-1f. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"8",KEYCODE_8,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"9",KEYCODE_9,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,":",KEYCODE_QUOTE,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,";",KEYCODE_COLON,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,",",KEYCODE_COMMA,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"-",KEYCODE_MINUS,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,".",KEYCODE_STOP,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"/",KEYCODE_SLASH,IP_JOY_NONE)
 PORT_START /* Serial keyboard scan codes 20-27. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"゛ / @",KEYCODE_TILDE,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"A",KEYCODE_A,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"B",KEYCODE_B,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"C",KEYCODE_C,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"D",KEYCODE_D,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"E",KEYCODE_E,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,"F",KEYCODE_F,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"G",KEYCODE_G,IP_JOY_NONE)
 PORT_START /* Serial keyboard scan codes 28-2f. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"H",KEYCODE_H,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"I",KEYCODE_I,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"J",KEYCODE_J,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"K",KEYCODE_K,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"L",KEYCODE_L,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"M",KEYCODE_M,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,"N",KEYCODE_N,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"O",KEYCODE_O,IP_JOY_NONE)
 PORT_START /* Serial keyboard scan codes 30-37. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"P",KEYCODE_P,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Q",KEYCODE_Q,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"R",KEYCODE_R,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"S",KEYCODE_S,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"T",KEYCODE_T,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"U",KEYCODE_U,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,"V",KEYCODE_V,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"W",KEYCODE_W,IP_JOY_NONE)
 PORT_START /* Serial keyboard scan codes 38-3f. */
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"X",KEYCODE_X,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Y",KEYCODE_Y,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Z",KEYCODE_Z,IP_JOY_NONE)
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"゜ / [",KEYCODE_OPENBRACE,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"ー / Yen",KEYCODE_BACKSLASH,IP_JOY_NONE)
 PORT_BITX(0x20,IP_ACTIVE_HIGH,IPT_KEYBOARD,"む / ]",KEYCODE_CLOSEBRACE,IP_JOY_NONE)
 PORT_BITX(0x40,IP_ACTIVE_HIGH,IPT_KEYBOARD,"へ / ^",KEYCODE_EQUALS,IP_JOY_NONE)
 PORT_BITX(0x80,IP_ACTIVE_HIGH,IPT_KEYBOARD,"ろ / Underscore",KEYCODE_BACKSLASH2,IP_JOY_NONE)
 PORT_START
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Right Shift",KEYCODE_RSHIFT,IP_JOY_NONE)
 PORT_BIT(0xfe,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START /* Left (red) pad: serial channel 100. */
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Left pad up",KEYCODE_HOME,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Left pad down",KEYCODE_END,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Left pad left",KEYCODE_DEL,IP_JOY_NONE)
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Left pad right",KEYCODE_PGDN,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Red action button",KEYCODE_INSERT,IP_JOY_NONE)
 PORT_BIT(0xe0,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START /* Right (blue) pad: serial channel 101. */
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Right pad up",KEYCODE_8_PAD,IP_JOY_NONE)
 PORT_BITX(0x04,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Right pad down",KEYCODE_2_PAD,IP_JOY_NONE)
 PORT_BITX(0x02,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Right pad left",KEYCODE_4_PAD,IP_JOY_NONE)
 PORT_BITX(0x01,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Right pad right",KEYCODE_6_PAD,IP_JOY_NONE)
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_KEYBOARD,"Blue action button",KEYCODE_0_PAD,IP_JOY_NONE)
 PORT_BIT(0xe0,IP_ACTIVE_HIGH,IPT_UNUSED)
INPUT_PORTS_END
static MACHINE_DRIVER_START(ekara)
 MDRV_CPU_ADD_TAG("main",XAVIX,21477272)
 MDRV_CPU_MEMORY(xavix_readmem,xavix_writemem)
 MDRV_CPU_VBLANK_INT(xavix_vblank,1)
 MDRV_FRAMES_PER_SECOND(60)
 MDRV_VBLANK_DURATION(DEFAULT_60HZ_VBLANK_DURATION)
 MDRV_MACHINE_INIT(xavixtv)
 MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER|VIDEO_RGB_DIRECT)
 MDRV_SCREEN_SIZE(256,256)
 MDRV_VISIBLE_AREA(0,255,16,239)
 MDRV_PALETTE_LENGTH(256)
 MDRV_VIDEO_START(xavixtv)
 MDRV_VIDEO_UPDATE(xavixtv)
 MDRV_SOUND_ADD(CUSTOM,xavix_sound)
MACHINE_DRIVER_END
static MACHINE_DRIVER_START(stvpc)
 MDRV_IMPORT_FROM(ekara)
 MDRV_CPU_REPLACE("main",XAVIX,42954544)
 MDRV_SCREEN_SIZE(512,512)
 MDRV_VISIBLE_AREA(0,511,32,479)
 MDRV_ASPECT_RATIO(4,3)
MACHINE_DRIVER_END

#define EKARA_BIOS \
 ROM_REGION(0x100000,REGION_USER1,0) \
 ROM_LOAD("ekara.bin",0,0x100000,CRC(9b27c4a2) SHA1(d75dda7434933135d2f7e353840a9384e9a0d586))
ROM_START(ekara_gk1)
 EKARA_BIOS
 ROM_REGION(0x200000,REGION_USER2,0)
 ROM_LOAD("gk1.bin",0,0x200000,CRC(775608cd) SHA1(8b53fd4e8ced709df2716c839abfd7e41cc719d2))
ROM_END
ROM_START(ekara_sc7)
 EKARA_BIOS
 ROM_REGION(0x200000,REGION_USER2,0)
 ROM_LOAD("sc0007-dcs.u1",0,0x200000,CRC(5b8cf5ee) SHA1(b31e610110fda7c7b49e7318d3191967fcf49ed8))
ROM_END
ROM_START(ekara_sc15)
 EKARA_BIOS
 ROM_REGION(0x200000,REGION_USER2,0)
 ROM_LOAD("sc0015-dcs.u1",0,0x200000,CRC(a1b31faa) SHA1(e5508940c5e674a1e12ef2cc9a5d10365ac4fa64))
ROM_END
ROM_START(stvpc_conan)
 ROM_REGION(0x800000,REGION_USER1,0)
 ROM_LOAD("supertvpc_dogs.u4",0x200000,0x200000,CRC(ab326e6d) SHA1(e22205f6ff4c8cc46538d78e27535be63acea42a))
 ROM_CONTINUE(0x000000,0x200000)
 ROM_CONTINUE(0x600000,0x200000)
 ROM_CONTINUE(0x400000,0x200000)
 ROM_REGION(0x400000,REGION_USER2,0)
 ROM_LOAD("c-04.u1",0,0x400000,CRC(89b6ef86) SHA1(1af69047508fce08fa949a7d821255e306a329a3))
ROM_END
GAMEX(2004,ekara_gk1,0,ekara,ekara,ekara,ROT0,"Takara","GK-1 - Detective Conan (e-kara)",GAME_IMPERFECT_SOUND)
GAMEX(2001,ekara_sc7,0,ekara,ekara,ekara,ROT0,"Takara","Saiten Cartridge - Detective Conan Vol. 1 (e-kara)",GAME_IMPERFECT_SOUND)
GAMEX(2002,ekara_sc15,0,ekara,ekara,ekara,ROT0,"Takara","Saiten Cartridge - Detective Conan Vol. 2 (e-kara)",GAME_IMPERFECT_SOUND)
GAMEX(2005,stvpc_conan,0,stvpc,stvpc,stvpc,ROT0,"Epoch","Detective Conan - Suiri Adventure (Super TV-PC)",GAME_NOT_WORKING|GAME_IMPERFECT_GRAPHICS|GAME_IMPERFECT_SOUND)
