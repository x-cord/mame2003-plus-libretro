// license:BSD-3-Clause
// Adapted from MAME 4edb90120a5ec0804a17d6f19ccc588e76b54a06.
// copyright-holders:Olivier Galibert, David Haywood
void adc_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	do_adc(m_TMP2);
	prefetch();
}
void adc_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	do_adc(m_TMP2);
	prefetch();
}
void adc_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	do_adc(m_TMP2);
	prefetch();
}
void adc_imm() {
	m_TMP = read_pc();
	m_PC++;
	do_adc(m_TMP);
	prefetch();
}
void adc_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	do_adc(m_TMP2);
	prefetch();
}
void adc_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_TMP2 = read(m_TMP+m_Y);
	do_adc(m_TMP2);
	prefetch();
}
void adc_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	do_adc(m_TMP2);
	prefetch();
}
void adc_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP2 = read_zeropage(uint8_t(m_TMP+m_X));
	do_adc(m_TMP2);
	prefetch();
}
void adcj_imp() {
	read_pc();
	do_adc(m_j);
	prefetch();
}
void adck_imp() {
	read_pc();
	do_adc(m_k);
	prefetch();
}
void adcl_imp() {
	read_pc();
	do_adc(m_l);
	prefetch();
}
void adcm_imp() {
	read_pc();
	do_adc(m_m);
	prefetch();
}
void adcpa_imp() {
	do_adc(read_full_data(m_pa));
	prefetch();
}
void adcpb_imp() {
	do_adc(read_full_data(m_pb));
	prefetch();
}
void anc_imm() {
	m_TMP = read_pc();
	m_PC++;
	m_A &= m_TMP;
	set_nz(m_A);
	if(m_A & 0x80)
		m_P |= F_C;
	else
		m_P &= ~F_C;
	prefetch();
}
void and_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void and_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void and_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void and_imm() {
	m_TMP = read_pc();
	m_PC++;
	m_A &= m_TMP;
	set_nz(m_A);
	prefetch();
}
void and_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void and_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_TMP2 = read(m_TMP+m_Y);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void and_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_A &= read_zeropage(m_TMP);
	set_nz(m_A);
	prefetch();
}
void and_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_A &= read_zeropage(uint8_t(m_TMP+m_X));
	set_nz(m_A);
	prefetch();
}
void andj_imp() {
	read_pc();
	m_A &= m_j;
	set_nz(m_A);
	prefetch();
}
void andk_imp() {
	read_pc();
	m_A &= m_k;
	set_nz(m_A);
	prefetch();
}
void andl_imp() {
	read_pc();
	m_A &= m_l;
	set_nz(m_A);
	prefetch();
}
void andm_imp() {
	read_pc();
	m_A &= m_m;
	set_nz(m_A);
	prefetch();
}
void andpa_imp() {
	read_pc();
	m_TMP = read_full_data(m_pa);
	m_A &= m_TMP;
	set_nz(m_A);
	prefetch();
}
void andpb_imp() {
	read_pc();
	m_TMP = read_full_data(m_pb);
	m_A &= m_TMP;
	set_nz(m_A);
	prefetch();
}
void ane_imm() {
	m_TMP = read_pc();
	m_PC++;
	m_A &= m_TMP & m_X;
	set_nz(m_A);
	prefetch();
}
void arr_imm() {
	m_TMP = read_pc();
	m_PC++;
	m_A &= m_TMP;
	do_arr();
	prefetch();
}
void asl_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asl(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void asl_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asl(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void asl_acc() {
	read_pc();
	m_A = do_asl(m_A);
	prefetch();
}
void asl_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_asl(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void asl_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_asl(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void asr_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asr(m_TMP2); // used by xavbox, see 14e8
	write(m_TMP, m_TMP2);
	prefetch();
}
void asr_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	read(m_TMP);
	m_TMP2 = do_asr(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void asr_acc() {
	m_A = do_asr(m_A);
	prefetch();
}
void asr_imm() {
	m_TMP = read_pc();
	m_PC++;
	m_A = do_lsr(m_A & m_TMP);
	prefetch();
}
void asr_xav_zpg() {
	m_TMP = read_pc(); // TODO: verify this, should it write back or set m_A?
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	do_asr(m_TMP2);
	write_zeropage(m_TMP,m_TMP2);
	prefetch();
}
void asr_xav_zpx() {
	m_TMP = read_pc();
	read_pc();
	m_PC++;
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	read_zeropage((m_TMP+1) & 0xff);
	m_TMP2 = do_asr(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void bcc_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(!(m_P & F_C)) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void bcs_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(m_P & F_C) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void beq_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(m_P & F_Z) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void bit_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	do_bit(m_TMP2);
	prefetch();
}
void bit_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	do_bit(m_TMP2);
	prefetch();
}
void bit_imm() {
	m_TMP = read_pc();
	m_PC++;
	do_bit(m_TMP);
	prefetch();
}
void bit_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	do_bit(read_zeropage(m_TMP));
	prefetch();
}
void bit_xav_zpx() {
	unsupported_opcode();
	read_pc();
	prefetch();
}
void bmi_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(m_P & F_N) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void bne_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(!(m_P & F_Z)) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void bpl_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(!(m_P & F_N)) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void bvc_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(!(m_P & F_V)) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void bvs_rel() {
	m_TMP = read_pc();
	m_PC++;
	if(m_P & F_V) {
		if(page_changing(m_PC, int8_t(m_TMP))) {
			read_pc();
			read_arg(set_l(m_PC, m_PC+int8_t(m_TMP)));
		} else {
			read_pc_noirq();
		}
		m_PC += int8_t(m_TMP);
	}
	prefetch();
}
void callf_xa3() {
	read_stack(m_SP);
	write_special_stack(m_SP, get_codebank());
	dec_special_stack();
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_pc();
	m_PC++;
	//read_stack(m_SP);
	write_stack(m_SP, m_PC>>8);
	dec_SP();
	write_stack(m_SP, m_PC);
	dec_SP();
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_PC = m_TMP;
	set_codebank(m_TMP2);
	prefetch();
}
void clc_imp() {
	read_pc();
	m_P &= ~F_C;
	prefetch();
}
void cld_imp() {
	read_pc();
	m_P &= ~F_D;
	prefetch();
}
void cli_imp() {
	read_pc();
	prefetch();
	m_P &= ~F_I; // Do *not* move it before the prefetch
}
void clr_acc() {
	read_pc();
	m_A = 0;
	set_nz(m_A);
	prefetch();
}
void clv_imp() {
	read_pc();
	m_P &= ~F_V;
	prefetch();
}
void cmc_imp() {
	read_pc();
	m_TMP = m_P & F_C;
	m_P &= ~F_C;
	m_TMP = ~m_TMP;
	m_P |= m_TMP & F_C;
	prefetch();
}
void cmp_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void cmp_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void cmp_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void cmp_imm() {
	m_TMP = read_pc();
	m_PC++;
	do_cmp(m_A, m_TMP);
	prefetch();
}
void cmp_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void cmp_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_TMP2 = read(m_TMP+m_Y);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void cmp_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void cmp_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP2 = read_zeropage(uint8_t(m_TMP+m_X));
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void cmpj_imp() {
	read_pc();
	do_cmp(m_A, m_j);
	prefetch();
}
void cmpk_imp() {
	read_pc();
	do_cmp(m_A, m_k);
	prefetch();
}
void cmpl_imp() {
	read_pc();
	do_cmp(m_A, m_l);
	prefetch();
}
void cmpm_imp() {
	read_pc();
	do_cmp(m_A, m_m);
	prefetch();
}
void cmppa_imp() {
	m_TMP = read_full_data(m_pa);
	do_cmp(m_A, m_TMP);
	prefetch();
}
void cmppb_imp() {
	unsupported_opcode();
	read_pc();
	prefetch();
}
void cpx_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	do_cmp(m_X, m_TMP2);
	prefetch();
}
void cpx_imm() {
	m_TMP = read_pc();
	m_PC++;
	do_cmp(m_X, m_TMP);
	prefetch();
}
void cpx_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	do_cmp(m_X, m_TMP2);
	prefetch();
}
void cpy_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	do_cmp(m_Y, m_TMP2);
	prefetch();
}
void cpy_imm() {
	m_TMP = read_pc();
	m_PC++;
	do_cmp(m_Y, m_TMP);
	prefetch();
}
void cpy_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	do_cmp(m_Y, m_TMP2);
	prefetch();
}
void dcp_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2--;
	write(m_TMP, m_TMP2);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void dcp_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2--;
	write(m_TMP, m_TMP2);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void dcp_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2--;
	write(m_TMP, m_TMP2);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void dcp_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2--;
	write(m_TMP, m_TMP2);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void dcp_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2--;
	write(m_TMP, m_TMP2);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void dcp_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2--;
	write_zeropage(m_TMP, m_TMP2);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void dcp_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2--;
	write_zeropage(m_TMP, m_TMP2);
	do_cmp(m_A, m_TMP2);
	prefetch();
}
void dec_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2--;
	set_nz(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void dec_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2--;
	set_nz(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void dec_acc() {
	read_pc();
	m_A--;
	set_nz(m_A);
	prefetch();
}
void dec_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2--;
	set_nz(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void dec_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2--;
	set_nz(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void decpa_imp() {
	read_pc();
	m_pa = (m_pa & ~0xff) | ((m_pa-1) & 0xff);
	set_nz(m_pa&0xff); // startup code seems to require flag to set if low byte is 0x00?
	prefetch();
}
void decpb_imp() {
	read_pc();
	m_pb = (m_pb & ~0xff) | ((m_pb-1) & 0xff);
	set_nz(m_pb&0xff); // startup code seems to require flag to set if low byte is 0x00?
	prefetch();
}
void dex_imp() {
	read_pc();
	m_X--;
	set_nz(m_X);
	prefetch();
}
void dey_imp() {
	read_pc();
	m_Y--;
	set_nz(m_Y);
	prefetch();
}
void eor_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void eor_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void eor_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void eor_imm() {
	m_TMP = read_pc();
	m_PC++;
	m_A ^= m_TMP;
	set_nz(m_A);
	prefetch();
}
void eor_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void eor_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_TMP2 = read(m_TMP+m_Y);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void eor_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_A ^= read_zeropage(m_TMP);
	set_nz(m_A);
	prefetch();
}
void eor_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_A ^= read_zeropage(uint8_t(m_TMP+m_X));
	set_nz(m_A);
	prefetch();
}
void eorj_imp() {
	read_pc();
	m_A ^= m_j;
	set_nz(m_A);
	prefetch();
}
void eork_imp() {
	read_pc();
	m_A ^= m_k;
	set_nz(m_A);
	prefetch();
}
void eorl_imp() {
	read_pc();
	m_A ^= m_l;
	set_nz(m_A);
	prefetch();
}
void eorm_imp() {
	read_pc();
	m_A ^= m_m;
	set_nz(m_A);
	prefetch();
}
void eorpa_imp() {
	read_pc();
	m_TMP = read_full_data(m_pa);
	m_A ^= m_TMP;
	set_nz(m_A);
	prefetch();
}
void eorpb_imp() {
	read_pc();
	m_TMP = read_full_data(m_pb);
	m_A ^= m_TMP;
	set_nz(m_A);
	prefetch();
}
void inc_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2++;
	set_nz(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void inc_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2++;
	set_nz(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void inc_acc() {
	read_pc();
	m_A++;
	set_nz(m_A);
	prefetch();
}
void inc_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2++;
	set_nz(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void inc_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2++;
	set_nz(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void incpa_imp() {
	read_pc();
	m_pa = (m_pa & ~0xff) | ((m_pa+1) & 0xff);
	set_nz(m_pa&0xff); // startup code seems to require flag to set if low byte is 0x00?
	prefetch();
}
void incpb_imp() {
	read_pc();
	m_pb = (m_pb & ~0xff) | ((m_pb+1) & 0xff);
	set_nz(m_pb&0xff); // startup code seems to require flag to set if low byte is 0x00?
	prefetch();
}
void inx_imp() {
	read_pc();
	m_X++;
	set_nz(m_X);
	prefetch();
}
void iny_imp() {
	read_pc();
	m_Y++;
	set_nz(m_Y);
	prefetch();
}
void isb_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2++;
	write(m_TMP, m_TMP2);
	do_sbc(m_TMP2);
	prefetch();
}
void isb_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2++;
	write(m_TMP, m_TMP2);
	do_sbc(m_TMP2);
	prefetch();
}
void isb_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2++;
	write(m_TMP, m_TMP2);
	do_sbc(m_TMP2);
	prefetch();
}
void isb_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2++;
	write(m_TMP, m_TMP2);
	do_sbc(m_TMP2);
	prefetch();
}
void isb_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2++;
	write(m_TMP, m_TMP2);
	do_sbc(m_TMP2);
	prefetch();
}
void isb_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2++;
	write_zeropage(m_TMP, m_TMP2);
	do_sbc(m_TMP2);
	prefetch();
}
void isb_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2++;
	write_zeropage(m_TMP, m_TMP2);
	do_sbc(m_TMP2);
	prefetch();
}
void jmp_adr() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC = m_TMP;
	prefetch();
}
void jmp_ind() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_PC = read(m_TMP);
	m_PC = set_h(m_PC, read(set_l(m_TMP, m_TMP+1)));
	prefetch();
}
void jmp_xa3() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_PC = m_TMP;
	set_codebank(m_TMP2);
	prefetch();
}
void jmpf_ind() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_PC = read(m_TMP);
	m_PC = set_h(m_PC, read(set_l(m_TMP, m_TMP+1)));
	m_TMP2 = read(m_TMP+2);
	set_codebank(m_TMP2);
	prefetch();
}
void jsr_xav_adr() {
	m_TMP = read_pc();
	m_PC++;
	read_stack(m_SP);
	write_stack(m_SP, m_PC>>8);
	dec_SP();
	write_stack(m_SP, m_PC);
	dec_SP();
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_PC = m_TMP;
	prefetch();
}
void kil_non() {
stopped=true; xavix_ICount=0;
}
void las_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_TMP2 = read(m_TMP+m_Y);
	m_A = m_TMP2 | 0x51;
	m_X = 0xff;
	set_nz(m_TMP2);
	prefetch();
}
void lax_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_A = m_X = read(m_TMP);
	set_nz(m_A);
	prefetch();
}
void lax_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_A = m_X = read(m_TMP+m_Y);
	set_nz(m_A);
	prefetch();
}
void lax_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dumm read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_A = m_X = read(m_TMP);
	set_nz(m_A);
	prefetch();
}
void lax_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_A = m_X = read(m_TMP+m_Y);
	set_nz(m_A);
	prefetch();
}
void lax_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_A = m_X = read_zeropage(m_TMP);
	set_nz(m_A);
	prefetch();
}
void lax_xav_zpy() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_Y);
	m_A = m_X = read_zeropage(m_TMP);
	set_nz(m_A);
	prefetch();
}
void lda_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_A = read(m_TMP);
	set_nz(m_A);
	prefetch();
}
void lda_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_A = read(m_TMP + m_X);
	set_nz(m_A);
	prefetch();
}
void lda_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_A = read(m_TMP + m_Y);
	set_nz(m_A);
	prefetch();
}
void lda_imm() {
	m_A = read_pc();
	m_PC++;
	set_nz(m_A);
	prefetch();
}
void lda_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_A = read(m_TMP);
	set_nz(m_A);
	prefetch();
}
void lda_xav_idy() {
	m_TMP2 = read_pc(); // we need to fetch the zero page part from zero page, but the rest as if zero page wasn't there, similar to the 6509
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_A = read(m_TMP+m_Y);
	set_nz(m_A);
	prefetch();
}
void lda_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_A = read_zeropage(m_TMP);
	set_nz(m_A);
	prefetch();
}
void lda_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_A = read_zeropage(uint8_t(m_TMP+m_X));
	set_nz(m_A);
	prefetch();
}
void ldaj_imp() {
	read_pc();
	m_A = m_j;
	set_nz(m_A);
	prefetch();
}
void ldak_imp() {
	read_pc();
	m_A = m_k;
	set_nz(m_A);
	prefetch();
}
void ldal_imp() {
	read_pc();
	m_A = m_l;
	set_nz(m_A);
	prefetch();
}
void ldam_imp() {
	read_pc();
	m_A = m_m;
	set_nz(m_A);
	prefetch();
}
void ldapa_imp() {
	m_A = read_full_data(m_pa);
	set_nz(m_A);
	prefetch();
}
void ldapb_imp() {
	m_A = read_full_data(m_pb);
	set_nz(m_A);
	prefetch();
}
void ldx_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_X = read(m_TMP);
	set_nz(m_X);
	prefetch();
}
void ldx_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_X = read(m_TMP + m_Y);
	set_nz(m_X);
	prefetch();
}
void ldx_imm() {
	m_X = read_pc();
	m_PC++;
	set_nz(m_X);
	prefetch();
}
void ldx_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_X = read_zeropage(m_TMP);
	set_nz(m_X);
	prefetch();
}
void ldx_xav_zpy() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_X = read_zeropage(uint8_t(m_TMP+m_Y));
	set_nz(m_X);
	prefetch();
}
void ldy_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_Y = read(m_TMP);
	set_nz(m_Y);
	prefetch();
}
void ldy_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_TMP += m_X;
	m_Y = read(m_TMP);
	set_nz(m_Y);
	prefetch();
}
void ldy_imm() {
	m_Y = read_pc();
	m_PC++;
	set_nz(m_Y);
	prefetch();
}
void ldy_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_Y = read_zeropage(m_TMP);
	set_nz(m_Y);
	prefetch();
}
void ldy_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_Y = read_zeropage(uint8_t(m_TMP+m_X));
	set_nz(m_Y);
	prefetch();
}
void lpa0_imp() {
	read_pc();
	m_A = (m_pa & 0x0000ff) >> 0;
	set_nz(m_A);
	prefetch();
}
void lpa1_imp() {
	read_pc();
	m_A = (m_pa & 0x00ff00) >> 8;
	set_nz(m_A);
	prefetch();
}
void lpa2_imp() {
	read_pc();
	m_A = (m_pa & 0xff0000) >> 16;
	set_nz(m_A);
	prefetch();
}
void lpb0_imp() {
	read_pc();
	m_A = (m_pb & 0x0000ff) >> 0;
	set_nz(m_A);
	prefetch();
}
void lpb1_imp() {
	read_pc();
	m_A = (m_pb & 0x00ff00) >> 8;
	set_nz(m_A);
	prefetch();
}
void lpb2_imp() {
	read_pc();
	m_A = (m_pb & 0xff0000) >> 16;
	set_nz(m_A);
	prefetch();
}
void lsr_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_lsr(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void lsr_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_lsr(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void lsr_acc() {
	read_pc();
	m_A = do_lsr(m_A);
	prefetch();
}
void lsr_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_lsr(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void lsr_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_lsr(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void lxa_imm() {
	m_A = m_X = read_pc();
	m_PC++;
	set_nz(m_A);
	prefetch();
}
void neg_acc() {
	read_pc();
	m_A = (0x00 - m_A) & 0xff;
	set_nz(m_A);
	prefetch();
}
void nop_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(m_TMP);
	prefetch();
}
void nop_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	read(m_TMP + m_X);
	prefetch();
}
void nop_imm() {
	read_pc();
	m_PC++;
	prefetch();
}
void nop_imp() {
	read_pc();
	prefetch();
}
void nop_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	prefetch();
}
void nop_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP);
	read_zeropage(uint8_t(m_TMP+m_X));
	prefetch();
}
void not_acc() {
	read_pc();
	m_A = m_A ^ 0xff;
	set_nz(m_A);
	prefetch();
}
void ora_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void ora_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void ora_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void ora_imm() {
	m_TMP = read_pc();
	m_PC++;
	m_A |= m_TMP;
	set_nz(m_A);
	prefetch();
}
void ora_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void ora_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_TMP2 = read(m_TMP+m_Y);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void ora_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_A |= read_zeropage(m_TMP);
	set_nz(m_A);
	prefetch();
}
void ora_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_A |= read_zeropage(uint8_t(m_TMP+m_X));
	set_nz(m_A);
	prefetch();
}
void oraj_imp() {
	read_pc();
	m_A |= m_j;
	set_nz(m_A);
	prefetch();
}
void orak_imp() {
	read_pc();
	m_A |= m_k;
	set_nz(m_A);
	prefetch();
}
void oral_imp() {
	read_pc();
	m_A |= m_l;
	set_nz(m_A);
	prefetch();
}
void oram_imp() {
	read_pc();
	m_A |= m_m;
	set_nz(m_A);
	prefetch();
}
void orapa_imp() {
	read_pc();
	m_TMP = read_full_data(m_pa);
	m_A |= m_TMP;
	set_nz(m_A);
	prefetch();
}
void orapb_imp() {
	read_pc();
	m_TMP = read_full_data(m_pb);
	m_A |= m_TMP;
	set_nz(m_A);
	prefetch();
}
void pha_xav_imp() {
	read_pc();
	write_stack(m_SP, m_A);
	dec_SP();
	prefetch();
}
void php_xav_imp() {
	read_pc();
	write_stack(m_SP, m_P);
	dec_SP();
	prefetch();
}
void phx_imp() {
	read_pc();
	write_stack(m_SP, m_X);
	dec_SP();
	prefetch();
}
void phy_imp() {
	read_pc();
	write_stack(m_SP, m_Y);
	dec_SP();
	prefetch();
}
void pla_xav_imp() {
	read_pc();
	read_stack(m_SP);
	inc_SP();
	m_A = read_stack(m_SP);
	set_nz(m_A);
	prefetch();
}
void plp_xav_imp() {
	read_pc();
	read_stack(m_SP);
	inc_SP();
	m_TMP = read_stack(m_SP) | (F_B|F_E);
	prefetch();
	m_P = m_TMP; // Do *not* move it before the prefetch
}
void plx_imp() {
	read_pc();
	read_stack(m_SP);
	inc_SP();
	m_X = read_stack(m_SP);
	set_nz(m_X);
	prefetch();
}
void ply_imp() {
	read_pc();
	read_stack(m_SP);
	inc_SP();
	m_Y = read_stack(m_SP);
	set_nz(m_Y);
	prefetch();
}
void retf_imp() {
	read_pc();
	read_stack(m_SP);
	inc_SP();
	m_PC = read_stack(m_SP);
	inc_SP();
	m_PC = set_h(m_PC, read_stack(m_SP));
	inc_special_stack();
	m_TMP2 = read_special_stack(m_SP);
	set_codebank(m_TMP2);
	read_pc();
	m_PC++;
	prefetch();
}
void rla_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_rol(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void rla_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_rol(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void rla_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_rol(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void rla_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_rol(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void rla_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_rol(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void rla_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_rol(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void rla_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_rol(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	m_A &= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void rol_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_rol(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void rol_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_rol(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void rol_acc() {
	read_pc();
	m_A = do_rol(m_A);
	prefetch();
}
void rol_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_rol(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void rol_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_rol(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void ror_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_ror(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void ror_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_ror(m_TMP2);
	write(m_TMP, m_TMP2);
	prefetch();
}
void ror_acc() {
	read_pc();
	m_A = do_ror(m_A);
	prefetch();
}
void ror_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_ror(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void ror_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_ror(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void rra_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_ror(m_TMP2);
	write(m_TMP, m_TMP2);
	do_adc(m_TMP2);
	prefetch();
}
void rra_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_ror(m_TMP2);
	write(m_TMP, m_TMP2);
	do_adc(m_TMP2);
	prefetch();
}
void rra_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_ror(m_TMP2);
	write(m_TMP, m_TMP2);
	do_adc(m_TMP2);
	prefetch();
}
void rra_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_ror(m_TMP2);
	write(m_TMP, m_TMP2);
	do_adc(m_TMP2);
	prefetch();
}
void rra_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_ror(m_TMP2);
	write(m_TMP, m_TMP2);
	do_adc(m_TMP2);
	prefetch();
}
void rra_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_ror(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	do_adc(m_TMP2);
	prefetch();
}
void rra_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_ror(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	do_adc(m_TMP2);
	prefetch();
}
void rti_xav_imp() {
	read_pc();
	read_stack(m_SP);
	inc_SP();
	m_P = read_stack(m_SP) | (F_B|F_E);
	inc_SP();
	m_PC = read_stack(m_SP);
	inc_SP();
	m_PC = set_h(m_PC, read_stack(m_SP));
	inc_special_stack();
	m_TMP2 = read_special_stack(m_SP);
	set_codebank(m_TMP2);
	prefetch();
}
void rts_xav_imp() {
	read_pc();
	read_stack(m_SP);
	inc_SP();
	m_PC = read_stack(m_SP);
	inc_SP();
	m_PC = set_h(m_PC, read_stack(m_SP));
	read_pc();
	m_PC++;
	prefetch();
}
void sax_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = m_A & m_X;
	write(m_TMP, m_TMP2);
	prefetch();
}
void sax_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = m_A & m_X;
	write(m_TMP, m_TMP2);
	prefetch();
}
void sax_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = m_A & m_X;
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void sax_xav_zpy() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_Y);
	m_TMP2 = m_A & m_X;
	write_zeropage(m_TMP, m_TMP2);
	prefetch();
}
void sbc_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	do_sbc(m_TMP2);
	prefetch();
}
void sbc_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_X)) {
		read(set_l(m_TMP, m_TMP+m_X));
	}
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	do_sbc(m_TMP2);
	prefetch();
}
void sbc_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y));
	}
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	do_sbc(m_TMP2);
	prefetch();
}
void sbc_imm() {
	m_TMP = read_pc();
	m_PC++;
	do_sbc(m_TMP);
	prefetch();
}
void sbc_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	do_sbc(m_TMP2);
	prefetch();
}
void sbc_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	if(page_changing(m_TMP, m_Y)) {
		read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	}
	m_TMP2 = read(m_TMP+m_Y);
	do_sbc(m_TMP2);
	prefetch();
}
void sbc_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	do_sbc(m_TMP2);
	prefetch();
}
void sbc_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP2 = read_zeropage(uint8_t(m_TMP+m_X));
	do_sbc(m_TMP2);
	prefetch();
}
void sbcj_imp() {
	read_pc();
	do_sbc(m_j);
	prefetch();
}
void sbck_imp() {
	read_pc();
	do_sbc(m_k);
	prefetch();
}
void sbcl_imp() {
	read_pc();
	do_sbc(m_l);
	prefetch();
}
void sbcm_imp() {
	read_pc();
	do_sbc(m_m);
	prefetch();
}
void sbcpa_imp() {
	do_sbc(read_full_data(m_pa));
	prefetch();
}
void sbcpb_imp() {
	do_sbc(read_full_data(m_pb));
	prefetch();
}
void sbx_imm() {
	m_TMP2 = read_pc();
	m_PC++;
	m_X &= m_A;
	if(m_X < m_TMP2)
		m_P &= ~F_C;
	else
		m_P |= F_C;
	m_X -= m_TMP2;
	set_nz(m_X);
	prefetch();
}
void sec_imp() {
	read_pc();
	m_P |= F_C;
	prefetch();
}
void sed_imp() {
	read_pc();
	m_P |= F_D;
	prefetch();
}
void sei_imp() {
	read_pc();
	prefetch();
	m_P |= F_I; // Do *not* move it before the prefetch
}
void sev_imp() {
	read_pc();
	m_P |= F_V;
	prefetch();
}
void sha_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP2 = m_A & m_X & ((m_TMP >> 8)+1);
	if(page_changing(m_TMP, m_Y))
		m_TMP = set_h(m_TMP+m_Y, m_TMP2);
	else
		m_TMP += m_Y;
	write(m_TMP, m_TMP2);
	prefetch();
}
void sha_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	m_TMP2 = m_A & m_X & ((m_TMP >> 8)+1);
	if(page_changing(m_TMP, m_Y))
		m_TMP = set_h(m_TMP+m_Y, m_TMP2);
	else
		m_TMP += m_Y;
	write(m_TMP, m_TMP2);
	prefetch();
}
void shs_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_SP = set_l(m_SP, m_A & m_X);
	m_TMP2 = m_A & m_X & ((m_TMP >> 8)+1);
	if(page_changing(m_TMP, m_Y))
		m_TMP = set_h(m_TMP+m_Y, m_TMP2);
	else
		m_TMP += m_Y;
	write(m_TMP, m_TMP2);
	prefetch();
}
void shx_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP2 = m_X & ((m_TMP >> 8)+1);
	if(page_changing(m_TMP, m_Y))
		m_TMP = set_h(m_TMP+m_Y, m_TMP2);
	else
		m_TMP += m_Y;
	write(m_TMP, m_TMP2);
	prefetch();
}
void shy_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP2 = m_Y & ((m_TMP >> 8)+1);
	if(page_changing(m_TMP, m_X))
		m_TMP = set_h(m_TMP+m_X, m_TMP2);
	else
		m_TMP += m_X;
	write(m_TMP, m_TMP2);
	prefetch();
}
void slo_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asl(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void slo_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asl(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void slo_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asl(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void slo_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asl(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void slo_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_asl(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void slo_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_asl(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void slo_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_asl(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	m_A |= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void spa0_imp() {
	read_pc();
	m_pa = (m_pa & 0xffff00) | m_A;
	prefetch();
}
void spa1_imp() {
	read_pc();
	m_pa = (m_pa & 0xff00ff) | (m_A << 8);
	prefetch();
}
void spa2_imp() {
	read_pc();
	m_pa = (m_pa & 0x00ffff) | (m_A << 16);
	prefetch();
}
void spb0_imp() {
	read_pc();
	m_pb = (m_pb & 0xffff00) | m_A;
	prefetch();
}
void spb1_imp() {
	read_pc();
	m_pb = (m_pb & 0xff00ff) | (m_A << 8);
	prefetch();
}
void spb2_imp() {
	read_pc();
	m_pb = (m_pb & 0x00ffff) | (m_A << 16);
	prefetch();
}
void sre_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_lsr(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void sre_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	m_TMP += m_X;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_lsr(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void sre_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_lsr(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void sre_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_lsr(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void sre_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	m_TMP += m_Y;
	m_TMP2 = read(m_TMP);
	write(m_TMP, m_TMP2);
	m_TMP2 = do_lsr(m_TMP2);
	write(m_TMP, m_TMP2);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void sre_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_lsr(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void sre_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	m_TMP = uint8_t(m_TMP+m_X);
	m_TMP2 = read_zeropage(m_TMP);
	write_zeropage(m_TMP, m_TMP2); // dummy write
	m_TMP2 = do_lsr(m_TMP2);
	write_zeropage(m_TMP, m_TMP2);
	m_A ^= m_TMP2;
	set_nz(m_A);
	prefetch();
}
void sta_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	write(m_TMP, m_A);
	prefetch();
}
void sta_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_X));
	write(m_TMP+m_X, m_A);
	prefetch();
}
void sta_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	read(set_l(m_TMP, m_TMP+m_Y));
	write(m_TMP+m_Y, m_A);
	prefetch();
}
void sta_xav_idx() {
	m_TMP2 = read_pc();
	m_PC++;
	read_zeropage(m_TMP2); // dummy read
	m_TMP2 += m_X;
	m_TMP = read_zeropage(m_TMP2 & 0xff);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	write(m_TMP, m_A); // TODO
	prefetch();
}
void sta_xav_idy() {
	m_TMP2 = read_pc();
	m_PC++;
	m_TMP = read_zeropage(m_TMP2);
	m_TMP = set_h(m_TMP, read_zeropage((m_TMP2+1) & 0xff));
	read_zeropage(set_l(m_TMP, m_TMP+m_Y)); // dummy read
	write(m_TMP+m_Y, m_A); // TODO
	prefetch();
}
void sta_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	write_zeropage(m_TMP, m_A);
	prefetch();
}
void sta_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	write_zeropage(uint8_t(m_TMP+m_X), m_A);
	prefetch();
}
void staj_imp() {
	read_pc();
	m_j = m_A;
	prefetch();
}
void stak_imp() {
	read_pc();
	m_k = m_A;
	prefetch();
}
void stal_imp() {
	read_pc();
	m_l = m_A;
	prefetch();
}
void stam_imp() {
	read_pc();
	m_m = m_A;
	prefetch();
}
void stapa_imp() {
	write_full_data(m_pa, m_A);
	prefetch();
}
void stapb_imp() {
	write_full_data(m_pb, m_A);
	prefetch();
}
void stx_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	write(m_TMP, m_X);
	prefetch();
}
void stx_aby() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	write(m_TMP+m_Y, m_X);
	prefetch();
}
void stx_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	write_zeropage(m_TMP, m_X);
	prefetch();
}
void stx_xav_zpy() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	write_zeropage(uint8_t(m_TMP+m_Y), m_X);
	prefetch();
}
void sty_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	write(m_TMP, m_Y);
	prefetch();
}
void sty_abx() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	write(m_TMP+m_X, m_Y);
	prefetch();
}
void sty_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	write_zeropage(m_TMP, m_Y);
	prefetch();
}
void sty_xav_zpx() {
	m_TMP = read_pc();
	m_PC++;
	read_zeropage(m_TMP); // dummy read
	write_zeropage(uint8_t(m_TMP+m_X), m_Y);
	prefetch();
}
void stz_aba() {
	m_TMP = read_pc();
	m_PC++;
	m_TMP = set_h(m_TMP, read_pc());
	m_PC++;
	write(m_TMP, 0x00);
	prefetch();
}
void stz_xav_zpg() {
	m_TMP = read_pc();
	m_PC++;
	write_zeropage(m_TMP, 0x00);
	prefetch();
}
void tax_imp() {
	read_pc();
	m_X = m_A;
	set_nz(m_X);
	prefetch();
}
void tay_imp() {
	read_pc();
	m_Y = m_A;
	set_nz(m_Y);
	prefetch();
}
void tsx_imp() {
	read_pc();
	m_X = m_SP;
	set_nz(m_X);
	prefetch();
}
void txa_imp() {
	read_pc();
	m_A = m_X;
	set_nz(m_A);
	prefetch();
}
void txs_imp() {
	read_pc();
	m_SP = set_l(m_SP, m_X);
	prefetch();
}
void tya_imp() {
	read_pc();
	m_A = m_Y;
	set_nz(m_A);
	prefetch();
}
void dispatch(uint8_t opcode) {
if (super) {
switch(opcode) {
case 0x00: brk_xav_imp(); break;
case 0x01: ora_xav_idx(); break;
case 0x02: cmc_imp(); break;
case 0x03: oraj_imp(); break;
case 0x04: asr_xav_zpg(); break;
case 0x05: ora_xav_zpg(); break;
case 0x06: asl_xav_zpg(); break;
case 0x07: orak_imp(); break;
case 0x08: php_xav_imp(); break;
case 0x09: ora_imm(); break;
case 0x0a: asl_acc(); break;
case 0x0b: oral_imp(); break;
case 0x0c: asr_aba(); break;
case 0x0d: ora_aba(); break;
case 0x0e: asl_aba(); break;
case 0x0f: oram_imp(); break;
case 0x10: bpl_rel(); break;
case 0x11: ora_xav_idy(); break;
case 0x12: phx_imp(); break;
case 0x13: orapa_imp(); break;
case 0x14: asr_xav_zpx(); break;
case 0x15: ora_xav_zpx(); break;
case 0x16: asl_xav_zpx(); break;
case 0x17: orapb_imp(); break;
case 0x18: clc_imp(); break;
case 0x19: ora_aby(); break;
case 0x1a: asr_acc(); break;
case 0x1b: spa0_imp(); break;
case 0x1c: asr_abx(); break;
case 0x1d: ora_abx(); break;
case 0x1e: asl_abx(); break;
case 0x1f: spb0_imp(); break;
case 0x20: jsr_xav_adr(); break;
case 0x21: and_xav_idx(); break;
case 0x22: callf_xa3(); break;
case 0x23: andj_imp(); break;
case 0x24: bit_xav_zpg(); break;
case 0x25: and_xav_zpg(); break;
case 0x26: rol_xav_zpg(); break;
case 0x27: andk_imp(); break;
case 0x28: plp_xav_imp(); break;
case 0x29: and_imm(); break;
case 0x2a: rol_acc(); break;
case 0x2b: andl_imp(); break;
case 0x2c: bit_aba(); break;
case 0x2d: and_aba(); break;
case 0x2e: rol_aba(); break;
case 0x2f: andm_imp(); break;
case 0x30: bmi_rel(); break;
case 0x31: and_xav_idy(); break;
case 0x32: plx_imp(); break;
case 0x33: andpa_imp(); break;
case 0x34: bit_xav_zpx(); break;
case 0x35: and_xav_zpx(); break;
case 0x36: rol_xav_zpx(); break;
case 0x37: andpb_imp(); break;
case 0x38: sec_imp(); break;
case 0x39: and_aby(); break;
case 0x3a: bit_imm(); break;
case 0x3b: lpa0_imp(); break;
case 0x3c: bit_abx(); break;
case 0x3d: and_abx(); break;
case 0x3e: rol_abx(); break;
case 0x3f: lpb0_imp(); break;
case 0x40: rti_xav_imp(); break;
case 0x41: eor_xav_idx(); break;
case 0x42: nop_imp(); break;
case 0x43: eorj_imp(); break;
case 0x44: nop_imp(); break;
case 0x45: eor_xav_zpg(); break;
case 0x46: lsr_xav_zpg(); break;
case 0x47: eork_imp(); break;
case 0x48: pha_xav_imp(); break;
case 0x49: eor_imm(); break;
case 0x4a: lsr_acc(); break;
case 0x4b: eorl_imp(); break;
case 0x4c: jmp_adr(); break;
case 0x4d: eor_aba(); break;
case 0x4e: lsr_aba(); break;
case 0x4f: eorm_imp(); break;
case 0x50: bvc_rel(); break;
case 0x51: eor_xav_idy(); break;
case 0x52: phy_imp(); break;
case 0x53: eorpa_imp(); break;
case 0x54: nop_imp(); break;
case 0x55: eor_xav_zpx(); break;
case 0x56: lsr_xav_zpx(); break;
case 0x57: eorpb_imp(); break;
case 0x58: cli_imp(); break;
case 0x59: eor_aby(); break;
case 0x5a: nop_imp(); break;
case 0x5b: spa1_imp(); break;
case 0x5c: jmp_xa3(); break;
case 0x5d: eor_abx(); break;
case 0x5e: lsr_abx(); break;
case 0x5f: spb1_imp(); break;
case 0x60: rts_xav_imp(); break;
case 0x61: adc_xav_idx(); break;
case 0x62: nop_imp(); break;
case 0x63: adcj_imp(); break;
case 0x64: nop_imp(); break;
case 0x65: adc_xav_zpg(); break;
case 0x66: ror_xav_zpg(); break;
case 0x67: adck_imp(); break;
case 0x68: pla_xav_imp(); break;
case 0x69: adc_imm(); break;
case 0x6a: ror_acc(); break;
case 0x6b: adcl_imp(); break;
case 0x6c: jmp_ind(); break;
case 0x6d: adc_aba(); break;
case 0x6e: ror_aba(); break;
case 0x6f: adcm_imp(); break;
case 0x70: bvs_rel(); break;
case 0x71: adc_xav_idy(); break;
case 0x72: ply_imp(); break;
case 0x73: adcpa_imp(); break;
case 0x74: nop_imp(); break;
case 0x75: adc_xav_zpx(); break;
case 0x76: ror_xav_zpx(); break;
case 0x77: adcpb_imp(); break;
case 0x78: sei_imp(); break;
case 0x79: adc_aby(); break;
case 0x7a: nop_imp(); break;
case 0x7b: lpa1_imp(); break;
case 0x7c: jmpf_ind(); break;
case 0x7d: adc_abx(); break;
case 0x7e: ror_abx(); break;
case 0x7f: lpb1_imp(); break;
case 0x80: retf_imp(); break;
case 0x81: sta_xav_idx(); break;
case 0x82: stz_xav_zpg(); break;
case 0x83: staj_imp(); break;
case 0x84: sty_xav_zpg(); break;
case 0x85: sta_xav_zpg(); break;
case 0x86: stx_xav_zpg(); break;
case 0x87: stak_imp(); break;
case 0x88: dey_imp(); break;
case 0x89: sev_imp(); break;
case 0x8a: txa_imp(); break;
case 0x8b: stal_imp(); break;
case 0x8c: sty_aba(); break;
case 0x8d: sta_aba(); break;
case 0x8e: stx_aba(); break;
case 0x8f: stam_imp(); break;
case 0x90: bcc_rel(); break;
case 0x91: sta_xav_idy(); break;
case 0x92: stz_aba(); break;
case 0x93: stapa_imp(); break;
case 0x94: sty_xav_zpx(); break;
case 0x95: sta_xav_zpx(); break;
case 0x96: stx_xav_zpy(); break;
case 0x97: stapb_imp(); break;
case 0x98: tya_imp(); break;
case 0x99: sta_aby(); break;
case 0x9a: txs_imp(); break;
case 0x9b: spa2_imp(); break;
case 0x9c: sty_abx(); break;
case 0x9d: sta_abx(); break;
case 0x9e: stx_aby(); break;
case 0x9f: spb2_imp(); break;
case 0xa0: ldy_imm(); break;
case 0xa1: lda_xav_idx(); break;
case 0xa2: ldx_imm(); break;
case 0xa3: ldaj_imp(); break;
case 0xa4: ldy_xav_zpg(); break;
case 0xa5: lda_xav_zpg(); break;
case 0xa6: ldx_xav_zpg(); break;
case 0xa7: ldak_imp(); break;
case 0xa8: tay_imp(); break;
case 0xa9: lda_imm(); break;
case 0xaa: tax_imp(); break;
case 0xab: ldal_imp(); break;
case 0xac: ldy_aba(); break;
case 0xad: lda_aba(); break;
case 0xae: ldx_aba(); break;
case 0xaf: ldam_imp(); break;
case 0xb0: bcs_rel(); break;
case 0xb1: lda_xav_idy(); break;
case 0xb2: clr_acc(); break;
case 0xb3: ldapa_imp(); break;
case 0xb4: ldy_xav_zpx(); break;
case 0xb5: lda_xav_zpx(); break;
case 0xb6: ldx_xav_zpy(); break;
case 0xb7: ldapb_imp(); break;
case 0xb8: clv_imp(); break;
case 0xb9: lda_aby(); break;
case 0xba: tsx_imp(); break;
case 0xbb: lpa2_imp(); break;
case 0xbc: ldy_abx(); break;
case 0xbd: lda_abx(); break;
case 0xbe: ldx_aby(); break;
case 0xbf: lpb2_imp(); break;
case 0xc0: cpy_imm(); break;
case 0xc1: cmp_xav_idx(); break;
case 0xc2: dec_acc(); break;
case 0xc3: cmpj_imp(); break;
case 0xc4: cpy_xav_zpg(); break;
case 0xc5: cmp_xav_zpg(); break;
case 0xc6: dec_xav_zpg(); break;
case 0xc7: cmpk_imp(); break;
case 0xc8: iny_imp(); break;
case 0xc9: cmp_imm(); break;
case 0xca: dex_imp(); break;
case 0xcb: cmpl_imp(); break;
case 0xcc: cpy_aba(); break;
case 0xcd: cmp_aba(); break;
case 0xce: dec_aba(); break;
case 0xcf: cmpm_imp(); break;
case 0xd0: bne_rel(); break;
case 0xd1: cmp_xav_idy(); break;
case 0xd2: not_acc(); break;
case 0xd3: cmppa_imp(); break;
case 0xd4: nop_imp(); break;
case 0xd5: cmp_xav_zpx(); break;
case 0xd6: dec_xav_zpx(); break;
case 0xd7: cmppb_imp(); break;
case 0xd8: cld_imp(); break;
case 0xd9: cmp_aby(); break;
case 0xda: nop_imp(); break;
case 0xdb: decpa_imp(); break;
case 0xdc: nop_imp(); break;
case 0xdd: cmp_abx(); break;
case 0xde: dec_abx(); break;
case 0xdf: decpb_imp(); break;
case 0xe0: cpx_imm(); break;
case 0xe1: sbc_xav_idx(); break;
case 0xe2: inc_acc(); break;
case 0xe3: sbcj_imp(); break;
case 0xe4: cpx_xav_zpg(); break;
case 0xe5: sbc_xav_zpg(); break;
case 0xe6: inc_xav_zpg(); break;
case 0xe7: sbck_imp(); break;
case 0xe8: inx_imp(); break;
case 0xe9: sbc_imm(); break;
case 0xea: nop_imp(); break;
case 0xeb: sbcl_imp(); break;
case 0xec: cpx_aba(); break;
case 0xed: sbc_aba(); break;
case 0xee: inc_aba(); break;
case 0xef: sbcm_imp(); break;
case 0xf0: beq_rel(); break;
case 0xf1: sbc_xav_idy(); break;
case 0xf2: neg_acc(); break;
case 0xf3: sbcpa_imp(); break;
case 0xf4: nop_imp(); break;
case 0xf5: sbc_xav_zpx(); break;
case 0xf6: inc_xav_zpx(); break;
case 0xf7: sbcpb_imp(); break;
case 0xf8: sed_imp(); break;
case 0xf9: sbc_aby(); break;
case 0xfa: nop_imp(); break;
case 0xfb: incpa_imp(); break;
case 0xfc: nop_imp(); break;
case 0xfd: sbc_abx(); break;
case 0xfe: inc_abx(); break;
case 0xff: incpb_imp(); break;
}
} else {
switch(opcode) {
case 0x00: brk_xav_imp(); break;
case 0x01: ora_xav_idx(); break;
case 0x02: kil_non(); break;
case 0x03: slo_xav_idx(); break;
case 0x04: nop_xav_zpg(); break;
case 0x05: ora_xav_zpg(); break;
case 0x06: asl_xav_zpg(); break;
case 0x07: slo_xav_zpg(); break;
case 0x08: php_xav_imp(); break;
case 0x09: ora_imm(); break;
case 0x0a: asl_acc(); break;
case 0x0b: anc_imm(); break;
case 0x0c: nop_aba(); break;
case 0x0d: ora_aba(); break;
case 0x0e: asl_aba(); break;
case 0x0f: slo_aba(); break;
case 0x10: bpl_rel(); break;
case 0x11: ora_xav_idy(); break;
case 0x12: kil_non(); break;
case 0x13: slo_xav_idy(); break;
case 0x14: nop_xav_zpx(); break;
case 0x15: ora_xav_zpx(); break;
case 0x16: asl_xav_zpx(); break;
case 0x17: slo_xav_zpx(); break;
case 0x18: clc_imp(); break;
case 0x19: ora_aby(); break;
case 0x1a: nop_imp(); break;
case 0x1b: slo_aby(); break;
case 0x1c: nop_abx(); break;
case 0x1d: ora_abx(); break;
case 0x1e: asl_abx(); break;
case 0x1f: slo_abx(); break;
case 0x20: jsr_xav_adr(); break;
case 0x21: and_xav_idx(); break;
case 0x22: callf_xa3(); break;
case 0x23: rla_xav_idx(); break;
case 0x24: bit_xav_zpg(); break;
case 0x25: and_xav_zpg(); break;
case 0x26: rol_xav_zpg(); break;
case 0x27: rla_xav_zpg(); break;
case 0x28: plp_xav_imp(); break;
case 0x29: and_imm(); break;
case 0x2a: rol_acc(); break;
case 0x2b: anc_imm(); break;
case 0x2c: bit_aba(); break;
case 0x2d: and_aba(); break;
case 0x2e: rol_aba(); break;
case 0x2f: rla_aba(); break;
case 0x30: bmi_rel(); break;
case 0x31: and_xav_idy(); break;
case 0x32: kil_non(); break;
case 0x33: rla_xav_idy(); break;
case 0x34: nop_xav_zpx(); break;
case 0x35: and_xav_zpx(); break;
case 0x36: rol_xav_zpx(); break;
case 0x37: rla_xav_zpx(); break;
case 0x38: sec_imp(); break;
case 0x39: and_aby(); break;
case 0x3a: nop_imp(); break;
case 0x3b: rla_aby(); break;
case 0x3c: nop_abx(); break;
case 0x3d: and_abx(); break;
case 0x3e: rol_abx(); break;
case 0x3f: rla_abx(); break;
case 0x40: rti_xav_imp(); break;
case 0x41: eor_xav_idx(); break;
case 0x42: kil_non(); break;
case 0x43: sre_xav_idx(); break;
case 0x44: nop_xav_zpg(); break;
case 0x45: eor_xav_zpg(); break;
case 0x46: lsr_xav_zpg(); break;
case 0x47: sre_xav_zpg(); break;
case 0x48: pha_xav_imp(); break;
case 0x49: eor_imm(); break;
case 0x4a: lsr_acc(); break;
case 0x4b: asr_imm(); break;
case 0x4c: jmp_adr(); break;
case 0x4d: eor_aba(); break;
case 0x4e: lsr_aba(); break;
case 0x4f: sre_aba(); break;
case 0x50: bvc_rel(); break;
case 0x51: eor_xav_idy(); break;
case 0x52: kil_non(); break;
case 0x53: sre_xav_idy(); break;
case 0x54: nop_xav_zpx(); break;
case 0x55: eor_xav_zpx(); break;
case 0x56: lsr_xav_zpx(); break;
case 0x57: sre_xav_zpx(); break;
case 0x58: cli_imp(); break;
case 0x59: eor_aby(); break;
case 0x5a: nop_imp(); break;
case 0x5b: sre_aby(); break;
case 0x5c: nop_abx(); break;
case 0x5d: eor_abx(); break;
case 0x5e: lsr_abx(); break;
case 0x5f: sre_abx(); break;
case 0x60: rts_xav_imp(); break;
case 0x61: adc_xav_idx(); break;
case 0x62: kil_non(); break;
case 0x63: rra_xav_idx(); break;
case 0x64: nop_xav_zpg(); break;
case 0x65: adc_xav_zpg(); break;
case 0x66: ror_xav_zpg(); break;
case 0x67: rra_xav_zpg(); break;
case 0x68: pla_xav_imp(); break;
case 0x69: adc_imm(); break;
case 0x6a: ror_acc(); break;
case 0x6b: arr_imm(); break;
case 0x6c: jmp_ind(); break;
case 0x6d: adc_aba(); break;
case 0x6e: ror_aba(); break;
case 0x6f: rra_aba(); break;
case 0x70: bvs_rel(); break;
case 0x71: adc_xav_idy(); break;
case 0x72: kil_non(); break;
case 0x73: rra_xav_idy(); break;
case 0x74: nop_xav_zpx(); break;
case 0x75: adc_xav_zpx(); break;
case 0x76: ror_xav_zpx(); break;
case 0x77: rra_xav_zpx(); break;
case 0x78: sei_imp(); break;
case 0x79: adc_aby(); break;
case 0x7a: nop_imp(); break;
case 0x7b: rra_aby(); break;
case 0x7c: nop_abx(); break;
case 0x7d: adc_abx(); break;
case 0x7e: ror_abx(); break;
case 0x7f: rra_abx(); break;
case 0x80: retf_imp(); break;
case 0x81: sta_xav_idx(); break;
case 0x82: nop_imm(); break;
case 0x83: sax_xav_idx(); break;
case 0x84: sty_xav_zpg(); break;
case 0x85: sta_xav_zpg(); break;
case 0x86: stx_xav_zpg(); break;
case 0x87: sax_xav_zpg(); break;
case 0x88: dey_imp(); break;
case 0x89: nop_imm(); break;
case 0x8a: txa_imp(); break;
case 0x8b: ane_imm(); break;
case 0x8c: sty_aba(); break;
case 0x8d: sta_aba(); break;
case 0x8e: stx_aba(); break;
case 0x8f: sax_aba(); break;
case 0x90: bcc_rel(); break;
case 0x91: sta_xav_idy(); break;
case 0x92: kil_non(); break;
case 0x93: sha_xav_idy(); break;
case 0x94: sty_xav_zpx(); break;
case 0x95: sta_xav_zpx(); break;
case 0x96: stx_xav_zpy(); break;
case 0x97: sax_xav_zpy(); break;
case 0x98: tya_imp(); break;
case 0x99: sta_aby(); break;
case 0x9a: txs_imp(); break;
case 0x9b: shs_aby(); break;
case 0x9c: shy_abx(); break;
case 0x9d: sta_abx(); break;
case 0x9e: shx_aby(); break;
case 0x9f: sha_aby(); break;
case 0xa0: ldy_imm(); break;
case 0xa1: lda_xav_idx(); break;
case 0xa2: ldx_imm(); break;
case 0xa3: lax_xav_idx(); break;
case 0xa4: ldy_xav_zpg(); break;
case 0xa5: lda_xav_zpg(); break;
case 0xa6: ldx_xav_zpg(); break;
case 0xa7: lax_xav_zpg(); break;
case 0xa8: tay_imp(); break;
case 0xa9: lda_imm(); break;
case 0xaa: tax_imp(); break;
case 0xab: lxa_imm(); break;
case 0xac: ldy_aba(); break;
case 0xad: lda_aba(); break;
case 0xae: ldx_aba(); break;
case 0xaf: lax_aba(); break;
case 0xb0: bcs_rel(); break;
case 0xb1: lda_xav_idy(); break;
case 0xb2: kil_non(); break;
case 0xb3: lax_xav_idy(); break;
case 0xb4: ldy_xav_zpx(); break;
case 0xb5: lda_xav_zpx(); break;
case 0xb6: ldx_xav_zpy(); break;
case 0xb7: lax_xav_zpy(); break;
case 0xb8: clv_imp(); break;
case 0xb9: lda_aby(); break;
case 0xba: tsx_imp(); break;
case 0xbb: las_aby(); break;
case 0xbc: ldy_abx(); break;
case 0xbd: lda_abx(); break;
case 0xbe: ldx_aby(); break;
case 0xbf: lax_aby(); break;
case 0xc0: cpy_imm(); break;
case 0xc1: cmp_xav_idx(); break;
case 0xc2: nop_imm(); break;
case 0xc3: dcp_xav_idx(); break;
case 0xc4: cpy_xav_zpg(); break;
case 0xc5: cmp_xav_zpg(); break;
case 0xc6: dec_xav_zpg(); break;
case 0xc7: dcp_xav_zpg(); break;
case 0xc8: iny_imp(); break;
case 0xc9: cmp_imm(); break;
case 0xca: dex_imp(); break;
case 0xcb: sbx_imm(); break;
case 0xcc: cpy_aba(); break;
case 0xcd: cmp_aba(); break;
case 0xce: dec_aba(); break;
case 0xcf: dcp_aba(); break;
case 0xd0: bne_rel(); break;
case 0xd1: cmp_xav_idy(); break;
case 0xd2: kil_non(); break;
case 0xd3: dcp_xav_idy(); break;
case 0xd4: nop_xav_zpx(); break;
case 0xd5: cmp_xav_zpx(); break;
case 0xd6: dec_xav_zpx(); break;
case 0xd7: dcp_xav_zpx(); break;
case 0xd8: cld_imp(); break;
case 0xd9: cmp_aby(); break;
case 0xda: nop_imp(); break;
case 0xdb: dcp_aby(); break;
case 0xdc: nop_abx(); break;
case 0xdd: cmp_abx(); break;
case 0xde: dec_abx(); break;
case 0xdf: dcp_abx(); break;
case 0xe0: cpx_imm(); break;
case 0xe1: sbc_xav_idx(); break;
case 0xe2: nop_imm(); break;
case 0xe3: isb_xav_idx(); break;
case 0xe4: cpx_xav_zpg(); break;
case 0xe5: sbc_xav_zpg(); break;
case 0xe6: inc_xav_zpg(); break;
case 0xe7: isb_xav_zpg(); break;
case 0xe8: inx_imp(); break;
case 0xe9: sbc_imm(); break;
case 0xea: nop_imp(); break;
case 0xeb: sbc_imm(); break;
case 0xec: cpx_aba(); break;
case 0xed: sbc_aba(); break;
case 0xee: inc_aba(); break;
case 0xef: isb_aba(); break;
case 0xf0: beq_rel(); break;
case 0xf1: sbc_xav_idy(); break;
case 0xf2: kil_non(); break;
case 0xf3: isb_xav_idy(); break;
case 0xf4: nop_xav_zpx(); break;
case 0xf5: sbc_xav_zpx(); break;
case 0xf6: inc_xav_zpx(); break;
case 0xf7: isb_xav_zpx(); break;
case 0xf8: sed_imp(); break;
case 0xf9: sbc_aby(); break;
case 0xfa: nop_imp(); break;
case 0xfb: isb_aby(); break;
case 0xfc: nop_abx(); break;
case 0xfd: sbc_abx(); break;
case 0xfe: inc_abx(); break;
case 0xff: isb_abx(); break;
}
}
}
