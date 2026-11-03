// license:BSD-3-Clause
#include <stdint.h>

// Super TV-PC keyboard/RTC serial bus. The host supplies physical key states;
// the firmware receives its normal six-bit scan codes and make/break bit.
struct StvpcKeyboard {
 uint64_t held=0;
 uint8_t queue[128]{}, head=0, tail=0;
 unsigned packet=0, remaining=0;
 uint8_t data=0;
 bool sampled=false, active=false;
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
  if(active||head==tail)return false;
  active=true;sampled=false;
  // Prefix 110 selects the key channel, followed by seven bits, MSB first.
  packet=(6<<7)|queue[head];head=(head+1)&127;remaining=10;
  return advance();
 }
 bool advance() {
  if(!remaining)return false;
  data=((packet>>(--remaining))&1)?0x80:0;
  return true;
 }
};
