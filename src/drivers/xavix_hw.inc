// license:BSD-3-Clause
// Adapted from MAME 4edb90120a5ec0804a17d6f19ccc588e76b54a06.
// copyright-holders:David Haywood
void XavixTV::set_data_address(int address, int bit)
{
	m_tmp_dataaddress = address;
	m_tmp_databit = bit;
}
uint8_t XavixTV::get_next_bit()
{
	// going through memory is slow, try not to do it too often!
	if (m_tmp_databit == 0)
	{
		if (m_disable_memory_bypass)
			m_bit = opcode_read(m_tmp_dataaddress);
		else
			m_bit = read_full_data_sp_bypass(m_tmp_dataaddress);
	}

	uint8_t ret = m_bit >> m_tmp_databit;
	ret &= 1;

	m_tmp_databit++;

	if (m_tmp_databit == 8)
	{
		m_tmp_databit = 0;
		m_tmp_dataaddress++;
	}

	return ret;
}
uint8_t XavixTV::get_next_bit_sx()
{
	if (m_tmp_databit == 0)
	{
		int address = m_tmp_dataaddress;

		// do the upper bits being set have some special meaning
		// or have we missed some segment register use by the time we get here?

		// anpanmdx has this set, this is an alternative to the
		// code to detect it in draw_bitmap_layer
		if (address & 0x2000000)
		{
			address &= 0x1ffffff;
			address ^= 0x0600000;
		}

		// the higher bit set when accessing video expands the address space
		if ((address & 0x1000000) && m_extra)
			m_bit = m_extra[address & 0x7fffff];
		else
			m_bit = read_full_data_sp_bypass(address);
	}

	uint8_t ret = m_bit >> m_tmp_databit;
	ret &= 1;

	m_tmp_databit++;

	if (m_tmp_databit == 8)
	{
		m_tmp_databit = 0;
		m_tmp_dataaddress++;
	}

	return ret;
}
uint8_t XavixTV::get_next_byte()
{
	uint8_t dat = 0;
	for (int i = 0; i < 8; i++)
	{
		dat |= (get_next_bit() << i);
	}
	return dat;
}
int XavixTV::get_current_address_byte()
{
	return m_tmp_dataaddress;
}
void XavixTV::palram_sh_w(offs_t offset, uint8_t data)
{
	m_palram_sh[offset] = data;
	update_pen(offset, m_palram_sh[offset], m_palram_l[offset]);
}
void XavixTV::palram_l_w(offs_t offset, uint8_t data)
{
	m_palram_l[offset] = data;
	update_pen(offset, m_palram_sh[offset], m_palram_l[offset]);
}
void XavixTV::bmp_palram_sh_w(offs_t offset, uint8_t data)
{
	m_bmp_palram_sh[offset] = data;
	update_pen(offset+256, m_bmp_palram_sh[offset], m_bmp_palram_l[offset]);
}
void XavixTV::bmp_palram_l_w(offs_t offset, uint8_t data)
{
	m_bmp_palram_l[offset] = data;
	update_pen(offset+256, m_bmp_palram_sh[offset], m_bmp_palram_l[offset]);
}
void XavixTV::spriteram_set_high_x(offs_t offset, uint8_t data)
{
	offset &= 0xff;
	m_fragment_sprite[0x000 + offset] = (m_fragment_sprite[0x000 + offset] & 0xfe) | (data ? 1 : 0);
	m_fragment_sprite[0x400 + offset] = data ? 1 : 0;
}
void XavixTV::spriteram_w(offs_t offset, uint8_t data)
{
	if (offset < 0x100) // 0x000 - 0x0ff
	{
		m_fragment_sprite[offset] = data & 0xfe;
		spriteram_set_high_x(offset, data & 0x01);
	}
	else if (offset < 0x300) // 0x100-0x2ff
	{
		m_fragment_sprite[offset] = data;
	}
	else if (offset < 0x400) // 0x300-0x3ff
	{
		// copy sprite x bit 7 into bit 8 (which is stored at 0x4xx, and can also be seen in the low bit of 0x0xx)
		// pl1000 has explicit codepaths where it doesn't bother to set the 0x400-0x4ff range if it isn't needed
		// and we can't be using old values from there
		m_fragment_sprite[offset] = data;
		spriteram_set_high_x(offset, data & 0x80);
	}
	else if (offset < 0x500) // 0x400-0x4ff
	{
		spriteram_set_high_x(offset, data & 0x01);
	}
	else // 0x500-0x7ff
	{
		m_fragment_sprite[offset] = data;
	}
}
void XavixTV::superxavix_crtc_1_w(offs_t offset, uint8_t data)
{ (void)0;
	m_sx_crtc_1[offset] = data;
}
uint8_t XavixTV::superxavix_crtc_1_r(offs_t offset)
{
	return m_sx_crtc_1[offset];
}
void XavixTV::bitmap_params_w(offs_t offset, uint8_t data)
{ (void)0;
	m_bmp_base[offset] = data;
	// if anything changes these mid-screen we may need to trigger a partial update here
}
uint8_t XavixTV::bitmap_params_r(offs_t offset)
{
	return m_bmp_base[offset];
}
void XavixTV::superxavix_crtc_2_w(offs_t offset, uint8_t data)
{ (void)0;
	m_sx_crtc_2[offset] = data;
}
uint8_t XavixTV::superxavix_crtc_2_r(offs_t offset)
{
	return m_sx_crtc_2[offset];
}
void XavixTV::ext_segment_regs_w(offs_t offset, uint8_t data)
{ (void)0;
	m_ext_segment_regs[offset] = data;
}
void XavixTV::superxavix_plt_flush_w(uint8_t data)
{
	// flush current write buffer (as we might not have filled a byte when plotting)
	// and set write mode?
	// do we need to cache the previous write address for the flush? (probably not, this seems to be explicitly
	// written both after every transfer, and before the next one.
	if (m_plotter_has_byte)
	{
		ext_write((m_sx_plt_address >> 3) & 0x7fffff, m_plotter_current_byte);
	}

	m_plotter_has_byte = 0;
	m_plotter_current_byte = 0x00;
	//printf("%s: superxavix_plt_flush_w %02x\n", "xavix".c_str(), data);
	m_sx_plt_mode = data;
}
uint8_t XavixTV::superxavix_plt_dat_r()
{
	return random_byte();
}
void XavixTV::superxavix_plt_dat_w(uint8_t data)
{
	uint8_t pixels = m_sx_plt_mode + 1;

	while (pixels)
	{
		if (!m_plotter_has_byte)
		{
			m_plotter_current_byte = ext_read((m_sx_plt_address>>3) & 0x7fffff);
			m_plotter_has_byte = 1;
		}

		uint8_t bit_to_write = data & 1;
		data >>= 1;

		uint8_t destbit = m_sx_plt_address & 0x7;
		m_plotter_current_byte &= ~(1 << destbit);
		m_plotter_current_byte |= (bit_to_write << destbit);

		m_sx_plt_address++;
		if (!(m_sx_plt_address & 0x7))
		{
			ext_write(((m_sx_plt_address-1)>>3) & 0x7fffff, m_plotter_current_byte);
			m_plotter_has_byte = 0;
		}

		pixels--;
	}
}
void XavixTV::superxavix_plt_loc_w(offs_t offset, uint8_t data)
{ (void)0;

	m_sx_plt_loc[offset] = data;
	if (offset == 3)
	{
		// Commit the old partial byte before latching a new destination.
		if (m_plotter_has_byte) superxavix_plt_flush_w(m_sx_plt_mode);
		m_sx_plt_address = (m_sx_plt_loc[3] << 24) | (m_sx_plt_loc[2] << 16) | (m_sx_plt_loc[1] << 8) | (m_sx_plt_loc[0] << 0);
 (void)0;
	}
}
uint8_t XavixTV::superxavix_plt_loc_r(offs_t offset)
{
	return m_sx_plt_loc[offset];
}
void XavixTV::superxavix_bitmap_pal_index_w(uint8_t data)
{
	m_superxavix_bitmap_pal_index = data;
}
uint8_t XavixTV::superxavix_bitmap_pal_index_r()
{
	return m_superxavix_bitmap_pal_index;
}
void XavixTV::superxavix_chr_pal_index_w(uint8_t data)
{
	m_superxavix_pal_index = data;
}
uint8_t XavixTV::superxavix_chr_pal_index_r()
{
	return m_superxavix_pal_index;
}
uint8_t XavixTV::superxavix_bitmap_pal_hue_r()
{
	return superxavix_pal_hue_r(true);
}
uint8_t XavixTV::superxavix_chr_pal_hue_r()
{
	return superxavix_pal_hue_r(false);
}
uint8_t XavixTV::superxavix_pal_hue_r(bool bitmap)
{
	u8 *sh, *sl;
	u8 ind;

	if (bitmap)
	{
		sh = m_bmp_palram_sh;
		sl = m_bmp_palram_l;
		ind = m_superxavix_bitmap_pal_index;
	}
	else
	{
		sh = m_palram_sh;
		sl = m_palram_l;
		ind = m_superxavix_pal_index;
	}

	uint16_t dat = (sh[ind]) | (sl[ind] << 8);
	uint16_t val = get_pen_hue_from_dat(dat);
	return val << 2;
}
uint8_t XavixTV::superxavix_bitmap_pal_saturation_r()
{
	return superxavix_pal_saturation_r(true);
}
uint8_t XavixTV::superxavix_chr_pal_saturation_r()
{
	return superxavix_pal_saturation_r(false);
}
uint8_t XavixTV::superxavix_pal_saturation_r(bool bitmap)
{
	u8 *sh, *sl;
	u8 ind;

	if (bitmap)
	{
		sh = m_bmp_palram_sh;
		sl = m_bmp_palram_l;
		ind = m_superxavix_bitmap_pal_index;
	}
	else
	{
		sh = m_palram_sh;
		sl = m_palram_l;
		ind = m_superxavix_pal_index;
	}

	uint16_t dat = (sh[ind]) | (sl[ind] << 8);
	uint16_t val = get_pen_saturation_from_dat(dat);
	return val << 4;
}
uint8_t XavixTV::superxavix_bitmap_pal_lightness_r()
{
	return superxavix_pal_lightness_r(true);
}
uint8_t XavixTV::superxavix_chr_pal_lightness_r()
{
	return superxavix_pal_lightness_r(false);
}
uint8_t XavixTV::superxavix_pal_lightness_r(bool bitmap)
{
	u8 *sh, *sl;
	u8 ind;

	if (bitmap)
	{
		sh = m_bmp_palram_sh;
		sl = m_bmp_palram_l;
		ind = m_superxavix_bitmap_pal_index;
	}
	else
	{
		sh = m_palram_sh;
		sl = m_palram_l;
		ind = m_superxavix_pal_index;
	}

	uint16_t dat = (sh[ind]) | (sl[ind] << 8);
	uint16_t val = get_pen_lightness_from_dat(dat);
	return val << 2;
}
void XavixTV::superxavix_bitmap_pal_hue_w(uint8_t data)
{
	superxavix_pal_hue_w(data, true);
}
void XavixTV::superxavix_chr_pal_hue_w(uint8_t data)
{
	superxavix_pal_hue_w(data, false);
}
void XavixTV::superxavix_pal_hue_w(uint8_t data, bool bitmap)
{
	u8 *sh, *sl;
	u8 ind;
	int offset;

	if (bitmap)
	{
		sh = m_bmp_palram_sh;
		sl = m_bmp_palram_l;
		ind = m_superxavix_bitmap_pal_index;
		offset = 256;
	}
	else
	{
		sh = m_palram_sh;
		sl = m_palram_l;
		ind = m_superxavix_pal_index;
		offset = 0;
	}

	uint16_t olddat = (sh[ind]) | (sl[ind] << 8);
	uint16_t newdata = apply_pen_hue_to_dat(olddat, data >> 2);
	sh[ind] = newdata & 0xff;
	sl[ind] = (newdata >> 8) & 0xff;
	update_pen(ind+offset, sh[ind], sl[ind]);
}
void XavixTV::superxavix_bitmap_pal_saturation_w(uint8_t data)
{
	superxavix_pal_saturation_w(data, true);
}
void XavixTV::superxavix_chr_pal_saturation_w(uint8_t data)
{
	superxavix_pal_saturation_w(data, false);
}
void XavixTV::superxavix_pal_saturation_w(uint8_t data, bool bitmap)
{
	u8 *sh, *sl;
	u8 ind;
	int offset;

	if (bitmap)
	{
		sh = m_bmp_palram_sh;
		sl = m_bmp_palram_l;
		ind = m_superxavix_bitmap_pal_index;
		offset = 256;
	}
	else
	{
		sh = m_palram_sh;
		sl = m_palram_l;
		ind = m_superxavix_pal_index;
		offset = 0;
	}

	uint16_t olddat = (sh[ind]) | (sl[ind] << 8);
	uint16_t newdata = apply_pen_saturation_to_dat(olddat, data >> 4);
	sh[ind] = newdata & 0xff;
	sl[ind] = (newdata >> 8) & 0xff;
	update_pen(ind+offset, sh[ind], sl[ind]);
}
void XavixTV::superxavix_bitmap_pal_lightness_w(uint8_t data)
{
	superxavix_pal_lightness_w(data, true);
}
void XavixTV::superxavix_chr_pal_lightness_w(uint8_t data)
{
	superxavix_pal_lightness_w(data, false);
}
void XavixTV::superxavix_pal_lightness_w(uint8_t data, bool bitmap)
{
	u8 *sh, *sl;
	u8 ind;
	int offset;

	if (bitmap)
	{
		sh = m_bmp_palram_sh;
		sl = m_bmp_palram_l;
		ind = m_superxavix_bitmap_pal_index;
		offset = 256;
	}
	else
	{
		sh = m_palram_sh;
		sl = m_palram_l;
		ind = m_superxavix_pal_index;
		offset = 0;
	}

	uint16_t olddat = (sh[ind]) | (sl[ind] << 8);
	uint16_t newdata = apply_pen_lightness_to_dat(olddat, data >> 2);
	sh[ind] = newdata & 0xff;
	sl[ind] = (newdata >> 8) & 0xff;
	update_pen(ind+offset, sh[ind], sl[ind]);
}
uint16_t XavixTV::apply_pen_lightness_to_dat(uint16_t dat, uint16_t lightness)
{
	lightness &= 0x3f;
	dat &= ~0x1f00;
	dat &= ~0x8000;
	dat |= (lightness & 0x3e) << 7;
	dat |= (lightness & 0x01) << 15;
	return dat;
}
uint16_t XavixTV::apply_pen_saturation_to_dat(uint16_t dat, uint16_t saturation)
{
	saturation &= 0x0f;
	dat &= ~0x00e0;
	dat &= ~0x4000;
	dat |= (saturation & 0x0e) << 4;
	dat |= (saturation & 0x01) << 14;
	return dat;
}
uint16_t XavixTV::apply_pen_hue_to_dat(uint16_t dat, uint16_t hue)
{
	hue &= 0x3f;
	dat &= ~0x001f;
	dat &= ~0x2000;
	dat |= (hue & 0x3e) >> 1;
	dat |= (hue & 0x01) << 13;
	return dat;
}
uint8_t XavixTV::get_pen_lightness_from_dat(uint16_t dat)
{
	uint8_t y_raw = (dat & 0x1f00) >> 7;
	y_raw |= (dat & 0x8000) >> 15; // currently we don't make use of this bit
	return y_raw;
}
uint8_t XavixTV::get_pen_saturation_from_dat(uint16_t dat)
{
	uint8_t c_raw = (dat & 0x00e0) >> 4;
	c_raw |= (dat & 0x4000) >> 14; // currently we don't make use of this bit
	return c_raw;
}
uint8_t XavixTV::get_pen_hue_from_dat(uint16_t dat)
{
	uint8_t h_raw = (dat & 0x001f) << 1;
	h_raw |= (dat & 0x2000) >> 13; // currently we don't make use of this bit
	return h_raw;
}
void XavixTV::update_pen(int pen, uint8_t shval, uint8_t lval)
{
	uint16_t dat;
	dat = shval;
	dat |= lval << 8;

	int y_raw = get_pen_lightness_from_dat(dat) >> 1;
	int c_raw = get_pen_saturation_from_dat(dat) >> 1;
	int h_raw = get_pen_hue_from_dat(dat) >> 1;

	// The dividers may be dynamic
	double y = y_raw / 20.0;
	double c = c_raw /  5.0;

	// These weights may be dynamic too.  They're standard NTSC values, would they change on PAL?
	const double wr = 0.299;
	const double wg = 0.587;
	const double wb = 0.114;

	// Table of hues
	// Values 24+ are transparent

	const double hues[32][3] = {
		{ 1.00, 0.00, 0.00 },
		{ 1.00, 0.25, 0.00 },
		{ 1.00, 0.50, 0.00 },
		{ 1.00, 0.75, 0.00 },
		{ 1.00, 1.00, 0.00 },
		{ 0.75, 1.00, 0.00 },
		{ 0.50, 1.00, 0.00 },
		{ 0.25, 1.00, 0.00 },
		{ 0.00, 1.00, 0.00 },
		{ 0.00, 1.00, 0.25 },
		{ 0.00, 1.00, 0.50 },
		{ 0.00, 1.00, 0.75 },
		{ 0.00, 1.00, 1.00 },
		{ 0.00, 0.75, 1.00 },
		{ 0.00, 0.50, 1.00 },
		{ 0.00, 0.25, 1.00 },
		{ 0.00, 0.00, 1.00 },
		{ 0.25, 0.00, 1.00 },
		{ 0.50, 0.00, 1.00 },
		{ 0.75, 0.00, 1.00 },
		{ 1.00, 0.00, 1.00 },
		{ 1.00, 0.00, 0.75 },
		{ 1.00, 0.00, 0.50 },
		{ 1.00, 0.00, 0.25 },

		{ 0   , 0   , 0    },
		{ 0   , 0   , 0    },
		{ 0   , 0   , 0    },
		{ 0   , 0   , 0    },
		{ 0   , 0   , 0    },
		{ 0   , 0   , 0    },
		{ 0   , 0   , 0    },
		{ 0   , 0   , 0    },
	};

	double r0 = hues[h_raw][0];
	double g0 = hues[h_raw][1];
	double b0 = hues[h_raw][2];

	double z = wr * r0 + wg * g0 + wb * b0;

	if(y < z)
		c *= y/z;
	else if(z < 1)
		c *= (1-y) / (1-z);

	double r1 = (r0 - z) * c + y;
	double g1 = (g0 - z) * c + y;
	double b1 = (b0 - z) * c + y;


	// lower overall brightness slightly, or some palette entries in tak_gin end up washed out / with identical colours, losing details
	// the darkest colours in certain flags still look wrong however, and appear nearly black
	// this might suggest the overall palette conversion needs work
	r1 = r1 * 0.92f;
	g1 = g1 * 0.92f;
	b1 = b1 * 0.92f;

	if(r1 < 0)
		r1 = 0;
	else if(r1 > 1)
		r1 = 1.0;

	if(g1 < 0)
		g1 = 0;
	else if(g1 > 1)
		g1 = 1.0;

	if(b1 < 0)
		b1 = 0;
	else if(b1 > 1)
		b1 = 1.0;

	int r_real = r1 * 255.0f;
	int g_real = g1 * 255.0f;
	int b_real = b1 * 255.0f;

	m_palette->set_pen_color(pen, r_real, g_real, b_real);

	m_screen->update_partial(m_screen->vpos());
}
void XavixTV::draw_tilemap(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect, int which)
{
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		draw_tilemap_line(screen, bitmap, cliprect, which, y);
	}
}
void XavixTV::decode_inline_header(int &flipx, int &flipy, int &test, int &pal, int debug_packets)
{
	uint8_t byte1 = 0;
	int done = 0;

	flipx = 0;
	flipy = 0;
	test = 0;

	int first = 1;

	do
	{
		byte1 = get_next_byte();

		// only the first byte matters when it comes to setting palette / flips, the rest are just ignored until we reach a 0x6 command, after which there is the tile data
		if (first == 1)
		{
			pal = (byte1 & 0xf0) >> 4;
			int cmd = (byte1 & 0x0f);

			switch (cmd)
			{
			// these cases haven't been seen
			case 0x0:
			case 0x2:
			case 0x4:
			case 0x8:
			case 0xa:
			case 0xc:
			case 0xe:

			// this is just the end command, changes nothing, can be pointed at directly tho
			case 0x6:
				break;

			// flip cases
			// does bit 0x02 have a meaning here, we have 2 values for each case

			case 0x1:
			case 0x3:
				flipx = 0; flipy = 0;
				break;

			case 0x5:
			case 0x7:
				flipx = 1; flipy = 0;
				break;

			case 0x9:
			case 0xb:
				flipx = 0; flipy = 1;
				break;

			case 0xd:
			case 0xf:
				flipx = 1; flipy = 1;
				break;
			}

			first = 0;
		}

		if ((byte1 & 0x0f) == 0x06)
		{
			// tile data will follow after this, always?
			done = 1;
			//if (debug_packets) LOG(" (setting palette)");
		}
	} while (done == 0);
	//if (debug_packets) LOG("\n");
}
void XavixTV::draw_tilemap_line(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect, int which, int line)
{
	uint8_t* tileregs;
	if (which == 0)
	{
		tileregs = m_tmap1_regs;
	}
	else
	{
		tileregs = m_tmap2_regs;
	}

	// bail if tilemap is disabled
	if (!(tileregs[0x7] & 0x80))
		return;

	int use_inline_header = 0;
	int alt_tileaddressing2 = 0;

	int ydimension = 0;
	int xdimension = 0;
	int ytilesize = 0;
	int xtilesize = 0;
	int yshift = 0;

	switch (tileregs[0x3] & 0x30)
	{
	case 0x00:
		ydimension = 32;
		xdimension = 32;
		ytilesize = 8;
		xtilesize = 8;
		yshift = 3;
		break;

	case 0x10:
		ydimension = 32;
		xdimension = 16;
		ytilesize = 8;
		xtilesize = 16;
		yshift = 3;
		break;

	case 0x20: // guess
		ydimension = 16;
		xdimension = 32;
		ytilesize = 16;
		xtilesize = 8;
		yshift = 4;
		break;

	case 0x30:
		ydimension = 16;
		xdimension = 16;
		ytilesize = 16;
		xtilesize = 16;
		yshift = 4;
		break;
	}

	if (tileregs[0x7] & 0x10)
		use_inline_header = 1;
	else
		use_inline_header = 0;

	if (tileregs[0x7] & 0x02)
		alt_tileaddressing2 = 1;

	if ((tileregs[0x7] & 0x7f) == 0x04)
		alt_tileaddressing2 = 2;

	// SuperXaviX only?
	if ((tileregs[0x7] & 0x7f) == 0x0d)
		alt_tileaddressing2 = 3;

	//LOG("draw tilemap %d, regs base0 %02x base1 %02x base2 %02x tilesize,bpp %02x scrollx %02x scrolly %02x pal %02x mode %02x\n", which, tileregs[0x0], tileregs[0x1], tileregs[0x2], tileregs[0x3], tileregs[0x4], tileregs[0x5], tileregs[0x6], tileregs[0x7]);

	// there's a tilemap register to specify base in main ram, although in the monster truck test mode it points to an unmapped region
	// and expected a fixed layout, we handle that in the memory map at the moment

	int drawline = line;
	int scrolly = tileregs[0x5];

	drawline += scrolly;

	drawline &= ((ydimension * ytilesize) - 1);

	int y = drawline >> yshift;
	int yyline = drawline & (ytilesize - 1);

	for (int x = 0; x < xdimension; x++)
	{
		int count = (y * xdimension) + x;

		int tile = 0;

		// the register being 0 probably isn't the condition here
		if (tileregs[0x0] != 0x00)
		{
			const offs_t realaddress = (tileregs[0x0] << 8) + count;
			if (m_disable_memory_bypass)
				tile |= opcode_read(realaddress);
			else
				tile |= read_full_data_sp_bypass(realaddress);
		}

		// only read the next byte if we're not in an 8-bit mode
		if (((tileregs[0x7] & 0x7f) != 0x00) && ((tileregs[0x7] & 0x7f) != 0x08))
		{
			const offs_t realaddress = (tileregs[0x1] << 8) + count;
			if (m_disable_memory_bypass)
				tile |= opcode_read(realaddress) << 8;
			else
				tile |= read_full_data_sp_bypass(realaddress) << 8;
		}

		// 24 bit modes can use reg 0x2, otherwise it gets used as extra attribute in other modes
		if (alt_tileaddressing2 == 2)
		{
			const offs_t realaddress = (tileregs[0x2] << 8) + count;
			if (m_disable_memory_bypass)
				tile |= opcode_read(realaddress) << 16;
			else
				tile |= read_full_data_sp_bypass(realaddress) << 16;
		}


		int bpp = (tileregs[0x3] & 0x0e) >> 1;
		bpp++;
		int pal = (tileregs[0x6] & 0xf0) >> 4;
		int zval = (tileregs[0x6] & 0x0f) >> 0;
		int scrollx = tileregs[0x4];

		int basereg;
		int flipx = (tileregs[0x03]&0x40)>>6;
		int flipy = (tileregs[0x03]&0x80)>>7;

		// epo_doka explicitly sets these registers on the XaviX logo
		// but expects no flipping.  Is code being executed out of order or
		// is this further evidence that they don't work as expected on SuperXaviX
		// hardware (see other hack for xavmusic needing sprite flip disabled)
		if (m_disable_tile_regs_flip)
		{
			flipx = flipy = 0;
		}

		int gfxbase;

		// tile 0 is always skipped, doesn't even point to valid data packets in alt mode
		// this is consistent with the top layer too
		// should we draw as solid in solid layer?
		if (tile == 0)
		{
			continue;
		}

		int debug_packets = 1;
		//if (line==128) debug_packets = 1;
		//else debug_packets = 0;

		int test = 0;

		if (!use_inline_header)
		{
			if (alt_tileaddressing2 == 0)
			{
				// Tile Based Addressing takes into account Tile Sizes and bpp
				const int offset_multiplier = (ytilesize * xtilesize) / 8;

				basereg = 0;
				gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);

				tile = tile * (offset_multiplier * bpp);
				tile += gfxbase;
			}
			else if (alt_tileaddressing2 == 1)
			{
				if (tileregs[0x7] & 0x01)
				{
					// only epo_stad has been seen using this mode (box behind dialog after you allow multiple strikes against you)
					basereg = (tile & 0xf000) >> 12;
					tile = tile & 0x0fff;
					gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);
					tile += gfxbase;
				}
				else
				{
					// 8-byte alignment Addressing Mode uses a fixed offset? (like sprites)
					basereg = (tile & 0xe000) >> 13;
					tile &= 0x1fff;
					tile = tile * 8;
					gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);
					tile += gfxbase;
				}
			}
			else if (alt_tileaddressing2 == 3)
			{
				// SuperXaviX only?
				if (m_ext_segment_regs)
				{
					// Tile Based Addressing takes into account Tile Sizes and bpp
					const int offset_multiplier = (ytilesize * xtilesize) / 8;
					const int basereg = 0;
					gfxbase = (m_ext_segment_regs[(basereg * 4) + 3] << 24) |
						(m_ext_segment_regs[(basereg * 4) + 2] << 16) |
						(m_ext_segment_regs[(basereg * 4) + 1] << 8) |
						(m_ext_segment_regs[(basereg * 4) + 0] << 0);

					tile = tile * (offset_multiplier * bpp);
					tile += gfxbase;
				}
				else
				{
					tile = 0;
				}
			}

			// Tilemap specific mode extension with an 8-bit per tile attribute, works in all modes except 24-bit (no room for attribute) and header (not needed?)
			if (tileregs[0x7] & 0x08)
			{
				const offs_t realaddress = (tileregs[0x2] << 8) + count;
				uint8_t extraattr;

				if (m_disable_memory_bypass)
					extraattr = opcode_read(realaddress);
				else
					extraattr = read_full_data_sp_bypass(realaddress);

				// make use of the extraattr stuff?
				pal = (extraattr & 0xf0) >> 4;
				zval = (extraattr & 0x0f) >> 0;
			}
		}
		else
		{
			// Addressing Mode 2 (plus Inline Header)

			//if (debug_packets) LOG("for tile %04x (at %d %d): ", tile, (((x * 16) + scrollx) & 0xff), (((y * 16) + scrolly) & 0xff));
			if (tileregs[0x7] == 0x94)
			{
				// multiplt uses this for the '3d' driving sections
				const offs_t realaddress = (tileregs[0x2] << 8) + count;
				uint8_t extraattr;

				if (m_disable_memory_bypass)
					extraattr = opcode_read(realaddress);
				else
					extraattr = read_full_data_sp_bypass(realaddress);

				tile += extraattr << 16;
			}
			else // tileregs[0x7] == 0x93
			{
				// rad_mtrk uses this
				basereg = (tile & 0xf000) >> 12;
				tile &= 0x0fff;
				gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);

				tile += gfxbase;
			}

			set_data_address(tile, 0);

			decode_inline_header(flipx, flipy, test, pal, debug_packets);

			tile = get_current_address_byte();
		}

		if (test == 1)
		{
			pal = random_byte() & 0xf;
		}

		draw_tile_line(screen, bitmap, cliprect, tile, bpp, (x * xtilesize) + scrollx, line, ytilesize, xtilesize, flipx, flipy, pal, zval, yyline);
		draw_tile_line(screen, bitmap, cliprect, tile, bpp, ((x * xtilesize) + scrollx) - 256, line, ytilesize, xtilesize, flipx, flipy, pal, zval, yyline); // wrap-x
	}
}
void XavixTV::draw_sprites(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		draw_sprites_line(screen, bitmap, cliprect, y);
	}
}
void XavixTV::draw_sprites_line(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect, int line)
{
	int alt_addressing = 0;

	// some games have the top bit set, why?
	m_spritereg &= 0x7f;

	if ((m_spritereg == 0x00) || (m_spritereg == 0x01) || (m_spritereg == 0x05))
	{
		// 8-bit addressing  (Tile Number)
		// 16-bit addressing (Tile Number) (rad_rh)

		// xavgolf uses 0x05, how does it differ?
		alt_addressing = 1;
	}
	else if (m_spritereg == 0x02)
	{
		// 16-bit addressing (8-byte alignment Addressing Mode)
		alt_addressing = 2;
	}
	else if (m_spritereg == 0x04)
	{
		// 24-bit addressing (Addressing Mode 2)
		alt_addressing = 0;
	}
	else if (m_spritereg == 0x07)
	{
		// used by anpanmdx - is this a specific 24-bit mode to enable SuperXaviX extra ROM access?
		alt_addressing = 3;
	}
	else
	{
		popmessage("unknown sprite reg %02x", m_spritereg);
	}


	//LOG("frame\n");
	// priority doesn't seem to be based on list order, there are bad sprite-sprite priorities with either forward or reverse

	for (int i = 0xff; i >= 0; i--)
	{

		uint8_t* spr_attr0 = m_fragment_sprite + 0x000;
		uint8_t* spr_attr1 = m_fragment_sprite + 0x100;
		uint8_t* spr_ypos = m_fragment_sprite + 0x200;
		uint8_t* spr_xpos = m_fragment_sprite + 0x300;
		uint8_t* spr_xposh = m_fragment_sprite + 0x400;
		uint8_t* spr_addr_lo = m_fragment_sprite + 0x500;
		uint8_t* spr_addr_md = m_fragment_sprite + 0x600;
		uint8_t* spr_addr_hi = m_fragment_sprite + 0x700;


		/* attribute 0 bits
		   pppp bbb-    p = palette, b = bpp

		   attribute 1 bits
		   zzzz ssFf    s = size, F = flipy f = flipx
		*/

		int ypos = spr_ypos[i];
		int xpos = spr_xpos[i];
		int tile = 0;

		// high 8-bits only used in 24-bit modes
		if (((m_spritereg & 0x7f) == 0x04) || ((m_spritereg & 0x7f) == 0x07) || ((m_spritereg & 0x7f) == 0x15))
			tile |= (spr_addr_hi[i] << 16);

		// mid 8-bits used in everything except 8-bit mode
		if ((m_spritereg & 0x7f) != 0x00)
			tile |= (spr_addr_md[i] << 8);

		// low 8-bits always read
		tile |= spr_addr_lo[i];

		int attr0 = spr_attr0[i];
		int attr1 = spr_attr1[i];

		int pal = (attr0 & 0xf0) >> 4;

		int zval = (attr1 & 0xf0) >> 4;
		int flipx = (attr1 & 0x01);
		int flipy = (attr1 & 0x02);

		// many elements, including the XaviX logo on xavmusic have yflip set, but don't want it, why?
		if (m_disable_sprite_yflip)
			flipy = 0;

		// many elements in xavmusic and xavjmat also require xflip to be disabled, maybe sprite flipping
		// is broken on SuperXaviX?
		//
		// non-super games, such as Gururuin world, use sprite mode 4, and expect flipping to work
		// while the SuperXaviX games use a mix of mode 4 and mode 7 and don't want it
		if (m_disable_sprite_xflip)
			flipx = 0;

		int drawheight = 16;
		int drawwidth = 16;

		int xpos_adjust = 0;
		int ypos_adjust = -16;

		// taito nost attr1 is 84 / 80 / 88 / 8c for the various elements of the xavix logo.  monster truck uses ec / fc / dc / 4c / 5c / 6c (final 6 sprites ingame are 00 00 f0 f0 f0 f0, radar?)

		drawheight = 8;
		drawwidth = 8;

		if (attr1 & 0x04) drawwidth = 16;
		if (attr1 & 0x08) drawheight = 16;

		xpos_adjust = -(drawwidth/2);
		ypos_adjust = -(drawheight/2);

		ypos ^= 0xff;

		if (ypos & 0x80)
		{
			ypos = -0x80 + (ypos & 0x7f);
		}
		else
		{
			ypos &= 0x7f;
		}

		ypos += 128 + 1;

		ypos += ypos_adjust;

		int spritelowy = ypos;
		int spritehighy = ypos + drawheight;

		if ((line >= spritelowy) && (line < spritehighy))
		{
			int drawline = line - spritelowy;

			// There's also an oddity with pl1000, where you can shoot cars that have
			// wrapped around to the edge before they vanish, previously these were
			// visible (incorrectly) now they are not visible, but you can still shoot
			// them (maybe BTNAB)

			int xposh = spr_xposh[i] & 1;

			xpos |= xposh << 8;
			xpos ^= 0x100;
			xpos += xpos_adjust;
			xpos -= 0x80;

			int bpp = 1;

			bpp = (attr0 & 0x0e) >> 1;
			bpp += 1;

			// Everything except directdirect addressing (Addressing Mode 2) goes through the segment registers?
			if (alt_addressing != 0)
			{
				int basereg = 0;

				// tile based addressing takes into account tile size (and bpp?)
				if (alt_addressing == 1)
				{
					tile = (tile * drawheight * drawwidth * bpp) / 8;
					int gfxbase = (m_segment_regs[1] << 16) | (m_segment_regs[0] << 8); // always use segment 0
					tile += gfxbase;

					// ban_bkgj is in sprite mode 0x01 but seems to expect the extended SuperXaviX registers to apply too?
					//
					// Is the register being set correctly? I'd expect it to be a different value to enable this. It does
					// briefly set mode 4 and 5 on the startup logos, where it *doesn't* need this logic
					if (m_ext_segment_regs)
					{
						int gfxbase = (m_ext_segment_regs[3] << 24) |
									  (m_ext_segment_regs[2] << 16) |
									  (m_ext_segment_regs[1] << 8) |
									  (m_ext_segment_regs[0] << 0);
						tile += gfxbase;
					}


				}
				else if (alt_addressing == 2)
				{
					tile = tile * 8;
					basereg = (tile & 0xf0000) >> 16;
					tile &= 0xffff;
					int gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);
					tile += gfxbase;
				}
				else if (alt_addressing == 3)
				{
					// 24-bit, with multiplier, no segment use?
					// seen in anpanmdx, might be superxavix specific to be able to access more memory?
					tile = tile * 8;
				}
				else // currently unused case
				{
					basereg = (tile & 0xf0000) >> 16;
					tile &= 0xffff;
					int gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);
					tile += gfxbase;
				}

			}

			draw_tile_line(screen, bitmap, cliprect, tile, bpp, xpos , line, drawheight, drawwidth, flipx, flipy, pal, zval, drawline);

			/*
			if ((spr_ypos[i] != 0x81) && (spr_ypos[i] != 0x80) && (spr_ypos[i] != 0x00))
			{
 (void)0;
			}
			*/
		}
	}
}
void XavixTV::get_tile_pixel_dat(uint8_t &dat, int bpp)
{
	dat |= graphics_bits(bpp,super);
}
void XavixTV::draw_tile_line(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect, int tile, int bpp, int xpos, int ypos, int drawheight, int drawwidth, int flipx, int flipy, int pal, int zval, int line)
{
	if (!cliprect.containsy(ypos))
		return;

	if (!cliprect.overlapsx(xpos * m_video_hres_multiplier, ((xpos + drawwidth) * m_video_hres_multiplier) - 1))
		return;

	const pen_t *paldata = m_palette->pens();

	// if bpp>4 then ignore unaligned palette selects bits based on bpp
	// ttv_lotr uses 5bpp graphics (so 32 colour alignment) but sets palette 0xf (a 16 colour boundary) when it expects palette 0xe
	if (bpp>4)
		pal &= (0xf<<(bpp-4));

	int bits_per_tileline = drawwidth * bpp;

	// set the address here so we can increment in bits in the draw function
	set_data_address(tile, 0);

	if (flipy)
		line = (drawheight - 1) - line;

	m_tmp_dataaddress = m_tmp_dataaddress + ((line * bits_per_tileline) / 8);
	m_tmp_databit = (line * bits_per_tileline) % 8;

	for (int x = 0; x < drawwidth; x++)
	{
		int col;
		if (flipx)
		{
			col = xpos + (drawwidth - 1) - x;
		}
		else
		{
			col = xpos + x;
		}

		uint8_t dat = 0;

		get_tile_pixel_dat(dat, bpp);

		col = col * m_video_hres_multiplier;

		// High-resolution bitmap pixels have independent priorities beneath doubled tiles.
		for (int pixel = col; pixel < col + m_video_hres_multiplier; pixel++)
		{
			if (!cliprect.containsx(pixel)) continue;
			uint16_t *const zyposptr = &m_zbuffer.pix(ypos);
			if (zval < zyposptr[pixel]) continue;
			int pen = (dat + (pal << 4)) & 0xff;
			if ((m_palram_sh[pen] & 0x1f) < 24)
			{
				bitmap.pix(ypos, pixel) = paldata[pen];
				zyposptr[pixel] = zval;
				if (regular_coverage) regular_coverage[ypos*512+pixel] = 1;
			}
		}
	}
}
rectangle XavixTV::do_arena(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	const pen_t *paldata = m_palette->pens();
	bitmap.fill(m_palette->black_pen(), cliprect);

	rectangle clip = cliprect;

	if (m_arena_control & 0x01)
	{
		/* Controls the clipping area (for all layers?) used for effect at start of Slap Fight and to add black borders in other cases
		   based on Slap Fight Tiger lives display (and reference videos) this is slightly offset as all status bar gfx must display
		   Monster Truck black bars are wider on the right hand side, but this matches with the area in which the tilemap is incorrectly rendered so seems to be intentional
		   Snowboard hides garbage sprites on the right hand side with this, confirming the right hand side offset
		   Taito Nostalgia 1 'Gladiator' portraits in demo mode are cut slightly due to the area specified, again the cut-off points for left and right are confirmed as correct on hardware

		   some games enable it with both regs as 00, which causes a problem, likewise ping pong sets both to 0xff
		   but Slap Fight Tiger has both set to 0x82 at a time when everything should be clipped
		*/
		if (((m_arena_start != 0x00) && (m_arena_end != 0x00)) && ((m_arena_start != 0xff) && (m_arena_end != 0xff)))
		{
			clip.setx(
					(m_arena_end - 2) * m_video_hres_multiplier, // must be -2 to render a single pixel line of the left border on Mappy remix (verified to render), although this creates a single pixel gap on the left of snowboarder status bar (need to verify)
					(m_arena_start - 3) * m_video_hres_multiplier); // must be -3 to hide garbage on the right hand side of snowboarder

			clip &= cliprect;
		}
	}

	bitmap.fill(paldata[0], clip);
	m_zbuffer.fill(0, clip);

	return clip;
}
void XavixTV::draw_regular_layers(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &clip)
{
	draw_tilemap(screen, bitmap, clip, 0);
	draw_tilemap(screen, bitmap, clip, 1); // epo_golf requires this layer in front when priorities are equal or menu doesn't show?
	draw_sprites(screen, bitmap, clip);
}
uint32_t XavixTV::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
 rectangle clip = do_arena(screen, bitmap, cliprect);
 if (super) draw_bitmap_layer(screen, bitmap, clip);
 draw_regular_layers(screen, bitmap, clip);
 return 0;
}
void XavixTV::draw_bitmap_layer(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	const pen_t *paldata = m_palette->pens();
	// incomplete!
	if (m_bmp_base)
	{
		// screen position for the bitmap?
		uint16_t top = ((m_bmp_base[0x01] << 8) | m_bmp_base[0x00]);
		uint16_t bot = ((m_bmp_base[0x03] << 8) | m_bmp_base[0x02]);
		uint16_t lft = ((m_bmp_base[0x05] << 8) | m_bmp_base[0x04]);
		uint16_t rgt = ((m_bmp_base[0x07] << 8) | m_bmp_base[0x06]);

		// and can specify base address relative start / end positions with these for data reading to be cut off?
		uint16_t topadr = ((m_bmp_base[0x09] << 8) | m_bmp_base[0x08]);
		uint16_t botadr = ((m_bmp_base[0x0b] << 8) | m_bmp_base[0x0a]);
		uint16_t lftadr = ((m_bmp_base[0x0d] << 8) | m_bmp_base[0x0c]);
		uint16_t rgtadr = ((m_bmp_base[0x0f] << 8) | m_bmp_base[0x0e]);

		uint16_t start = ((m_bmp_base[0x11] << 8) | m_bmp_base[0x10]);
		uint8_t step = m_bmp_base[0x12]; // step value to next line base
		uint8_t size = m_bmp_base[0x13]; // some kind of additional scaling?
		uint8_t mode = m_bmp_base[0x14]; // enable,bpp, zval etc.

		uint32_t unused = ((m_bmp_base[0x15] << 16) | (m_bmp_base[0x16] << 8) | (m_bmp_base[0x17] << 0));

		if (mode & 0x01)
		{
			if (0)
			{
				popmessage("bitmap t:%04x b:%04x l:%04x r:%04x  -- -- ta: %04x ba:%04x la:%04x ra:%04x   -- -- start %04x (%08x) step:%02x - size:%02x unused:%08x\n",
					top, bot, lft, rgt,
					topadr, botadr, lftadr, rgtadr,
					start, start * 0x800, step, size, unused);
			}

			// xavgolf title is 308c (start) needs to be 188c
			// xavgolf menu  is 30fc (start) needs to be 18fc

			// xavbaseb xavix logo is 1500
			//          title      is 14e0

			// 0x800 (offset multiplier) * 0x1000 = 0x800000 (24 bit address - usual xavix address space)
			// 0x2000 being set implies address beyond 24-bit space = extended xavix modes

			// anpanmdx title screen ends up with a seemingly incorrect value for start
			// when it does the scroller.
			// It has values of 0x7xxx which has bits 0x2000 and 0x4000 set, pushing the addressing beyond
			// even double the usual XaviX address space)
			//
			// maybe addresses above 24-bit present the ROM data to the system in an unusual order
			// (this might also apply to sprites)

			// we currently detect this in get_next_bit_sx instead
			/*
			if (m_extra)
			{
			    if (start & 0x4000)
			    {
			        start &= 0x3fff;
			        start ^= 0xc00;
			    }
			}
			*/

			int base = start * 0x800;
			int base2 = topadr * 0x8;

			int bpp = ((mode & 0x0e) >> 1) + 1;

			// Select adjacent source rows for the two output row phases.
			int rowbytes = ((rgt - lft) * bpp + 7) / 8;
			if (super && rowbytes > 0 && step * 8 == rowbytes * 2)
			{
				if (base2 % (rowbytes * 2) == rowbytes) base2 -= rowbytes;
				// The bitmap origin falls between fields: output row zero starts
				// at stored row one, then continues into the next row pair.
				base2 += (xavix_output_row_phase + 1) * rowbytes;
			}
			int zval = ((mode & 0xf0) >> 4);

			int hposadjust, vpostadjust;
			bool is_highres = true;

			// SuperXaviX seems to have CRTC registers (0x6f80 - 0x6faf area), so in reality this probably
			// comes from there due to different overall screen positioning; might also depend on bitmap
			// bpp mode (so this is probably not the correct way to detect this)
			// there could also be XaviX2000+ opcode bugs causing wrong register values!
			if ((size & 0x70) == 0x70)
				is_highres = false;

			if (is_highres)
				hposadjust = 0x90; // good for xavtenni, xavbowl, tmy_thom, tmy_rkmj, but not xavbaseb
			else
				hposadjust = 0x46; // good for udance, ban_ordj, not quite correct for ban_kksj in demo

			vpostadjust = super ? 0x05 : 0x06; // Super TV-PC bitmap text aligns with its tilemap windows.

			top += vpostadjust;
			bot += vpostadjust;

			for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
			{
				int line = y - top;

				if ((line > 0) && (y < bot - 1)) // top and bottom edges are outside the active bitmap
				{
					set_data_address(base + base2 + ((line * (step * 64)) / 8), 0);

					// also needs to take into account 'lftadr' address from the line base
					// and clip against 'rgtadr' address if reads go beyond linebase + that for the line
					bitmap_row(bitmap, cliprect, y, lft-hposadjust, rgt-hposadjust, bpp, zval, is_highres?1:2);
				}
			}

		}
	}
}
void XavixTV::spritefragment_dma_params_1_w(offs_t offset, uint8_t data)
{
	m_spritefragment_dmaparam1[offset] = data;
}
void XavixTV::spritefragment_dma_params_2_w(offs_t offset, uint8_t data)
{
	m_spritefragment_dmaparam2[offset] = data;
}
void XavixTV::spritefragment_dma_trg_w(uint8_t data)
{
	uint16_t len = data & 0x07;
	uint16_t src = (m_spritefragment_dmaparam1[1] << 8) | m_spritefragment_dmaparam1[0];
	uint16_t dst = (m_spritefragment_dmaparam2[0] << 8);

	uint8_t src_block_size = m_spritefragment_dmaparam2[1];
 (void)0;

	src_block_size--; // 0x00 is maximum

	if (len == 0x00)
	{
		len = 0x08;
 (void)0;
	}

	len = len << 8;

	if (data & 0x40)
	{
		int readaddress = 0;

		for (int i = 0; i < len; i++)
		{
			// tak_chq explicitly sets unk & 0x80
			// and the source data is in 0x80 byte blocks, not 0x100
			// epo_doka uses 0xb4, which seems to confirm this
			if ((i & 0xff) > src_block_size)
			{
				// do nothing, maybe unk is the length of each section to copy?
			}
			else
			{
				//uint8_t dat = m_maincpu->read_full_data_sp(src + i);
				const offs_t realaddress = src + readaddress;
				uint8_t dat;
				if (m_disable_memory_bypass)
					dat = opcode_read(realaddress);
				else
					dat = read_full_data_sp_bypass(realaddress);
				//m_fragment_sprite[(dst + i) & 0x7ff] = dat;
				spriteram_w((dst + i) & 0x7ff, dat);
				readaddress++;
			}
		}
	}
}
uint8_t XavixTV::spritefragment_dma_status_r()
{
	// expects bit 0x40 to clear in most cases
	return 0x00;
}
uint8_t XavixTV::pal_ntsc_r()
{
	// only seen 0x10 checked in code
	// in monster truck the tile base address gets set based on this, there are 2 copies of the test screen in rom, one for pal, one for ntsc, see 1854c
	// likewise card night has entirely different tilesets for each region title
	return 0;
}
void XavixTV::tmap1_regs_w(offs_t offset, uint8_t data, uint8_t mem_mask)
{
	/*
	   0x0 pointer to low tile bits
	   0x1 pointer to middle tile bits
	   0x2 pointer to upper tile bits

	   0x3 Fftt bbb-  Ff = flip Y,X
	                  tt = tile/tilemap size
	                  b = bpp
	                  - = unused

	   0x4 scroll
	   0x5 scroll

	   0x6 pppp zzzz  p = palette
	                  z = priority

	   0x7 e--m mmmm  e = enable
	                  m = mode

	   modes are
	    ---0 0000 (00) 8-bit addressing  (Tile Number)
	    ---0 0001 (01) 16-bit addressing (Tile Number) (monster truck, ekara)
	    ---0 0010 (02) 16-bit addressing (8-byte alignment Addressing Mode) (boxing)
	    ---0 0011 (03) 16-bit addressing (Addressing Mode 2)
	    ---0 0100 (04) 24-bit addressing (Addressing Mode 2) (epo_efdx)

	    ---0 1000 (08) 8-bit+8 addressing  (Tile Number + 8-bit Attribute)
	    ---0 1001 (09) 16-bit+8 addressing (Tile Number + 8-bit Attribute) (Taito Nostalgia 2)
	    ---0 1010 (0a) 16-bit+8 addressing (8-byte alignment Addressing Mode + 8-bit Attribute) (boxing, Snowboard)
	    ---0 1011 (0b) 16-bit+8 addressing (Addressing Mode 2 + 8-bit Attribute)

	    ---0 1100 (0d) ban_bkgj - seems to be a SuperXaviX only mode using the extended 32-bit segment regs

	    ---1 0011 (13) 16-bit addressing (Addressing Mode 2 + Inline Header)  (monster truck)
	    ---1 0100 (14) 24-bit addressing (Addressing Mode 2 + Inline Header)

	*/

	if ((offset != 0x4) && (offset != 0x5))
	{
 (void)0;
	}

	m_tmap1_regs[offset] = (m_tmap1_regs[offset] & ~mem_mask) | (data & mem_mask);
}
void XavixTV::tmap2_regs_w(offs_t offset, uint8_t data, uint8_t mem_mask)
{
	// same as above but for 2nd tilemap
	if ((offset != 0x4) && (offset != 0x5))
	{
 (void)0;
	}

	m_tmap2_regs[offset] = (m_tmap2_regs[offset] & ~mem_mask) | (data & mem_mask);
}
uint8_t XavixTV::spriteregs_r()
{
	// xavjmat relies on being able to read this back or the sprite addresses
	// in some of the stages get calculated incorrectly
	return m_spritereg;
}
void XavixTV::spriteregs_w(uint8_t data)
{ (void)0;
	/*
	    This is similar to Tilemap reg 7 and is used to set the addressing mode for sprite data

	    ---0 -000 (00) 8-bit addressing  (Tile Number)
	    ---0 -001 (01) 16-bit addressing (Tile Number)
	    ---0 -010 (02) 16-bit addressing (8-byte alignment Addressing Mode)
	    ---0 -011 (03) 16-bit addressing (Addressing Mode 2)
	    ---0 -100 (04) 24-bit addressing (Addressing Mode 2)

	    ---1 -011 (13) 16-bit addressing (Addressing Mode 2 + Inline Header)
	    ---1 -100 (14) 24-bit addressing (Addressing Mode 2 + Inline Header)
	*/
	m_spritereg = data;
}
uint8_t XavixTV::tmap1_regs_r(offs_t offset)
{ (void)0;
	return m_tmap1_regs[offset];
}
uint8_t XavixTV::tmap2_regs_r(offs_t offset)
{ (void)0;
	return m_tmap2_regs[offset];
}
void XavixTV::xavix_memoryemu_txarray_w(offs_t offset, uint8_t data)
{
	if (offset < 0x400)
	{
		// nothing
	}
	else if (offset < 0x800)
	{
		m_txarray[0] = data;
	}
	else if (offset < 0xc00)
	{
		m_txarray[1] = data;
	}
	else if (offset < 0x1000)
	{
		m_txarray[2] = data;
	}
}
uint8_t XavixTV::xavix_memoryemu_txarray_r(offs_t offset)
{
	return txarray_r(offset);
}
void XavixTV::rom_dmatrg_w(uint8_t data)
{
	// 0x80 is set in the IRQ routine, presumably to ack it
	if (data & 0x80)
	{
		if (m_irqsource & 0x20)
		{
			m_irqsource &= ~0x20;
			update_irqs();
		}
	}

	if (data & 0x01) // namcons2 writes 0x81, most of the time things write 0x01
	{
 (void)0;

		uint32_t source = (m_rom_dma_src[2] << 16) | (m_rom_dma_src[1] << 8) | m_rom_dma_src[0];
		uint16_t dest = (m_rom_dma_dst[1] << 8) | m_rom_dma_dst[0];
		uint16_t len = (m_rom_dma_len[1] << 8) | m_rom_dma_len[0];
 (void)0;

		for (int i = 0; i < len; i++)
		{
			uint32_t m_tmpaddress = source + i;

			// many games explicitly want to access with the high bank bit set, so probably the same logic as when grabbing tile data
			// we have to be careful here or we get the zero page memory read, hence not just using read8 on the whole space
			// this again probably indicates there is 'data space' where those don't appear
			//uint8_t dat = m_maincpu->read_full_data_sp(m_tmpaddress);
			uint8_t dat = read_full_data_sp_bypass(m_tmpaddress);
			full_write(dest+i, dat);
		}

		if (data & 0x40) // or merely the absense of 0x80 being set? (ttv_lotr and drgqst are the only games needing the IRQ and both set 0x40 tho)
		{
			m_irqsource |= 0x20;
			update_irqs();
		}

		// ekara definitely wants this cleared
		m_rom_dma_len[0] = 0;
		m_rom_dma_len[1] = 0;

	}
	else // the interrupt routine writes 0x80 to the trigger, maybe 'clear IRQ?'
	{
 (void)0;
	}
}
void XavixTV::rom_dmasrc_w(offs_t offset, uint8_t data)
{
	// has_wamg expects to be able to read back the source to modify it (need to check if it expects it to change after an operation)
 (void)0;
	m_rom_dma_src[offset] = data;
}
void XavixTV::rom_dmadst_w(offs_t offset, uint8_t data)
{ (void)0;
	m_rom_dma_dst[offset] = data;
}
void XavixTV::rom_dmalen_w(offs_t offset, uint8_t data)
{ (void)0;
	m_rom_dma_len[offset] = data;
}
uint8_t XavixTV::rom_dmastat_r()
{ (void)0;
	return 0x00;
}
void XavixTV::vector_enable_w(uint8_t data)
{ (void)0;
	m_vectorenable = data;
}
uint8_t XavixTV::nmi_vector_lo_r()
{
	return m_nmi_vector_lo_data;
}
void XavixTV::nmi_vector_lo_w(uint8_t data)
{ (void)0;
	m_nmi_vector_lo_data = data;
}
uint8_t XavixTV::nmi_vector_hi_r()
{
	return m_nmi_vector_hi_data;
}
void XavixTV::nmi_vector_hi_w(uint8_t data)
{ (void)0;
	m_nmi_vector_hi_data = data;
}
void XavixTV::irq_vector_lo_w(uint8_t data)
{ (void)0;
	m_irq_vector_lo_data = data;
}
void XavixTV::irq_vector_hi_w(uint8_t data)
{ (void)0;
	m_irq_vector_hi_data = data;
}
uint8_t XavixTV::irq_vector_lo_r()
{ (void)0;
	return m_irq_vector_lo_data;
}
uint8_t XavixTV::irq_vector_hi_r()
{ (void)0;
	return m_irq_vector_hi_data;
}
uint8_t XavixTV::extintrf_790x_r(offs_t offset)
{ (void)0;
	return m_extbusctrl[offset];
}
void XavixTV::extintrf_790x_w(offs_t offset, uint8_t data)
{
	// Popira, Taiko De Popira etc. games with SEEPROM cartridges uses this to swap between ROM and other peripherals in the cart for data bus access?
 (void)0;
	m_extbusctrl[offset] = data;
}
uint8_t XavixTV::ioevent_enable_r()
{ (void)0;
	return m_ioevent_enable;
}
void XavixTV::ioevent_enable_w(uint8_t data)
{ (void)0;
	m_ioevent_enable = data;
}
void XavixTV::process_ioevent(uint8_t bits)
{
	if (m_ioevent_enable & bits)
	{
		m_ioevent_active |= bits;

		if (m_ioevent_active & 0x0f)
		{
			m_irqsource |= 0x08;
		}

		update_irqs();
	}
}
void XavixTV::ioevent_trg01(int state)
{
	process_ioevent(0x01);
}
void XavixTV::ioevent_trg02(int state)
{
	process_ioevent(0x02);
}
void XavixTV::ioevent_trg04(int state)
{
	process_ioevent(0x04);
}
void XavixTV::ioevent_trg08(int state)
{
	process_ioevent(0x08);
}
uint8_t XavixTV::ioevent_irqstate_r()
{ (void)0;
	return m_ioevent_active;
}
void XavixTV::ioevent_irqack_w(uint8_t data)
{ (void)0;

	if (data & 0x01)
	{
		m_ioevent_active &= ~0x01;
	}

	if (data & 0x02)
	{
		m_ioevent_active &= ~0x02;
	}

	if (data & 0x04)
	{
		m_ioevent_active &= ~0x04;
	}

	if (data & 0x08)
	{
		m_ioevent_active &= ~0x08;
	}

	if (!(m_ioevent_active & 0x0f))
	{
		m_irqsource &= ~0x08;
	}

	update_irqs();
}
void XavixTV::slotreg_7810_w(uint8_t data)
{ (void)0;
}
void XavixTV::colmix_sh_w(offs_t offset, uint8_t data)
{
	m_colmix_sh[offset] = data;
}
void XavixTV::colmix_l_w(offs_t offset, uint8_t data)
{
	m_colmix_l[offset] = data;
}
void XavixTV::colmix_6ff2_w(uint8_t data)
{ (void)0;
	m_colmix_ctrl[0] = data;
	// bits 0xe0 seem to enable a TV static effect somehow, used by rad_opus
}
uint8_t XavixTV::dispctrl_6ff8_r()
{
	// 0x80 = main IRQ asserted flag
	// 0x40 = raster IRQ asserted flag
	// 0x20 = main IRQ enable
	// 0x10 = raster IRQ enable?

	//LOG("%s: dispctrl_6ff8_r\n", "xavix");
	return m_video_ctrl;
}
void XavixTV::dispctrl_6ff8_w(uint8_t data)
{
	// 0x80 = main IRQ ack?
	// 0x40 = raster IRQ ack?
	// 0x20 = main IRQ enable
	// 0x10 = raster IRQ enable?

	if (data & 0x40)
	{
		m_irqsource &= ~0x40;
		update_irqs();
	}

	if (data & 0x80)
	{
		
	}

	uint8_t old_vid = m_video_ctrl;

	m_video_ctrl = data & 0x3f;

	// epo_guru needs something like this, otherwise raster IRQ ends up blocking all other IRQs forever
	if ((old_vid & 0x10) != (m_video_ctrl & 0x10))
	{
		if (!(m_video_ctrl & 0x10))
		{
			//printf("callback on scanline %d %d with IRQ enabled\n", m_screen->vpos(), m_screen->hpos());
			m_video_ctrl &= ~0x40;
			m_irqsource &= ~0x40;
			update_irqs();
		}
	}


	//  printf("%s: dispctrl_6ff8_w %02x\n", "xavix", data);
}
void XavixTV::update_irqs()
{
	if (m_irqsource != 0x00)
	{
		cpu.irq = true;
	}
	else
	{
		cpu.irq = false;
	}
}
void XavixTV::dispctrl_posirq_x_w(uint8_t data)
{ (void)0;
	m_posirq_x[0] = data;
}
void XavixTV::dispctrl_posirq_y_w(uint8_t data)
{ (void)0;
	m_posirq_y[0] = data;

	m_interrupt_timer->adjust(m_screen->time_until_pos(m_posirq_y[0], m_posirq_x[0]), 0);
}
uint8_t XavixTV::io0_data_r()
{
	uint8_t ret = read_io0(m_io0_direction) & ~m_io0_direction;
	ret |= m_io0_data & m_io0_direction;
	return ret;
}
uint8_t XavixTV::io1_data_r()
{
	uint8_t ret = read_io1(m_io1_direction) & ~m_io1_direction;
	ret |= m_io1_data & m_io1_direction;
	return ret;
}
uint8_t XavixTV::io0_direction_r()
{
	return m_io0_direction;
}
uint8_t XavixTV::io1_direction_r()
{
	return m_io1_direction;
}
void XavixTV::io0_data_w(uint8_t data)
{
	m_io0_data = data;
	write_io0((data & m_io0_direction) | (read_io0(m_io0_direction) & ~m_io0_direction), m_io0_direction);
 (void)0;
}
void XavixTV::io1_data_w(uint8_t data)
{
	m_io1_data = data;
	write_io1((data & m_io1_direction) | (read_io1(m_io1_direction) & ~m_io1_direction), m_io1_direction);
 (void)0;
}
void XavixTV::io0_direction_w(uint8_t data)
{
	m_io0_direction = data;
 (void)0;
	io0_data_w(m_io0_data);
}
void XavixTV::io1_direction_w(uint8_t data)
{
	m_io1_direction = data;
 (void)0;
	io1_data_w(m_io1_data); // requires this for i2cmem to work, is it correct tho?
}
uint8_t XavixTV::arena_start_r()
{
	//LOG("%s: arena_start_r\n", "xavix");
	return m_arena_start;
}
void XavixTV::arena_start_w(uint8_t data)
{ (void)0;
	m_arena_start = data; // expected to return data written

}
uint8_t XavixTV::arena_end_r()
{ (void)0;
	return m_arena_end;
}
void XavixTV::arena_end_w(uint8_t data)
{ (void)0;
	m_arena_end = data; // expected to return data written
}
uint8_t XavixTV::arena_control_r()
{
	// xavtenni expects 0x40 to go high (interlace related?)
	m_arena_control ^= 0x40;
	return m_arena_control;
}
void XavixTV::arena_control_w(uint8_t data)
{ (void)0;
	m_arena_control = data;

	// rad_bb2 waits on this in the IRQ (what does it want?) is this hblank related?
	if (data & 0x80)
		m_arena_control &= ~0x80;
}
uint8_t XavixTV::timer_baseval_r()
{ (void)0;
	return m_timer_baseval;
}
uint8_t XavixTV::timer_status_r()
{
	uint8_t ret = m_timer_control;
 (void)0;
	return ret;
}
void XavixTV::timer_baseval_w(uint8_t data)
{
	// expected to return data written
	m_timer_baseval = data;
 (void)0;
}
uint8_t XavixTV::timer_freq_r()
{ (void)0;
	return m_timer_freq;
}
uint8_t XavixTV::timer_curval_r()
{
	// TODO implement properly with timers etc. as rad_fb / rad_madfb rely on these values to calculate throw strength!
 (void)0;
	return m_timer_currentval;
}
void XavixTV::timer_freq_w(uint8_t data)
{
	// 4-bit prescale
 (void)0;

	/* if master clock (MC) is XTAL(21'477'272) (NTSC master)

	   divide value        clock source
	   0x0 = MC / 2      = 10.738636 MHz (10738636 Hz)
	   0x1 = MC / 4      = 5.369318 MHz
	   0x2 = MC / 8      = 2.684659 MHz
	   0x3 = MC / 16     = 1.3423295 MHz
	   0x4 = MC / 32     = 671.16475 kHz
	   0x5 = MC / 64     = 335.582375 kHz
	   0x6 = MC / 128    = 167.7911875 kHz
	   0x7 = MC / 256    = 83.89559375 kHz
	   0x8 = MC / 512    = 41.947796875 kHz
	   0x9 = MC / 1024   = 20.9738984375 kHz
	   0xa = MC / 2048   = 10.48694921875 kHz
	   0xb = MC / 4096   = 5.243474609375 kHz
	   0xc = MC / 8192   = 2.6217373046875 kHz
	   0xd = MC / 16384  = 1.31086865234375 kHz
	   0xe = MC / 32768  = 655.434326171875 Hz
	   0xf = MC / 65536  = 327.7171630859375 Hz
	*/
	m_timer_freq = data & 0x0f;

	if (data & 0xf0)
 (void)0;
}
uint8_t XavixTV::irq_source_r()
{
	/* the 2nd IRQ routine (regular IRQ) reads here before deciding what to do

	 the following bits have been seen to be checked (active high)
	 monster truck does most extensive checking

	  0x80 - Sound Irq
	  0x40 - Picture / Arena Irq? (including raster interrupt)
	  0x20 - DMA Irq  (most routines check this as first priority, and ignore other requests if it is set?)
	  0x10 - Timer / Counter IRQ
	  0x08 - IO Event Irq (uses 7a00 top bit to determine direction, enabled with 0x08 on 7a80, IRQ acked / cleared with 0x08 written to 7a81, 4 possible sources with different bits in 7a80 / 7a81 ? )
	         (this is the type of interrupt where the irq frequency adds a counter which determines analog value)
	  0x04 - ADC Conversion IRQ - loads/stores 7b81 (to ack interrupt)
	*/
 (void)0;
	return m_irqsource;
}
void XavixTV::irq_source_w(uint8_t data)
{ (void)0;
	// cleared on startup in monster truck, no purpose?
}
uint8_t XavixTV::sound_current_page() const
{
	return m_sound_regbase & 0x3f;
}
uint8_t XavixTV::sound_regram_read_cb(offs_t offset)
{
	// 0x00 would be zero page memory; assume it's not valid
	if ((m_sound_regbase & 0x3f) != 0x00)
	{
		const uint16_t memorybase = (m_sound_regbase & 0x3f) << 8;
		return m_mainram[memorybase + offset];
	}
	return 0x00;
}
void XavixTV::sound_regram_write_cb(offs_t offset, u8 data)
{
	if ((m_sound_regbase & 0x3f) != 0x00)
	{
		const uint16_t memorybase = (m_sound_regbase & 0x3f) << 8;
		m_mainram[memorybase + offset] = data;
	}
}
uint8_t XavixTV::sound_voice_startstop_r(offs_t offset)
{
	return m_soundreg16_0[offset];
}
void XavixTV::sound_voice_startstop_w(offs_t offset, uint8_t data)
{ (void)0;
	for (int i = 0; i < 8; i++)
	{
		const int voice_state      = BIT(data, i);
		const int old_voice_state  = BIT(m_soundreg16_0[offset], i);
		if (voice_state != old_voice_state)
		{
			const int voice = (offset * 8 + i);
			if (voice_state) m_sound->enable_voice(voice, false);
			else             m_sound->disable_voice(voice);
		}
	}
	m_soundreg16_0[offset] = data;
}
uint8_t XavixTV::sound_voice_updateenv_r(offs_t offset)
{
	// On real hardware, might be read-only or always return 0.
	return 0x00;
}
void XavixTV::sound_voice_updateenv_w(offs_t offset, uint8_t data)
{ (void)0;
	for (int i = 0; i < 8; i++)
	{
		if (BIT(data, i))
		{
			const int voice = (offset * 8 + i);
			m_sound->enable_voice(voice, true);
		}
	}
}
uint8_t XavixTV::sound_voice_status_r(offs_t offset)
{
	uint8_t ret = 0x00;
	for (int i = 0; i < 8; i++)
	{
		const int voice = (offset * 8 + i);
		if (m_sound->is_voice_enabled(voice))
			ret |= 1 << i;
	}
	return ret;
}
uint8_t XavixTV::sound_regbase_r()
{
	return m_sound_regbase & 0x3f; // upper bits read as 0
}
void XavixTV::sound_regbase_w(uint8_t data)
{
	// upper 6 bits of RAM address where the per-voice register sets live
	m_sound_regbase = data & 0x3f;
}
uint8_t XavixTV::sound_cyclerate_r()
{
	return m_cyclerate;
}
void XavixTV::sound_cyclerate_w(uint8_t data)
{
	m_cyclerate = data; // store for readback / debug
	if (m_sound) m_sound->set_cyclerate(data);
 (void)0;
}
void XavixTV::sound_tp0_w(uint8_t data)
{
	m_tp[0] = data;
	if (m_sound) m_sound->set_tempo(0, data);
 (void)0;
	reprogram_sound_timer(0);
}
void XavixTV::sound_tp1_w(uint8_t data)
{
	m_tp[1] = data;
	if (m_sound) m_sound->set_tempo(1, data);
 (void)0;
	reprogram_sound_timer(1);
}
void XavixTV::sound_tp2_w(uint8_t data)
{
	m_tp[2] = data;
	if (m_sound) m_sound->set_tempo(2, data);
 (void)0;
	reprogram_sound_timer(2);
}
void XavixTV::sound_tp3_w(uint8_t data)
{
	m_tp[3] = data;
	if (m_sound) m_sound->set_tempo(3, data);
 (void)0;
	reprogram_sound_timer(3);
}
uint8_t XavixTV::sound_irq_status_r()
{
	// UK e-kara carts check the upper nibble for sound-timer IRQ source
	return m_sound_irqstatus;
}
void XavixTV::sound_irq_status_w(uint8_t data)
{
	const uint8_t old_enable = m_sound_irqstatus & 0x0f;

	const uint8_t clear_mask = (data >> 4) & 0x0f;
	if (clear_mask)
		m_sound_irqstatus &= ~(clear_mask << 4);

	const uint8_t new_enable = data & 0x0f;
	m_sound_irqstatus = (m_sound_irqstatus & 0xf0) | new_enable;

	const uint8_t pending = (m_sound_irqstatus >> 4) & 0x0f;
 (void)0;

	const uint8_t changed = old_enable ^ new_enable;
	if (changed)
	{
		for (int t = 0; t < 4; t++)
			if (changed & (1 << t))
				reprogram_sound_timer(t);
	}

	refresh_sound_irq_state();
	update_irqs();
}
int16_t XavixTV::get_vectors(int which, int half)
{
	//  LOG("get_vectors %d %d\n", which, half);
	if (m_vectorenable == 0)
		return -1;

	if (which == 0) // irq?
	{
		if (half == 0)
			return m_nmi_vector_hi_data;
		else
			return m_nmi_vector_lo_data;
	}
	else
	{
		if (half == 0)
			return m_irq_vector_hi_data;
		else
			return m_irq_vector_lo_data;
	}
}
uint8_t XavixTV::barrel_r(offs_t offset)
{
	uint8_t ret = 0;
	if (offset == 0)
	{
		ret = (m_sgn_mpd ? 0x80 : 0x00) | (m_mpr & 0x40) | (m_mpr & 0x07);
	}
	else
	{
		ret = (m_mpr & 0x40) ?
			m_multresults[1] ^ m_multresults[0] : (m_mpr & 0x08) ?
			m_multresults[1] : m_multresults[0];
	}
 (void)0;
	return ret;
}
void XavixTV::barrel_w(offs_t offset, uint8_t data)
{ (void)0;

	if (offset == 0)
	{
		m_mad = 0;
		m_sgn_mpd = data & 0x80;
		m_sgn_mpr = 0;
		m_mpr = data;
		do_math(true);
	}
	else
	{
		m_mpd = data;
	}
}
uint8_t XavixTV::mult_r(offs_t offset)
{
	uint8_t ret = m_multresults[offset];
 (void)0;
	return ret;
}
void XavixTV::mult_w(offs_t offset, uint8_t data)
{ (void)0;
	// rad_madf writes here to set the base value which the multiplication result gets added to
	m_multresults[offset] = data;
}
uint8_t XavixTV::mult_param_r(offs_t offset)
{
	uint8_t ret = 0;

	if (offset == 0)
	{
		ret = m_mad | (m_sgn_mpd ? 0x02 : 0x00) | m_sgn_mpr;
	}
	else if (offset == 1)
	{
		ret = m_mpd;
	}
	else
	{
		ret = m_mpr;
	}
 (void)0;
	return ret;
}
void XavixTV::do_math(bool mul_shf)
{
	uint16_t result = 0;
	uint8_t mpd = m_mpd;
	uint8_t mulinp;

	if (!mul_shf)
	{
		mulinp = m_mpr;
	}
	else
	{
		mulinp = 1 << (m_mpr & 0x7);
	}

	if (m_sgn_mpd && (mpd & 0x80) && m_sgn_mpr && (mulinp & 0x80))
	{
		result = mpd * mulinp - (mpd << 8) - (mulinp << 8);
	}
	else if (m_sgn_mpd && (mpd & 0x80))
	{
		result = mpd * mulinp - (mulinp << 8);
	}
	else if (m_sgn_mpr && (mulinp & 0x80))
	{
		result = mpd * mulinp - (mpd << 8);
	}
	else
	{
		result = mpd * mulinp;
	}

	if (m_mad)
	{
		uint16_t oldresult = (m_multresults[1] << 8) | m_multresults[0];
		result = oldresult + result;
	}

	m_multresults[1] = (result >> 8) & 0xff;
	m_multresults[0] = result & 0xff;
}
void XavixTV::mult_param_w(offs_t offset, uint8_t data, uint8_t mem_mask)
{
	// there are NOPs after one of the writes, so presumably the operation is write triggerd and not intstant
	// see test code at 0184a4 in monster truck

	// offset0 is control

	// m?-- --Ss
	// m = mode, S = sign for param1, s = sign for param2
 (void)0;

	if (offset == 0)
	{
		m_mad = data & 0x80;
		// sometimes 0x40 is set along with 0x80, sometimes only 0x80, why? is it important?

		m_sgn_mpd = data & 0x02;
		m_sgn_mpr = data & 0x01;
	}
	else if (offset == 1)
	{
		m_mpd = data;
	}
	else if (offset == 2)
	{
		m_mpr = data;
		do_math(false);
	}
}
