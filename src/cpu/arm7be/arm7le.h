/*****************************************************************************
 *
 *   arm7le.h
 *   Portable ARM7TDMI CPU Emulator
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *
 *   Adapted to the mame2003-plus (MAME 0.78) CPU interface.
 *
 *****************************************************************************/

#ifndef ARM7LE_H
#define ARM7LE_H

#include "cpuintrf.h"

extern int arm7le_ICount;

extern void arm7le_init(void);
extern void arm7le_reset(void *param);
extern void arm7le_exit(void);
extern int arm7le_execute(int cycles);
extern void arm7le_add_hotspot(unsigned pc);
extern void arm7le_clear_hotspots(void);
extern void arm7le_set_irq_line(int irqline, int state);
extern void arm7le_set_irq_callback(int (*irq_callback)(int));
extern unsigned arm7le_get_context(void *dst);
extern void arm7le_set_context(void *src);
extern unsigned arm7le_get_reg(int regnum);
extern void arm7le_set_reg(int regnum, unsigned val);
extern const char *arm7le_info(void *context, int regnum);
extern unsigned arm7le_dasm(char *buffer, unsigned pc);

#endif /* ARM7LE_H */
