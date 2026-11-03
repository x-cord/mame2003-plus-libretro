// license:BSD-3-Clause
#include <stdint.h>
#include <time.h>

// Super TV-PC keyboard/RTC serial bus. The host supplies physical key states;
// the firmware receives its normal six-bit scan codes and make/break bit.
// The same controller carries the real-time clock: a four-byte time packet is
// sent as four prefixed bytes (011/010/001/000) separated by one idle bit,
// which the firmware's decoder consumes before each channel prefix.
struct StvpcKeyboard {
 uint8_t pads[2]{}, sent_pads[2]{};
 uint64_t held=0;
 uint8_t queue[128]{}, head=0, tail=0;
 uint64_t packet=0; unsigned remaining=0;
 uint8_t data=0;
 bool sampled=false, active=false;
 int rtc_wait=60;
 uint8_t read() {sampled=true;return data;}
 bool clock() {if(!sampled)return false;sampled=false;if(!remaining){active=false;return false;}return advance();}
 void reset() {*this=StvpcKeyboard{};}
 void poll(uint64_t keys) {
  // Modifiers precede other make codes, and follow other break codes.
  uint64_t changes=keys^held;
  for(int pass=0;pass<2;pass++)for(int key=0;key<64;key++) {
   bool modifier=key==1||key==2;
   bool pressed=(keys>>key)&1;
   bool first=modifier?pressed:!pressed;
   if(!(changes&(uint64_t(1)<<key)) || first!=(pass==0))continue;
   uint8_t next=(tail+1)&127;
   if(next==head)return; // Retain held states; retry remaining edges next poll.
   queue[tail]=key|(((keys>>key)&1)?0x40:0);tail=next;
   held^=uint64_t(1)<<key;
  }
 }
 bool start() {
  if(active)return false;
  // Pad channels carry five held buttons: directions and the action button.
  for(int i=0;i<2;i++)if(pads[i]!=sent_pads[i]) {
   active=true;sampled=false;sent_pads[i]=pads[i];
   packet=((4+i)<<5)|pads[i];remaining=8;
   return advance();
  }
  if(head==tail)return rtc_start();
  active=true;sampled=false;
  // Prefix 110 selects the key channel, followed by seven bits, MSB first.
  packet=(6<<7)|queue[head];head=(head+1)&127;remaining=10;
  return advance();
 }
 // Queue the current wall-clock time when the firmware is due for an update.
 bool rtc_start() {
  if(rtc_wait>0){rtc_wait--;return false;}
  rtc_wait=60;
  time_t now=time(nullptr);
  struct tm *t=localtime(&now);
  if(!t)return false;
  int year=t->tm_year+1900-2000;
  int month=t->tm_mon+1;
  uint8_t bytes[4]={
   uint8_t((t->tm_sec&0x3f)|((t->tm_min&3)<<6)),
   uint8_t(((t->tm_min>>2)&0x0f)|((t->tm_hour&0x0f)<<4)),
   uint8_t(((t->tm_hour>>4)&1)|((t->tm_mday&0x1f)<<1)|((month&3)<<6)),
   uint8_t(((month>>2)&3)|((year&0x3f)<<2))};
  // Bytes travel in reverse order: 011 then 010 then 001 then 000. Every
  // byte is followed by an idle bit, which the firmware's decoder consumes
  // before the next channel prefix and which leaves it armed again.
  uint64_t bits=0;unsigned count=0;
  for(int i=0;i<4;i++) {
   int prefix=3-i;
   bits=(bits<<3)|prefix;count+=3;
   bits=(bits<<8)|bytes[3-i];count+=8;
   bits=(bits<<1);count++;
  }
  packet=bits;remaining=count;active=true;sampled=false;
  return advance();
 }
 bool advance() {
  if(!remaining)return false;
  data=((packet>>(--remaining))&1)?0x80:0;
  return true;
 }
};
