// license:BSD-3-Clause
// Adapted from MAME 4edb90120a5ec0804a17d6f19ccc588e76b54a06.
// copyright-holders:Olivier Galibert
void do_adc_d(uint8_t val)
{
	uint8_t c = m_P & F_C ? 1 : 0;
	m_P &= ~(F_N|F_V|F_Z|F_C);
	uint8_t al = (m_A & 15) + (val & 15) + c;
	if(al > 9)
		al += 6;
	uint8_t ah = (m_A >> 4) + (val >> 4) + (al > 15);
	if(!uint8_t(m_A + val + c))
		m_P |= F_Z;
	else if(ah & 8)
		m_P |= F_N;
	if(~(m_A^val) & (m_A^(ah << 4)) & 0x80)
		m_P |= F_V;
	if(ah > 9)
		ah += 6;
	if(ah > 15)
		m_P |= F_C;
	m_A = (ah << 4) | (al & 15);
}

void do_adc_nd(uint8_t val)
{
	uint16_t sum;
	sum = m_A + val + (m_P & F_C ? 1 : 0);
	m_P &= ~(F_N|F_V|F_Z|F_C);
	if(!uint8_t(sum))
		m_P |= F_Z;
	else if(int8_t(sum) < 0)
		m_P |= F_N;
	if(~(m_A^val) & (m_A^sum) & 0x80)
		m_P |= F_V;
	if(sum & 0xff00)
		m_P |= F_C;
	m_A = sum;
}

void do_adc(uint8_t val)
{
	if(m_P & F_D)
		do_adc_d(val);
	else
		do_adc_nd(val);
}

void do_arr_nd()
{
	bool c = m_P & F_C;
	m_P &= ~(F_N|F_Z|F_C|F_V);
	m_A >>= 1;
	if(c)
		m_A |= 0x80;
	if(!m_A)
		m_P |= F_Z;
	else if(int8_t(m_A)<0)
		m_P |= F_N;
	if(m_A & 0x40)
		m_P |= F_V|F_C;
	if(m_A & 0x20)
		m_P ^= F_V;
}

void do_arr_d()
{
	// The adc/ror interaction gives an extremely weird result
	bool c = m_P & F_C;
	m_P &= ~(F_N|F_Z|F_C|F_V);
	uint8_t a = m_A >> 1;
	if(c)
		a |= 0x80;
	if(!a)
		m_P |= F_Z;
	else if(int8_t(a) < 0)
		m_P |= F_N;
	if((a ^ m_A) & 0x40)
		m_P |= F_V;

	if((m_A & 0x0f) >= 0x05)
		a = ((a + 6) & 0x0f) | (a & 0xf0);

	if((m_A & 0xf0) >= 0x50) {
		a += 0x60;
		m_P |= F_C;
	}
	m_A = a;
}

void do_arr()
{
	if(m_P & F_D)
		do_arr_d();
	else
		do_arr_nd();
}

void do_cmp(uint8_t val1, uint8_t val2)
{
	m_P &= ~(F_N|F_Z|F_C);
	uint16_t r = val1-val2;
	if(!r)
		m_P |= F_Z;
	else if(int8_t(r) < 0)
		m_P |= F_N;
	if(!(r & 0xff00))
		m_P |= F_C;
}

void do_sbc_d(uint8_t val)
{
	uint8_t c = m_P & F_C ? 0 : 1;
	m_P &= ~(F_N|F_V|F_Z|F_C);
	uint16_t diff = m_A - val - c;
	uint8_t al = (m_A & 15) - (val & 15) - c;
	uint8_t ah = (m_A >> 4) - (val >> 4) - (int8_t(al) < 0);
	if(!uint8_t(diff))
		m_P |= F_Z;
	else if(diff & 0x80)
		m_P |= F_N;
	if((m_A^val) & (m_A^diff) & 0x80)
		m_P |= F_V;
	if(!(diff & 0xff00))
		m_P |= F_C;
	if(int8_t(al) < 0)
		al -= 6;
	if(int8_t(ah) < 0)
		ah -= 6;
	m_A = (ah << 4) | (al & 15);
}

void do_sbc_nd(uint8_t val)
{
	uint16_t diff = m_A - val - (m_P & F_C ? 0 : 1);
	m_P &= ~(F_N|F_V|F_Z|F_C);
	if(!uint8_t(diff))
		m_P |= F_Z;
	else if(int8_t(diff) < 0)
		m_P |= F_N;
	if((m_A^val) & (m_A^diff) & 0x80)
		m_P |= F_V;
	if(!(diff & 0xff00))
		m_P |= F_C;
	m_A = diff;
}

void do_sbc(uint8_t val)
{
	if(m_P & F_D)
		do_sbc_d(val);
	else
		do_sbc_nd(val);
}

void do_bit(uint8_t val)
{
	m_P &= ~(F_N|F_Z|F_V);
	uint8_t r = m_A & val;
	if(!r)
		m_P |= F_Z;
	if(val & 0x80)
		m_P |= F_N;
	if(val & 0x40)
		m_P |= F_V;
}

uint8_t do_asl(uint8_t v)
{
	m_P &= ~(F_N|F_Z|F_C);
	uint8_t r = v<<1;
	if(!r)
		m_P |= F_Z;
	else if(int8_t(r) < 0)
		m_P |= F_N;
	if(v & 0x80)
		m_P |= F_C;
	return r;
}

uint8_t do_lsr(uint8_t v)
{
	m_P &= ~(F_N|F_Z|F_C);
	if(v & 1)
		m_P |= F_C;
	v >>= 1;
	if(!v)
		m_P |= F_Z;
	return v;
}

uint8_t do_ror(uint8_t v)
{
	bool c = m_P & F_C;
	m_P &= ~(F_N|F_Z|F_C);
	if(v & 1)
		m_P |= F_C;
	v >>= 1;
	if(c)
		v |= 0x80;
	if(!v)
		m_P |= F_Z;
	else if(int8_t(v)<0)
		m_P |= F_N;
	return v;
}

uint8_t do_rol(uint8_t v)
{
	bool c = m_P & F_C;
	m_P &= ~(F_N|F_Z|F_C);
	if(v & 0x80)
		m_P |= F_C;
	v <<= 1;
	if(c)
		v |= 0x01;
	if(!v)
		m_P |= F_Z;
	else if(int8_t(v)<0)
		m_P |= F_N;
	return v;
}

uint8_t do_asr(uint8_t v)
{
	m_P &= ~(F_N|F_Z|F_C);
	if(v & 1)
		m_P |= F_C;
	v >>= 1;
	if(!v)
		m_P |= F_Z;
	else if(v & 0x40) {
		m_P |= F_N;
		v |= 0x80;
	}
	return v;
}

