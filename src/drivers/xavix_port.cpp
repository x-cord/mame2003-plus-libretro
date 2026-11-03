// license:BSD-3-Clause
// copyright-holders:David Haywood, Olivier Galibert, ramacat
// Portable XaviX hardware adapter. The upstream algorithms and instruction
// definitions are kept in the adjacent generated includes.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <type_traits>
#include <stddef.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include "xavix_port.h"
#include "ekara_microphone.h"
static EkaraMicrophone ekara_microphone;

struct EkaraI2C {
 enum State { IDLE=0, DEVSEL=1, ADDR=2, DATAIN=3, READSELACK=4, DATAOUT=5 };
 uint8_t data[1024];
 uint8_t page[16];
 uint8_t scl=0;
 uint8_t sdaw=1;
 uint8_t sdar=1;
 uint8_t state=IDLE;
 int bits=0;
 uint8_t shift=0;
 uint8_t devsel=0;
 int byteaddr=0;
 int page_offset=0;
 int page_written=0;

 EkaraI2C() { memset(data,0xff,sizeof(data)); }

 // Reset the serial bus without erasing the cartridge EEPROM.
 void reset() {
  memset(page,0xff,sizeof(page));
  scl=0; sdaw=1; sdar=1;
  state=IDLE; bits=0; shift=0; devsel=0;
  byteaddr=0; page_offset=0; page_written=0;
 }

 void write_sda(int val) {
  val&=1;
  if(sdaw!=val) {
   sdaw=val;
   if(scl && sdar) {
    if(sdaw) {
     if(page_written>0) {
      int base=byteaddr&0x3ff;
      int root=base&~15;
      for(int i=0;i<page_written;i++) data[root|((base+i)&15)]=page[i];
      page_written=0;
     }
     state=IDLE;
     bits=0;
    } else {
     state=DEVSEL;
     bits=0;
     shift=0;
     page_written=0;
    }
   }
  }
 }

 void write_scl(int val) {
  val&=1;
  if(scl!=val) {
   scl=val;
   switch(state) {
   case DEVSEL:
   case ADDR:
   case DATAIN:
    if(bits<8) {
     if(scl) {
      shift=((shift<<1)|sdaw)&0xff;
      bits++;
     }
    } else {
     if(scl) {
      bits++;
     } else {
      if(bits==8) {
       switch(state) {
       case DEVSEL:
        devsel=shift;
        if((devsel&0xf0)!=0xa0) {
         state=IDLE;
        } else if((devsel&1)==0) {
         state=ADDR;
        } else {
         byteaddr=(byteaddr&0xff)|(((devsel&0x06)<<7)&0x3ff);
         state=READSELACK;
        }
        break;
       case ADDR:
        byteaddr=shift|(((devsel&0x06)<<7)&0x3ff);
        page_offset=0;
        page_written=0;
        state=DATAIN;
        break;
       case DATAIN:
        page[page_offset]=shift;
        page_offset=(page_offset+1)&15;
        if(page_written<16) page_written++;
        break;
       default: break;
       }
       if(state!=IDLE) sdar=0;
      } else {
       bits=0;
       sdar=1;
      }
     }
    }
    break;

   case READSELACK:
    bits=0;
    state=DATAOUT;
    break;

   case DATAOUT:
    if(bits<8) {
     if(scl) {
      bits++;
     } else {
      if(bits==0) {
       shift=data[byteaddr&0x3ff];
       byteaddr=(byteaddr+1)&0x3ff;
      }
      sdar=(shift>>7)&1;
      shift=(shift<<1)&0xff;
     }
    } else {
     if(scl) {
      if(sdaw) state=IDLE;
      bits=0;
     } else {
      sdar=1;
     }
    }
    break;

   default: break;
   }
  }
 }

 int read_sda() const {
  return (state==IDLE) ? (sdaw&1) : (sdar&1);
 }
};
static EkaraI2C ekara_i2c;
static bool xavix_cart_has_eeprom = false;
extern "C" void xavix_tv_set_cart_eeprom(int enable) { xavix_cart_has_eeprom = (enable != 0); }
extern "C" unsigned char *ekara_eeprom_data(void) { return ekara_i2c.data; }
static EkaraI2C stvpc_i2c;
static bool xavix_save_super = false;
extern "C" unsigned char *xavix_save_eeprom_data(void) {
 return xavix_cart_has_eeprom ? (xavix_save_super ? stvpc_i2c.data : ekara_i2c.data) : nullptr;
}
extern "C" unsigned char *stvpc_eeprom_data(void) { return stvpc_i2c.data; }
static double (*xavix_emulated_clock)(void)=nullptr;
extern "C" void xavix_tv_set_clock(double (*clock)(void)) { xavix_emulated_clock=clock; }
static uint8_t *xavix_ram_ptr = nullptr;
#include "../cpu/xavix_engine.inc"
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using s8=int8_t; using s16=int16_t; using s32=int32_t;
using offs_t=uint32_t; using pen_t=uint32_t;
static uint32_t BIT(uint32_t v,int b,int n=1) {return (v>>b)&((1u<<n)-1);}
#define popmessage(...) ((void)0)
struct attotime {
 double seconds;
 static attotime from_hz(double hz) { return {1.0/hz}; }
 static attotime from_ticks(uint64_t n,uint32_t hz) { return {double(n)/hz}; }
 static const attotime never;
 double as_double() const { return seconds; }
};
const attotime attotime::never={1e30};
struct SyncStream { void update() { xavix_host_audio_sync(); } };
static SyncStream sync_stream;
struct sound_stream {
 int count; int16_t **out;
 int samples() const { return count; }
 void add_int(int ch,int i,int v,int scale) { out[ch][i]=std::max(-32768,std::min(32767,v)); }
};
static uint8_t xavix_sound_reg_read(unsigned);
static void xavix_sound_reg_write(unsigned,uint8_t);
static uint8_t xavix_sound_sample_read(unsigned);
#include "../sound/xavix_port.h"
#include "../sound/xavix_port.inc"
static xavix_sound_device sound;
struct rectangle {
 int min_x,max_x,min_y,max_y;
 rectangle(int x=0,int X=511,int y=0,int Y=255):min_x(x),max_x(X),min_y(y),max_y(Y){}
 bool contains(int x,int y) const {return containsx(x)&&containsy(y);}
 bool containsx(int x) const {return x>=min_x&&x<=max_x;}
 bool containsy(int y) const {return y>=min_y&&y<=max_y;}
 bool overlapsx(int x,int X) const {return x<=max_x&&X>=min_x;}
 int top() const {return min_y;} int bottom() const {return max_y;}
 int left() const {return min_x;} int right() const {return max_x;}
 void setx(int x,int X) {min_x=x;max_x=X;}
 rectangle& operator&=(const rectangle& r) {min_x=std::max(min_x,r.min_x);max_x=std::min(max_x,r.max_x);min_y=std::max(min_y,r.min_y);max_y=std::min(max_y,r.max_y);return *this;}
};
template<typename T> struct Bitmap {
 T storage[512*256]{}; T *data=storage;
 T& pix(int y,int x=0) { return data[y*512+x]; }
 void fill(T c,const rectangle& rect) { rectangle r=rect;r&=rectangle();for(int y=r.min_y;y<=r.max_y;y++)for(int x=r.min_x;x<=r.max_x;x++)pix(y,x)=c; }
};
using bitmap_rgb32=Bitmap<uint32_t>;using bitmap_ind16=Bitmap<uint16_t>;
struct screen_device {
 int vpos() const {return 0;}
 void update_partial(int) {xavix_host_video_sync();}
 attotime time_until_pos(int y,int x) { extern double xavix_raster_delay(int,int);return {xavix_raster_delay(y,x)}; }
};
struct Timer {int id;void adjust(attotime delay,int=0) {xavix_host_timer(id,delay.seconds,0);}};
struct Palette {
 uint32_t colors[512]{};
 const pen_t* pens() const {return colors;}
 unsigned black_pen() const {return 0;}
 void set_pen_color(int i,int r,int g,int b) {colors[i]=(r<<16)|(g<<8)|b;}
};
#include "stvpc_printer.h"
#include "stvpc_keyboard.h"
static StvpcKeyboard stvpc_keyboard;
static StvpcPrinter stvpc_printer;
static int xavix_output_row_phase = 0;
static struct CursorInput {
 bool keyboard=false, seen=false;
 int host[2]{};
} cursor_input;
static int ekara_effect_profile = 0;
static Palette ekara_effect_palette;
struct EkaraEffectState { uint8_t echo=0, effect=0, active=0, previous=0; };
static uint8_t ekara_effect_report[3]{};
static EkaraEffectState ekara_effect_hardware;
struct XavixTV {
 #include "xavix_members.inc"
 #include "xavix_decls.inc"
 uint8_t *regular_coverage=nullptr;
 const uint8_t *bitmap_coverage=nullptr;
 int logo_sprite_y_adjust=0;
 bool logo_second_phase() const {
  if(!super || (m_spritereg&0x7f)!=4)return false;
  for(int i=0;i<16;i++) {
   const unsigned tile=m_fragment_sprite[0x500+i]|(m_fragment_sprite[0x600+i]<<8)|(m_fragment_sprite[0x700+i]<<16);
   if((m_fragment_sprite[i]&0xfe)!=6 || m_fragment_sprite[0x100+i]!=0x2c ||
      tile<0x3dd500 || tile>0x3dff80 || (tile&0x7f))return false;
  }
  return true;
 }
 void bitmap_row(bitmap_rgb32 &bitmap,const rectangle &clip,int y,int left,int right,int bpp,int z,int multiplier) {
  auto pixels=&bitmap.pix(y);auto priorities=&m_zbuffer.pix(y);
  const auto covered=bitmap_coverage?bitmap_coverage+y*512:nullptr;
  const unsigned mask=(1u<<bpp)-1;
  const int per_byte=8/bpp;
  const bool sx=m_extra!=nullptr;
  const uint8_t *const palram=m_bmp_palram_sh;
  const uint32_t *const colors=palette.colors+256;
  const int clip_min=clip.min_x,clip_max=clip.max_x;
  const bool must_clip=!(left*multiplier>=clip_min&&(right-1)*multiplier+multiplier-1<=clip_max);
  auto put=[&](int col,unsigned pen) {
   if((palram[pen]&31)>=24)return;
   if(multiplier==1) {
    if(must_clip&&(col<clip_min||col>clip_max))return;
    if(z>priorities[col]||(z==priorities[col]&&(!covered||!covered[col]))) {
     pixels[col]=colors[pen];priorities[col]=z;
    }
    return;
   }
   int x=col*multiplier;
   int end=x+multiplier-1;
   if(end<clip_min||x>clip_max)return;
   if(x<clip_min)x=clip_min;
   if(end>clip_max)end=clip_max;
   const uint32_t color=colors[pen];
   for(int pixel=x;pixel<=end;pixel++) {
    if(z>priorities[pixel]||(z==priorities[pixel]&&(!covered||!covered[pixel]))) {
     pixels[pixel]=color;priorities[pixel]=z;
    }
   }
  };
  const bool byte_aligned=(bpp==1||bpp==2||bpp==4||bpp==8);
  if(byte_aligned&&!m_tmp_databit) {
   if(per_byte==2) {
    while(right-left>=2) {
     uint8_t dat=read_graphics_byte(sx);
     put(left,dat&mask);++left;
     put(left,(dat>>bpp)&mask);++left;
    }
   } else if(per_byte==4) {
    while(right-left>=4) {
     uint8_t dat=read_graphics_byte(sx);
     put(left,dat&mask);++left;
     put(left,(dat>>bpp)&mask);++left;
     put(left,(dat>>(bpp*2))&mask);++left;
     put(left,(dat>>(bpp*3))&mask);++left;
    }
   } else if(per_byte==8) {
    while(right-left>=8) {
     uint8_t dat=read_graphics_byte(sx);
     put(left,dat&mask);++left;
     put(left,(dat>>bpp)&mask);++left;
     put(left,(dat>>(bpp*2))&mask);++left;
     put(left,(dat>>(bpp*3))&mask);++left;
     put(left,(dat>>(bpp*4))&mask);++left;
     put(left,(dat>>(bpp*5))&mask);++left;
     put(left,(dat>>(bpp*6))&mask);++left;
     put(left,(dat>>(bpp*7))&mask);++left;
    }
   } else {
    while(left<right) {
     uint8_t dat=read_graphics_byte(sx);
     put(left,dat&mask);++left;
    }
   }
  }
  while(left<right) {
   int count=right-left<per_byte?right-left:per_byte;
   unsigned packed=graphics_bits(count*bpp,sx);
   for(int i=0;i<count;i++) {
    put(left,packed&mask);packed>>=bpp;++left;
   }
  }
 }
 uint8_t graphics_bits(int count,bool sx) {
  if(count<=8) {
   if(!m_tmp_databit) m_bit=peek_graphics_byte(sx);
   const int avail=8-m_tmp_databit;
   if(count<=avail) {
    const unsigned value=(m_bit>>m_tmp_databit)&((1u<<count)-1);
    m_tmp_databit+=count;
    if(m_tmp_databit==8){m_tmp_databit=0;m_tmp_dataaddress++;}
    return uint8_t(value);
   }
  }
  unsigned value=0,shift=0;
  while(count>0) {
   if(!m_tmp_databit) m_bit=peek_graphics_byte(sx);
   int take=std::min(count,8-m_tmp_databit);
   value|=((m_bit>>m_tmp_databit)&((1u<<take)-1))<<shift;
   m_tmp_databit+=take;count-=take;shift+=take;
   if(m_tmp_databit==8){m_tmp_databit=0;m_tmp_dataaddress++;}
  }
  return value;
 }
 __attribute__((always_inline)) uint8_t peek_graphics_byte(bool sx) {
  int address=m_tmp_dataaddress;
  if(sx) {
   if(address&0x2000000){address&=0x1ffffff;address^=0x600000;}
   if((address&0x1000000)&&m_extra) m_bit=m_extra[address&0x7fffff];
   else if(address&0x800000) {
    const unsigned e=address&0x7fffff;
    if(super) {
     if(e>=0x600000&&e<=0x67ffff) m_bit=bitmap_buffer[e-0x600000];
     else if(e<0x400000) m_bit=cart[e];
     else m_bit=ext_read(e);
    } else m_bit=ext_read(e);
   } else m_bit=read_full_data_sp_bypass(address);
  } else m_bit=m_disable_memory_bypass?opcode_read(address):read_full_data_sp_bypass(address);
  return m_bit;
 }
 __attribute__((always_inline)) uint8_t read_graphics_byte(bool sx) {
  const uint8_t dat=peek_graphics_byte(sx);
  m_tmp_dataaddress++;
  return dat;
 }
 void read_tile_pixels(uint8_t *out,int count,int bpp,bool sx) {
  const unsigned mask=(1u<<bpp)-1;
  const int per_byte=8/bpp;
  int left=0;
  if((bpp==1||bpp==2||bpp==4||bpp==8)&&!m_tmp_databit) {
   if(per_byte==2) {
    while(count>=2) {
     uint8_t dat=read_graphics_byte(sx);
     out[left++]=dat&mask;
     out[left++]=(dat>>bpp)&mask;
     count-=2;
    }
   } else if(per_byte==4) {
    while(count>=4) {
     uint8_t dat=read_graphics_byte(sx);
     out[left++]=dat&mask;
     out[left++]=(dat>>bpp)&mask;
     out[left++]=(dat>>(bpp*2))&mask;
     out[left++]=(dat>>(bpp*3))&mask;
     count-=4;
    }
   } else if(per_byte==8) {
    while(count>=8) {
     uint8_t dat=read_graphics_byte(sx);
     out[left++]=dat&mask;
     out[left++]=(dat>>bpp)&mask;
     out[left++]=(dat>>(bpp*2))&mask;
     out[left++]=(dat>>(bpp*3))&mask;
     out[left++]=(dat>>(bpp*4))&mask;
     out[left++]=(dat>>(bpp*5))&mask;
     out[left++]=(dat>>(bpp*6))&mask;
     out[left++]=(dat>>(bpp*7))&mask;
     count-=8;
    }
   } else {
    while(count>0) {
     out[left++]=read_graphics_byte(sx)&mask;
     count--;
    }
   }
  }
  while(count>0) {
   out[left++]=uint8_t(graphics_bits(bpp,sx));
   count--;
  }
 }
 uint8_t read_full_data_sp_bypass(uint32_t a) {
  if((a&0x800000)||(a&0x8000))return ext_read(a&0x7fffff);
  a&=0x7fff;
  if(a<0x4000)return ram[a];
  if(a<0x5000)return txarray_r(a&0xfff);
  if(a>=0x6000&&a<0x6a20)return ram[a];
  return 0;
 }
 uint8_t txarray_r(uint16_t a) {
  if(a<0x400) {uint8_t v=a;return ((a&0x200)?((~v>>4)&15):(v>>4))|((a&0x100)?(uint8_t(~v)<<4):(v<<4));}
  return m_txarray[(a-0x400)/0x400];
 }
 bool mouse_position(int &x,int &y) {
  bool valid=xavix_host_pointer(&x,&y)!=0;
  if(valid) {
   if((!cursor_input.seen || x!=cursor_input.host[0] || y!=cursor_input.host[1]) && !xavix_host_input(2))
    cursor_input.keyboard=false;
   cursor_input.host[0]=x;cursor_input.host[1]=y;cursor_input.seen=true;
  }
  return valid;
 }
 int mouse_current(int axis) {
  unsigned address=0x2bf7+axis*2;
  return ram[address]|(ram[address+1]<<8);
 }
 void mouse_keys() {
  if(!super)return;
  int x,y;mouse_position(x,y);
  if(xavix_host_input(2))cursor_input.keyboard=true;
 }
 uint8_t mouse_read(int axis) {
  if(!mouse_latched[axis]) {
   unsigned arrows=xavix_host_input(2);
   if(arrows)cursor_input.keyboard=true;
   int x=0,y=0;
   bool valid=mouse_position(x,y);
   mouse_delta[axis]=0;
   int current=mouse_current(axis);
   if(current<=255 && (valid || cursor_input.keyboard)) {
    if(cursor_input.keyboard) {
     // Read the live direction for this axis, never a target from an older
     // vblank. An inactive axis must remain still across direction changes.
     int step=((arrows&(axis?4:2))?5:0)-((arrows&(axis?8:1))?5:0);
     mouse_delta[axis]=step?std::clamp(current+step,axis?24:8,axis?232:248)-current:0;
    } else {
     int target=axis?std::clamp(y,24,232):std::clamp(x,8,248);
     mouse_delta[axis]=std::clamp(target-current,-127,127);
    }
   }
   mouse_latched[axis]=true;
  }
  return uint8_t(mouse_delta[axis]);
 }
 void mouse_clear(int axis) {
  mouse_delta[axis]=0;mouse_latched[axis]=false;
 }
 uint8_t read_io0(uint8_t) {return xavix_host_input(0);}
 uint8_t read_io1(uint8_t) {
  if(super)return xavix_host_input(1);
  int index=0;uint8_t sel=extraioselect&0x7f;
  if(!sel||(sel&(sel-1)))return 0;
  while(sel>1){sel>>=1;index++;}
  uint8_t value=(ekara_effect_profile==3 && index>=4)?0:xavix_host_input(index<4?2:3);index=index<4?index:index-4;
  return (value>>(2*index))&3;
 }
 void write_io0(uint8_t d,uint8_t dir) {extraioselect=d&dir;}
 void write_io1(uint8_t,uint8_t) {}
 void full_write(uint32_t a,uint8_t d) {if(!(a&0x800000)&&!(a&0x8000))low_write(a&0x7fff,d);else ext_write(a&0x7fffff,d);}
 uint8_t sound_volume_r(){return sound.sound_volume_r();}
 void sound_volume_w(uint8_t d){sound.sound_volume_w(d);}
 uint8_t sound_mixer_r(){return sound.sound_mixer_r();}
 void sound_mixer_w(uint8_t d){sound.sound_mixer_w(d);}
 uint8_t sound_dac_control_r(){return sound.dac_control_r();}
 void sound_dac_control_w(uint8_t d){sound.dac_control_w(d);}
 uint8_t sound_tp0_r(){return m_tp[0];}
 uint8_t sound_tp1_r(){return m_tp[1];}
 uint8_t sound_tp2_r(){return m_tp[2];}
 uint8_t sound_tp3_r(){return m_tp[3];}
 void raise_nmi() {cpu.m_nmi_pending=true;cpu.pending_irq=true;}
 uint8_t random_byte() {random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
 // A reload cycle takes one prescaler tick in addition to the countdown.
 void timer_start(unsigned clocks=1) {
  xavix_host_timer(1,clocks*double(1u<<((m_timer_freq&15)+1))/(super?42954544:21477272),0);
 }
 void timer_control_w(uint8_t data) {
  const uint8_t previous=m_timer_control;
  m_timer_control=data;
  if(data&0x80) {m_irqsource&=~0x10;update_irqs();}
  if(!(data&1)) {xavix_host_timer(1,1e30,0);return;}
  // Bit 1 keeps the counter running through IRQ acknowledgement. Restarting
  // here adds the firmware's interrupt latency to every sampling period.
  if((previous&3)!=3 || !(data&2)) {
   m_timer_currentval=m_timer_baseval;
   timer_start();
  }
 }
 void refresh_sound_irq_state() {if((m_sound_irqstatus&15)&(m_sound_irqstatus>>4))m_irqsource|=128;else m_irqsource&=~128;}
 void reprogram_sound_timer(int i) {double hz=sound.tempo_tick_hz(m_tp[i]);if(!(m_sound_irqstatus&(1<<i))||!m_tp[i]||hz<=0)xavix_host_timer(2+i,1e30,0);else xavix_host_timer(2+i,1/hz,1/hz);}
};
static XavixTV tv;
extern "C" unsigned char *xavix_tv_ram(void) { return tv.ram; }
// The scoring cartridges retain the ordinary e-kara effect UI but do not
// install it during karaoke. Its preset scratch byte overlaps the scoring
// microphone buffer size; leave that byte alone while karaoke is active.
static bool ekara_effect_song() {
 if(ekara_effect_profile==1) return tv.ram[0x16b2]==4 && tv.ram[0x1658]==6 && tv.ram[0x165b]==2;
 if(ekara_effect_profile==2) return tv.ram[0x172a]==4 && tv.ram[0x1678]==6 && tv.ram[0x167b]==2;
 return false;
}
static bool ekara_effect_icon(int tile,int bpp,int pal) {
 unsigned address=tile&0x7fffff;
 return pal==15 && bpp==4 && address>=0x694a00 && address<0x695800 && ekara_effect_song();
}
#include "xavix_hw.inc"

static void stvpc_update_i2c() {
 int sda = 1;
 int scl = 0;
 if(tv.pio_dir[2] & 0x40) {
  sda = (tv.pio_out[2] >> 6) & 1;
 } else if(tv.pio_dir[2] & 0x20) {
  sda = (tv.pio_out[2] >> 5) & 1;
 }
 if(tv.pio_dir[2] & 0x80) {
  scl = (tv.pio_out[2] >> 7) & 1;
 } else if(tv.pio_dir[2] & 0x10) {
  scl = (tv.pio_out[2] >> 4) & 1;
 }
 if(stvpc_i2c.scl && !scl) {
  stvpc_i2c.write_scl(scl);
  stvpc_i2c.write_sda(sda);
 } else {
  stvpc_i2c.write_sda(sda);
  stvpc_i2c.write_scl(scl);
 }
}

static uint8_t low_read(uint16_t a) {
 if(a<0x4000)return tv.ram[a];
 xavix_polling_safe=false;
 #include "xavix_read.inc"
 if(a==0x7b00||a==0x7b01)return tv.super?0:255;
 if(a==0x7b10||a==0x7b11)return tv.super?tv.mouse_read(a&1):255;
 if(a==0x7b80)return tv.super?255:tv.ram[0x7b80];
 if(a==0x7b81)return tv.random_byte();
 if(tv.super && a>=0x7a10&&a<=0x7a12)return tv.pio_dir[a-0x7a10];
 if(tv.super && a>=0x7a20&&a<=0x7a22)return tv.pio_out[a-0x7a20];
 if(tv.super && a==0x7a32) {
  uint8_t in = stvpc_printer.status();
  if(xavix_cart_has_eeprom && stvpc_i2c.read_sda()) in |= 0x40 | 0x20;
  return (tv.pio_out[2] & tv.pio_dir[2]) | (in & ~tv.pio_dir[2]);
 }
 if(tv.super && a==0x7a30)return stvpc_keyboard.read();
 if(tv.super && a==0x7a31)return 0;
 return tv.ram[a&0x7fff];
}
static void low_write(uint16_t a,uint8_t d) {
 xavix_polling_safe=false;
 if(a==0xfe){cpu.bank=d;return;}
 if(a==0xff){cpu.databank=d;return;}
 if(a<0x4000){tv.ram[a]=d;return;}
 if(tv.super && (a==0x7b10||a==0x7b11)){tv.mouse_clear(a&1);return;}
 if(!tv.super && a==0x7b81) {
  tv.ram[a]=d;
  uint64_t time=xavix_emulated_clock?uint64_t(xavix_emulated_clock()*21477272.0):ekara_microphone.cycles-xavix_ICount;
  tv.ram[0x7b80]=(d&0x13)==0?ekara_microphone.convert(time):255;
  return;
 }
 if(a>=0x75f0 && a<=0x75ff)xavix_host_audio_sync();
 #include "xavix_write.inc"
 if(tv.super && a>=0x7a10&&a<=0x7a12){
  if(a==0x7a11)stvpc_printer.reset();
  tv.pio_dir[a-0x7a10]=d;
  if(xavix_cart_has_eeprom && a==0x7a12)stvpc_update_i2c();
  return;
 }
 if(tv.super && a>=0x7a20&&a<=0x7a22){
  if(a==0x7a21&&(tv.pio_dir[0]&0x20)&&(tv.pio_dir[1]&2))stvpc_printer.clock(tv.pio_out[1],d,tv.pio_out[0]);
  bool clock=a==0x7a20&&((tv.pio_out[0]^d)&0x40);
  tv.pio_out[a-0x7a20]=d;
  if(xavix_cart_has_eeprom && a==0x7a22)stvpc_update_i2c();
  // Data-ready must follow the acknowledgement after the ISR has unwound.
  // Immediate IRQs recursively nest the serial handler and overwrite the stack.
  if(clock&&stvpc_keyboard.clock())xavix_host_timer(6,0.0001,0);
  return;
 }
 tv.ram[a&0x7fff]=d;
}
static uint8_t ext_read(uint32_t a) {
 a&=0x7fffff;
 if(tv.super) {
  if(a>=0x600000&&a<=0x67ffff)return tv.bitmap_buffer[a-0x600000];
  if(a<0x400000)return tv.cart[a];
  if(a>=0x500000&&a<=0x5fffff && (tv.pio_dir[2]&tv.pio_out[2]&4))a+=0x200000;
  return tv.bios[a];
 }
 if(xavix_cart_has_eeprom && a==0x5fffff && (tv.m_extbusctrl[1]&8))return ekara_i2c.read_sda() ? 255 : 0;
 if(a<0x600000)return tv.cart[a&0x1fffff];
 return tv.bios[(a-0x600000)&0xfffff];
}
static void ext_write(uint32_t a,uint8_t d) {
 xavix_polling_safe=false;
 a&=0x7fffff;
 if(tv.super && a>=0x600000&&a<=0x67ffff)tv.bitmap_buffer[a-0x600000]=d;
 if(!tv.super && xavix_cart_has_eeprom && a==0x3fffff && (tv.m_extbusctrl[1]&0x04)) {
  ekara_i2c.write_sda(d&1);
  ekara_i2c.write_scl((d>>1)&1);
 }
}
static uint8_t opcode_read(uint32_t a) {
 unsigned patch=ekara_effect_profile==1?0xd94b:0xc6cf;
 if((a==patch || a==patch+1) && ekara_effect_song())return 0xea;
 if(!(a&0x800000)&&!(a&0x8000)) {
  if(a<0x4000)return tv.ram[a];
  return low_read(a&0x7fff);
 }
 return ext_read(a&0x7fffff);
}
static int vector_read(int which,int half) {return tv.get_vectors(which,half);}
static uint8_t xavix_sound_reg_read(unsigned a) {return tv.sound_regram_read_cb(a);}
static void xavix_sound_reg_write(unsigned a,uint8_t d) {tv.sound_regram_write_cb(a,d);}
static uint8_t xavix_sound_sample_read(unsigned a) {return tv.read_full_data_sp_bypass(a);}

#include <new>
extern "C" void xavix_tv_reset(int super,unsigned char *bios,unsigned char *cart) {
 xavix_save_super = super != 0;
 stvpc_printer.reset();
 stvpc_keyboard.reset();
 cursor_input=CursorInput{};
 ekara_microphone.reset();
 ekara_i2c.reset();
 stvpc_i2c.reset();
 new (&tv) XavixTV{};
 xavix_ram_ptr=tv.ram;
 tv.super=super;tv.bios=bios;tv.cart=cart;
 ekara_effect_profile=0;ekara_effect_hardware=EkaraEffectState{};
 if(!super && cart[0xd94b]==0x85 && cart[0xd94c]==0x76 && cart[0xd974]==0xa5 && cart[0xd975]==0x46) ekara_effect_profile=1;
 if(!super && cart[0xc6cf]==0x85 && cart[0xc6d0]==0x76 && cart[0xc6f8]==0xa5 && cart[0xc6f9]==0x46) ekara_effect_profile=2;
 static const uint8_t gk1_signature[]={0x1a,0xad,0xbc,0x1a,0x8d,0xb9,0x1a,0xad,0xbd,0x1a,0x8d,0xba,0x1a,0x4c,0x4a,0x80};
 if(!super && !memcmp(cart+0x8000,gk1_signature,sizeof(gk1_signature))) ekara_effect_profile=3;
 // Saiten lyrics occupy palette F; status icons retain the BIOS UI colors.
 if(ekara_effect_profile==1 || ekara_effect_profile==2) {
  tv.m_palette=&ekara_effect_palette;
  for(int i=0;i<16;i++)tv.update_pen(0xf0+i,bios[0x8f10+i],bios[0x9010+i]);
  tv.m_palette=&tv.palette;
 }
 tv.m_video_hres_multiplier=super?2:1;
 tv.m_disable_memory_bypass=super;
 tv.m_ext_segment_regs=super?tv.ram+0x6a40:nullptr;
 tv.m_sound_regbase=2;
 memset(tv.ram,255,0x4000);
 tv.ram[0x7b80]=128;
 sound=xavix_sound_device{};
 sound.m_sequencer_rate_hz=21477272/128;
 sound.set_default_tempo(super?0x80:0x40);
 sound.device_reset();
 for(int i=0;i<7;i++)xavix_host_timer(i,1e30,0);
}
extern "C" unsigned char xavix_tv_read(unsigned a) {return opcode_read(a);}
extern "C" void xavix_tv_write(unsigned a,unsigned char d) {tv.full_write(a,d);}
extern "C" void xavix_tv_input_sync() {
 stvpc_printer.reset();
 if(ekara_effect_profile==3) {
  ekara_effect_hardware.previous=xavix_host_input(3)&3;
  ekara_effect_hardware.active=(xavix_host_input(2)&1)!=0;
 }
 stvpc_keyboard.sent_pads[0]=stvpc_keyboard.sent_pads[1]=0xff;
 ekara_microphone.reset();
}
extern "C" void xavix_tv_vblank() {
 if(ekara_effect_profile==3) {
  uint8_t held=xavix_host_input(3)&3, pressed=held&~ekara_effect_hardware.previous;
  if(pressed&1) ekara_effect_hardware.echo=(ekara_effect_hardware.echo+1)%4;
  if(pressed&2) ekara_effect_hardware.effect=(ekara_effect_hardware.effect+1)%3;
  ekara_effect_hardware.previous=held;
  ekara_effect_hardware.active=(xavix_host_input(2)&1)!=0;
 }
 if(ekara_effect_song()) {
  unsigned slot=ekara_effect_profile==1?0x1659:0x1679;
  if(!tv.ram[slot] && !tv.ram[slot+1]) tv.ram[slot]=1;
 }
 if(tv.super) {
  uint64_t keys=0;
  for(int i=0;i<8;i++)keys|=uint64_t(xavix_host_input(3+i))<<(i*8);
  if(xavix_host_input(11)&1)keys|=uint64_t(1)<<2;
  // The arrow keys also retain their absolute mouse cursor shortcuts.
  unsigned arrows=xavix_host_input(2);
  if(arrows&1)keys|=uint64_t(1)<<12;
  if(arrows&2)keys|=uint64_t(1)<<13;
  if(arrows&4)keys|=uint64_t(1)<<14;
  if(arrows&8)keys|=uint64_t(1)<<15;
  stvpc_keyboard.poll(keys);
  for(int i=0;i<2;i++)stvpc_keyboard.pads[i]=xavix_host_input(12+i)&0x1f;
  if((tv.m_ioevent_enable&4)&&!(tv.pio_dir[0]&0x80)&&stvpc_keyboard.start())xavix_host_timer(6,0.0001,0);
 }
 tv.mouse_keys();if(tv.m_video_ctrl&0x20){tv.raise_nmi();tv.m_video_ctrl|=0x80;}}
extern "C" void xavix_tv_timer(int id) {
 if(id==6) {
  // MAME's completed one-shot retains its old expiry. Disarm it explicitly
  // so save/load cannot re-arm a stale data-ready edge ahead of a new packet.
  xavix_host_timer(6,1e30,0);
  if(tv.super && stvpc_keyboard.active)tv.process_ioevent(4);
 } else if(id==0) {
  if(tv.m_video_ctrl&16){tv.m_video_ctrl|=64;tv.m_irqsource|=64;tv.update_irqs();xavix_host_video_sync();}
  tv.m_interrupt_timer->adjust(tv.m_screen->time_until_pos(tv.m_posirq_y[0],tv.m_posirq_x[0]));
 } else if(id==1) {
  tv.m_timer_currentval--;
  if(tv.m_timer_currentval==255) {
   if(tv.m_timer_control&0x40) {
    tv.m_irqsource|=0x10;tv.m_timer_control|=0x80;tv.update_irqs();
   }
   if(tv.m_timer_control&2) {
    tv.m_timer_currentval=tv.m_timer_baseval;
    tv.timer_start(2);
   }
  } else tv.timer_start();
 } else {
  if(tv.m_sound_irqstatus&(1<<(id-2))){tv.m_sound_irqstatus|=16<<(id-2);tv.refresh_sound_irq_state();tv.update_irqs();}
 }
}
static bitmap_rgb32 framebuffer;
extern "C" void xavix_tv_render(unsigned int *pixels,int first,int last) {
 xavix_output_row_phase=0;
 framebuffer.data=pixels;
 tv.screen_update(tv.screen,framebuffer,rectangle(0,tv.super?511:255,first,last));
}
extern "C" void xavix_tv_render_progressive(unsigned int *pixels,int first,int last) {
 static bitmap_rgb32 field,regular;
 static uint16_t depth[512*256];
 static uint8_t coverage[512*256];
 const bool offset_logo=tv.logo_second_phase();
 const int first_row=first/2,last_row=last/2;
 rectangle clip;
 int saved_address=0,saved_bit=0,saved_cache=0;
 for(int parity=0;parity<2;parity++) {
  const int top=first/2+((first&1)>parity),bottom=last/2-((last&1)<parity);
  // Moving odd output rows up one logical row shifts the sprite by one output pixel.
  tv.logo_sprite_y_adjust=offset_logo&&parity?-1:0;
  if(!parity||offset_logo) {
   clip=tv.do_arena(tv.screen,regular,rectangle(0,511,first_row,last_row));
   memset(coverage+first_row*512,0,(last_row-first_row+1)*512);
   tv.regular_coverage=coverage;
   tv.draw_regular_layers(tv.screen,regular,clip);
   tv.regular_coverage=nullptr;
   for(int y=first_row;y<=last_row;y++)memcpy(depth+y*512,&tv.m_zbuffer.pix(y),512*sizeof(*depth));
  }
  if(!parity){saved_address=tv.m_tmp_dataaddress;saved_bit=tv.m_tmp_databit;saved_cache=tv.m_bit;}
  if(top>bottom)continue;
  xavix_output_row_phase=parity;
  for(int y=top;y<=bottom;y++) {
   memcpy(&field.pix(y),&regular.pix(y),512*sizeof(*pixels));
   memcpy(&tv.m_zbuffer.pix(y),depth+y*512,512*sizeof(*depth));
  }
  tv.bitmap_coverage=coverage;
  rectangle bitmap_clip=clip;bitmap_clip.min_y=top;bitmap_clip.max_y=bottom;
  tv.draw_bitmap_layer(tv.screen,field,bitmap_clip);
  for(int y=top;y<=bottom;y++) {
   int out=y*2+parity;
   memcpy(pixels+out*512,&field.pix(y),512*sizeof(*pixels));
  }
 }
 tv.bitmap_coverage=nullptr;
 tv.logo_sprite_y_adjust=0;
 tv.m_tmp_dataaddress=saved_address;tv.m_tmp_databit=saved_bit;tv.m_bit=saved_cache;
 xavix_output_row_phase=0;
}
extern "C" void xavix_tv_audio(short **outputs,int samples) {sound_stream stream{samples,outputs};sound.sound_stream_update(stream);}
extern "C" void xavix_init(void) {xavix_host_save("cpu",&cpu,sizeof(cpu));}
extern "C" void xavix_reset(void *) {
 memset(&cpu,0,sizeof(cpu));cpu.super=tv.super;cpu.m_SP=0x1fd;cpu.m_P=0x34;cpu.specialpos=255;
 cpu.m_PC=opcode_read(0xfffc)|(opcode_read(0xfffd)<<8);
}
extern "C" void xavix_exit(void) {}
extern "C" int xavix_execute(int cycles) {
 struct {
  bool valid=false;uint16_t branch=0,target=0;uint8_t bank=0;
  int remaining=0;
  uint8_t registers[offsetof(XavixCPU,special)]{};
 } polling;
 xavix_polling_safe=false;
 ekara_microphone.cycles+=cycles;
 xavix_ICount=cycles;
 while(xavix_ICount>0) {
  if(cpu.stopped){xavix_ICount=0;break;}
  if(cpu.m_nmi_pending||cpu.pending_irq){polling.valid=false;cpu.pending_irq=false;cpu.m_irq_taken=true;cpu.brk_xav_imp();}
  else {
   cpu.m_PPC=cpu.m_PC;cpu.prevbank=cpu.bank;
   uint8_t op=opcode_read((cpu.bank<<16)|cpu.m_PC);cpu.m_PC++;cpu.dispatch(op);
   if(cpu.super&&cpu.bank==cpu.prevbank&&cpu.m_PC<=cpu.m_PPC&&!cpu.pending_irq&&!cpu.m_nmi_pending) {
    // Read-only polling may skip whole iterations with identical CPU state.
    // The host's execution budget still bounds the next interrupt or timer.
    if(polling.valid&&xavix_polling_safe&&polling.branch==cpu.m_PPC&&polling.target==cpu.m_PC&&polling.bank==cpu.bank&&
       !memcmp(polling.registers,&cpu,sizeof(polling.registers))) {
     int period=polling.remaining-xavix_ICount;
     if(period>0&&xavix_ICount>0)xavix_ICount%=period;
    }
    polling.valid=true;polling.branch=cpu.m_PPC;polling.target=cpu.m_PC;polling.bank=cpu.bank;
    polling.remaining=xavix_ICount;memcpy(polling.registers,&cpu,sizeof(polling.registers));
    xavix_polling_safe=true;
   }
  }
 }
 ekara_microphone.cycles-=xavix_ICount;
 return cycles-xavix_ICount;
}
extern "C" unsigned xavix_get_context(void *p) {if(p)memcpy(p,&cpu,sizeof(cpu));return sizeof(cpu);}
extern "C" void xavix_set_context(void *p) {if(p)memcpy(&cpu,p,sizeof(cpu));}
extern "C" unsigned xavix_get_pc(void) {return (cpu.bank<<16)|cpu.m_PC;}
extern "C" void xavix_set_pc(unsigned v) {cpu.bank=v>>16;cpu.m_PC=v;}
extern "C" unsigned xavix_get_sp(void) {return cpu.m_SP;}
extern "C" void xavix_set_sp(unsigned v) {cpu.m_SP=0x100|(v&255);}
extern "C" unsigned xavix_get_reg(int r) {switch(r){case -1:return (cpu.prevbank<<16)|cpu.m_PPC;case -2:case 1:return xavix_get_pc();case -3:case 2:return cpu.m_SP;case 3:return cpu.m_P;case 4:return cpu.m_A;case 5:return cpu.m_X;case 6:return cpu.m_Y;default:return 0;}}
extern "C" void xavix_set_reg(int r,unsigned v) {switch(r){case -2:case 1:xavix_set_pc(v);break;case -3:case 2:xavix_set_sp(v);break;case 3:cpu.m_P=v;break;case 4:cpu.m_A=v;break;case 5:cpu.m_X=v;break;case 6:cpu.m_Y=v;break;}}
extern "C" void xavix_set_irq_line(int line,int state) {if(line==127){if(state&&!cpu.nmi_line)cpu.m_nmi_pending=true;cpu.nmi_line=state;}else cpu.irq=state;}
extern "C" void xavix_set_irq_callback(int (*)(int)) {}
extern "C" unsigned xavix_dasm(char *s,unsigned pc) {sprintf(s,"$%02x",opcode_read(pc));return 1;}

extern "C" void xavix_tv_register_state(void) {
#define SAVE(v) xavix_host_save(#v,&(v),sizeof(v))
 if(ekara_effect_profile==3) SAVE(ekara_effect_hardware);
 if(!tv.super && xavix_cart_has_eeprom) {
  SAVE(ekara_i2c.data);
  SAVE(ekara_i2c.page);
  SAVE(ekara_i2c.scl);
  SAVE(ekara_i2c.sdaw);
  SAVE(ekara_i2c.sdar);
  SAVE(ekara_i2c.state);
  SAVE(ekara_i2c.bits);
  SAVE(ekara_i2c.shift);
  SAVE(ekara_i2c.devsel);
  SAVE(ekara_i2c.byteaddr);
  SAVE(ekara_i2c.page_offset);
  SAVE(ekara_i2c.page_written);
 }

 // The controller can be mid-packet at a frame boundary. Preserve the wire
 // and pending edges so restoring a state cannot manufacture or stick keys.
 if(tv.super) {
  if(xavix_cart_has_eeprom) {
   SAVE(stvpc_i2c.data);
   SAVE(stvpc_i2c.page);
   SAVE(stvpc_i2c.scl);
   SAVE(stvpc_i2c.sdaw);
   SAVE(stvpc_i2c.sdar);
   SAVE(stvpc_i2c.state);
   SAVE(stvpc_i2c.bits);
   SAVE(stvpc_i2c.shift);
   SAVE(stvpc_i2c.devsel);
   SAVE(stvpc_i2c.byteaddr);
   SAVE(stvpc_i2c.page_offset);
   SAVE(stvpc_i2c.page_written);
  }
  SAVE(stvpc_keyboard.held);SAVE(stvpc_keyboard.queue);
  SAVE(stvpc_keyboard.head);SAVE(stvpc_keyboard.tail);
  SAVE(stvpc_keyboard.packet);SAVE(stvpc_keyboard.remaining);
  SAVE(stvpc_keyboard.data);SAVE(stvpc_keyboard.sampled);SAVE(stvpc_keyboard.active);
  SAVE(stvpc_keyboard.rtc_wait);
 }
 SAVE(tv.m_ioevent_enable);
 SAVE(tv.m_ioevent_active);
 SAVE(tv.m_io0_data);
 SAVE(tv.m_io1_data);
 SAVE(tv.m_io0_direction);
 SAVE(tv.m_io1_direction);
 SAVE(tv.m_sound_irqstatus);
 SAVE(tv.m_soundreg16_0);
 SAVE(tv.m_soundreg16_1);
 SAVE(tv.m_sound_regbase);
 SAVE(tv.m_timer_control);
 SAVE(tv.m_timer_freq);
 SAVE(tv.m_timer_currentval);
 SAVE(tv.m_txarray);
 SAVE(tv.m_irqsource);
 SAVE(tv.m_vectorenable);
 SAVE(tv.m_nmi_vector_lo_data);
 SAVE(tv.m_nmi_vector_hi_data);
 SAVE(tv.m_irq_vector_lo_data);
 SAVE(tv.m_irq_vector_hi_data);
 SAVE(tv.m_spritefragment_dmaparam1);
 SAVE(tv.m_spritefragment_dmaparam2);
 SAVE(tv.m_tmap1_regs);
 SAVE(tv.m_tmap2_regs);
 SAVE(tv.m_arena_start);
 SAVE(tv.m_arena_end);
 SAVE(tv.m_arena_control);
 SAVE(tv.m_6ff0);
 SAVE(tv.m_video_ctrl);
 SAVE(tv.m_cyclerate);
 SAVE(tv.m_mixer);
 SAVE(tv.m_unk_snd75ff);
 SAVE(tv.m_tp);
 SAVE(tv.m_timer_baseval);
 SAVE(tv.m_spritereg);
 SAVE(tv.m_tmp_dataaddress);
 SAVE(tv.m_tmp_databit);
 SAVE(tv.m_bit);
 SAVE(tv.m_extbusctrl);
 SAVE(tv.m_disable_memory_bypass);
 SAVE(tv.m_disable_sprite_yflip);
 SAVE(tv.m_disable_sprite_xflip);
 SAVE(tv.m_disable_tile_regs_flip);
 SAVE(tv.m_video_hres_multiplier);
 SAVE(tv.m_default_audio_tempo_override);
 SAVE(tv.m_superxavix_pal_index);
 SAVE(tv.m_superxavix_bitmap_pal_index);
 SAVE(tv.m_sx_plt_address);
 SAVE(tv.m_sx_plt_mode);
 SAVE(tv.m_plotter_has_byte);
 SAVE(tv.m_plotter_current_byte);
 SAVE(tv.m_sx_extended_extbus);
 SAVE(tv.m_mpr);
 SAVE(tv.ram);
 SAVE(tv.pio_dir);
 SAVE(tv.super);
 SAVE(tv.random_state);
 SAVE(tv.mouse_delta);SAVE(tv.mouse_latched);
 SAVE(tv.bitmap_buffer);SAVE(tv.pio_out);SAVE(tv.extraioselect);SAVE(tv.palette.colors);
 SAVE(tv.m_mpd);
 SAVE(tv.m_mad);
 SAVE(tv.m_sgn_mpd);
 SAVE(tv.m_sgn_mpr);
 SAVE(tv.m_multresults);
 SAVE(sound.m_mix);
 SAVE(sound.m_voice);
 SAVE(sound.m_tempo_div);
 SAVE(sound.m_cyclerate_div);
 SAVE(sound.m_sequencer_rate_hz);
 SAVE(sound.m_tempo_reset_value);
#undef SAVE
}

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void *ekara_microphone_buffer() { return &ekara_microphone.buffer; }
extern "C" EMSCRIPTEN_KEEPALIVE int stvpc_printer_height(void) { return stvpc_printer.height; }
extern "C" EMSCRIPTEN_KEEPALIVE int stvpc_printer_rows(void) { return stvpc_printer.rows; }
extern "C" EMSCRIPTEN_KEEPALIVE const unsigned char *stvpc_printer_pixels(void) { return stvpc_printer.pixels; }
extern "C" EMSCRIPTEN_KEEPALIVE void *ekara_effect_state() {
 if(ekara_effect_profile==3) return &ekara_effect_hardware.echo;
 if(!ekara_effect_song()) return nullptr;
 unsigned address=ekara_effect_profile==1 ? 0x16d5 : 0x174d;
 ekara_effect_report[0]=tv.ram[address];
 ekara_effect_report[1]=tv.ram[address+1];
 ekara_effect_report[2]=(tv.ram[0x41]&1)!=0;
 return ekara_effect_report;
}
#endif

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void xavix_set_cart_eeprom(int enable) { xavix_tv_set_cart_eeprom(enable); }
#endif
