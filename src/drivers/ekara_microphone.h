// license:BSD-3-Clause
#ifndef EKARA_MICROPHONE_H
#define EKARA_MICROPHONE_H
#include <stdint.h>
#include <algorithm>

// Single browser producer and emulation-thread consumer, mono PCM at 16 kHz.
struct EkaraMicrophone {
 enum { capacity=8192, rate=16000 };
 struct Buffer {
  uint32_t write=0, read=0, enabled=0;
  int32_t pcm[capacity]{};
 } buffer;
 uint64_t cycles=0, last_cycle=0;
 double phase=0;
 int sample=0;
 bool primed=false;
 void reset() {
  __atomic_store_n(&buffer.read,__atomic_load_n(&buffer.write,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
  last_cycle=cycles;phase=0;sample=0;primed=false;
 }
 uint8_t convert(uint64_t now) {
  if(!__atomic_load_n(&buffer.enabled,__ATOMIC_ACQUIRE)) { reset();last_cycle=now;return 128; }
  if(now<last_cycle)last_cycle=now;
  phase+=(now-last_cycle)*(16000.0/21477272.0);last_cycle=now;
  unsigned count=unsigned(phase);phase-=count;
  uint32_t read=__atomic_load_n(&buffer.read,__ATOMIC_RELAXED);
  uint32_t write=__atomic_load_n(&buffer.write,__ATOMIC_ACQUIRE);
  unsigned available=write-read;
  // Bound latency after pauses and slow frames.
  if(available>2048) {read=write-2048;available=2048;}
  // Keep a short lead over block delivery before advancing the sample clock.
  if(!primed) {
   phase=0;sample=0;
   if(available<768)return 128;
   primed=true;
   count=0;
  }
  if(count) {
   unsigned consumed=std::min(count,available);
   sample=consumed==count?buffer.pcm[(read+consumed-1)%capacity]:0;
   if(consumed<count)primed=false;
   __atomic_store_n(&buffer.read,read+consumed,__ATOMIC_RELEASE);
  }
  return uint8_t(std::max(0,std::min(255,(32768+sample)/256)));
 }
};
#endif
