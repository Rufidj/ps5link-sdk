#ifndef PS5LINK_NID_H
#define PS5LINK_NID_H

/*
 * Sony NID encoder: turns a plain C symbol name (e.g. "sceVideoOutOpen") into
 * the 11-character identifier the real PS5 dynamic loader resolves imports
 * by. Ported byte-for-byte from SharpProspero.Link/NidEncoder.cs, which was
 * itself validated against a real on-device libc.prx dump (this is the
 * long-public PS4/PS5 NID scheme: SHA1(name + fixed salt), first 8 digest
 * bytes reversed, custom-alphabet base64, truncated to 11 chars).
 *
 * out must have room for at least 12 bytes (11 chars + NUL).
 */
void nid_encode(const char *symbol_name, char out[12]);

/*
 * Sony's numeric-suffix encoding used for the "#lib#mod" id fields in a
 * mangled import name ("{nid}#{Encode(libId)}#{Encode(modId)}"). This is a
 * distinct, simpler mapping from nid_encode - NOT SHA1-based: id==0 encodes
 * as "A"; otherwise it's the same 64-char alphabet applied as a variable-
 * length base-64 representation of id, most-significant digit first, no
 * leading zero digits (ported from DynamicWriter.cs's private Encode(int)).
 * out must have room for at least 8 bytes (covers any 32-bit id + NUL).
 */
void nid_encode_id(int id, char out[8]);

#endif
