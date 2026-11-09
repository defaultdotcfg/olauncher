"""gcn_dis.py shader.dxbc | blob.bin -> disassemble the GCN (Sea Islands / Durango) microcode embedded in a Durango DXBC shader.

Pipeline: DXBC -> SHEX CUSTOMDATA blocks -> zlib -> 'shader binary' container -> code (starts after a 0x7e000000 NOP/padding run,
ends at s_endpgm 0xBF810000). Only the encodings/opcodes seen in the game's UI shaders are named; others print as raw.
"""
import sys, struct, zlib, re, os

SOP2 = {0:'s_add_u32',1:'s_sub_u32',2:'s_add_i32',3:'s_sub_i32',4:'s_addc_u32',5:'s_subb_u32',6:'s_min_i32',7:'s_min_u32',8:'s_max_i32',9:'s_max_u32',
        10:'s_cselect_b32',11:'s_cselect_b64',14:'s_and_b32',15:'s_and_b64',16:'s_or_b32',17:'s_or_b64',18:'s_xor_b32',19:'s_xor_b64',
        20:'s_andn2_b32',21:'s_andn2_b64',22:'s_orn2_b32',23:'s_orn2_b64',24:'s_nand_b32',25:'s_nand_b64',26:'s_nor_b32',27:'s_nor_b64',
        28:'s_xnor_b32',29:'s_xnor_b64',30:'s_lshl_b32',31:'s_lshl_b64',32:'s_lshr_b32',33:'s_lshr_b64',34:'s_ashr_i32',35:'s_ashr_i64',
        36:'s_bfm_b32',37:'s_bfm_b64',38:'s_mul_i32',39:'s_bfe_u32',40:'s_bfe_i32',41:'s_bfe_u64',42:'s_bfe_i64',44:'s_absdiff_i32'}
SOP1 = {3:'s_mov_b32',4:'s_mov_b64',5:'s_cmov_b32',6:'s_cmov_b64',7:'s_not_b32',8:'s_not_b64',9:'s_wqm_b32',10:'s_wqm_b64',11:'s_brev_b32',12:'s_brev_b64',
        13:'s_bcnt0_i32_b32',14:'s_bcnt0_i32_b64',15:'s_bcnt1_i32_b32',16:'s_bcnt1_i32_b64',17:'s_ff0_i32_b32',19:'s_ff1_i32_b32',
        21:'s_flbit_i32_b32',25:'s_sext_i32_i8',26:'s_sext_i32_i16',27:'s_bitset0_b32',28:'s_bitset0_b64',29:'s_bitset1_b32',30:'s_bitset1_b64',
        31:'s_getpc_b64',32:'s_setpc_b64',33:'s_swappc_b64',34:'s_rfe_b64',36:'s_and_saveexec_b64',37:'s_or_saveexec_b64',38:'s_xor_saveexec_b64',
        39:'s_andn2_saveexec_b64',40:'s_orn2_saveexec_b64',41:'s_nand_saveexec_b64',42:'s_nor_saveexec_b64',43:'s_xnor_saveexec_b64',44:'s_quadmask_b32',
        45:'s_quadmask_b64',48:'s_abs_i32'}
SOPK = {0:'s_movk_i32',2:'s_cmovk_i32',3:'s_cmpk_eq_i32',4:'s_cmpk_lg_i32',5:'s_cmpk_gt_i32',6:'s_cmpk_ge_i32',7:'s_cmpk_lt_i32',8:'s_cmpk_le_i32',
        15:'s_addk_i32',16:'s_mulk_i32',18:'s_getreg_b32',19:'s_setreg_b32'}
SOPC = {0:'s_cmp_eq_i32',1:'s_cmp_lg_i32',2:'s_cmp_gt_i32',3:'s_cmp_ge_i32',4:'s_cmp_lt_i32',5:'s_cmp_le_i32',6:'s_cmp_eq_u32',7:'s_cmp_lg_u32',
        8:'s_cmp_gt_u32',9:'s_cmp_ge_u32',10:'s_cmp_lt_u32',11:'s_cmp_le_u32',12:'s_bitcmp0_b32',13:'s_bitcmp1_b32'}
SOPP = {0:'s_nop',1:'s_endpgm',2:'s_branch',4:'s_cbranch_scc0',5:'s_cbranch_scc1',6:'s_cbranch_vccz',7:'s_cbranch_vccnz',8:'s_cbranch_execz',
        9:'s_cbranch_execnz',10:'s_barrier',12:'s_waitcnt',13:'s_sethalt',14:'s_sleep',15:'s_setprio',16:'s_sendmsg',17:'s_sendmsghalt',18:'s_trap',
        19:'s_icache_inv',20:'s_incperflevel',21:'s_decperflevel',22:'s_ttracedata'}
SMRD = {0:'s_load_dword',1:'s_load_dwordx2',2:'s_load_dwordx4',3:'s_load_dwordx8',4:'s_load_dwordx16',8:'s_buffer_load_dword',9:'s_buffer_load_dwordx2',
        10:'s_buffer_load_dwordx4',11:'s_buffer_load_dwordx8',12:'s_buffer_load_dwordx16',30:'s_dcache_inv_vol',31:'s_memtime',29:'s_dcache_inv'}
SMRD_N = {0:1,1:2,2:4,3:8,4:16,8:1,9:2,10:4,11:8,12:16}
VOP2 = {0:'v_cndmask_b32',1:'v_readlane_b32',2:'v_writelane_b32',3:'v_add_f32',4:'v_sub_f32',5:'v_subrev_f32',6:'v_mac_legacy_f32',7:'v_mul_legacy_f32',
        8:'v_mul_f32',9:'v_mul_i32_i24',10:'v_mul_hi_i32_i24',11:'v_mul_u32_u24',12:'v_mul_hi_u32_u24',13:'v_min_legacy_f32',14:'v_max_legacy_f32',
        15:'v_min_f32',16:'v_max_f32',17:'v_min_i32',18:'v_max_i32',19:'v_min_u32',20:'v_max_u32',21:'v_lshr_b32',22:'v_lshrrev_b32',23:'v_ashr_i32',
        24:'v_ashrrev_i32',25:'v_lshl_b32',26:'v_lshlrev_b32',27:'v_and_b32',28:'v_or_b32',29:'v_xor_b32',30:'v_bfm_b32',31:'v_mac_f32',32:'v_madmk_f32',
        33:'v_madak_f32',34:'v_bcnt_u32_b32',35:'v_mbcnt_lo_u32_b32',36:'v_mbcnt_hi_u32_b32',37:'v_add_i32',38:'v_sub_i32',39:'v_subrev_i32',40:'v_addc_u32',
        41:'v_subb_u32',42:'v_subbrev_u32',43:'v_ldexp_f32',44:'v_cvt_pkaccum_u8_f32',45:'v_cvt_pknorm_i16_f32',46:'v_cvt_pknorm_u16_f32',
        47:'v_cvt_pkrtz_f16_f32',48:'v_cvt_pk_u16_u32',49:'v_cvt_pk_i16_i32'}
VOP1 = {0:'v_nop',1:'v_mov_b32',2:'v_readfirstlane_b32',3:'v_cvt_i32_f64',4:'v_cvt_f64_i32',5:'v_cvt_f32_i32',6:'v_cvt_f32_u32',7:'v_cvt_u32_f32',
        8:'v_cvt_i32_f32',10:'v_cvt_f16_f32',11:'v_cvt_f32_f16',12:'v_cvt_rpi_i32_f32',13:'v_cvt_flr_i32_f32',14:'v_cvt_off_f32_i4',15:'v_cvt_f32_f64',
        16:'v_cvt_f64_f32',17:'v_cvt_f32_ubyte0',18:'v_cvt_f32_ubyte1',19:'v_cvt_f32_ubyte2',20:'v_cvt_f32_ubyte3',21:'v_cvt_u32_f64',22:'v_cvt_f64_u32',
        32:'v_fract_f32',33:'v_trunc_f32',34:'v_ceil_f32',35:'v_rndne_f32',36:'v_floor_f32',37:'v_exp_f32',39:'v_log_f32',42:'v_rcp_f32',43:'v_rcp_iflag_f32',
        46:'v_rsq_f32',47:'v_rcp_f64',49:'v_rsq_f64',51:'v_sqrt_f32',52:'v_sqrt_f64',53:'v_sin_f32',54:'v_cos_f32',55:'v_not_b32',56:'v_bfrev_b32',
        57:'v_ffbh_u32',58:'v_ffbl_b32',59:'v_ffbh_i32',60:'v_frexp_exp_i32_f64',61:'v_frexp_mant_f64',62:'v_fract_f64',63:'v_frexp_exp_i32_f32',
        64:'v_frexp_mant_f32',65:'v_clrexcp',66:'v_movreld_b32',67:'v_movrels_b32',68:'v_movrelsd_b32'}
VOPC_CMP = ['f','lt','eq','le','gt','lg','ge','o','u','nge','nlg','ngt','nle','neq','nlt','tru']   # f32 compares: base 0x00; 0x40 = cmps etc.
VINTRP = {0:'v_interp_p1_f32',1:'v_interp_p2_f32',2:'v_interp_mov_f32'}
VOP3_EXTRA = {320:'v_mad_legacy_f32',321:'v_mad_f32',322:'v_mad_i32_i24',323:'v_mad_u32_u24',324:'v_cubeid_f32',325:'v_cubesc_f32',326:'v_cubetc_f32',327:'v_cubema_f32',
              328:'v_bfe_u32',329:'v_bfe_i32',330:'v_bfi_b32',331:'v_fma_f32',332:'v_fma_f64',333:'v_lerp_u8',334:'v_alignbit_b32',335:'v_alignbyte_b32',
              336:'v_mullit_f32',337:'v_min3_f32',338:'v_min3_i32',339:'v_min3_u32',340:'v_max3_f32',341:'v_max3_i32',342:'v_max3_u32',343:'v_med3_f32',
              344:'v_med3_i32',345:'v_med3_u32',346:'v_sad_u8',347:'v_sad_hi_u8',348:'v_sad_u16',349:'v_sad_u32',350:'v_cvt_pk_u8_f32',351:'v_div_fixup_f32',
              352:'v_div_fixup_f64',353:'v_lshl_b64',354:'v_lshr_b64',355:'v_ashr_i64',356:'v_add_f64',357:'v_mul_f64',358:'v_min_f64',359:'v_max_f64',360:'v_ldexp_f64',
              361:'v_mul_lo_u32',362:'v_mul_hi_u32',363:'v_mul_lo_i32',364:'v_mul_hi_i32',365:'v_div_scale_f32',366:'v_div_scale_f64',367:'v_div_fmas_f32',368:'v_div_fmas_f64',
              369:'v_msad_u8',370:'v_qsad_u8',371:'v_mqsad_u8',372:'v_trig_preop_f64'}
MIMG = {0:'image_load',1:'image_load_mip',2:'image_load_pck',3:'image_load_pck_sgn',4:'image_load_mip_pck',5:'image_load_mip_pck_sgn',8:'image_store',9:'image_store_mip',
        14:'image_get_resinfo',32:'image_sample',33:'image_sample_cl',34:'image_sample_d',35:'image_sample_d_cl',36:'image_sample_l',37:'image_sample_b',
        38:'image_sample_b_cl',39:'image_sample_lz',40:'image_sample_c',41:'image_sample_c_cl',42:'image_sample_c_d',44:'image_sample_c_l',45:'image_sample_c_b',
        47:'image_sample_c_lz',48:'image_sample_o',64:'image_gather4',65:'image_gather4_cl',68:'image_gather4_l',69:'image_gather4_b',71:'image_gather4_lz',
        104:'image_get_lod'}
MUBUF = {0:'buffer_load_format_x',1:'buffer_load_format_xy',2:'buffer_load_format_xyz',3:'buffer_load_format_xyzw',4:'buffer_store_format_x',
         5:'buffer_store_format_xy',6:'buffer_store_format_xyz',7:'buffer_store_format_xyzw',8:'buffer_load_ubyte',9:'buffer_load_sbyte',10:'buffer_load_ushort',
         11:'buffer_load_sshort',12:'buffer_load_dword',13:'buffer_load_dwordx2',14:'buffer_load_dwordx4',15:'buffer_load_dwordx3',24:'buffer_store_byte',
         26:'buffer_store_short',28:'buffer_store_dword',29:'buffer_store_dwordx2',30:'buffer_store_dwordx4',31:'buffer_store_dwordx3'}
MUBUF.update({48:'buffer_atomic_swap',49:'buffer_atomic_cmpswap',50:'buffer_atomic_add',51:'buffer_atomic_sub',52:'buffer_atomic_rsub',53:'buffer_atomic_smin',
  54:'buffer_atomic_umin',55:'buffer_atomic_smax',56:'buffer_atomic_umax',57:'buffer_atomic_and',58:'buffer_atomic_or',59:'buffer_atomic_xor',60:'buffer_atomic_inc',61:'buffer_atomic_dec'})
DS = {0:'ds_add_u32',1:'ds_sub_u32',2:'ds_rsub_u32',3:'ds_inc_u32',4:'ds_dec_u32',5:'ds_min_i32',6:'ds_max_i32',7:'ds_min_u32',8:'ds_max_u32',9:'ds_and_b32',10:'ds_or_b32',11:'ds_xor_b32',
  13:'ds_write_b32',14:'ds_write2_b32',15:'ds_write2st64_b32',30:'ds_write_b8',31:'ds_write_b16',32:'ds_add_rtn_u32',33:'ds_sub_rtn_u32',34:'ds_rsub_rtn_u32',35:'ds_inc_rtn_u32',36:'ds_dec_rtn_u32',
  37:'ds_min_rtn_i32',38:'ds_max_rtn_i32',39:'ds_min_rtn_u32',40:'ds_max_rtn_u32',41:'ds_and_rtn_b32',42:'ds_or_rtn_b32',43:'ds_xor_rtn_b32',45:'ds_wrxchg_rtn_b32',48:'ds_cmpst_rtn_b32',
  53:'ds_swizzle_b32',54:'ds_read_b32',55:'ds_read2_b32',56:'ds_read2st64_b32',57:'ds_read_i8',58:'ds_read_u8',59:'ds_read_i16',60:'ds_read_u16',77:'ds_write_b64',78:'ds_write2_b64',118:'ds_read_b64',119:'ds_read2_b64'}


def vopc_name(op):
    I=['f','lt','eq','le','gt','ne','ge','t']
    if op<0x80:
        g=op>>4; t=['f32','f32','f64','f64','f32','f32','f64','f64'][op>>4]
        pre='v_cmps' if op>=0x40 else 'v_cmp'
        x='x' if (op>>4)&1 else ''
        return f"{pre}{x}_{VOPC_CMP[op&15]}_{t}"
    if op>=0x80:
        x='x' if (op>>4)&1 else ''
        grp=op>>5; lo=op&7
        if (op&0xF)==8: return f"v_cmp{x}_class_f32"
        if (op&0xF)==9: return f"v_cmp{x}_class_f64" 
        if (op&8)==0:
            t={4:'i32',5:'i64',6:'u32',7:'u64'}[grp] if False else ['i32','i64','u32','u64'][(op-0x80)>>5]
            return f"v_cmp{x}_{I[lo]}_{t}"
    return f"v_cmp_op{op:#x}"

def sreg(n, w=1):
    if n <= 103:
        return f"s{n}" if w == 1 else f"s[{n}:{n+w-1}]"
    names = {106:'vcc_lo',107:'vcc_hi',124:'m0',126:'exec_lo',127:'exec_hi',128:'0',253:'scc',251:'vccz',252:'execz'}
    if w == 2:
        if n == 106: return 'vcc'
        if n == 126: return 'exec'
    return names.get(n, f"sreg{n}")

def src(n, lit=None, w=1):
    """9-bit source operand (SSRC / VOP SRC0)."""
    if n < 256:
        if 129 <= n <= 192: return str(n - 128)
        if 193 <= n <= 208: return str(-(n - 192))
        if n == 240: return '0.5'
        if n == 241: return '-0.5'
        if n == 242: return '1.0'
        if n == 243: return '-1.0'
        if n == 244: return '2.0'
        if n == 245: return '-2.0'
        if n == 246: return '4.0'
        if n == 247: return '-4.0'
        if n == 255: return f"0x{lit:x}" if lit is not None else 'literal'
        return sreg(n, w)
    v = n - 256
    return f"v{v}" if w == 1 else f"v[{v}:{v+w-1}]"

def uses_lit(n): return n == 255

def disasm(code):
    out = []; i = 0; n = len(code) // 4
    W = lambda k: struct.unpack_from('<I', code, 4 * k)[0]
    while i < n:
        w = W(i); pc = i * 4; text = None; ln = 1
        top9 = w >> 23
        if top9 == 0x17D:            # SOP1
            op = (w >> 8) & 0xFF; sd = (w >> 16) & 0x7F; s0 = w & 0xFF
            lit = W(i + 1) if s0 == 255 else None; ln = 2 if lit is not None else 1
            wide = op in (4, 6, 8, 10, 12, 28, 30, 31, 32, 33, 34, 36, 37, 38, 39, 40, 41, 42, 43, 45)
            name = SOP1.get(op, f"sop1_op{op}")
            text = f"{name} {sreg(sd, 2 if wide else 1)}, {src(s0, lit, 2 if wide else 1)}"
        elif top9 == 0x17E:          # SOPC
            op = (w >> 16) & 0x7F; s0 = w & 0xFF; s1 = (w >> 8) & 0xFF
            text = f"{SOPC.get(op, f'sopc_op{op}')} {src(s0)}, {src(s1)}"
        elif top9 == 0x17F:          # SOPP
            op = (w >> 16) & 0x7F; imm = w & 0xFFFF
            name = SOPP.get(op, f"sopp_op{op}")
            if op == 12:
                vm = imm & 0xF; lg = (imm >> 4) & 7; ex = (imm >> 8) & 0xF
                text = f"s_waitcnt vmcnt({vm}) expcnt({lg}) lgkmcnt({ex})"
            elif op in (2, 4, 5, 6, 7, 8, 9):
                off = imm - 0x10000 if imm & 0x8000 else imm
                text = f"{name} label_{pc + 4 + off * 4:04x}"
            else:
                text = name if op == 1 else f"{name} 0x{imm:x}"
        elif (w >> 28) == 0xB:       # SOPK
            op = (w >> 23) & 0x1F; sd = (w >> 16) & 0x7F; imm = w & 0xFFFF
            if imm & 0x8000: imm -= 0x10000
            text = f"{SOPK.get(op, f'sopk_op{op}')} {sreg(sd)}, {imm}"
        elif (w >> 30) == 2:         # SOP2
            op = (w >> 23) & 0x7F; sd = (w >> 16) & 0x7F; s1 = (w >> 8) & 0xFF; s0 = w & 0xFF
            lit = W(i + 1) if 255 in (s0, s1) else None; ln = 2 if lit is not None else 1
            wide = op in (11, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 41, 42)
            text = f"{SOP2.get(op, f'sop2_op{op}')} {sreg(sd, 2 if wide else 1)}, {src(s0, lit, 2 if wide else 1)}, {src(s1, lit, 2 if wide and op not in (31,33,35) else 1)}"
        elif (w >> 27) == 0x18:      # SMRD
            op = (w >> 22) & 0x1F; sd = (w >> 15) & 0x7F; sb = ((w >> 9) & 0x3F) * 2; imm = (w >> 8) & 1; off = w & 0xFF
            cnt = SMRD_N.get(op, 1)
            if (not imm) and off == 255:
                lit = W(i + 1); ln = 2; o = f"lit:0x{lit:x}"   # byte offset literal
            else:
                o = f"0x{off:x}" if imm else sreg(off)
            if imm and op < 8: o += f" ; byte offset {off*4}"
            text = f"{SMRD.get(op, f'smrd_op{op}')} {sreg(sd, cnt)}, {sreg(sb, 2)}, {o}" + (f"   ; entry {off*4//32}" if (imm and cnt == 8 and op == 3) else "")
        elif (w >> 26) == 0x32 and ((w >> 26) & 0x3F) == 0x32:   # VINTRP (110010)
            op = (w >> 16) & 3; vd = (w >> 18) & 0xFF; at = (w >> 10) & 0x3F; ch = (w >> 8) & 3; vs = w & 0xFF
            text = f"{VINTRP.get(op, f'vintrp_op{op}')} v{vd}, v{vs}, attr{at}.{'xyzw'[ch]}"
        elif (w >> 26) == 0x34:      # VOP3 (110100)
            w2 = W(i + 1); ln = 2
            op = (w >> 17) & 0x1FF; vd = w & 0xFF; abs_ = (w >> 8) & 7; clamp = (w >> 11) & 1
            s0 = w2 & 0x1FF; s1 = (w2 >> 9) & 0x1FF; s2 = (w2 >> 18) & 0x1FF; omod = (w2 >> 27) & 3; neg = (w2 >> 29) & 7
            def mod(k, t):
                if (abs_ >> k) & 1: t = f"|{t}|"
                if (neg >> k) & 1: t = "-" + t
                return t
            name = VOP3_EXTRA.get(op)
            if name is None:
                if op < 0x100:   # VOPC/ VOP3 compare form
                    name = vopc_name(op)
                    vd_s = sreg(vd, 2)
                    text = f"{name} {vd_s}, {mod(0, src(s0))}, {mod(1, src(s1))}"
                elif op - 0x100 in VOP2: name = VOP2[op - 0x100] + '_e64'
                elif op - 0x180 in VOP1: name = VOP1[op - 0x180] + '_e64'
                else: name = f"vop3_op{op}"
            if text is None:
                args = [mod(0, src(s0)), mod(1, src(s1))]
                if name in VOP3_EXTRA.values() or op in (321,331): args.append(mod(2, src(s2)))
                elif name in ('v_cndmask_b32_e64', 'v_addc_u32_e64', 'v_subb_u32_e64', 'v_subbrev_u32_e64'): args.append(sreg(s2, 2))   # VOP3 form: carry/select mask is an SGPR pair in src2
                text = f"{name} v{vd}, " + ", ".join(args) + (" clamp" if clamp else "") + (f" omod:{omod}" if omod else "")
        elif (w >> 26) == 0x3C:      # MIMG (111100)
            w2 = W(i + 1); ln = 2
            op = (w >> 18) & 0x7F; dmask = (w >> 8) & 0xF; unorm = (w >> 12) & 1
            vaddr = w2 & 0xFF; vdata = (w2 >> 8) & 0xFF; srsrc = ((w2 >> 16) & 0x1F) * 4; ssamp = ((w2 >> 21) & 0x1F) * 4
            text = f"{MIMG.get(op, f'mimg_op{op}')} v[{vdata}:{vdata + bin(dmask).count('1') - 1}], v{vaddr}.., s[{srsrc}:{srsrc+7}], s[{ssamp}:{ssamp+3}] dmask:0x{dmask:x}" + (" unorm" if unorm else "")
        elif (w >> 26) == 0x38:      # MUBUF (111000)
            w2 = W(i + 1); ln = 2
            op = (w >> 18) & 0x7F; off = w & 0xFFF; offen = (w >> 12) & 1; idxen = (w >> 13) & 1
            vaddr = w2 & 0xFF; vdata = (w2 >> 8) & 0xFF; srsrc = ((w2 >> 16) & 0x1F) * 4; soff = (w2 >> 24) & 0xFF
            glc = (w >> 14) & 1
            nm_ = MUBUF.get(op, f'mubuf_op{op}')
            nreg = 1
            mm_ = re.search(r'dword(x(\d))?$', nm_)
            if mm_ and mm_.group(2): nreg = int(mm_.group(2))
            mf_ = re.search(r'format_(x|xy|xyz|xyzw)$', nm_)
            if mf_: nreg = len(mf_.group(1))
            if nm_ == 'buffer_atomic_cmpswap': nreg = 2
            text = (f"{nm_} v[{vdata}:{vdata + nreg - 1}], v{vaddr}, s[{srsrc}:{srsrc+3}], {src(soff)} offset:{off}" + (" offen" if offen else "") + (" idxen" if idxen else "") + (" glc" if glc else ""))
        elif (w >> 26) == 0x3A:      # MTBUF
            w2 = W(i + 1); ln = 2
            off = w & 0xFFF; offen = (w >> 12) & 1; idxen = (w >> 13) & 1; op = (w >> 16) & 7; dfmt = (w >> 19) & 15; nfmt = (w >> 23) & 7
            vaddr = w2 & 0xFF; vdata = (w2 >> 8) & 0xFF; srsrc = ((w2 >> 16) & 0x1F) * 4; soff = (w2 >> 24) & 0xFF
            nc = [1, 2, 3, 4, 1, 2, 3, 4][op]
            nm = ['tbuffer_load_format_x', 'tbuffer_load_format_xy', 'tbuffer_load_format_xyz', 'tbuffer_load_format_xyzw',
                  'tbuffer_store_format_x', 'tbuffer_store_format_xy', 'tbuffer_store_format_xyz', 'tbuffer_store_format_xyzw'][op]
            text = (f"{nm} v[{vdata}:{vdata + nc - 1}], v{vaddr}, s[{srsrc}:{srsrc+3}], {src(soff)} dfmt:{dfmt} nfmt:{nfmt} offset:{off}"
                    + (" offen" if offen else "") + (" idxen" if idxen else ""))
        elif (w >> 26) == 0x3E:      # EXP (111110)
            w2 = W(i + 1); ln = 2
            en = w & 0xF; tgt = (w >> 4) & 0x3F; compr = (w >> 10) & 1; done = (w >> 11) & 1; vm = (w >> 12) & 1
            t = f"mrt{tgt}" if tgt < 8 else ("mrtz" if tgt == 8 else ("null" if tgt == 9 else (f"pos{tgt-12}" if 12 <= tgt <= 15 else f"param{tgt-32}")))
            text = f"exp {t} v{w2 & 0xFF}, v{(w2 >> 8) & 0xFF}, v{(w2 >> 16) & 0xFF}, v{(w2 >> 24) & 0xFF} en:0x{en:x}" + (" compr" if compr else "") + (" done" if done else "") + (" vm" if vm else "")
        elif (w >> 26) == 0x36:      # DS
            w2 = W(i + 1); ln = 2
            op = (w >> 18) & 0xFF; o0 = w & 0xFF; o1 = (w >> 8) & 0xFF; gds = (w >> 17) & 1
            va = w2 & 0xFF; d0 = (w2 >> 8) & 0xFF; d1 = (w2 >> 16) & 0xFF; vd = (w2 >> 24) & 0xFF
            text = f"{DS.get(op, f'ds_op{op}')} vdst:v{vd} addr:v{va} data0:v{d0} data1:v{d1} off0:{o0} off1:{o1}" + (" gds" if gds else "")
        elif (w >> 25) == 0x3F:      # VOP1 (0111111)
            op = (w >> 9) & 0xFF; vd = (w >> 17) & 0xFF; s0 = w & 0x1FF
            lit = W(i + 1) if s0 == 255 else None; ln = 2 if lit is not None else 1
            text = f"{VOP1.get(op, f'vop1_op{op}')} v{vd}, {src(s0, lit)}"
        elif (w >> 25) == 0x3E:      # VOPC (0111110)
            op = (w >> 17) & 0xFF; s0 = w & 0x1FF; s1 = (w >> 9) & 0xFF
            text = f"{vopc_name(op)} vcc, {src(s0)}, v{s1}"
        elif (w >> 31) == 0:         # VOP2
            op = (w >> 25) & 0x3F; vd = (w >> 17) & 0xFF; s1 = (w >> 9) & 0xFF; s0 = w & 0x1FF
            lit = W(i + 1) if (s0 == 255 or op in (32, 33)) else None; ln = 2 if lit is not None else 1
            name = VOP2.get(op, f"vop2_op{op}")
            if op in (32, 33): text = f"{name} v{vd}, {src(s0)}, v{s1}, 0x{lit:08x}"
            else: text = f"{name} v{vd}, {src(s0, lit)}, v{s1}"
        if text is None:
            text = f".dword 0x{w:08x}"
        raw = " ".join(f"{W(i + k):08x}" for k in range(ln))
        out.append(f"{pc:04x}: {raw:<18} {text}")
        i += ln
    return out

def extract_code_from_dxbc(path):
    b = open(path, 'rb').read()
    n = struct.unpack_from('<I', b, 0x1C)[0]
    shex = None
    for k in range(n):
        off = struct.unpack_from('<I', b, 0x20 + 4 * k)[0]
        if b[off:off+4] in (b'SHEX', b'SHDR'):
            shex = (off + 8, struct.unpack_from('<I', b, off + 4)[0])
    off, size = shex
    d = b[off:off + size]; i = 2; blocks = []
    want = int(os.environ.get('GCN_PROG', '0'))
    while i * 4 < size:
        t = struct.unpack_from('<I', d, i * 4)[0]
        if (t & 0x7FF) == 0x35:
            L = struct.unpack_from('<I', d, (i + 1) * 4)[0]
            blocks.append(d[(i + 2) * 4:(i + L) * 4]); i += L; continue
        L = (t >> 24) & 0x7F or 1; i += L
    for blob in blocks:
        for o in range(0, min(40, len(blob) - 2)):
            if blob[o] == 0x78 and blob[o+1] in (1, 0x5e, 0x9c, 0xda):
                try: dec = zlib.decompressobj().decompress(blob[o:])
                except Exception: continue
                if dec[8:12] != b'\x00' * 0 and b'\x00\x00\x81\xbf' in dec[:]:   # contains s_endpgm
                    # a VS blob of a tessellated PSO carries several hardware programs (VS, ?, LS); GCN_PROG picks one
                    if want > 0: want -= 1; break
                    return dec
    return None

def find_code(blob):
    end = blob.find(struct.pack('<I', 0xBF810000))
    # walk back over padding (0x7e000000 NOPs / zeros) is not reliable: start after the last 0x00000000 dword preceding the first SALU token
    start = None
    for k in range(end - 4, 0, -4):
        if struct.unpack_from('<I', blob, k)[0] == 0:
            # a zero dword can also be the 2nd dword of a 64-bit instruction (e.g. 'exp mrt0 v0, v0, v0, v0'
            # = f8001c0f 00000000): skip zeros preceded by a VOP3/DS/MUBUF/MTBUF/MIMG/EXP first dword
            prev = struct.unpack_from('<I', blob, k - 4)[0] if k >= 4 else 0
            if (prev >> 26) in (0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E) and not os.environ.get('OLD_FIND_CODE'):
                continue
            start = k + 4; break
    return start, end + 4

if __name__ == '__main__':
    p = sys.argv[1]
    raw = open(p, 'rb').read()
    blob = extract_code_from_dxbc(p) if raw[:4] == b'DXBC' else raw
    if blob is None: print("no GCN blob found"); sys.exit(1)
    s, e = find_code(blob)
    print(f"; container {len(blob)} bytes, code {s:#x}..{e:#x}")
    for ln in disasm(blob[s:e]): print(ln)
