/* Little-endian ARM7TDMI instantiation of Steve Ellenoff's ARM7 core.
   Modified for BREW: memory access and opcode base use the LE bus.
   The big-endian Beena instantiation retains its original implementation. */
#include "driver.h"
#undef change_pc32bedw
#define change_pc32bedw change_pc32ledw
#define cpu_readmem32bedw cpu_readmem32ledw
#define cpu_writemem32bedw cpu_writemem32ledw
#define cpu_readmem32bedw_word cpu_readmem32ledw_word
#define cpu_writemem32bedw_word cpu_writemem32ledw_word
#define cpu_readmem32bedw_dword cpu_readmem32ledw_dword
#define cpu_writemem32bedw_dword cpu_writemem32ledw_dword
#define arm7_ICount arm7le_ICount
#define arm7_init arm7le_init
#define arm7_reset arm7le_reset
#define arm7_exit arm7le_exit
#define arm7_execute arm7le_execute
#define arm7_add_hotspot arm7le_add_hotspot
#define arm7_clear_hotspots arm7le_clear_hotspots
#define arm7_set_irq_line arm7le_set_irq_line
#define arm7_set_irq_callback arm7le_set_irq_callback
#define arm7_get_context arm7le_get_context
#define arm7_set_context arm7le_set_context
#define arm7_get_reg arm7le_get_reg
#define arm7_set_reg arm7le_set_reg
#define arm7_info arm7le_info
#define arm7_dasm arm7le_dasm
#define arm7_coproc_do_callback arm7le_coproc_do_callback
#define arm7_coproc_rt_r_callback arm7le_coproc_rt_r_callback
#define arm7_coproc_rt_w_callback arm7le_coproc_rt_w_callback
#define arm7_coproc_dt_r_callback arm7le_coproc_dt_r_callback
#define arm7_coproc_dt_w_callback arm7le_coproc_dt_w_callback

#define ARM7_V5_INTERWORK
#include "arm7.c"
