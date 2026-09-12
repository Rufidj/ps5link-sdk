// Two-texture pixel program: samples two textures at the same interpolated
// UV, multiplies them together, then by the interpolated vertex colour.
// Written by hand in the same style as textured_p.s (no compiler involved -
// see FINDINGS.md / PS5GL notes on why: Mesa's RADV/ACO needs either real AMD
// hardware or a from-source build to target gfx1030 offline, neither
// available yet).
//
// Inputs: attr0.xy texture coordinates (shared by both textures),
//         attr1     colour (rgba).
// Resources (NEW layout, needs on-hardware validation - see agcpack.py
// texture_container2): tex0 s[0:7], samp0 s[8:11], tex1 s[12:19], samp1 s[20:23].
// Uses v0-v15; container VGPRS must be packed with --vgprs 4 (covers v0-v20).
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

	// v6, v7 = u, v   v10, v9, v0, v8 = colour r, g, b, a
	// v[2:5]  = texel0 rgba (from s[0:7]/s[8:11])
	// v[12:15] = texel1 rgba (from s[12:19]/s[20:23])
	image_sample v[2:5],   v[6:7], s[0:7],   s[8:11]  dmask:0xf dim:SQ_RSRC_IMG_2D
	image_sample v[12:15], v[6:7], s[12:19], s[20:23] dmask:0xf dim:SQ_RSRC_IMG_2D
	s_waitcnt vmcnt(0)

	// texel0 * texel1
	v_mul_f32_e32 v2, v2, v12
	v_mul_f32_e32 v3, v3, v13
	v_mul_f32_e32 v4, v4, v14
	v_mul_f32_e32 v5, v5, v15

	// (texel0 * texel1) * colour
	v_mul_f32_e32 v10, v2, v10
	v_mul_f32_e32 v9, v9, v3
	v_mul_f32_e32 v0, v0, v4
	v_mul_f32_e32 v8, v8, v5
	v_cvt_pkrtz_f16_f32_e32 v1, v10, v9
	v_cvt_pkrtz_f16_f32_e32 v0, v0, v8
	s_mov_b64 exec, vcc
	exp mrt0 v1, v1, v0, v0 done compr vm
	s_endpgm
