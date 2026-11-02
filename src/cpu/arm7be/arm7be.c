/* Big-endian ARM7TDMI instantiation of Steve Ellenoff's ARM7 core.
   The core is shared with the little-endian BREW instantiation (arm7le.c);
   the public symbols are prefixed so both can coexist with the ARM7 core
   used by the PGM driver. */
#include "driver.h"
#define arm7_ICount arm7be_ICount
#define arm7_init arm7be_init
#define arm7_reset arm7be_reset
#define arm7_exit arm7be_exit
#define arm7_execute arm7be_execute
#define arm7_add_hotspot arm7be_add_hotspot
#define arm7_clear_hotspots arm7be_clear_hotspots
#define arm7_set_irq_line arm7be_set_irq_line
#define arm7_set_irq_callback arm7be_set_irq_callback
#define arm7_get_context arm7be_get_context
#define arm7_set_context arm7be_set_context
#define arm7_get_reg arm7be_get_reg
#define arm7_set_reg arm7be_set_reg
#define arm7_info arm7be_info
#define arm7_dasm arm7be_dasm
#define arm7_coproc_do_callback arm7be_coproc_do_callback
#define arm7_coproc_rt_r_callback arm7be_coproc_rt_r_callback
#define arm7_coproc_rt_w_callback arm7be_coproc_rt_w_callback
#define arm7_coproc_dt_r_callback arm7be_coproc_dt_r_callback
#define arm7_coproc_dt_w_callback arm7be_coproc_dt_w_callback
#include "arm7.c"
