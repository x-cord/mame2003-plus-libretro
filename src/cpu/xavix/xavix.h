/* XaviX / Super XaviX CPU interface for MAME 0.78. */
#ifndef XAVIX_078_H
#define XAVIX_078_H
#ifdef __cplusplus
extern "C" {
#endif
extern int xavix_ICount;
void xavix_init(void);
void xavix_reset(void *param);
void xavix_exit(void);
int xavix_execute(int cycles);
unsigned xavix_get_context(void *dst);
void xavix_set_context(void *src);
unsigned xavix_get_pc(void);
void xavix_set_pc(unsigned val);
unsigned xavix_get_sp(void);
void xavix_set_sp(unsigned val);
unsigned xavix_get_reg(int regnum);
void xavix_set_reg(int regnum, unsigned val);
void xavix_set_irq_line(int irqline, int state);
void xavix_set_irq_callback(int (*callback)(int));
const char *xavix_info(void *context, int regnum);
unsigned xavix_dasm(char *buffer, unsigned pc);
#ifdef __cplusplus
}
#endif
#endif
