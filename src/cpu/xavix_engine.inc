// license:BSD-3-Clause
// copyright-holders:David Haywood, Olivier Galibert
// Instruction-level adapter for MAME 0.78. Instruction semantics are imported
// from MAME; every bus operation consumes one CPU clock.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../drivers/xavix_port.h"
#include "xavix/xavix.h"
extern "C" { int xavix_ICount = 0; }
static uint8_t low_read(uint16_t);
static void low_write(uint16_t, uint8_t);
static uint8_t ext_read(uint32_t);
static void ext_write(uint32_t, uint8_t);
static uint8_t opcode_read(uint32_t);
static int vector_read(int, int);
struct XavixCPU {
 uint16_t m_PC, m_SP, m_TMP, m_PPC;
 uint8_t m_TMP2, m_A, m_X, m_Y, m_P, bank, databank, prevbank;
 uint8_t m_j, m_k, m_l, m_m;
 uint32_t m_pa, m_pb;
 uint8_t special[256], specialpos;
 bool super, m_irq_taken, m_nmi_pending, irq, nmi_line, pending_irq, stopped;
 unsigned unsupported;
 enum { F_N=128,F_V=64,F_E=32,F_B=16,F_D=8,F_I=4,F_Z=2,F_C=1 };
 static uint16_t set_h(uint16_t v,uint8_t h) { return (v&255)|(h<<8); }
 static uint16_t set_l(uint16_t v,uint8_t l) { return (v&0xff00)|l; }
 static bool page_changing(uint16_t v,uint8_t x) { return (v&255)+x>255; }
 void dec_SP() { m_SP=set_l(m_SP,m_SP-1); }
 void inc_SP() { m_SP=set_l(m_SP,m_SP+1); }
 void set_nz(uint8_t v) { m_P=(m_P&~(F_N|F_Z))|(v&128)|(!v?F_Z:0); }
 uint8_t get_codebank() { return bank; }
 void set_codebank(uint8_t v) { bank=v; }
 uint8_t read_full_data(uint32_t a) { return read_full_data(a>>16,a); }
 uint8_t read_full_data(uint8_t b,uint16_t a) {
  --xavix_ICount;
  if(b<128 && a<0x8000) { if(a==0xfe) return bank; if(a==0xff) return databank; return low_read(a); }
  return ext_read(((b<<16)|a)&0x7fffff);
 }
 void write_full_data(uint32_t a,uint8_t v) { write_full_data(a>>16,a,v); }
 void write_full_data(uint8_t b,uint16_t a,uint8_t v) {
  --xavix_ICount;
  if(b<128 && a<0x8000) { if(a==0xfe) bank=v; else if(a==0xff) databank=v; else low_write(a,v); }
  else ext_write(((b<<16)|a)&0x7fffff,v);
 }
 uint8_t read(uint16_t a) { return read_full_data(databank,a); }
 void write(uint16_t a,uint8_t v) { write_full_data(databank,a,v); }
 uint8_t read_arg(uint16_t a) { --xavix_ICount; return opcode_read((bank<<16)|a); }
 uint8_t read_pc() { return read_arg(m_PC); }
 uint8_t read_pc_noirq() { return read_pc(); }
 uint8_t read_zeropage(uint32_t a) { --xavix_ICount; if((a&255)==254) return bank; if((a&255)==255) return databank; return low_read(a&255); }
 void write_zeropage(uint32_t a,uint8_t v) { --xavix_ICount; if((a&255)==254) bank=v; else if((a&255)==255) databank=v; else low_write(a&255,v); }
 uint8_t read_stack(uint32_t a) { --xavix_ICount; return low_read(0x100|(a&255)); }
 void write_stack(uint32_t a,uint8_t v) { --xavix_ICount; low_write(0x100|(a&255),v); }
 void write_special_stack(uint32_t a,uint8_t v) { if(super) write_stack(a,v); else special[specialpos]=v; }
 uint8_t read_special_stack(uint32_t a) { return super?read_stack(a):special[specialpos]; }
 void dec_special_stack() { if(super) dec_SP(); else specialpos--; }
 void inc_special_stack() { if(super) inc_SP(); else specialpos++; }
 void prefetch() { --xavix_ICount; pending_irq=m_nmi_pending||(irq && !(m_P&F_I)); }
 void prefetch_noirq() { --xavix_ICount; }
 void unsupported_opcode() { unsupported++; stopped=true; }
 void brk_xav_imp() {
  read_pc(); if(!m_irq_taken) m_PC++;
  write_special_stack(m_SP,bank); dec_special_stack(); bank=0;
  write_stack(m_SP,m_PC>>8);dec_SP();write_stack(m_SP,m_PC);dec_SP();
  write_stack(m_SP,m_irq_taken?m_P&~F_B:m_P);dec_SP();
  int which=m_nmi_pending?0:1;
  int lo=vector_read(which,1),hi=vector_read(which,0);
  m_PC=lo<0?read_arg(which?0xfffe:0xfffa):lo;
  m_PC=set_h(m_PC,hi<0?read_arg(which?0xffff:0xfffb):hi);
  m_nmi_pending=false;m_irq_taken=false;m_P|=F_I;prefetch();
 }
 #include "xavix/xavix_alu.inc"
 #include "xavix/xavix_ops.inc"
};
static XavixCPU cpu;
