/*****************************************************************************
 *
 *   arm7.h
 *   Portable ARM7TDMI CPU Emulator (big-endian)
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *
 *   Adapted to the mame2003-plus (MAME 0.78) CPU interface.
 *
 *****************************************************************************/

#ifndef ARM7BE_H
#define ARM7BE_H

#include "cpuintrf.h"

extern int arm7be_ICount;

extern void arm7be_init(void);
extern void arm7be_reset(void *param);
extern void arm7be_exit(void);
extern int arm7be_execute(int cycles);
extern void arm7be_add_hotspot(unsigned pc);
extern void arm7be_clear_hotspots(void);
extern void arm7be_set_irq_line(int irqline, int state);
extern void arm7be_set_irq_callback(int (*irq_callback)(int));
extern unsigned arm7be_get_context(void *dst);
extern void arm7be_set_context(void *src);
extern unsigned arm7be_get_reg(int regnum);
extern void arm7be_set_reg(int regnum, unsigned val);
extern const char *arm7be_info(void *context, int regnum);
extern unsigned arm7be_dasm(char *buffer, unsigned pc);

#endif /* ARM7BE_H */
