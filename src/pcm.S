/* Virtualcraft: the PCM interrupt: 10000 a second, adds the 3 streams, looks the sum up in pcm_lv and
   writes channel 5 volume and envelope (its wave a constant 63: these two are the sample).
   Short on purpose: it saves the 4 registers it uses, not crt0's 18. */
	.section .text
	.align	1
	.global	_pcm_isr
_pcm_isr:
	addi	-24, sp, sp
	st.w	r6, 0[sp]
	st.w	r7, 4[sp]
	st.w	r8, 8[sp]
	st.w	r9, 12[sp]
	st.w	r10, 16[sp]
	st.w	r11, 20[sp]
	movhi	hi(_pcm_dummy), r0, r6
	ld.b	lo(_pcm_dummy)[r6], r7
	cmp	r0, r7
	bne	4f			/* the follow-up interrupt: only the clear */
	movhi	hi(_pcm), r0, r6
	movea	lo(_pcm), r6, r6
	mov	r0, r7
	/* the voice */
	ld.w	4[r6], r8
	cmp	r0, r8
	ble	1f
	add	-1, r8
	st.w	r8, 4[r6]
	ld.w	0[r6], r8
	ld.b	0[r8], r9
	add	1, r8
	st.w	r8, 0[r6]
	add	r9, r7
1:	/* floor A */
	ld.w	12[r6], r8
	cmp	r0, r8
	ble	2f
	add	-1, r8
	st.w	r8, 12[r6]
	ld.w	8[r6], r8
	ld.b	0[r8], r9
	add	1, r8
	st.w	r8, 8[r6]
	add	r9, r7
2:	/* floor B */
	ld.w	20[r6], r8
	cmp	r0, r8
	ble	3f
	add	-1, r8
	st.w	r8, 20[r6]
	ld.w	16[r6], r8
	ld.b	0[r8], r9
	add	1, r8
	st.w	r8, 16[r6]
	add	r9, r7
3:	/* the level: pcm_lv[s + 384] */
	movhi	hi(_pcm_lv), r0, r8
	movea	lo(_pcm_lv), r8, r8
	shl	1, r7
	add	r7, r8
	ld.h	768[r8], r9
	movhi	0x0100, r0, r8
	st.b	r9, 0x0504[r8]		/* channel 5 (index 4): SxLRV */
	shr	8, r9
	st.b	r9, 0x0510[r8]		/* SxEV0 */
	movhi	hi(_pcm_step), r0, r8
	ld.w	lo(_pcm_step)[r8], r9
	movhi	hi(_pcm_time), r0, r8
	ld.w	lo(_pcm_time)[r8], r7
	add	r9, r7
	st.w	r7, lo(_pcm_time)[r8]		/* 0.1 ms units at either rate */
4:	/* acknowledge the timer (enabled, its interrupt on, 20 us, clear the zero flag). The clear is
	   ignored while the counter is still 0 (the tick it fired on): the interrupt then comes again a tick later
	   and that one only clears. */
	movhi	0x0200, r0, r8
	movea	0x1D, r0, r9
	st.b	r9, 0x20[r8]
	ld.b	0x20[r8], r9
	andi	2, r9, r9
	movhi	hi(_pcm_dummy), r0, r8
	st.b	r9, lo(_pcm_dummy)[r8]
	/* Virtualcraft: the right eye's picture to the display, 4 columns a tick (384 in 9.6 ms: done before the display reads
	   it at 15 ms), so the sound never waits for a 2.2 ms copy (src/main.c sets cp_src, cp_dst, cp_left) */
	movhi	hi(_cp_left), r0, r6
	ld.w	lo(_cp_left)[r6], r7
	cmp	r0, r7
	ble	6f
	add	-8, r7
	st.w	r7, lo(_cp_left)[r6]
	movhi	hi(_cp_src), r0, r6
	ld.w	lo(_cp_src)[r6], r8
	movhi	hi(_cp_dst), r0, r11
	ld.w	lo(_cp_dst)[r11], r9
	ld.w	0[r8], r10
	st.w	r10, 0[r9]
	ld.w	4[r8], r10
	st.w	r10, 4[r9]
	ld.w	8[r8], r10
	st.w	r10, 8[r9]
	ld.w	12[r8], r10
	st.w	r10, 12[r9]
	ld.w	16[r8], r10
	st.w	r10, 16[r9]
	ld.w	20[r8], r10
	st.w	r10, 20[r9]
	ld.w	24[r8], r10
	st.w	r10, 24[r9]
	ld.w	28[r8], r10
	st.w	r10, 28[r9]
	ld.w	32[r8], r10
	st.w	r10, 32[r9]
	ld.w	36[r8], r10
	st.w	r10, 36[r9]
	ld.w	40[r8], r10
	st.w	r10, 40[r9]
	ld.w	44[r8], r10
	st.w	r10, 44[r9]
	ld.w	48[r8], r10
	st.w	r10, 48[r9]
	ld.w	52[r8], r10
	st.w	r10, 52[r9]
	ld.w	64[r8], r10
	st.w	r10, 64[r9]
	ld.w	68[r8], r10
	st.w	r10, 68[r9]
	ld.w	72[r8], r10
	st.w	r10, 72[r9]
	ld.w	76[r8], r10
	st.w	r10, 76[r9]
	ld.w	80[r8], r10
	st.w	r10, 80[r9]
	ld.w	84[r8], r10
	st.w	r10, 84[r9]
	ld.w	88[r8], r10
	st.w	r10, 88[r9]
	ld.w	92[r8], r10
	st.w	r10, 92[r9]
	ld.w	96[r8], r10
	st.w	r10, 96[r9]
	ld.w	100[r8], r10
	st.w	r10, 100[r9]
	ld.w	104[r8], r10
	st.w	r10, 104[r9]
	ld.w	108[r8], r10
	st.w	r10, 108[r9]
	ld.w	112[r8], r10
	st.w	r10, 112[r9]
	ld.w	116[r8], r10
	st.w	r10, 116[r9]
	ld.w	128[r8], r10
	st.w	r10, 128[r9]
	ld.w	132[r8], r10
	st.w	r10, 132[r9]
	ld.w	136[r8], r10
	st.w	r10, 136[r9]
	ld.w	140[r8], r10
	st.w	r10, 140[r9]
	ld.w	144[r8], r10
	st.w	r10, 144[r9]
	ld.w	148[r8], r10
	st.w	r10, 148[r9]
	ld.w	152[r8], r10
	st.w	r10, 152[r9]
	ld.w	156[r8], r10
	st.w	r10, 156[r9]
	ld.w	160[r8], r10
	st.w	r10, 160[r9]
	ld.w	164[r8], r10
	st.w	r10, 164[r9]
	ld.w	168[r8], r10
	st.w	r10, 168[r9]
	ld.w	172[r8], r10
	st.w	r10, 172[r9]
	ld.w	176[r8], r10
	st.w	r10, 176[r9]
	ld.w	180[r8], r10
	st.w	r10, 180[r9]
	ld.w	192[r8], r10
	st.w	r10, 192[r9]
	ld.w	196[r8], r10
	st.w	r10, 196[r9]
	ld.w	200[r8], r10
	st.w	r10, 200[r9]
	ld.w	204[r8], r10
	st.w	r10, 204[r9]
	ld.w	208[r8], r10
	st.w	r10, 208[r9]
	ld.w	212[r8], r10
	st.w	r10, 212[r9]
	ld.w	216[r8], r10
	st.w	r10, 216[r9]
	ld.w	220[r8], r10
	st.w	r10, 220[r9]
	ld.w	224[r8], r10
	st.w	r10, 224[r9]
	ld.w	228[r8], r10
	st.w	r10, 228[r9]
	ld.w	232[r8], r10
	st.w	r10, 232[r9]
	ld.w	236[r8], r10
	st.w	r10, 236[r9]
	ld.w	240[r8], r10
	st.w	r10, 240[r9]
	ld.w	244[r8], r10
	st.w	r10, 244[r9]
	ld.w	256[r8], r10
	st.w	r10, 256[r9]
	ld.w	260[r8], r10
	st.w	r10, 260[r9]
	ld.w	264[r8], r10
	st.w	r10, 264[r9]
	ld.w	268[r8], r10
	st.w	r10, 268[r9]
	ld.w	272[r8], r10
	st.w	r10, 272[r9]
	ld.w	276[r8], r10
	st.w	r10, 276[r9]
	ld.w	280[r8], r10
	st.w	r10, 280[r9]
	ld.w	284[r8], r10
	st.w	r10, 284[r9]
	ld.w	288[r8], r10
	st.w	r10, 288[r9]
	ld.w	292[r8], r10
	st.w	r10, 292[r9]
	ld.w	296[r8], r10
	st.w	r10, 296[r9]
	ld.w	300[r8], r10
	st.w	r10, 300[r9]
	ld.w	304[r8], r10
	st.w	r10, 304[r9]
	ld.w	308[r8], r10
	st.w	r10, 308[r9]
	ld.w	320[r8], r10
	st.w	r10, 320[r9]
	ld.w	324[r8], r10
	st.w	r10, 324[r9]
	ld.w	328[r8], r10
	st.w	r10, 328[r9]
	ld.w	332[r8], r10
	st.w	r10, 332[r9]
	ld.w	336[r8], r10
	st.w	r10, 336[r9]
	ld.w	340[r8], r10
	st.w	r10, 340[r9]
	ld.w	344[r8], r10
	st.w	r10, 344[r9]
	ld.w	348[r8], r10
	st.w	r10, 348[r9]
	ld.w	352[r8], r10
	st.w	r10, 352[r9]
	ld.w	356[r8], r10
	st.w	r10, 356[r9]
	ld.w	360[r8], r10
	st.w	r10, 360[r9]
	ld.w	364[r8], r10
	st.w	r10, 364[r9]
	ld.w	368[r8], r10
	st.w	r10, 368[r9]
	ld.w	372[r8], r10
	st.w	r10, 372[r9]
	ld.w	384[r8], r10
	st.w	r10, 384[r9]
	ld.w	388[r8], r10
	st.w	r10, 388[r9]
	ld.w	392[r8], r10
	st.w	r10, 392[r9]
	ld.w	396[r8], r10
	st.w	r10, 396[r9]
	ld.w	400[r8], r10
	st.w	r10, 400[r9]
	ld.w	404[r8], r10
	st.w	r10, 404[r9]
	ld.w	408[r8], r10
	st.w	r10, 408[r9]
	ld.w	412[r8], r10
	st.w	r10, 412[r9]
	ld.w	416[r8], r10
	st.w	r10, 416[r9]
	ld.w	420[r8], r10
	st.w	r10, 420[r9]
	ld.w	424[r8], r10
	st.w	r10, 424[r9]
	ld.w	428[r8], r10
	st.w	r10, 428[r9]
	ld.w	432[r8], r10
	st.w	r10, 432[r9]
	ld.w	436[r8], r10
	st.w	r10, 436[r9]
	ld.w	448[r8], r10
	st.w	r10, 448[r9]
	ld.w	452[r8], r10
	st.w	r10, 452[r9]
	ld.w	456[r8], r10
	st.w	r10, 456[r9]
	ld.w	460[r8], r10
	st.w	r10, 460[r9]
	ld.w	464[r8], r10
	st.w	r10, 464[r9]
	ld.w	468[r8], r10
	st.w	r10, 468[r9]
	ld.w	472[r8], r10
	st.w	r10, 472[r9]
	ld.w	476[r8], r10
	st.w	r10, 476[r9]
	ld.w	480[r8], r10
	st.w	r10, 480[r9]
	ld.w	484[r8], r10
	st.w	r10, 484[r9]
	ld.w	488[r8], r10
	st.w	r10, 488[r9]
	ld.w	492[r8], r10
	st.w	r10, 492[r9]
	ld.w	496[r8], r10
	st.w	r10, 496[r9]
	ld.w	500[r8], r10
	st.w	r10, 500[r9]
	addi	512, r8, r8
	st.w	r8, lo(_cp_src)[r6]
	addi	512, r9, r9
	st.w	r9, lo(_cp_dst)[r11]
6:	/* nothing to play and nothing to copy: the timer slows to 1 kHz (it is the clock of src/main.c hand_over) */
	movhi	hi(_pcm_step), r0, r6
	ld.w	lo(_pcm_step)[r6], r7
	cmp	1, r7
	bne	7f
	movhi	hi(_cp_left), r0, r8
	ld.w	lo(_cp_left)[r8], r9
	cmp	r0, r9
	bgt	7f
	movhi	hi(_pcm), r0, r8
	movea	lo(_pcm), r8, r8
	ld.w	4[r8], r9
	cmp	r0, r9
	bgt	7f
	ld.w	12[r8], r9
	cmp	r0, r9
	bgt	7f
	ld.w	20[r8], r9
	cmp	r0, r9
	bgt	7f
	movea	10, r0, r7
	st.w	r7, lo(_pcm_step)[r6]
	movhi	0x0200, r0, r8
	movea	50, r0, r9
	st.b	r9, 0x18[r8]			/* TLR: 50 x 20 us */
7:
	ld.w	0[sp], r6
	ld.w	4[sp], r7
	ld.w	8[sp], r8
	ld.w	12[sp], r9
	ld.w	16[sp], r10
	ld.w	20[sp], r11
	addi	24, sp, sp
	reti
