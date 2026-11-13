/* Virtual Boy hardware: the registers this demo uses (addresses as libgccvb has them, public domain). */
#ifndef VC_HW_H
#define VC_HW_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef long long s64;

/* VIP: framebuffers (384 columns x 64 bytes: 256 rows of 2-bit pixels, a column after the other) */
#define L_FB0 ((volatile u32 *)0x00000000)
#define L_FB1 ((volatile u32 *)0x00008000)
#define R_FB0 ((volatile u32 *)0x00010000)
#define R_FB1 ((volatile u32 *)0x00018000)
#define CLMN_TBL ((volatile u16 *)0x0003DC00)
#define WAM ((volatile u16 *)0x0003D800)
#define VIP ((volatile u16 *)0x0005F800)

#define INTPND 0x00
#define INTENB 0x01
#define INTCLR 0x02
#define DPSTTS 0x10
#define DPCTRL 0x11
#define BRTA 0x12
#define BRTB 0x13
#define BRTC 0x14
#define REST 0x15
#define FRMCYC 0x17
#define CTA 0x18
#define XPSTTS 0x20
#define XPCTRL 0x21
#define GPLT0 0x30
#define JPLT0 0x34
#define BKCOL 0x38

/* INTPND bits */
#define LFBEND 0x0002
#define GAMESTART 0x0008
#define FRAMESTART 0x0010
/* DPCTRL bits */
#define SYNCE 0x0200
#define SCANRDY 0x0040
#define RE 0x0100
#define DISP 0x0002
/* XPCTRL / XPSTTS bits */
#define XPEN 0x0002
#define XPBSY0 0x0004
#define XPBSY1 0x0008
/* DPSTTS: which framebuffer the display shows */
#define L0BSY 0x0004
#define R0BSY 0x0008
#define L1BSY 0x0010
#define R1BSY 0x0020

/* VSU (sound) */
#define WAVE(n) ((volatile u8 *)(0x01000000 + (n) * 0x80))   /* 5 wave tables, 32 samples (6 bits) each */
#define MODDATA ((volatile u8 *)0x01000280)
#define SND(ch, reg) (*(volatile u8 *)(0x01000400 + (ch) * 0x40 + (reg) * 4))
#define SxINT 0
#define SxLRV 1
#define SxFQL 2
#define SxFQH 3
#define SxEV0 4
#define SxEV1 5
#define SxRAM 6
#define S5SWP 7
#define SSTOP (*(volatile u8 *)0x01000580)

/* hardware control */
#define HW ((volatile u8 *)0x02000000)
#define SDLR 0x10
#define SDHR 0x14
#define TLR 0x18
#define THR 0x1C
#define TCR 0x20
#define WCR 0x24
#define SCR 0x28

#endif
