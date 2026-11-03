/* Host bridge shared by the C MAME driver and the portable C++ hardware port. */
#ifndef XAVIX_PORT_H
#define XAVIX_PORT_H
#ifdef __cplusplus
extern "C" {
#endif
void xavix_tv_set_clock(double (*clock)(void));
void xavix_tv_reset(int super, unsigned char *bios, unsigned char *cart);
void xavix_tv_set_cart_eeprom(int enable);
unsigned char *ekara_eeprom_data(void);
unsigned char *xavix_tv_ram(void);
unsigned char xavix_tv_read(unsigned address);
void xavix_tv_write(unsigned address, unsigned char data);
void xavix_tv_vblank(void);
void xavix_tv_input_sync(void);
void xavix_tv_timer(int id);
void xavix_tv_render(unsigned int *pixels, int first, int last);
void xavix_tv_render_progressive(unsigned int *pixels, int first, int last);
void xavix_tv_audio(short **outputs, int samples);
void xavix_tv_register_state(void);
double xavix_raster_delay(int y, int x);
void xavix_host_timer(int id, double delay, double period);
void xavix_host_audio_sync(void);
void xavix_host_video_sync(void);
unsigned char xavix_host_input(int port);
int xavix_host_pointer(int *x, int *y);
void xavix_host_save(const char *name, void *data, unsigned bytes);
#ifdef __cplusplus
}
#endif
#endif
