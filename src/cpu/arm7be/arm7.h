/*****************************************************************************
 *
 *   arm7.h
 *   Portable ARM7TDMI CPU Emulator
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *
 *   Adapted to the mame2003-plus (MAME 0.78) CPU interface.
 *
 *****************************************************************************/

#ifndef ARM7_H
#define ARM7_H

#include "cpuintrf.h"

extern int arm7_ICount;

extern void arm7_init(void);
extern void arm7_reset(void *param);
extern void arm7_exit(void);
extern int arm7_execute(int cycles);
extern void arm7_add_hotspot(unsigned pc);
extern void arm7_clear_hotspots(void);
extern void arm7_set_irq_line(int irqline, int state);
extern void arm7_set_irq_callback(int (*irq_callback)(int));
extern unsigned arm7_get_context(void *dst);
extern void arm7_set_context(void *src);
extern unsigned arm7_get_reg(int regnum);
extern void arm7_set_reg(int regnum, unsigned val);
extern const char *arm7_info(void *context, int regnum);
extern unsigned arm7_dasm(char *buffer, unsigned pc);

#endif /* ARM7_H */
