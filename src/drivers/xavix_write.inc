// license:BSD-3-Clause
// Adapted from MAME 4edb90120a5ec0804a17d6f19ccc588e76b54a06.
if (a>=0x4000 && a<=0x4fff) { tv.xavix_memoryemu_txarray_w(a-0x4000, d); return; }
if (a>=0x6000 && a<=0x67ff) { tv.spriteram_w(a-0x6000, d); return; }
if (a>=0x6800 && a<=0x68ff) { tv.palram_sh_w(a-0x6800, d); return; }
if (a>=0x6900 && a<=0x69ff) { tv.palram_l_w(a-0x6900, d); return; }
if (a>=0x6fc8 && a<=0x6fcf) { tv.tmap1_regs_w(a-0x6fc8, d); return; }
if (a>=0x6fd0 && a<=0x6fd7) { tv.tmap2_regs_w(a-0x6fd0, d); return; }
if (a>=0x6fd8 && a<=0x6fd8) { tv.spriteregs_w(d); return; }
if (a>=0x6fe0 && a<=0x6fe0) { tv.spritefragment_dma_trg_w(d); return; }
if (a>=0x6fe1 && a<=0x6fe2) { tv.spritefragment_dma_params_1_w(a-0x6fe1, d); return; }
if (a>=0x6fe5 && a<=0x6fe6) { tv.spritefragment_dma_params_2_w(a-0x6fe5, d); return; }
if (a>=0x6fe8 && a<=0x6fe8) { tv.arena_start_w(d); return; }
if (a>=0x6fe9 && a<=0x6fe9) { tv.arena_end_w(d); return; }
if (a>=0x6fea && a<=0x6fea) { tv.arena_control_w(d); return; }
if (a>=0x6ff0 && a<=0x6ff0) { tv.colmix_sh_w(a-0x6ff0, d); return; }
if (a>=0x6ff1 && a<=0x6ff1) { tv.colmix_l_w(a-0x6ff1, d); return; }
if (a>=0x6ff2 && a<=0x6ff2) { tv.colmix_6ff2_w(d); return; }
if (a>=0x6ff8 && a<=0x6ff8) { tv.dispctrl_6ff8_w(d); return; }
if (a>=0x6ffa && a<=0x6ffa) { tv.dispctrl_posirq_x_w(d); return; }
if (a>=0x6ffb && a<=0x6ffb) { tv.dispctrl_posirq_y_w(d); return; }
if (a>=0x75f0 && a<=0x75f1) { tv.sound_voice_startstop_w(a-0x75f0, d); return; }
if (a>=0x75f2 && a<=0x75f3) { tv.sound_voice_updateenv_w(a-0x75f2, d); return; }
if (a>=0x75f6 && a<=0x75f6) { tv.sound_volume_w(d); return; }
if (a>=0x75f7 && a<=0x75f7) { tv.sound_regbase_w(d); return; }
if (a>=0x75f8 && a<=0x75f8) { tv.sound_cyclerate_w(d); return; }
if (a>=0x75f9 && a<=0x75f9) { tv.sound_mixer_w(d); return; }
if (a>=0x75fa && a<=0x75fa) { tv.sound_tp0_w(d); return; }
if (a>=0x75fb && a<=0x75fb) { tv.sound_tp1_w(d); return; }
if (a>=0x75fc && a<=0x75fc) { tv.sound_tp2_w(d); return; }
if (a>=0x75fd && a<=0x75fd) { tv.sound_tp3_w(d); return; }
if (a>=0x75fe && a<=0x75fe) { tv.sound_irq_status_w(d); return; }
if (a>=0x75ff && a<=0x75ff) { tv.sound_dac_control_w(d); return; }
if (a>=0x7810 && a<=0x7810) { tv.slotreg_7810_w(d); return; }
if (a>=0x7900 && a<=0x7902) { tv.extintrf_790x_w(a-0x7900, d); return; }
if (a>=0x7980 && a<=0x7980) { tv.rom_dmatrg_w(d); return; }
if (a>=0x7981 && a<=0x7983) { tv.rom_dmasrc_w(a-0x7981, d); return; }
if (a>=0x7984 && a<=0x7985) { tv.rom_dmadst_w(a-0x7984, d); return; }
if (a>=0x7986 && a<=0x7987) { tv.rom_dmalen_w(a-0x7986, d); return; }
if (a>=0x7a00 && a<=0x7a00) { tv.io0_data_w(d); return; }
if (a>=0x7a01 && a<=0x7a01) { tv.io1_data_w(d); return; }
if (a>=0x7a02 && a<=0x7a02) { tv.io0_direction_w(d); return; }
if (a>=0x7a03 && a<=0x7a03) { tv.io1_direction_w(d); return; }
if (a>=0x7a80 && a<=0x7a80) { tv.ioevent_enable_w(d); return; }
if (a>=0x7a81 && a<=0x7a81) { tv.ioevent_irqack_w(d); return; }
if (a>=0x7c00 && a<=0x7c00) { tv.timer_control_w(d); return; }
if (a>=0x7c01 && a<=0x7c01) { tv.timer_baseval_w(d); return; }
if (a>=0x7c02 && a<=0x7c02) { tv.timer_freq_w(d); return; }
if (a>=0x7ff0 && a<=0x7ff1) { tv.barrel_w(a-0x7ff0, d); return; }
if (a>=0x7ff2 && a<=0x7ff4) { tv.mult_param_w(a-0x7ff2, d, 0xff); return; }
if (a>=0x7ff5 && a<=0x7ff6) { tv.mult_w(a-0x7ff5, d); return; }
if (a>=0x7ff9 && a<=0x7ff9) { tv.vector_enable_w(d); return; }
if (a>=0x7ffa && a<=0x7ffa) { tv.nmi_vector_lo_w(d); return; }
if (a>=0x7ffb && a<=0x7ffb) { tv.nmi_vector_hi_w(d); return; }
if (a>=0x7ffc && a<=0x7ffc) { tv.irq_source_w(d); return; }
if (a>=0x7ffe && a<=0x7ffe) { tv.irq_vector_lo_w(d); return; }
if (a>=0x7fff && a<=0x7fff) { tv.irq_vector_hi_w(d); return; }
if (tv.super && a>=0x6c00 && a<=0x6cff) { tv.bmp_palram_sh_w(a-0x6c00, d); return; }
if (tv.super && a>=0x6d00 && a<=0x6dff) { tv.bmp_palram_l_w(a-0x6d00, d); return; }
if (tv.super && a>=0x6a40 && a<=0x6a7f) { tv.ext_segment_regs_w(a-0x6a40, d); return; }
if (tv.super && a>=0x6f60 && a<=0x6f60) { tv.superxavix_plt_flush_w(d); return; }
if (tv.super && a>=0x6f62 && a<=0x6f62) { tv.superxavix_plt_dat_w(d); return; }
if (tv.super && a>=0x6f64 && a<=0x6f67) { tv.superxavix_plt_loc_w(a-0x6f64, d); return; }
if (tv.super && a>=0x6f78 && a<=0x6f78) { tv.superxavix_chr_pal_index_w(d); return; }
if (tv.super && a>=0x6f79 && a<=0x6f79) { tv.superxavix_chr_pal_hue_w(d); return; }
if (tv.super && a>=0x6f7a && a<=0x6f7a) { tv.superxavix_chr_pal_saturation_w(d); return; }
if (tv.super && a>=0x6f7b && a<=0x6f7b) { tv.superxavix_chr_pal_lightness_w(d); return; }
if (tv.super && a>=0x6f7c && a<=0x6f7c) { tv.superxavix_bitmap_pal_index_w(d); return; }
if (tv.super && a>=0x6f7d && a<=0x6f7d) { tv.superxavix_bitmap_pal_hue_w(d); return; }
if (tv.super && a>=0x6f7e && a<=0x6f7e) { tv.superxavix_bitmap_pal_saturation_w(d); return; }
if (tv.super && a>=0x6f7f && a<=0x6f7f) { tv.superxavix_bitmap_pal_lightness_w(d); return; }
if (tv.super && a>=0x6f80 && a<=0x6f99) { tv.superxavix_crtc_1_w(a-0x6f80, d); return; }
if (tv.super && a>=0x6fa0 && a<=0x6fa7) { tv.superxavix_crtc_2_w(a-0x6fa0, d); return; }
if (tv.super && a>=0x6fb0 && a<=0x6fc7) { tv.bitmap_params_w(a-0x6fb0, d); return; }
