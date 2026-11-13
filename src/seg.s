/* Virtualcraft: the outlined column loop in assembly (src/render.c seg_e has the C it replaces, kept for the PC build).
   void seg_e(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, const u32 *ep, const u32 *door,
              int k, int tt, int bt)
   n columns from pA (and pB, both eyes, unless 0): rows [ty >> 16, by >> 16) clipped to 0..224, the body pat[k] through
   door[k], the top tt rows and the bottom bt rows in ep[k] (none where clipped); k steps 0..7.
   Args: r6 pA, r7 pB, r8 n, r9 ty; on the stack 0 tdy, 4 by, 8 bdy, 12 pat, 16 ep, 20 door, 24 k, 28 tt, 32 bt. */
	.section .text
	.macro SEGE n, w
	.align	1
	.global	_\n
_\n:
	addi	-44, sp, sp
	st.w	r20, 0[sp]
	st.w	r21, 4[sp]
	st.w	r22, 8[sp]
	st.w	r23, 12[sp]
	st.w	r24, 16[sp]
	st.w	r25, 20[sp]
	st.w	r26, 24[sp]
	st.w	r27, 28[sp]
	st.w	r28, 32[sp]
	st.w	r29, 36[sp]
	ld.w	44[sp], r20		/* tdy */
	ld.w	48[sp], r21		/* by */
	ld.w	52[sp], r22		/* bdy */
	ld.w	56[sp], r23		/* pat */
	ld.w	60[sp], r24		/* ep */
	ld.w	64[sp], r25		/* door */
	ld.w	68[sp], r26		/* k */
	shl	2, r26			/* k * 4 */
	ld.w	72[sp], r27
	shl	1, r27			/* tt * 2 (bits) */
	ld.w	76[sp], r28
	shl	1, r28			/* bt * 2 */
	cmp	r0, r8
	ble	.L\n\()done
.L\n\()col:
	mov	r9, r10
	sar	16, r10			/* a */
	mov	r21, r11
	sar	16, r11			/* b */
	add	r20, r9
	add	r22, r21
	mov	r27, r12		/* this column's top rows (0: clipped) */
	mov	r28, r13
	cmp	r0, r10
	bge	1f
	mov	r0, r10
	mov	r0, r12
1:	movea	224, r0, r14
	cmp	r14, r11
	ble	2f
	mov	r14, r11
	mov	r0, r13
2:	cmp	r11, r10
	bge	.L\n\()next
	/* masks: ma rows >= a in its word, mb rows <= b - 1 in its word */
	andi	15, r10, r15
	shl	1, r15			/* ka * 2 */
	mov	-1, r16
	shl	r15, r16		/* ma */
	add	-1, r11
	andi	15, r11, r17
	shl	1, r17
	add	2, r17			/* kb * 2 (2..32) */
	movea	32, r0, r18
	sub	r17, r18
	mov	-1, r19
	shr	r18, r19		/* mb */
	/* et: rows a .. a + tt - 1 of the first word */
	cmp	r0, r12
	be	3f
	add	r15, r12		/* ka2 + tt2 */
	movea	32, r0, r18
	cmp	r18, r12
	bge	4f
	sub	r12, r18
	mov	-1, r12
	shr	r18, r12
	and	r16, r12
	br	3f
4:	mov	r16, r12
3:	/* eb: rows b - bt .. b - 1 of the last word */
	cmp	r0, r13
	be	5f
	mov	r17, r18
	sub	r13, r18		/* kb2 - bt2 */
	cmp	r0, r18
	ble	6f
	mov	-1, r13
	shl	r18, r13
	and	r19, r13
	br	5f
6:	mov	r19, r13
5:	/* the patterns */
	mov	r23, r14
	add	r26, r14
	ld.w	0[r14], r14		/* P */
	mov	r24, r15
	add	r26, r15
	ld.w	0[r15], r15		/* E */
	mov	r25, r17
	add	r26, r17
	ld.w	0[r17], r17		/* D */
	shr	4, r10
	shl	2, r10			/* wa * 4 */
	shr	4, r11
	shl	2, r11			/* wb * 4 */
	cmp	r11, r10
	bne	.L\n\()multi
	/* one word: m = ma & mb, e = (et | eb) & m, f = m & D & ~e */
	and	r19, r16		/* m */
	or	r13, r12
	and	r16, r12		/* e */
	and	r17, r16
	not	r12, r18
	and	r18, r16		/* f */
	mov	r14, r18
	and	r16, r18
	mov	r15, r29
	and	r12, r29
	or	r29, r18		/* val */
	or	r12, r16		/* mask */
	mov	r6, r29
	add	r10, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
	cmp	r0, r7
	be	.L\n\()next
	mov	r7, r29
	add	r10, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
	br	.L\n\()next
.L\n\()multi:
	/* the first word: f = ma & D & ~et */
	and	r17, r16
	not	r12, r18
	and	r18, r16
	mov	r14, r18
	and	r16, r18
	mov	r15, r29
	and	r12, r29
	or	r29, r18
	or	r12, r16
	mov	r6, r29
	add	r10, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
	cmp	r0, r7
	be	7f
	mov	r7, r29
	add	r10, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r16, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
7:	/* the middle words */
	add	4, r10
	cmp	r11, r10
	bge	.L\n\()last
	cmp	-1, r17
	bne	.L\n\()midd
.L\n\()mid:
	mov	r6, r29
	add	r10, r29
	st.w	r14, 0[r29]
	.if \w
	st.w	r14, 64[r29]
	.endif
	cmp	r0, r7
	be	8f
	mov	r7, r29
	add	r10, r29
	st.w	r14, 0[r29]
	.if \w
	st.w	r14, 64[r29]
	.endif
8:	add	4, r10
	cmp	r11, r10
	blt	.L\n\()mid
	br	.L\n\()last
.L\n\()midd:
	cmp	r0, r17
	be	.L\n\()last
9:	mov	r6, r29
	add	r10, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r17, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r17, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
	cmp	r0, r7
	be	10f
	mov	r7, r29
	add	r10, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r17, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r17, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
10:	add	4, r10
	cmp	r11, r10
	blt	9b
.L\n\()last:
	/* the last word: f = mb & D & ~eb */
	and	r17, r19
	not	r13, r18
	and	r18, r19
	mov	r14, r18
	and	r19, r18
	mov	r15, r29
	and	r13, r29
	or	r29, r18
	or	r13, r19
	mov	r6, r29
	add	r11, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r19, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r19, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
	cmp	r0, r7
	be	.L\n\()next
	mov	r7, r29
	add	r11, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r19, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r18, r1
	and	r19, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
.L\n\()next:
	addi	64 + 64 * \w, r6, r6
	cmp	r0, r7
	be	11f
	addi	64 + 64 * \w, r7, r7
11:	add	4 + 4 * \w, r26
	andi	28, r26, r26
	add	-1 - \w, r8
	bgt	.L\n\()col
.L\n\()done:
	ld.w	0[sp], r20
	ld.w	4[sp], r21
	ld.w	8[sp], r22
	ld.w	12[sp], r23
	ld.w	16[sp], r24
	ld.w	20[sp], r25
	ld.w	24[sp], r26
	ld.w	28[sp], r27
	ld.w	32[sp], r28
	ld.w	36[sp], r29
	addi	44, sp, sp
	jmp	[lp]

	.endm
	SEGE seg_e, 0
	SEGE seg_ew, 1

/* void seg_tex_a(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, s32 tu, s32 tdu, const u32 *base, const u16 *hoff)
   n columns of a near textured face (src/render.c seg_tex, the C it replaces): column = rows [ty >> 16, by >> 16) from the
   pre-scaled column of texel tu >> 16 and that height (tools/textures.py), clipped to 0..224, into pA (and pB unless 0).
   A column's words all shift by the same amount: two loads, two shifts and an or a word.
   Args r6 pA, r7 pB, r8 n, r9 ty; stack 0 tdy, 4 by, 8 bdy, 12 tu, 16 tdu, 20 base, 24 hoff */
	.macro STEX n, w
	.align	1
	.global	_\n
_\n:
	addi	-44, sp, sp
	st.w	r20, 0[sp]
	st.w	r21, 4[sp]
	st.w	r22, 8[sp]
	st.w	r23, 12[sp]
	st.w	r24, 16[sp]
	st.w	r25, 20[sp]
	st.w	r26, 24[sp]
	st.w	r27, 28[sp]
	st.w	r28, 32[sp]
	st.w	r29, 36[sp]
	st.w	r2, 40[sp]
	ld.w	44[sp], r20		/* tdy */
	ld.w	48[sp], r21		/* by */
	ld.w	52[sp], r22		/* bdy */
	ld.w	56[sp], r23		/* tu */
	ld.w	60[sp], r24		/* tdu */
	ld.w	64[sp], r25		/* base */
	ld.w	68[sp], r26		/* hoff */
	ld.w	72[sp], r2		/* key: shade 0 is see-through (plants) */
	cmp	r0, r8
	ble	.T\n\()tdone
.T\n\()col:
	mov	r9, r10
	sar	16, r10			/* ra */
	mov	r21, r11
	sar	16, r11			/* rb */
	mov	r23, r15
	sar	16, r15			/* u */
	add	r20, r9
	add	r22, r21
	add	r24, r23
	mov	r11, r12
	sub	r10, r12		/* h */
	movea	128, r0, r13		/* TEX_HMAX: taller columns go to the C path, src/render.c tex_col */
	cmp	r13, r12
	bgt	.T\n\()tnext
	mov	r10, r13		/* a */
	cmp	r0, r13
	bge	1f
	mov	r0, r13
1:	mov	r11, r14		/* b */
	movea	224, r0, r16
	cmp	r16, r14
	ble	2f
	mov	r16, r14
2:	cmp	r14, r13
	bge	.T\n\()tnext
	/* u clamped 0..15; src = base + u * 2816 + hoff[h] * 4 (TEX_PER / 16 words, TEX_HMAX 128) */
	cmp	r0, r15
	bge	3f
	mov	r0, r15
3:	cmp	15, r15
	ble	4f
	movea	15, r0, r15
4:	mov	r15, r16
	shl	11, r16
	mov	r15, r17
	shl	9, r17
	add	r17, r16
	shl	8, r15
	add	r15, r16		/* u * 2816 */
	add	r25, r16
	shl	1, r12
	add	r26, r12
	ld.h	0[r12], r12
	andi	0xFFFF, r12, r12
	shl	2, r12
	add	r12, r16		/* src */
	/* words wa..wb; p = wa * 16 - ra; S = src + (p >> 4) * 4; sh = (p & 15) * 2 */
	mov	r13, r17
	shr	4, r17			/* wa */
	add	-1, r14
	mov	r14, r18
	shr	4, r18			/* wb */
	mov	r17, r19
	shl	4, r19
	sub	r10, r19		/* p */
	mov	r19, r27
	sar	4, r27
	shl	2, r27
	add	r27, r16		/* S */
	andi	15, r19, r19
	shl	1, r19			/* sh */
	movea	32, r0, r28
	sub	r19, r28		/* 32 - sh */
	/* masks: ma (rows >= a & 15), mb (rows <= (b - 1) & 15) */
	andi	15, r13, r12
	shl	1, r12
	mov	-1, r13
	shl	r12, r13		/* ma */
	andi	15, r14, r12
	shl	1, r12
	movea	30, r0, r14
	sub	r12, r14
	mov	-1, r12
	shr	r14, r12		/* mb */
	shl	2, r17			/* wa * 4 */
	shl	2, r18			/* wb * 4 */
.T\n\()word:
	ld.w	0[r16], r14
	cmp	r0, r19
	be	5f
	shr	r19, r14
	ld.w	4[r16], r15
	shl	r28, r15
	or	r15, r14		/* v */
5:	mov	-1, r15			/* the mask: mb on the last word, ma on the first (r11 >= 0 until it is done) */
	cmp	r18, r17
	bne	6f
	and	r12, r15
6:	cmp	r0, r11			/* r11 >= 0 marks the first word: then and with ma */
	blt	7f
	and	r13, r15
	mov	-1, r11
7:	cmp	r0, r2
	be	11f
	mov	r14, r29		/* key: only the non-zero pixels */
	shr	1, r29
	or	r14, r29
	movhi	0x5555, r0, r1
	movea	0x5555, r1, r1
	and	r1, r29
	mov	r29, r1
	shl	1, r1
	or	r1, r29
	and	r29, r15
11:	mov	r6, r29
	add	r17, r29
	cmp	-1, r15
	bne	8f
	st.w	r14, 0[r29]
	.if \w
	st.w	r14, 64[r29]
	.endif
	cmp	r0, r7
	be	9f
	mov	r7, r29
	add	r17, r29
	st.w	r14, 0[r29]
	.if \w
	st.w	r14, 64[r29]
	.endif
	br	9f
8:	ld.w	0[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r15, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r15, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
	cmp	r0, r7
	be	9f
	mov	r7, r29
	add	r17, r29
	ld.w	0[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r15, r1
	xor	r1, r30
	st.w	r30, 0[r29]
	.if \w
	ld.w	64[r29], r30
	mov	r30, r1
	xor	r14, r1
	and	r15, r1
	xor	r1, r30
	st.w	r30, 64[r29]
	.endif
9:	add	4, r16
	add	4, r17
	cmp	r18, r17
	ble	.T\n\()word
.T\n\()tnext:
	addi	64 + 64 * \w, r6, r6
	cmp	r0, r7
	be	10f
	addi	64 + 64 * \w, r7, r7
10:	add	-1 - \w, r8
	bgt	.T\n\()col
.T\n\()tdone:
	ld.w	0[sp], r20
	ld.w	4[sp], r21
	ld.w	8[sp], r22
	ld.w	12[sp], r23
	ld.w	16[sp], r24
	ld.w	20[sp], r25
	ld.w	24[sp], r26
	ld.w	28[sp], r27
	ld.w	32[sp], r28
	ld.w	36[sp], r29
	ld.w	40[sp], r2
	addi	44, sp, sp
	jmp	[lp]

	.endm
	STEX seg_tex_a, 0
	STEX seg_tex_aw, 1

/* int far_step(FarStep *s): one depth step of the far field's march (src/render.c far_field: the C it replaces). For each
   pair p (step pw) still open above ytop: the sample's cell (x16, z16 >> 16), not in the near ring, its surface h, its row
   ytab[h]; where that row is above the pair's horizon yb[p] an event (p | t << 8 | rise << 13 | parity << 14,
   y | old yb << 16) and yb[p] = y; hp[p] = h. Returns the number of events.
   FarStep: 0 x16, 4 z16, 8 dx16, 12 dz16, 16 pw, 20 yb, 24 hp, 28 ytab (s16), 32 ytop, 36 hmap, 40 surf_t, 44 ring,
            48 ccx, 52 ccz, 56 r2, 60 ev */
	.align	1
	.global	_far_step
_far_step:
	addi	-40, sp, sp
	st.w	r20, 0[sp]
	st.w	r21, 4[sp]
	st.w	r22, 8[sp]
	st.w	r23, 12[sp]
	st.w	r24, 16[sp]
	st.w	r25, 20[sp]
	st.w	r26, 24[sp]
	st.w	r27, 28[sp]
	st.w	r28, 32[sp]
	st.w	r29, 36[sp]
	ld.w	0[r6], r7
	ld.w	4[r6], r8
	ld.w	8[r6], r9
	ld.w	12[r6], r10
	ld.w	16[r6], r26
	ld.w	20[r6], r12
	ld.w	24[r6], r13
	ld.w	28[r6], r14
	ld.w	32[r6], r15
	ld.w	36[r6], r16
	ld.w	40[r6], r17
	ld.w	44[r6], r18
	ld.w	48[r6], r19
	ld.w	52[r6], r20
	ld.w	56[r6], r21
	ld.w	60[r6], r22
	ld.w	60[r6], r11		/* the events' start, for the count */
	mov	r0, r25			/* p */
.Fsample:
	ld.h	0[r12], r27		/* yb */
	cmp	r15, r27
	ble	.Fnext
	movea	64, r0, r1
	mov	r7, r28
	sar	16, r28			/* ix */
	cmp	r1, r28
	bnl	.Fnext
	mov	r8, r29
	sar	16, r29			/* iz */
	cmp	r1, r29
	bnl	.Fnext
	cmp	r0, r18
	be	1f
	mov	r28, r23
	sub	r19, r23
	mul	r23, r23
	mov	r29, r24
	sub	r20, r24
	mul	r24, r24
	add	r24, r23
	cmp	r21, r23
	bgt	1f
	/* in the near ring: drawn by the polygons; its surface still hides what lies behind it */
	shl	6, r28
	or	r29, r28
	mov	r16, r23
	add	r28, r23
	ld.b	0[r23], r24
	andi	255, r24, r24
	shl	1, r24
	add	r14, r24
	ld.h	0[r24], r24
	cmp	r27, r24
	bge	.Fnext
	st.h	r24, 0[r12]
	cmp	1, r26
	be	.Fnext
	st.h	r24, 2[r12]
	br	.Fnext
1:	shl	6, r28
	or	r29, r28		/* i */
	mov	r16, r23
	add	r28, r23
	ld.b	0[r23], r24
	andi	255, r24, r24		/* h */
	mov	r24, r23
	shl	1, r23
	add	r14, r23
	ld.h	0[r23], r23		/* y */
	cmp	r27, r23
	bge	2f
	ld.b	0[r13], r30
	andi	255, r30, r30		/* hp */
	mov	r17, r1
	add	r28, r1
	ld.b	0[r1], r1
	andi	255, r1, r1		/* t */
	shl	8, r1
	or	r25, r1
	cmp	r30, r24
	ble	3f
	ori	0x2000, r1, r1		/* rise */
3:	mov	r28, r30
	shr	6, r30
	xor	r28, r30
	andi	1, r30, r30
	shl	14, r30
	or	r30, r1
	st.w	r1, 0[r22]
	andi	0xFFFF, r23, r30
	mov	r27, r1
	shl	16, r1
	or	r1, r30
	st.w	r30, 4[r22]
	add	8, r22
	st.h	r23, 0[r12]
	cmp	1, r26
	be	2f
	st.h	r23, 2[r12]
2:	st.b	r24, 0[r13]
	cmp	1, r26
	be	.Fnext
	st.b	r24, 1[r13]
.Fnext:
	add	r9, r7
	add	r10, r8
	add	r26, r25
	add	r26, r13
	add	r26, r12
	add	r26, r12
	movea	192, r0, r1
	cmp	r1, r25
	blt	.Fsample
	mov	r22, r10
	sub	r11, r10
	shr	3, r10
	ld.w	0[sp], r20
	ld.w	4[sp], r21
	ld.w	8[sp], r22
	ld.w	12[sp], r23
	ld.w	16[sp], r24
	ld.w	20[sp], r25
	ld.w	24[sp], r26
	ld.w	28[sp], r27
	ld.w	32[sp], r28
	ld.w	36[sp], r29
	addi	40, sp, sp
	jmp	[lp]

/* void far_span(u32 *colL, u32 *colR, int nc, int k, int a, int b, const u32 *pat, u32 E, int rim)
   rows [a, b) of nc columns at colL and colR (both eyes), pattern pat[(k + i) & 7] for column i, the top row in E if rim
   (src/render.c far_put: the C it replaces). Args r6 colL, r7 colR, r8 nc, r9 k; stack 0 a, 4 b, 8 pat, 12 E, 16 rim */
	.align	1
	.global	_far_span
_far_span:
	addi	-32, sp, sp
	st.w	r20, 0[sp]
	st.w	r21, 4[sp]
	st.w	r22, 8[sp]
	st.w	r23, 12[sp]
	st.w	r24, 16[sp]
	st.w	r25, 20[sp]
	st.w	r26, 24[sp]
	st.w	r27, 28[sp]
	ld.w	32[sp], r10		/* a */
	ld.w	36[sp], r11		/* b */
	ld.w	40[sp], r12		/* pat */
	ld.w	44[sp], r13		/* E */
	ld.w	48[sp], r14		/* rim */
	/* wa, wb (bytes), ma, mb, er (the rim's row) */
	mov	r10, r15
	shr	4, r15
	shl	2, r15			/* wa * 4 */
	add	-1, r11
	mov	r11, r16
	shr	4, r16
	shl	2, r16			/* wb * 4 */
	andi	15, r10, r17
	shl	1, r17
	mov	-1, r18
	shl	r17, r18		/* ma */
	mov	r0, r20
	cmp	r0, r14
	be	1f
	movea	3, r0, r20
	shl	r17, r20		/* er */
1:	andi	15, r11, r17
	shl	1, r17
	movea	30, r0, r19
	sub	r17, r19
	mov	-1, r17
	shr	r19, r17		/* mb */
	shl	2, r9			/* k * 4 */
.Scol:
	andi	28, r9, r21
	add	r12, r21
	ld.w	0[r21], r21		/* P */
	/* first word value: P with the rim row in E */
	not	r20, r22
	and	r21, r22
	mov	r13, r23
	and	r20, r23
	or	r23, r22		/* v0 */
	cmp	r16, r15
	bne	.Smulti
	/* one word: mask ma & mb */
	mov	r18, r24
	and	r17, r24
	mov	r6, r25
	add	r15, r25
	ld.w	0[r25], r26
	mov	r26, r27
	xor	r22, r27
	and	r24, r27
	xor	r27, r26
	st.w	r26, 0[r25]
	mov	r7, r25
	add	r15, r25
	ld.w	0[r25], r26
	mov	r26, r27
	xor	r22, r27
	and	r24, r27
	xor	r27, r26
	st.w	r26, 0[r25]
	br	.Snext
.Smulti:
	mov	r6, r25
	add	r15, r25
	ld.w	0[r25], r26
	mov	r26, r27
	xor	r22, r27
	and	r18, r27
	xor	r27, r26
	st.w	r26, 0[r25]
	mov	r7, r24
	add	r15, r24
	ld.w	0[r24], r26
	mov	r26, r27
	xor	r22, r27
	and	r18, r27
	xor	r27, r26
	st.w	r26, 0[r24]
	mov	r15, r23
	add	4, r23
	mov	r6, r25
	add	r23, r25
	mov	r7, r24
	add	r23, r24
2:	cmp	r16, r23
	bge	3f
	st.w	r21, 0[r25]
	st.w	r21, 0[r24]
	add	4, r25
	add	4, r24
	add	4, r23
	br	2b
3:	ld.w	0[r25], r26
	mov	r26, r27
	xor	r21, r27
	and	r17, r27
	xor	r27, r26
	st.w	r26, 0[r25]
	ld.w	0[r24], r26
	mov	r26, r27
	xor	r21, r27
	and	r17, r27
	xor	r27, r26
	st.w	r26, 0[r24]
.Snext:
	addi	64, r6, r6
	addi	64, r7, r7
	add	4, r9
	add	-1, r8
	bgt	.Scol
	ld.w	0[sp], r20
	ld.w	4[sp], r21
	ld.w	8[sp], r22
	ld.w	12[sp], r23
	ld.w	16[sp], r24
	ld.w	20[sp], r25
	ld.w	24[sp], r26
	ld.w	28[sp], r27
	addi	32, sp, sp
	jmp	[lp]
