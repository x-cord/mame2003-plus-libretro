/*****************************************************************************
 *
 *   arm7.c
 *   Portable ARM7TDMI CPU Emulator
 *
 *   Copyright Steve Ellenoff, all rights reserved.
 *
 *   Based on the MAME 0.124 "src/emu/cpu/arm7/arm7.c" cpuintrf implementation,
 *   adapted to the mame2003-plus (MAME 0.78) simplified CPU interface.
 *
 *****************************************************************************/
#include <stdio.h>
#include <string.h>

#include "driver.h"
#include "state.h"
#include "cpuintrf.h"
#include "log.h"
#include "arm7.h"
#include "arm7core.h"

/* Macros that can be re-defined for custom cpu implementations */
#define READ8(addr)         arm7_cpu_read8(addr)
#define WRITE8(addr,data)   arm7_cpu_write8(addr,data)
#define READ16(addr)        arm7_cpu_read16(addr)
#define WRITE16(addr,data)  arm7_cpu_write16(addr,data)
#define READ32(addr)        arm7_cpu_read32(addr)
#define WRITE32(addr,data)  arm7_cpu_write32(addr,data)
#define PTR_READ32          &arm7_cpu_read32
#define PTR_WRITE32         &arm7_cpu_write32

/* Core expects these */
#define change_pc(pc)       change_pc32bedw(pc)
#define CALL_DEBUGGER(x)    do {} while (0)

#define ARM7REG(reg)        arm7.sArmRegister[reg]
#define ARM7                arm7
#define ARM7_ICOUNT         arm7_ICount

/* The core takes the address of some INLINE helpers (e.g. PTR_WRITE32 for
   coprocessor callbacks); force them to be static so they get local symbols. */
#undef INLINE
#define INLINE static inline

/* mame2003-plus's fatalerror() exits the process; during bring-up keep going
   so undefined Thumb opcodes are logged instead of killing the emulator. */
#define fatalerror(...) do {} while (0)

/* CPU Registers */
typedef struct
{
    ARM7CORE_REGS
} ARM7_REGS;

static ARM7_REGS arm7;
int arm7_ICount;

/* AP2010 games copy their IRQ wait loop into RAM. Mirror upstream MAME's
   AP2010 CPU hotspots: on entry to a known wait instruction, consume the
   timeslice except for the final 30 cycles. */
static unsigned arm7_hotspots[5];
static int arm7_hotspot_count;

void arm7_clear_hotspots(void)
{
    arm7_hotspot_count = 0;
}

void arm7_add_hotspot(unsigned pc)
{
    if (arm7_hotspot_count < 5)
        arm7_hotspots[arm7_hotspot_count++] = pc;
}

/* include the arm7 core */
#include "arm7core.c"

/***************************************************************************
 * CPU SPECIFIC IMPLEMENTATIONS
 **************************************************************************/
void arm7_init(void)
{
    int cpu = cpu_getactivecpu();

    arm7_core_init("arm7", cpu);

    ARM7.irq_callback = NULL;
    arm7_coproc_do_callback = NULL;
    arm7_coproc_rt_r_callback = NULL;
    arm7_coproc_rt_w_callback = NULL;
    arm7_coproc_dt_r_callback = NULL;
    arm7_coproc_dt_w_callback = NULL;
}

void arm7_reset(void *param)
{
    (void)param;
    arm7_clear_hotspots();
    arm7_core_reset();
}

void arm7_exit(void)
{
}

int arm7_execute(int cycles)
{
#include "arm7exec.c"
}

void arm7_set_irq_line(int irqline, int state)
{
    arm7_core_set_irq_line(irqline, state);
}

void arm7_set_irq_callback(int (*callback)(int))
{
    ARM7.irq_callback = callback;
}

unsigned arm7_get_context(void *dst)
{
    if (dst)
        memcpy(dst, &ARM7, sizeof(ARM7));
    return sizeof(ARM7);
}

void arm7_set_context(void *src)
{
    if (src)
        memcpy(&ARM7, src, sizeof(ARM7));
    change_pc32bedw(ARM7REG(eR15));
}

unsigned arm7_get_reg(int regnum)
{
    switch (regnum)
    {
        case REG_PC:
        case ARM7_PC:       return R15;
        case REG_SP:        return GetRegister(13);
        case REG_PREVIOUSPC: return 0;
        case ARM7_CPSR:     return GET_CPSR;
    }

    if (regnum >= ARM7_R0 && regnum <= ARM7_R15)
        return ARM7REG((regnum - ARM7_R0) + eR0);
    if (regnum >= ARM7_FR8 && regnum <= ARM7_FR14)
        return ARM7REG(eR8_FIQ + (regnum - ARM7_FR8));

    switch (regnum)
    {
        case ARM7_FSPSR: return ARM7REG(eSPSR_FIQ);
        case ARM7_IR13:  return ARM7REG(eR13_IRQ);
        case ARM7_IR14:  return ARM7REG(eR14_IRQ);
        case ARM7_ISPSR: return ARM7REG(eSPSR_IRQ);
        case ARM7_SR13:  return ARM7REG(eR13_SVC);
        case ARM7_SR14:  return ARM7REG(eR14_SVC);
        case ARM7_SSPSR: return ARM7REG(eSPSR_SVC);
        case ARM7_AR13:  return ARM7REG(eR13_ABT);
        case ARM7_AR14:  return ARM7REG(eR14_ABT);
        case ARM7_ASPSR: return ARM7REG(eSPSR_ABT);
        case ARM7_UR13:  return ARM7REG(eR13_UND);
        case ARM7_UR14:  return ARM7REG(eR14_UND);
        case ARM7_USPSR: return ARM7REG(eSPSR_UND);
    }
    return 0;
}

void arm7_set_reg(int regnum, unsigned val)
{
    switch (regnum)
    {
        case REG_PC:
        case ARM7_PC:       R15 = val; change_pc32bedw(R15); return;
        case REG_SP:        SetRegister(13, val); return;
        case ARM7_CPSR:     SET_CPSR(val); return;
    }

    if (regnum >= ARM7_R0 && regnum <= ARM7_R15)
    {
        ARM7REG((regnum - ARM7_R0) + eR0) = val;
        return;
    }
    if (regnum >= ARM7_FR8 && regnum <= ARM7_FR14)
    {
        ARM7REG(eR8_FIQ + (regnum - ARM7_FR8)) = val;
        return;
    }

    switch (regnum)
    {
        case ARM7_FSPSR: ARM7REG(eSPSR_FIQ) = val; return;
        case ARM7_IR13:  ARM7REG(eR13_IRQ) = val; return;
        case ARM7_IR14:  ARM7REG(eR14_IRQ) = val; return;
        case ARM7_ISPSR: ARM7REG(eSPSR_IRQ) = val; return;
        case ARM7_SR13:  ARM7REG(eR13_SVC) = val; return;
        case ARM7_SR14:  ARM7REG(eR14_SVC) = val; return;
        case ARM7_SSPSR: ARM7REG(eSPSR_SVC) = val; return;
        case ARM7_AR13:  ARM7REG(eR13_ABT) = val; return;
        case ARM7_AR14:  ARM7REG(eR14_ABT) = val; return;
        case ARM7_ASPSR: ARM7REG(eSPSR_ABT) = val; return;
        case ARM7_UR13:  ARM7REG(eR13_UND) = val; return;
        case ARM7_UR14:  ARM7REG(eR14_UND) = val; return;
        case ARM7_USPSR: ARM7REG(eSPSR_UND) = val; return;
    }
}

const char *arm7_info(void *context, int regnum)
{
    static char buffer[32][47 + 1];
    static int which = 0;
    ARM7_REGS *r = context;

    which = (which + 1) % 32;
    buffer[which][0] = '\0';
    if (!context)
        r = &ARM7;

    switch (regnum)
    {
        case CPU_INFO_REG + ARM7_PC:   sprintf(buffer[which], "PC:%08X", r->sArmRegister[eR15]); break;
        case CPU_INFO_REG + ARM7_R0:   sprintf(buffer[which], "R0:%08X", r->sArmRegister[eR0]); break;
        case CPU_INFO_REG + ARM7_R1:   sprintf(buffer[which], "R1:%08X", r->sArmRegister[eR1]); break;
        case CPU_INFO_REG + ARM7_R2:   sprintf(buffer[which], "R2:%08X", r->sArmRegister[eR2]); break;
        case CPU_INFO_REG + ARM7_R3:   sprintf(buffer[which], "R3:%08X", r->sArmRegister[eR3]); break;
        case CPU_INFO_REG + ARM7_R4:   sprintf(buffer[which], "R4:%08X", r->sArmRegister[eR4]); break;
        case CPU_INFO_REG + ARM7_R5:   sprintf(buffer[which], "R5:%08X", r->sArmRegister[eR5]); break;
        case CPU_INFO_REG + ARM7_R6:   sprintf(buffer[which], "R6:%08X", r->sArmRegister[eR6]); break;
        case CPU_INFO_REG + ARM7_R7:   sprintf(buffer[which], "R7:%08X", r->sArmRegister[eR7]); break;
        case CPU_INFO_REG + ARM7_R8:   sprintf(buffer[which], "R8:%08X", r->sArmRegister[eR8]); break;
        case CPU_INFO_REG + ARM7_R9:   sprintf(buffer[which], "R9:%08X", r->sArmRegister[eR9]); break;
        case CPU_INFO_REG + ARM7_R10:  sprintf(buffer[which], "R10:%08X", r->sArmRegister[eR10]); break;
        case CPU_INFO_REG + ARM7_R11:  sprintf(buffer[which], "R11:%08X", r->sArmRegister[eR11]); break;
        case CPU_INFO_REG + ARM7_R12:  sprintf(buffer[which], "R12:%08X", r->sArmRegister[eR12]); break;
        case CPU_INFO_REG + ARM7_R13:  sprintf(buffer[which], "R13:%08X", r->sArmRegister[eR13]); break;
        case CPU_INFO_REG + ARM7_R14:  sprintf(buffer[which], "R14:%08X", r->sArmRegister[eR14]); break;
        case CPU_INFO_REG + ARM7_R15:  sprintf(buffer[which], "R15:%08X", r->sArmRegister[eR15]); break;
        case CPU_INFO_REG + ARM7_CPSR: sprintf(buffer[which], "CPSR:%08X", r->sArmRegister[eCPSR]); break;
        case CPU_INFO_FLAGS:
            sprintf(buffer[which], "%c%c%c%c%c%c%c %s",
                    (r->sArmRegister[eCPSR] & N_MASK) ? 'N' : '-',
                    (r->sArmRegister[eCPSR] & Z_MASK) ? 'Z' : '-',
                    (r->sArmRegister[eCPSR] & C_MASK) ? 'C' : '-',
                    (r->sArmRegister[eCPSR] & V_MASK) ? 'V' : '-',
                    (r->sArmRegister[eCPSR] & I_MASK) ? 'I' : '-',
                    (r->sArmRegister[eCPSR] & F_MASK) ? 'F' : '-',
                    (r->sArmRegister[eCPSR] & T_MASK) ? 'T' : '-',
                    (r->sArmRegister[eCPSR] & 0x1f) == eARM7_MODE_USER ? "usr" :
                    (r->sArmRegister[eCPSR] & 0x1f) == eARM7_MODE_FIQ  ? "fiq" :
                    (r->sArmRegister[eCPSR] & 0x1f) == eARM7_MODE_IRQ  ? "irq" :
                    (r->sArmRegister[eCPSR] & 0x1f) == eARM7_MODE_SVC  ? "svc" :
                    (r->sArmRegister[eCPSR] & 0x1f) == eARM7_MODE_ABT  ? "abt" :
                    (r->sArmRegister[eCPSR] & 0x1f) == eARM7_MODE_UND  ? "und" :
                    (r->sArmRegister[eCPSR] & 0x1f) == eARM7_MODE_SYS  ? "sys" : "???");
            break;
        case CPU_INFO_NAME:    return "ARM7";
        case CPU_INFO_FAMILY:  return "Acorn Risc Machine";
        case CPU_INFO_VERSION: return "1.3";
        case CPU_INFO_FILE:    return __FILE__;
        case CPU_INFO_CREDITS: return "Copyright Steve Ellenoff, sellenoff@hotmail.com";
    }
    return buffer[which];
}

unsigned arm7_dasm(char *buffer, unsigned pc)
{
    sprintf(buffer, "$%08X", cpu_readmem32bedw_dword(pc & ~3));
    return 4;
}
