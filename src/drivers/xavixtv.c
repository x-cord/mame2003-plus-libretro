/* license:BSD-3-Clause
 * XaviX e-kara / Super XaviX Super TV-PC, adapted from current MAME.
 * Upstream copyright-holders: David Haywood, Olivier Galibert, ramacat.
 */
#include "driver.h"
#include "state.h"
#include "cpu/xavix/xavix.h"
#include "xavix_port.h"

static mame_timer *xavix_timers[6];
static int xavix_stream = -1;
static double xavix_remaining[6], xavix_period[6];
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
 for(i=0;i<6;i++)xavix_remaining[i]=timer_timeleft(xavix_timers[i]);
}
static void xavix_postload(void) {
 int i;
 for(i=0;i<6;i++)timer_adjust(xavix_timers[i],xavix_remaining[i],i,xavix_period[i]);
}
static MACHINE_INIT(xavixtv) {
 int i;
 for(i=0;i<6;i++)xavix_timers[i]=timer_alloc(xavix_timer_callback);
 xavix_tv_reset(xavix_super,memory_region(REGION_USER1),memory_region(REGION_USER2));
 xavix_tv_register_state();
 state_save_register_double("xavix",0,"timer remaining",xavix_remaining,6);
 state_save_register_double("xavix",0,"timer period",xavix_period,6);
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
 for(y=cliprect->min_y;y<=cliprect->max_y;y++)
  for(x=cliprect->min_x;x<=cliprect->max_x;x++) {
   UINT32 c=xavix_pixels[y*512+x];
   if(bitmap->depth==32)((UINT32*)bitmap->line[y])[x]=c;
   else ((UINT16*)bitmap->line[y])[x]=((c>>9)&0x7c00)|((c>>6)&0x03e0)|((c>>3)&0x001f);
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
 PORT_BITX(0x10,IP_ACTIVE_HIGH,IPT_BUTTON1,"Left mouse",KEYCODE_ENTER,JOYCODE_MOUSE_1_BUTTON1)
 PORT_BIT(0xef,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 /* IO1 bit 3 is the right button; both inputs are active high. */
 PORT_BITX(0x08,IP_ACTIVE_HIGH,IPT_BUTTON2,"Right mouse",KEYCODE_BACKSPACE,JOYCODE_MOUSE_1_BUTTON2)
 PORT_BIT(0xf7,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_BIT(0xff,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_BIT(0xff,IP_ACTIVE_HIGH,IPT_UNUSED)
 PORT_START
 PORT_ANALOG(0xff,0,IPT_TRACKBALL_X|IPF_REVERSE,25,32,0,255)
 PORT_START
 PORT_ANALOG(0xff,0,IPT_TRACKBALL_Y,25,32,0,255)
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
