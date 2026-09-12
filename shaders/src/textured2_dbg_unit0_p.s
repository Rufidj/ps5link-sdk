// Debug variant of textured2_p: same N=2 resource declaration (two textures,
// two samplers), but only samples unit 0 (s[0:7]/s[8:11]) and ignores unit 1
// entirely. If this shows clean magenta (same as the N=1 control), unit 0's
// binding survives the header's growth to N=2 intact and the bug is in unit 1.
.text
	s_inst_prefetch 0x1
	s_mov_b32 m0, s12
	s_mov_b64 vcc, exec
	s_wqm_b64 exec, exec
	v_interp_p1_f32_e32 v6, v0, attr0.x
	v_interp_p1_f32_e32 v7, v0, attr0.y
	v_interp_p1_f32_e32 v10, v0, attr1.x
	v_interp_p1_f32_e32 v9, v0, attr1.y
	v_interp_p1_f32_e32 v8, v0, attr1.w
	v_interp_p2_f32_e32 v6, v1, attr0.x
	v_interp_p2_f32_e32 v7, v1, attr0.y
	v_interp_p1_f32_e32 v0, v0, attr1.z
	v_interp_p2_f32_e32 v10, v1, attr1.x
	v_interp_p2_f32_e32 v9, v1, attr1.y
	v_interp_p2_f32_e32 v8, v1, attr1.w
	v_interp_p2_f32_e32 v0, v1, attr1.z

	image_sample v[2:5], v[6:7], s[0:7], s[8:11] dmask:0xf dim:SQ_RSRC_IMG_2D
	s_waitcnt vmcnt(0)

	v_mul_f32_e32 v10, v2, v10
	v_mul_f32_e32 v9, v9, v3
	v_mul_f32_e32 v0, v0, v4
	v_mul_f32_e32 v8, v8, v5
	v_cvt_pkrtz_f16_f32_e32 v1, v10, v9
	v_cvt_pkrtz_f16_f32_e32 v0, v0, v8
	s_mov_b64 exec, vcc
	exp mrt0 v1, v1, v0, v0 done compr vm
	s_endpgm
