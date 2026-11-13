/* Virtualcraft: the renderer. Every picture is drawn live, both eyes, by the CPU into the second framebuffers.
   - Cells back to front: columns in rings of falling Manhattan distance round the camera's cell, in each column the
     cells far from the camera's height first. For unit cubes a cell can only hide one no farther along every axis,
     so this order needs no depth buffer.
   - A face is clipped and projected once (camera space, Q8 = 256 a block), then filled column by column in each eye
     with that eye's disparity per corner: every corner sits at its true depth.
   - Fills write 2-bit pixels into column-major words (16 rows a word): a span costs a few words, not its pixels.
   - Shade: 49 levels (0, 16, 32, 48 are the VB's 4 shades, between them a 4 x 4 Bayer dither) per face direction,
     a screen texture per material (8 columns x 16 rows, shifted by the face's disparity so it lies on the face in
     both eyes), fog to the horizon, outlines and texture rectangles on near faces. */
#include "game.h"
#include "tex.h"
#include "recip.h"
#define RECIP(z) ((u32)(z) < 4096 ? (s32)recip_tab[z] : (FOC << 16) / (z))
#ifdef HOST
u32 *tex_host;
#define TEXB ((const u32 *)tex_host + 1)
#else
extern const u32 tex_bin[];
#define TEXB (tex_bin + 1)
#endif
static u16 hoff[TEX_HMAX + 2];

s32 cam_x, cam_y, cam_z;
u16 cam_yaw, cam_pitch;
s32 sel_x, sel_y, sel_z, sel_on, sel_crack;
s32 frame_no, water_view;
u32 *fb_l, *fb_r;
s32 cube_detail, cube_tilt;
#ifdef HOST
#define PT 0
#else
extern volatile unsigned int pcm_time;
#define PT pcm_time
#endif
u32 prof[8];

#define CX4 (192 * 16)
#define CY4 (112 * 16)
#define NEAR 40
#ifndef NEAR_WIDE
#define NEAR_WIDE 1                               /* (near faces two columns a step too: 12 % faster, slightly coarser) */
#endif
#ifndef TOP_SINGLE
#define TOP_SINGLE 12
#endif
#ifdef CINEMA
#define TEXD (12 * 256)                          /* (faces nearer than this textured: the video mode sees more) */
#else
#define TEXD (6 * 256)
#endif
#define FAR_TWO (4 * 256)                         /* faces past this: two columns a step */
#define FAR_ONE (3 * 256)                         /* faces past this: one disparity for the whole face */
#define EYE 15                                    /* half the eyes' distance, Q8 */
#define DMAX (14 * 16)                            /* crossed disparity past this is compressed (Q4, each eye) */
static s32 conv = 768, s_conv;                    /* the depth at the screen (Q8), its shift */

static s32 rx, rz, ux, uy, uz, fx, fy, fz;        /* the camera's axes in world space, Q14 */

/* ---- pattern tables: lev_pat[tex][level][phase] */
#define NLEV 49
#define NTEX 10
enum { T_FLAT, T_GRASS, T_DIRT, T_STONE, T_COBBLE, T_WOOD, T_SAND, T_LEAVES, T_ORE, T_WATER };
static u32 lev_pat[NTEX][NLEV][8];
static u32 door_mask[4][8];                       /* screen doors: 0 1/4 holes, 1 1/2, 2 water (moving), 3 3/4 */
static const u8 bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static s8 tex_ofs[NTEX][8][16];

static u32 hsh(u32 a)
{
    a = (a ^ 61) ^ (a >> 16); a = a + (a << 3); a = a ^ (a >> 4); a = a * 0x27d4eb2d; return a ^ (a >> 15);
}

static void make_tex(void)
{
    int t, c, r, l;
    for (t = 0; t < NTEX; t++)
        for (c = 0; c < 8; c++)
            for (r = 0; r < 16; r++) {
                u32 h = hsh(t * 977 + c * 31 + r * 7919);
                int n = (int)(h & 15) - 8, o = 0;     /* -8..7 */
                switch (t) {
                case T_GRASS: o = (h & 0xF0) == 0 ? -9 : (h & 0x700) == 0 ? 4 : n / 4; break;
                case T_DIRT: o = (h & 0x70) == 0 ? -7 : (h & 0x380) == 0 ? 5 : n / 4; break;
                case T_STONE: o = (h & 0x1F0) == 0 ? -8 : n / 5; break;
                case T_COBBLE: o = n / 4; break;
                case T_WOOD: o = n / 5; break;
                case T_SAND: o = (h & 0x1F0) == 0 ? -4 : n / 6; break;
                case T_LEAVES: o = ((c >> 2) + (r >> 3)) & 1 ? ((h & 0x70) == 0 ? 6 : 2) : ((h & 0x70) == 0 ? -8 : -2); break;
                case T_ORE: o = (h & 0x1F0) == 0 ? -8 : n / 5; break;
                case T_WATER: o = ((r + c * 2) & 7) == 0 ? 22 : ((r + c * 2) & 7) == 1 ? 10 : -2; break;
                }
                tex_ofs[t][c][r] = (s8)o;
            }
    for (t = 0; t < NTEX; t++)
        for (l = 0; l < NLEV; l++)
            for (c = 0; c < 8; c++) {
                u32 w = 0;
                for (r = 0; r < 16; r++) {
                    int v = l + tex_ofs[t][c][r], s;
                    if (t != T_FLAT && l < 6) v = l + (tex_ofs[t][c][r] * l) / 6;   /* (texture fades with the light) */
                    if (v < 0) v = 0;
                    if (v > 48) v = 48;
                    s = v >> 4;
                    if ((v & 15) > bayer[r & 3][c & 3]) s++;
                    if (s > 3) s = 3;
                    w |= (u32)s << (r * 2);
                }
                lev_pat[t][l][c] = w;
            }
    for (c = 0; c < 8; c++) {
        u32 a = 0, b = 0, d = 0, e = 0;
        for (r = 0; r < 16; r++) {
            if (((r & 1) && (c & 1)) == 0 || ((r ^ c) & 3)) a |= 3u << (r * 2);
            if (((r ^ c) & 1) == 0) b |= 3u << (r * 2);
            if (((r ^ c) & 1) == 0 && ((r + c) & 3) != 1) d |= 3u << (r * 2);
            if (((r & 1) | (c & 1)) == 0) e |= 3u << (r * 2);
        }
        door_mask[0][c] = a; door_mask[1][c] = b; door_mask[2][c] = d; door_mask[3][c] = e;
    }
}

/* ---- materials: per block and face direction (0 -x, 1 +x, 2 -y, 3 +y, 4 -z, 5 +z) a texture, a level, decoration */
enum { D_NONE, D_GRASS, D_LOG, D_PLANKS, D_BRICK, D_COBBLE, D_GLASS, D_LOGTOP, D_LAMP, D_GOLD, D_WOOL };
typedef struct { u8 tex, lev, deco, door; } Look;
#define TOPL(t, l, d) {t, l, d, 0}
static const Look looks[NBLOCK][3] = {        /* top, side, bottom */
    {{0}},
    {{T_GRASS, 40, D_NONE, 0}, {T_DIRT, 24, D_GRASS, 0}, {T_DIRT, 24, 0, 0}},          /* grass */
    {{T_DIRT, 25, 0, 0}, {T_DIRT, 24, 0, 0}, {T_DIRT, 24, 0, 0}},                    /* dirt */
    {{T_STONE, 34, 0, 0}, {T_STONE, 33, 0, 0}, {T_STONE, 33, 0, 0}},                 /* stone */
    {{T_COBBLE, 30, D_COBBLE, 0}, {T_COBBLE, 29, D_COBBLE, 0}, {T_COBBLE, 29, D_COBBLE, 0}},
    {{T_WOOD, 37, D_PLANKS, 0}, {T_WOOD, 36, D_PLANKS, 0}, {T_WOOD, 36, D_PLANKS, 0}},  /* planks */
    {{T_WOOD, 36, D_LOGTOP, 0}, {T_WOOD, 22, D_LOG, 0}, {T_WOOD, 36, D_LOGTOP, 0}},     /* log */
    {{T_LEAVES, 26, 0, 1}, {T_LEAVES, 24, 0, 1}, {T_LEAVES, 20, 0, 1}},              /* leaves */
    {{T_FLAT, 44, D_GLASS, 9}, {T_FLAT, 44, D_GLASS, 9}, {T_FLAT, 44, D_GLASS, 9}},    /* glass: lines only */
    {{T_FLAT, 30, D_BRICK, 0}, {T_FLAT, 30, D_BRICK, 0}, {T_FLAT, 28, D_BRICK, 0}},
    {{T_SAND, 45, 0, 0}, {T_SAND, 44, 0, 0}, {T_SAND, 44, 0, 0}},
    {{T_WATER, 14, 0, 3}, {T_WATER, 12, 0, 3}, {T_WATER, 12, 0, 3}},                 /* water: dark, its waves bright */
    {{T_STONE, 12, 0, 0}, {T_STONE, 12, 0, 0}, {T_STONE, 12, 0, 0}},                 /* bedrock */
    {{T_FLAT, 48, D_LAMP, 0}, {T_FLAT, 48, D_LAMP, 0}, {T_FLAT, 48, D_LAMP, 0}},       /* lamp */
    {{T_ORE, 32, D_GOLD, 0}, {T_ORE, 31, D_GOLD, 0}, {T_ORE, 31, D_GOLD, 0}},         /* gold ore */
    {{T_FLAT, 44, D_WOOL, 0}, {T_FLAT, 43, D_WOOL, 0}, {T_FLAT, 43, D_WOOL, 0}},       /* wool */
    {{T_FLAT, 36, 0, 0}, {T_FLAT, 36, 0, 0}, {T_FLAT, 36, 0, 0}},                    /* flower (drawn as a plant) */
    {{T_FLAT, 34, 0, 0}, {T_FLAT, 34, 0, 0}, {T_FLAT, 34, 0, 0}},                    /* tall grass */
    {{T_FLAT, 34, 0, 0}, {T_FLAT, 34, 0, 0}, {T_FLAT, 34, 0, 0}},                    /* (the sword: an item) */
};
static const u8 dir_shade[6] = {165, 165, 128, 256 - 1, 210, 210};   /* /256: Minecraft's face shading, z brighter than x */

/* texture rectangles: u0 u1 v0 v1 in 16ths of the face, a level change (or an absolute level with 100 +) */
typedef struct { u8 u0, u1, v0, v1; s8 d; } Rect;
static const Rect r_planks[] = {{0, 16, 4, 4, -14}, {0, 16, 8, 8, -14}, {0, 16, 12, 12, -14}, {0, 16, 16, 16, -14},
    {10, 10, 0, 4, -12}, {4, 4, 4, 8, -12}, {12, 12, 8, 12, -12}, {6, 6, 12, 16, -12}, {0, 255, 0, 0, 0}};
static const Rect r_brick[] = {{0, 16, 0, 0, -16}, {0, 16, 4, 4, -16}, {0, 16, 8, 8, -16}, {0, 16, 12, 12, -16},
    {8, 8, 0, 4, -16}, {0, 0, 4, 8, -16}, {4, 4, 4, 8, -16}, {12, 12, 4, 8, -16}, {8, 8, 8, 12, -16}, {4, 4, 12, 16, -16},
    {12, 12, 12, 16, -16}, {1, 7, 1, 3, 6}, {9, 15, 9, 11, 6}, {0, 255, 0, 0, 0}};
static const Rect r_log[] = {{2, 2, 0, 16, -10}, {6, 6, 0, 16, -8}, {10, 10, 0, 16, -10}, {13, 13, 0, 16, -8},
    {0, 255, 0, 0, 0}};
static const Rect r_logtop[] = {{2, 14, 2, 2, -10}, {2, 14, 14, 14, -10}, {2, 2, 2, 14, -10}, {14, 14, 2, 14, -10},
    {5, 11, 5, 5, -8}, {5, 11, 11, 11, -8}, {5, 5, 5, 11, -8}, {11, 11, 5, 11, -8}, {0, 2, 0, 16, -14}, {14, 16, 0, 16, -14},
    {0, 255, 0, 0, 0}};
static const Rect r_cobble[] = {{0, 16, 5, 5, -14}, {0, 16, 11, 11, -14}, {6, 6, 0, 5, -14}, {12, 12, 0, 5, -14},
    {3, 3, 5, 11, -14}, {10, 10, 5, 11, -14}, {7, 7, 11, 16, -14}, {1, 4, 1, 3, 7}, {8, 11, 7, 9, 7}, {0, 255, 0, 0, 0}};
static const Rect r_glass[] = {{3, 3, 3, 6, 0}, {4, 4, 2, 5, 0}, {5, 5, 2, 3, 0}, {11, 11, 10, 12, 0}, {12, 12, 9, 11, 0},
    {0, 255, 0, 0, 0}};
static const Rect r_lamp[] = {{0, 16, 0, 2, -30}, {0, 16, 14, 16, -30}, {0, 2, 0, 16, -30}, {14, 16, 0, 16, -30},
    {7, 9, 2, 14, -18}, {2, 14, 7, 9, -18}, {0, 255, 0, 0, 0}};
static const Rect r_gold[] = {{2, 5, 3, 5, 115}, {9, 12, 2, 4, 115}, {5, 8, 9, 11, 115}, {11, 14, 11, 14, 115},
    {3, 5, 12, 14, 112}, {0, 255, 0, 0, 0}};
static const Rect r_wool[] = {{0, 16, 3, 3, -6}, {0, 16, 9, 9, -6}, {0, 16, 14, 14, -6}, {4, 4, 0, 16, -5}, {11, 11, 0, 16, -5},
    {0, 255, 0, 0, 0}};
static const Rect *const deco_rects[] = {0, 0, r_log, r_planks, r_brick, r_cobble, r_glass, r_logtop, r_lamp, r_gold, r_wool};
static const u8 grass_lip[16] = {3, 4, 3, 2, 3, 5, 4, 3, 2, 3, 4, 3, 5, 3, 2, 3};

/* ---- sin table */
const s16 sin_tab[1024 + 256] = {
#include "sintab.h"
};

/* ---- framebuffer spans */
static u32 mfrom[17], mto[17];                     /* (bits of rows >= k / < k in a word) */

static void span(u32 *__restrict__ col, int a, int b, u32 p)
{
    int wa = a >> 4, wb = (b - 1) >> 4, w;
    u32 ma = mfrom[a & 15], mb = mto[((b - 1) & 15) + 1];
    if (wa == wb) { u32 m = ma & mb; col[wa] = (col[wa] & ~m) | (p & m); return; }
    col[wa] = (col[wa] & ~ma) | (p & ma);
    for (w = wa + 1; w < wb; w++) col[w] = p;
    col[wb] = (col[wb] & ~mb) | (p & mb);
}


/* ---- polygon fill */
typedef struct { s32 x, y, z, u, v; } CV;          /* camera space Q8, face u v Q8 */
u32 st_faces, st_cols, st_cells, st_colsv, st_words, st_polys, st_bucket[8], st_pbucket[8];
static s32 cur_zmin;

/* a face's look for this picture */
typedef struct {
    const u32 *pat;                                /* 8 phases */
    const u32 *door;                               /* screen door or 0 */
    const u32 *edge;                               /* outline pattern or 0 */
    const Rect *rects;
    s32 lev, tex, deco, lines_only, texv, key;
} Style;

/* one side of the polygon (top or bottom), walked column by column: y in rows Q16 (+ the rounding) */
typedef struct { s32 y, dy, u, du, cend, thick; int i, step, last; } Chain;
static Chain gt, gb;                               /* (globals: the loops keep their copies in registers) */
static const s32 *gxs, *gys, *gus;
static int gn;

static void chain_set(Chain *h, int c)
{
    const s32 *xs = gxs, *ys = gys, *us = gus;
    for (;;) {
        int j = h->i + h->step;
        s32 x0, x1, m;
        if (j < 0) j += gn; else if (j >= gn) j -= gn;
        x0 = xs[h->i]; x1 = xs[j];
        h->cend = (x1 + 7) >> 4;
        if (h->cend <= c && j != h->last) { h->i = j; continue; }
        if (x1 <= x0) { h->dy = 0; h->y = (ys[j] << 12) + (7 << 12); h->u = us[j] << 12; h->du = 0; h->thick = 16; h->cend = 1 << 20; return; }
        m = ((ys[j] - ys[h->i]) << 12) / (x1 - x0);
        h->y = (ys[h->i] << 12) + (c * 16 + 8 - x0) * m + (7 << 12);
        h->dy = m << 4;
        h->thick = (m < 0 ? -m : m) >> 12;
        if (h->thick < 1) h->thick = 1;
        if (h->thick > 15) h->thick = 15;
        if (us) {
            m = ((us[j] - us[h->i]) << 12) / (x1 - x0);
            h->u = (us[h->i] << 12) + (c * 16 + 8 - x0) * m;
            h->du = m << 4;
        }
        return;
    }
}

/* rows [a, b) of a column: the body P through the screen door D, the top tt and bottom bt rows in E. Masks by shifts:
   a load costs as much as 5 instructions here. */
#define MFROM(k) (0xFFFFFFFFu << ((k) << 1))
#define MTO(k) (0xFFFFFFFFu >> (32 - ((k) << 1)))
#define WCOL(col, a, b, P, D, E, tt, bt) do { \
    u32 *__restrict__ c_ = (col); int wa_ = (a) >> 4, wb_ = ((b) - 1) >> 4, ka_ = (a) & 15, kb_ = (((b) - 1) & 15) + 1; \
    u32 mb_ = MFROM(ka_) & (D), me_ = 0, eb_ = 0, v_; \
    if (tt) { int t_ = ka_ + (tt); me_ = MFROM(ka_) & (t_ >= 16 ? 0xFFFFFFFFu : MTO(t_)); } \
    if (bt) { int t_ = kb_ - (bt); eb_ = MTO(kb_) & (t_ <= 0 ? 0xFFFFFFFFu : MFROM(t_)); } \
    if (wa_ == wb_) { u32 r_ = MTO(kb_); mb_ &= r_; me_ = (me_ | eb_) & r_; v_ = c_[wa_]; \
        c_[wa_] = (v_ & ~(mb_ | me_)) | ((P) & mb_ & ~me_) | ((E) & me_); } \
    else { int w_; v_ = c_[wa_]; c_[wa_] = (v_ & ~(mb_ | me_)) | ((P) & mb_ & ~me_) | ((E) & me_); \
        if ((D) == 0xFFFFFFFFu) for (w_ = wa_ + 1; w_ < wb_; w_++) c_[w_] = (P); \
        else if (D) for (w_ = wa_ + 1; w_ < wb_; w_++) c_[w_] = (c_[w_] & ~(D)) | ((P) & (D)); \
        mb_ = MTO(kb_) & (D); v_ = c_[wb_]; c_[wb_] = (v_ & ~(mb_ | eb_)) | ((P) & mb_ & ~eb_) | ((E) & eb_); } \
} while (0)
/* plain: no outline, words through D */
#define WPLAIN(col, a, b, P, D) do { \
    u32 *__restrict__ c_ = (col); int wa_ = (a) >> 4, wb_ = ((b) - 1) >> 4; \
    u32 ma_ = MFROM((a) & 15) & (D), mb_ = MTO((((b) - 1) & 15) + 1) & (D); \
    if (wa_ == wb_) { ma_ &= mb_; c_[wa_] = (c_[wa_] & ~ma_) | ((P) & ma_); } \
    else { int w_; c_[wa_] = (c_[wa_] & ~ma_) | ((P) & ma_); \
        if ((D) == 0xFFFFFFFFu) for (w_ = wa_ + 1; w_ < wb_; w_++) c_[w_] = (P); \
        else for (w_ = wa_ + 1; w_ < wb_; w_++) c_[w_] = (c_[w_] & ~(D)) | ((P) & (D)); \
        c_[wb_] = (c_[wb_] & ~mb_) | ((P) & mb_); } \
} while (0)

static const u32 ones8[8] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u}, door_none[8] = {0, 0, 0, 0, 0, 0, 0, 0};

/* ---- segment loops: n columns where the top and bottom edges stay the same. Few live values, so gcc keeps them in
   registers (the general loop spilled dozens a column). v ^ ((v ^ P) & m) puts P into the bits of m. */
#define PUT(p, w, P, m) { u32 v_ = (p)[w]; (p)[w] = v_ ^ ((v_ ^ (P)) & (m)); }
#define SEG_HEAD \
    for (; n > 0; n--, pA += 16, k = (k + 1) & 7) { \
        int a = ty >> 16, b = by >> 16, wa, wb; \
        u32 P, ma, mb; \
        ty += tdy; by += bdy; \
        if (a < 0) a = 0; \
        if (b > VH) b = VH; \
        if (a >= b) { BSTEP; continue; } \
        wa = a >> 4; wb = (b - 1) >> 4; \
        ma = 0xFFFFFFFFu << ((a & 15) << 1); \
        mb = 0xFFFFFFFFu >> ((15 - ((b - 1) & 15)) << 1);

/* both eyes, plain */
#define BSTEP pB += 16
u32 st_px, st_kind[8];
static void __attribute__((noinline)) seg2(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, int k)
{
    SEG_HEAD
#ifdef HOST
        st_px += 2 * (b - a);
#endif
        P = pat[k];
        if (wa == wb) { u32 m = ma & mb; PUT(pA, wa, P, m); PUT(pB, wa, P, m); }
        else {
            int w;
            PUT(pA, wa, P, ma); PUT(pB, wa, P, ma);
            for (w = wa + 1; w < wb; w++) { pA[w] = P; pB[w] = P; }
            PUT(pA, wb, P, mb); PUT(pB, wb, P, mb);
        }
        pB += 16;
    }
}
/* both eyes, a screen door */
static void __attribute__((noinline)) seg2d(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, const u32 *door, int k)
{
    SEG_HEAD
        u32 D = door[k];
        P = pat[k];
        if (wa == wb) { u32 m = ma & mb & D; PUT(pA, wa, P, m); PUT(pB, wa, P, m); }
        else {
            int w;
            ma &= D; mb &= D;
            PUT(pA, wa, P, ma); PUT(pB, wa, P, ma);
            for (w = wa + 1; w < wb; w++) { PUT(pA, w, P, D); PUT(pB, w, P, D); }
            PUT(pA, wb, P, mb); PUT(pB, wb, P, mb);
        }
        pB += 16;
    }
}
/* both eyes, two columns a step (far faces) */
static void __attribute__((noinline)) seg2w(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, int k)
{
    for (; n > 0; n -= 2, pA += 32, pB += 32, k = (k + 2) & 7) {
        int a = ty >> 16, b = by >> 16, wa, wb;
        u32 P, Q, ma, mb;
        ty += tdy; by += bdy;
        if (a < 0) a = 0;
        if (b > VH) b = VH;
        if (a >= b) continue;
#ifdef HOST
        st_px += 4 * (b - a);
#endif
        wa = a >> 4; wb = (b - 1) >> 4;
        ma = 0xFFFFFFFFu << ((a & 15) << 1);
        mb = 0xFFFFFFFFu >> ((15 - ((b - 1) & 15)) << 1);
        P = pat[k]; Q = pat[k + 1];
        if (wa == wb) {
            u32 m = ma & mb;
            PUT(pA, wa, P, m); PUT(pB, wa, P, m);
            if (n > 1) { PUT(pA + 16, wa, Q, m); PUT(pB + 16, wa, Q, m); }
        } else {
            int w;
            PUT(pA, wa, P, ma); PUT(pB, wa, P, ma);
            for (w = wa + 1; w < wb; w++) { pA[w] = P; pB[w] = P; }
            PUT(pA, wb, P, mb); PUT(pB, wb, P, mb);
            if (n > 1) {
                u32 *qA = pA + 16, *qB = pB + 16;
                PUT(qA, wa, Q, ma); PUT(qB, wa, Q, ma);
                for (w = wa + 1; w < wb; w++) { qA[w] = Q; qB[w] = Q; }
                PUT(qA, wb, Q, mb); PUT(qB, wb, Q, mb);
            }
        }
    }
}
#undef BSTEP
#define BSTEP
/* one eye, plain or a screen door (door all ones: plain) */
static void __attribute__((noinline)) seg1(u32 *pA, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, const u32 *door, int k)
{
    SEG_HEAD
#ifdef HOST
        st_px += (b - a);
#endif
        u32 D = door[k];
        P = pat[k];
        if (wa == wb) { u32 m = ma & mb & D; PUT(pA, wa, P, m); }
        else {
            int w;
            PUT(pA, wa, P, ma & D);
            if (D == 0xFFFFFFFFu) for (w = wa + 1; w < wb; w++) pA[w] = P;
            else for (w = wa + 1; w < wb; w++) PUT(pA, w, P, D);
            PUT(pA, wb, P, mb & D);
        }
    }
}
/* one eye or both (pB 0), outlined: the top tt rows and bottom bt rows in E; body through door */
#ifdef HOST
static void __attribute__((noinline)) seg_e(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, const u32 *ep,
    const u32 *door, int k, int tt, int bt)
{
    for (; n > 0; n--, pA += 16, k = (k + 1) & 7) {
        int a = ty >> 16, b = by >> 16, ra = a, rb = b, wa, wb, ka, kb;
        u32 P, E, D, ma, mb, et, eb;
        ty += tdy; by += bdy;
        if (a < 0) a = 0;
        if (b > VH) b = VH;
        if (a >= b) { if (pB) pB += 16; continue; }
    #ifdef HOST
    st_px += (b - a) * (pB ? 2 : 1);
#endif
        wa = a >> 4; wb = (b - 1) >> 4; ka = a & 15; kb = ((b - 1) & 15) + 1;
        P = pat[k]; E = ep[k]; D = door[k];
        ma = 0xFFFFFFFFu << (ka << 1);
        mb = 0xFFFFFFFFu >> ((16 - kb) << 1);
        et = a == ra ? ma & (ka + tt >= 16 ? 0xFFFFFFFFu : 0xFFFFFFFFu >> ((16 - ka - tt) << 1)) : 0;
        eb = b == rb ? mb & (kb - bt <= 0 ? 0xFFFFFFFFu : 0xFFFFFFFFu << ((kb - bt) << 1)) : 0;
        if (wa == wb) {
            u32 m = ma & mb, e = (et | eb) & m, f = m & D & ~e, val = (P & f) | (E & e);
            m = f | e;
            PUT(pA, wa, val, m);
            if (pB) PUT(pB, wa, val, m);
        } else {
            int w;
            u32 f = ma & D & ~et, val = (P & f) | (E & et), m = f | et;
            PUT(pA, wa, val, m);
            if (pB) PUT(pB, wa, val, m);
            for (w = wa + 1; w < wb; w++) { PUT(pA, w, P, D); if (pB) PUT(pB, w, P, D); }
            f = mb & D & ~eb; val = (P & f) | (E & eb); m = f | eb;
            PUT(pA, wb, val, m);
            if (pB) PUT(pB, wb, val, m);
        }
        if (pB) pB += 16;
    }
}

/* (the PC's stand-in for src/seg.s seg_ew: two columns a step, both written the same) */
static void seg_ew(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, const u32 *ep, const u32 *door, int k, int tt, int bt)
{
    for (; n > 0; n -= 2, pA += 32, pB = pB ? pB + 32 : 0, ty += tdy, by += bdy, k += 2) {
        seg_e(pA, pB, 1, ty, 0, by, 0, pat, ep, door, k & 7, tt, bt);
        seg_e(pA + 16, pB ? pB + 16 : 0, 1, ty, 0, by, 0, pat, ep, door, k & 7, tt, bt);
    }
}
#else
void seg_e(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, const u32 *ep, const u32 *door, int k, int tt, int bt);   /* src/seg.s */
void seg_ew(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, const u32 *pat, const u32 *ep, const u32 *door, int k, int tt, int bt);
#endif

/* textured columns: the face's rows [ra, rb) from the pre-scaled column of texel u and height rb - ra */
#ifndef HOST
void seg_tex_a(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, s32 tu, s32 tdu, const u32 *base, const u16 *hoffs, int key);
void seg_tex_aw(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, s32 tu, s32 tdu, const u32 *base, const u16 *hoffs, int key);   /* (two columns a step) */   /* src/seg.s */
#endif
/* one textured column, any height: rows [ra, rb) of texel column u. Up to TEX_HMAX rows from its pre-scaled column; taller
   (a face right in front of the eye) from the column of half (a quarter) the height with each row doubled (4 times), a word
   at a time through spread tables, so the ROM keeps only heights up to TEX_HMAX (2 MB) */
static u32 spread2[256], spread4[256];             /* (4 pixels -> each twice: 16 bits; -> each 4 times: 32 bits) */
static void tex_col(u32 *pA, u32 *qB, int ra, int rb, int u, const u32 *base, int key)
{
    int a = ra, b = rb, h = rb - ra, wa, wb, w, f = 1, hh;
    const u32 *src;
    if (a < 0) a = 0;
    if (b > VH) b = VH;
    if (a >= b) return;
    if (h > TEX_HMAX) { f = h > 2 * TEX_HMAX ? 4 : 2; if (h > 4 * TEX_HMAX) return; }
    hh = f == 1 ? h : (h + f - 1) / f;
    if (hh > TEX_HMAX) hh = TEX_HMAX;
    src = base + (u & 15) * (TEX_PER / 16) + hoff[hh];
    wa = a >> 4; wb = (b - 1) >> 4;
    for (w = wa; w <= wb; w++) {
        int p = (w << 4) - ra, q = p < 0 ? 0 : p;           /* (output row of this word's first row; from row q on) */
        int s0 = q / f, j = s0 >> 4, sh = (s0 & 15) << 1, r;
        u32 v, m = 0xFFFFFFFFu, x = sh ? (src[j] >> sh) | (src[j + 1] << (32 - sh)) : src[j];
        if (f == 1) v = x;
        else if (f == 2) {
            r = q & 1;
            v = spread2[x & 255] | (spread2[(x >> 8) & 255] << 16);
            if (r) v = (v >> 2) | (((x >> 16) & 3) << 30);
        } else {
            r = (q & 3) << 1;
            v = (spread4[x & 255] >> r) | (r ? spread4[(x >> 8) & 3] << (32 - r) : 0);
        }
        if (p < 0) v <<= (-p) << 1;
        if (w == wa) m = 0xFFFFFFFFu << ((a & 15) << 1);
        if (w == wb) m &= 0xFFFFFFFFu >> ((15 - ((b - 1) & 15)) << 1);
        if (key) { u32 nz = (v | (v >> 1)) & 0x55555555u; m &= nz | (nz << 1); }   /* (plants: shade 0 is see-through) */
        if (m == 0xFFFFFFFFu) { pA[w] = v; if (qB) qB[w] = v; }
        else { PUT(pA, w, v, m); if (qB) PUT(qB, w, v, m); }
    }
}

static void __attribute__((noinline)) seg_tex_c(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, s32 tu, s32 tdu, const u32 *base, int key)
{
    for (; n > 0; n--, pA += 16, ty += tdy, by += bdy, tu += tdu) {
        tex_col(pA, pB, ty >> 16, by >> 16, tu >> 16, base, key);   /* (u & 15: a merged run repeats the texture) */
        if (pB) pB += 16;
    }
}

static void seg_tex(u32 *pA, u32 *pB, int n, s32 ty, s32 tdy, s32 by, s32 bdy, s32 tu, s32 tdu, const u32 *base, int key)
{
#ifndef HOST
    seg_tex_a(pA, pB, n, ty, tdy, by, bdy, tu, tdu, base, hoff, key); return;
#endif
    seg_tex_c(pA, pB, n, ty, tdy, by, bdy, tu, tdu, base, key);
}

/* texture rectangles and the grass lip (near faces only): a second pass over the columns */
u32 st_deco;
static void deco_pass(int cs, int ce, u32 *fA, u32 *fB, const Style *st, int ph)
{
    st_deco += ce - cs;
    s32 ty, tdy, by, bdy, tu, tdu;
    int c, lines = st->lines_only;
    chain_set(&gt, cs); chain_set(&gb, cs);
    ty = gt.y; tdy = gt.dy; by = gb.y; bdy = gb.dy; tu = gt.u; tdu = gt.du;
    for (c = cs; c < ce; c++, tu += tdu) {
        int a, b, ra, rb, k = (c + ph) & 7, u, h, q, e;
        if (c >= gt.cend) { chain_set(&gt, c); ty = gt.y; tdy = gt.dy; tu = gt.u; tdu = gt.du; }
        if (c >= gb.cend) { chain_set(&gb, c); by = gb.y; bdy = gb.dy; }
        ra = a = ty >> 16; rb = b = by >> 16;
        ty += tdy; by += bdy;
        if (a < 0) a = 0;
        if (b > VH) b = VH;
        if (a >= b || rb - ra < 4) continue;
        u = tu >> 16; h = rb - ra;
        if (u < 0) u = 0;
        if (u > 15) u = 15;
        if (st->deco == D_GRASS && !lines) {
            e = ra + ((h * grass_lip[u] + 8) >> 4);
            if (e > b) e = b;
            if (e > a) {
                u32 g = lev_pat[T_GRASS][st->lev + 12 > 48 ? 48 : st->lev + 12][k];
                span(fA + c * 16, a, e, g);
                if (fB) span(fB + c * 16, a, e, g);
            }
        }
        if (st->rects)
            for (q = 0; st->rects[q].u1 != 255; q++) {
                const Rect *r = &st->rects[q];
                int r0, r1, lv;
                u32 g;
                if (r->u0 == r->u1) { if (u != r->u0 && !(r->u0 == 16 && u == 15)) continue; }
                else if (u < r->u0 || u >= r->u1) continue;
                r0 = ra + ((h * r->v0) >> 4); r1 = ra + ((h * r->v1) >> 4);
                if (r1 <= r0) r1 = r0 + 1;
                if (r->v1 == 16 && r->v0 == 16) { r0 = rb - 1; r1 = rb; }
                if (r0 < a) r0 = a;
                if (r1 > b) r1 = b;
                if (r0 >= r1) continue;
                lv = r->d >= 100 ? r->d - 100 + (st->lev >> 1) : lines ? st->lev : st->lev + r->d;
                if (lv < 0) lv = 0;
                if (lv > 48) lv = 48;
                g = lev_pat[T_FLAT][lv][k];
                span(fA + c * 16, r0, r1, g);
                if (fB) span(fB + c * 16, r0, r1, g);
            }
    }
}

#define GUARD 120                                  /* columns past either side that may be written (character memory) */

/* fill a convex polygon (screen Q4). Column c lands at c + offA in fbA and, if fbB, at c + offB in fbB: one walk for both
   eyes when the face is far enough that its disparity is one number. The texture's phase: (c + ph) & 7. */
static void fill_core(const s32 *xs, const s32 *ys, const s32 *us, int n, const Style *st, u32 *fbA, int offA, u32 *fbB, int offB, int ph, int far, int clo, int chi, int area, int L, int R);

/* fill a polygon: the right eye is clipped exactly (its spare buffer has no room round it), the left may spill into the
   character memory beside it. Where only the left eye is on screen its columns go alone. */
static void fill_poly(const s32 *xs, const s32 *ys, const s32 *us, int n, const Style *st, u32 *fbA, int offA, u32 *fbB, int offB, int ph, int far)
{
    int i, area = 0, L = 0, R = 0, c0, c1;
    for (i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        area += (xs[i] >> 2) * (ys[j] >> 2) - (xs[j] >> 2) * (ys[i] >> 2);
        if (xs[i] < xs[L]) L = i;
        if (xs[i] > xs[R]) R = i;
    }
    if (area == 0) return;
    if (!fbB) {
        if (fbA == fb_r) fill_core(xs, ys, us, n, st, fbA, offA, 0, 0, ph, far, -offA, VW - offA, area, L, R);
        else fill_core(xs, ys, us, n, st, fbA, offA, 0, 0, ph, far, -offA - GUARD + 2, VW - offA + GUARD - 2, area, L, R);
        return;
    }
    c0 = (xs[L] + 7) >> 4; c1 = (xs[R] + 7) >> 4;
    fill_core(xs, ys, us, n, st, fbA, offA, fbB, offB, ph, far, -offB, VW - offB, area, L, R);
    if (c0 < -offB && -offB > -offA - GUARD + 2) fill_core(xs, ys, us, n, st, fbA, offA, 0, 0, ph, far, -offA - GUARD + 2, -offB, area, L, R);
    if (c1 > VW - offB && VW - offB < VW - offA + GUARD - 2) fill_core(xs, ys, us, n, st, fbA, offA, 0, 0, ph, far, VW - offB, VW - offA + GUARD - 2, area, L, R);
}

static void fill_core(const s32 *xs, const s32 *ys, const s32 *us, int n, const Style *st, u32 *fbA, int offA, u32 *fbB, int offB, int ph, int far, int clo, int chi, int area, int L, int R)
{
    int c0, c1, cs, ce, lo, hi;
#ifdef NO_LOOP
    return;
#endif
    c0 = (xs[L] + 7) >> 4; c1 = (xs[R] + 7) >> 4;
    lo = clo; hi = chi;
    cs = c0 < lo ? lo : c0; ce = c1 > hi ? hi : c1;
    if (cs >= ce) return;
#ifdef HOST
    st_polys++;
#endif
    gxs = xs; gys = ys; gus = st->rects || st->deco == D_GRASS || st->texv >= 0 ? us : 0; gn = n;
    gt.i = L; gt.last = R; gt.step = area > 0 ? 1 : -1;
    gb.i = L; gb.last = R; gb.step = -gt.step;
    gt.u = gt.du = 0;
    chain_set(&gt, cs);
    chain_set(&gb, cs);
    {
        u32 *fA = fbA + offA * 16, *fB = fbB ? fbB + offB * 16 : 0;
        const u32 *door = st->door ? st->door : ones8;
        const u32 *ep = st->edge;
        int c = cs, wide = (far || NEAR_WIDE) && !st->lines_only;
        while (c < ce) {
            int e, n, k = (c + ph) & 7, w2;
            u32 *pb;
            if (c >= gt.cend) chain_set(&gt, c);
            if (c >= gb.cend) chain_set(&gb, c);
            e = gt.cend < gb.cend ? gt.cend : gb.cend;
            if (e > ce) e = ce;
            if (ep) {
                /* the polygon's first and last columns are the outline whole */
                if (c == c0 || c == c1 - 1) e = c + 1;
                else if (e > c1 - 1) e = c1 - 1;
            }
            n = e - c;
            /* (the pair loops write columns two at a time: a run that ends the face or the clip on an odd count leaves its
               last column to the next step, else the right eye's column 384 lands in the height table after it) */
            if (n > 1 && (n & 1) && e == ce) { n--; e--; }
#ifdef HOST
            st_cols += fB ? 2 * n : n; st_words++;
#endif
            w2 = wide && n > 1;
            pb = fB ? fB + c * 16 : 0;
#ifdef HOST
            st_kind[st->texv >= 0 ? 0 : ep ? 1 : w2 && !st->door ? 2 : fB ? (st->door ? 3 : 4) : 5] += fB ? 2 * n : n;
#endif
            {
            if (st->texv >= 0) {
                if (((gb.y - gt.y) >> 16) > TEX_HMAX || ((gb.y + gb.dy * (n - 1) - gt.y - gt.dy * (n - 1)) >> 16) > TEX_HMAX)
                    seg_tex_c(fA + c * 16, pb, n, gt.y, gt.dy, gb.y, gb.dy, gt.u, gt.du, TEXB + st->texv * TEX_PER, st->key);
                else
#ifndef HOST
                if ((far || NEAR_WIDE) && n > 1)
                    seg_tex_aw(fA + c * 16, pb, n, gt.y, gt.dy << 1, gb.y, gb.dy << 1, gt.u, gt.du << 1, TEXB + st->texv * TEX_PER, hoff, st->key);
                else
#endif
                seg_tex(fA + c * 16, pb, n, gt.y, gt.dy, gb.y, gb.dy, gt.u, gt.du, TEXB + st->texv * TEX_PER, st->key);
            }
            else if (ep) {
                int whole = c == c0 || c == c1 - 1;
                if (whole) seg_e(fA + c * 16, pb, n, gt.y, gt.dy, gb.y, gb.dy, ep, ep, ones8, k, 0, 0);
                else if (st->lines_only) seg_e(fA + c * 16, pb, n, gt.y, gt.dy, gb.y, gb.dy, ep, ep, door_none, k, gt.thick, gb.thick);
                else if (w2) seg_ew(fA + c * 16, pb, n, gt.y, gt.dy << 1, gb.y, gb.dy << 1, st->pat, ep, door, k, gt.thick, gb.thick);
                else seg_e(fA + c * 16, pb, n, gt.y, gt.dy, gb.y, gb.dy, st->pat, ep, door, k, gt.thick, gb.thick);
            } else if (w2 && (st->door || !fB)) seg_ew(fA + c * 16, pb, n, gt.y, gt.dy << 1, gb.y, gb.dy << 1, st->pat, st->pat, door, k, 0, 0);
            else if (w2 && !st->door) seg2w(fA + c * 16, pb, n, gt.y, gt.dy << 1, gb.y, gb.dy << 1, st->pat, k);
            else if (fB) {
                if (st->door) seg2d(fA + c * 16, pb, n, gt.y, gt.dy, gb.y, gb.dy, st->pat, door, k);
                else seg2(fA + c * 16, pb, n, gt.y, gt.dy, gb.y, gb.dy, st->pat, k);
            } else seg1(fA + c * 16, n, gt.y, gt.dy, gb.y, gb.dy, st->pat, door, k);
            }
            gt.y += gt.dy * n; gb.y += gb.dy * n; gt.u += gt.du * n;
            c = e;
        }
        if ((st->rects || st->deco == D_GRASS) && st->texv < 0 && gus) {
            gt.i = L; gb.i = L;
            deco_pass(cs, ce, fA, fB, st, ph);
        }
    }
}

/* ---- projection */
static s32 dcomp(s32 d)
{
    if (d > DMAX) d = DMAX + ((d - DMAX) >> 2);
    return d;
}

s32 disparity(s32 z)
{
    s32 inv;
    if (z < NEAR) z = NEAR;
    inv = RECIP(z);
    return dcomp(((EYE * inv) >> 12) - s_conv);
}

#define OC_NEAR 1
#define OC_GUARD 2
#define OC_L 4
#define OC_R 8
#define OC_T 16
#define OC_B 32
static int outcode(const CV *v)
{
    int o = 0;
    s32 z = v->z, xf = v->x * FOC, yf = v->y * FOC;
    if (z < NEAR) o |= OC_NEAR;
    if (v->x > 12 * z || v->x < -12 * z || v->y > 12 * z || v->y < -12 * z) o |= OC_GUARD;
    if (xf < -216 * z) o |= OC_L;
    if (xf > 216 * z) o |= OC_R;
    if (yf > 116 * z) o |= OC_T;
    if (yf < -116 * z) o |= OC_B;
    return o;
}

static int clip_plane(CV *in, int n, CV *out, int pl)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        const CV *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
        s32 da, db;
        switch (pl) {
        case 0: da = a->z - NEAR; db = b->z - NEAR; break;
        case 1: da = 12 * a->z - a->x; db = 12 * b->z - b->x; break;
        case 2: da = 12 * a->z + a->x; db = 12 * b->z + b->x; break;
        case 3: da = 12 * a->z - a->y; db = 12 * b->z - b->y; break;
        default: da = 12 * a->z + a->y; db = 12 * b->z + b->y; break;
        }
        if (da >= 0) out[m++] = *a;
        if ((da >= 0) != (db >= 0)) {
            s32 t = (da << 14) / (da - db);        /* Q14 */
            CV *o = &out[m++];
            o->x = a->x + (((b->x - a->x) * t) >> 14);
            o->y = a->y + (((b->y - a->y) * t) >> 14);
            o->z = a->z + (((b->z - a->z) * t) >> 14);
            o->u = a->u + (((b->u - a->u) * t) >> 14);
            o->v = a->v + (((b->v - a->v) * t) >> 14);
        }
    }
    return m;
}

/* draw a polygon given in camera space */
/* fill a projected polygon in both eyes */
static void draw_proj(const s32 *xs, const s32 *ys, const s32 *ds, const s32 *us, int n, s32 zmin, s32 dsum, const Style *st, int ph_extra)
{
    s32 xe[12];
    int i, ph = (dsum / n + 8) >> 4;              /* (the texture's shift: the face's mean disparity) */
    u32 t0 = PT;
#ifdef NO_FILL
    return;
#endif
    if (zmin >= FAR_ONE)                          /* far enough: one walk, both eyes, the face's mean disparity */
        fill_poly(xs, ys, us, n, st, fb_l, ph, fb_r, -ph, ph_extra, zmin >= FAR_TWO);
    else {
        for (i = 0; i < n; i++) xe[i] = xs[i] + ds[i];
        fill_poly(xe, ys, us, n, st, fb_l, 0, 0, 0, -ph + ph_extra, 0);
        for (i = 0; i < n; i++) xe[i] = xs[i] - ds[i];
        fill_poly(xe, ys, us, n, st, fb_r, 0, 0, 0, ph + ph_extra, 0);
    }
    prof[4] += PT - t0;
}

/* draw a polygon given in camera space (clipped as needed) */
static void draw_cv(CV *v, int n, const Style *st, int ph_extra)
{
    static CV b1[12], b2[12];
    s32 xs[12], ys[12], ds[12], us[12], dsum = 0, zmin = 1 << 30;
    int i, oa = 0xFF, oo = 0;
    for (i = 0; i < n; i++) { int o = outcode(&v[i]); oa &= o; oo |= o; }
    if (oa & (OC_NEAR | OC_L | OC_R | OC_T | OC_B)) return;
    st_words++;
    if (oo & (OC_NEAR | OC_GUARD)) {
        n = clip_plane(v, n, b1, 0);
        if (oo & OC_GUARD) {
            n = clip_plane(b1, n, b2, 1); n = clip_plane(b2, n, b1, 2);
            n = clip_plane(b1, n, b2, 3); n = clip_plane(b2, n, b1, 4);
        }
        if (n < 3) return;
        v = b1;
    }
    for (i = 0; i < n; i++) {
        s32 z = v[i].z < NEAR ? NEAR : v[i].z, inv = RECIP(z);
        xs[i] = CX4 + ((v[i].x * inv) >> 12);
        ys[i] = CY4 - ((v[i].y * inv) >> 12);
        ds[i] = dcomp(((EYE * inv) >> 12) - s_conv);
        us[i] = v[i].u;
        dsum += ds[i];
        if (z < zmin) zmin = z;
    }
    draw_proj(xs, ys, ds, us, n, zmin, dsum, st, ph_extra);
}

/* ---- camera */
static s32 b22x, b22y, b22z;                       /* the world origin in camera space, Q22 */

void cam_to(const V3 *w, V3 *c)
{
    s32 dx = w->x - cam_x, dy = w->y - cam_y, dz = w->z - cam_z;
    c->x = (rx * dx + rz * dz) >> 14;
    c->y = (ux * dx + uy * dy + uz * dz) >> 14;
    c->z = (fx * dx + fy * dy + fz * dz) >> 14;
}

static void setup_cam(void)
{
    s32 sy = SIN(cam_yaw), cy = COS(cam_yaw), sp = SIN(cam_pitch), cp = COS(cam_pitch);
    rx = cy; rz = -sy;
    ux = -((sy * sp) >> 14); uy = cp; uz = -((cy * sp) >> 14);
    fx = (sy * cp) >> 14; fy = sp; fz = (cy * cp) >> 14;
    s_conv = (EYE * FOC * 16) / conv;
}

/* ---- sky and sea: a column per dither phase, rebuilt each picture for the horizon's row */
static u32 sky_col[8][14];
static s32 horizon;
static const s32 SKY_TOP = 14, SKY_HZ = 38, SEA_HZ = 20, SEA_NEAR = 9;

static void make_sky(void)
{
    s32 t = (SIN(cam_pitch) << 8) / (COS(cam_pitch) > 64 ? COS(cam_pitch) : 64);   /* tan, Q8 */
    int c, r;
    s32 lev[VH];
    static s32 last_h = -9999, last_w = -1;
    horizon = 112 + ((FOC * t) >> 8);
    if (horizon == last_h && water_view == last_w) return;
    last_h = horizon; last_w = water_view;
    for (r = 0; r < VH; r++) {
        s32 d = r - horizon, l;
        if (water_view) l = 10 + (r >> 5);
        else if (d < 0) { d = -d; l = SKY_HZ - (d * (SKY_HZ - SKY_TOP)) / 140; if (l < SKY_TOP) l = SKY_TOP; }
        else {
            /* the open sea: dark, darker toward the viewer, with wave lines that crowd toward the horizon */
            l = SEA_HZ - (d * (SEA_HZ - SEA_NEAR)) / 50; if (l < SEA_NEAR) l = SEA_NEAR;
            if (((4096 / (d + 3)) & 15) < 4) l += 9;
            if (d < 2) l = SKY_HZ + 4;
        }
        lev[r] = l;
    }
    for (c = 0; c < 8; c++)
        for (r = 0; r < 14; r++) {
            u32 w = 0;
            int k;
            for (k = 0; k < 16; k++) {
                int y = r * 16 + k, v = lev[y], s = v >> 4;
                if ((v & 15) > bayer[y & 3][c & 3]) s++;
                w |= (u32)s << (k * 2);
            }
            sky_col[c][r] = w;
        }
}

static void sky_fill(u32 *__restrict__ fb)
{
    int k;
    for (k = 0; k < 8; k++) {
        /* (a phase's 14 words in registers, stored into every 8th column: a load per word costs 5 instructions) */
        const u32 *s = sky_col[k];
        u32 a0 = s[0], a1 = s[1], a2 = s[2], a3 = s[3], a4 = s[4], a5 = s[5], a6 = s[6];
        u32 a7 = s[7], a8 = s[8], a9 = s[9], a10 = s[10], a11 = s[11], a12 = s[12], a13 = s[13];
        u32 *col = fb + k * 16, *end = fb + VW * 16;
        for (; col < end; col += 128) {
            col[0] = a0; col[1] = a1; col[2] = a2; col[3] = a3; col[4] = a4; col[5] = a5; col[6] = a6;
            col[7] = a7; col[8] = a8; col[9] = a9; col[10] = a10; col[11] = a11; col[12] = a12; col[13] = a13;
        }
    }
}

/* a quad far away in a world direction (the sun, clouds): dir Q14 offsets from the camera, size in Q8 at 64 blocks */
static void far_quad(s32 wx0, s32 wy0, s32 wz0, s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz, const Style *st)
{
    CV v[4];
    V3 w, c;
    int i;
    for (i = 0; i < 4; i++) {
        s32 ka = (i == 1 || i == 2), kb = (i >= 2);
        w.x = cam_x + wx0 + ka * ax + kb * bx; w.y = cam_y + wy0 + ka * ay + kb * by; w.z = cam_z + wz0 + ka * az + kb * bz;
        cam_to(&w, &c);
        v[i].x = c.x; v[i].y = c.y; v[i].z = c.z; v[i].u = 0; v[i].v = 0;
    }
    draw_cv(v, 4, st, 0);
}

static void sky_things(void)
{
    Style st;
    int i;
    st.door = 0; st.rects = 0; st.deco = 0; st.lines_only = 0; st.tex = T_FLAT; st.texv = -1; st.key = 0;
    /* the sun: a square, as it should be */
    st.lev = 48; st.pat = &lev_pat[T_FLAT][48][0]; st.edge = 0;
    far_quad(9000, 7000, 12000, 2400, 0, -1800, 0, 2400, 0, &st);
    st.lev = 38; st.pat = &lev_pat[T_FLAT][36][0];
    /* clouds: flat slabs high up, drifting */
    for (i = 0; i < 12; i++) {
        u32 h = hsh(i * 131 + 7);
        /* (world-anchored in a field of 256 blocks that wraps round the camera) */
        s32 x = (s32)(h & 0xFFFF) + frame_no * 2 - cam_x, z = (s32)((h >> 16) & 0xFFFF) - cam_z;
        s32 w = 1024 + (s32)((h >> 12) & 3) * 768, d = 768 + (s32)((h >> 14) & 3) * 512;
        x = ((x + 32768) & 65535) - 32768; z = ((z + 32768) & 65535) - 32768;
        if ((x >> 8) * (x >> 8) + (z >> 8) * (z >> 8) > 110 * 110) continue;
        far_quad(x, 34 * 256 - cam_y, z, w, 0, 0, 0, 0, d, &st);
    }
}

/* ---- the world */
static const u8 fc[6][4] = {{6, 2, 0, 4}, {3, 7, 5, 1}, {0, 1, 5, 4}, {6, 7, 3, 2}, {2, 3, 1, 0}, {7, 6, 4, 5}};
static const s32 fu[4] = {0, 256, 256, 0}, fv[4] = {0, 0, 256, 256};
static s32 sxv[3], syv[3], szv[3];                /* Q22 a block along x y z, in camera space */

#ifdef CINEMA
#define FOG0 (10 * 256)
#define FOG1 (46 * 256)
#define FOGS 7                                    /* (fog_tab: a step of 1 << FOGS: 128 steps reach FOGS = 7: 64 blocks) */
#else
#define FOG0 (6 * 256)
#define FOG1 (30 * 256)
#define FOGS 6
#endif
#define FOGI(z) ((z) >= (127 << FOGS) ? 255 : fog_tab[(z) >> FOGS])
#define FOG_L 35

static u8 lev_base[NBLOCK][6], fog_tab[128];
static const u8 *const edge_col_air = 0;
static u8 out_col[WH];                             /* a column outside the world: bedrock, sea, air */

static void world_tables(void)
{
    int t, d, i;
    for (t = 0; t < NBLOCK; t++)
        for (d = 0; d < 6; d++) {
            const Look *lk = &looks[t][d == 3 ? 0 : d == 2 ? 2 : 1];
            lev_base[t][d] = (u8)((t == LAMP || t == GLASS) ? lk->lev : (lk->lev * dir_shade[d]) >> 8);
        }
    for (i = 0; i < 128; i++) {
        s32 z = i << FOGS, f = z <= FOG0 ? 0 : z >= FOG1 ? 256 : ((z - FOG0) << 8) / (FOG1 - FOG0);
        f = (f * (512 - f)) >> 10;                /* (eased: far hills and shores keep their shape, at most 1/2 haze) */
        fog_tab[i] = (u8)(f > 255 ? 255 : f);
    }
    for (i = 0; i < WH; i++) out_col[i] = (u8)(i < 3 ? BEDROCK : i < SEA ? WATER : AIR);
}

static void face_style(Style *st, int t, int dir, s32 z, int sel)
{
    const Look *lk = &looks[t][dir == 3 ? 0 : dir == 2 ? 2 : 1];
    s32 lev = lev_base[t][dir], f = z < 0 ? 0 : FOGI(z);
    lev += ((FOG_L - lev) * f) >> 8;
    st->lev = lev; st->tex = lk->tex; st->deco = lk->deco;
    st->pat = &lev_pat[(z < TEXD || dir == 3) && z < TEXD + 3 * 256 ? lk->tex : (lk->tex == T_LEAVES || lk->tex == T_WATER ? lk->tex : T_FLAT)][lev][0];
    st->door = lk->door == 1 ? door_mask[0] : lk->door == 3 ? door_mask[2] : 0;
    st->lines_only = lk->door == 9;
    st->rects = z < TEXD && (lk->deco == D_GLASS || (dir != 2 && dir != 3)) ? deco_rects[lk->deco] : 0;   /* (tops: no seams, they don't map there) */
    if (t == GRASS && dir != 3 && dir != 2 && z >= TEXD) st->deco = 0;
    st->edge = 0;
    if (z < 8 * 256 || st->lines_only || sel) {
        s32 e = z < 5 * 256 ? lev - 16 - (lev >> 2) : lev - 10;
        if (t == LAMP) e = 20;
        if (st->lines_only) e = lev;
        if (e < 0) e = 0;
        if (sel) e = ((frame_no >> 1) & 8) ? 48 : 40;
        st->edge = &lev_pat[T_FLAT][e][0];
    }
#ifdef X_NOEDGE
    if (!st->lines_only && !sel) st->edge = 0;
#endif
#ifdef X_NODECO
    st->rects = 0; if (st->deco == D_GRASS) st->deco = 0;
#endif
    st->texv = -1; st->key = 0;
    if (z < TEXD && dir != 2 && dir != 3 && tex_var[t] >= 0 && !(sel && sel_crack)) {
        st->texv = tex_var[t] + (dir < 2);
        st->edge = sel ? st->edge : 0; st->rects = 0; st->deco = 0;
        if (sel) st->texv = -1;
    }
    if (sel && sel_crack) {
        /* breaking: the face darkens as the crack grows */
        st->door = 0;
        st->pat = &lev_pat[T_ORE][lev - (sel_crack >> 3) < 0 ? 0 : lev - (sel_crack >> 3)][0];
    }
}

/* the faces of one cell. bx..: its corner in camera space (Q22); dirs: the faces toward the camera that show.
   Each corner is projected once; a face that reaches the near plane or far off screen goes the clipping way. */
static void draw_faces(int t, const int *dirs, int nd, s32 bx, s32 by, s32 bz, int sel)
{
    s32 cx_[8], cy_[8], cz_[8], sx[8], sy[8], sd[8];
    u8 oc[8];
    int have = 0, i, d, need = 0;
    s32 cz = (bz + ((sxv[2] + syv[2] + szv[2]) >> 1)) >> 14;
    st_faces += nd;
    for (d = 0; d < nd; d++) need |= (1 << fc[dirs[d]][0]) | (1 << fc[dirs[d]][1]) | (1 << fc[dirs[d]][2]) | (1 << fc[dirs[d]][3]);
    for (i = 0; i < 8; i++) {
        s32 px, py, pz, o = 0;
        if (!(need & (1 << i))) continue;
        px = bx; py = by; pz = bz;
        if (i & 1) { px += sxv[0]; py += sxv[1]; pz += sxv[2]; }
        if (i & 2) {
            if (t == WATER) { px += syv[0] - (syv[0] >> 3); py += syv[1] - (syv[1] >> 3); pz += syv[2] - (syv[2] >> 3); }
            else { px += syv[0]; py += syv[1]; pz += syv[2]; }
        }
        if (i & 4) { px += szv[0]; py += szv[1]; pz += szv[2]; }
        px >>= 14; py >>= 14; pz >>= 14;
        cx_[i] = px; cy_[i] = py; cz_[i] = pz;
        if (pz < NEAR) o = OC_NEAR;
        else {
            s32 g = (pz << 3) + (pz << 2);          /* (12 z: the guard band) */
            if (px > g || px < -g || py > g || py < -g) o = OC_GUARD;
            else {
                /* the frustum's codes from the projected point itself: no multiplies */
                s32 inv = RECIP(pz), xs, ys;
                xs = CX4 + ((px * inv) >> 12);
                ys = CY4 - ((py * inv) >> 12);
                sx[i] = xs; sy[i] = ys;
                sd[i] = dcomp((((inv << 4) - inv) >> 12) - s_conv);
                if (xs < -24 * 16) o |= OC_L;
                if (xs > (VW + 24) * 16) o |= OC_R;
                if (ys < -4 * 16) o |= OC_T;
                if (ys > (VH + 4) * 16) o |= OC_B;
                have |= 1 << i;
            }
        }
        oc[i] = (u8)o;
    }
    for (d = 0; d < nd; d++) {
        Style st;
        int dir = dirs[d], k0 = fc[dir][0], k1 = fc[dir][1], k2 = fc[dir][2], k3 = fc[dir][3];
        int oa = oc[k0] & oc[k1] & oc[k2] & oc[k3], oo = oc[k0] | oc[k1] | oc[k2] | oc[k3];
        if (oa & (OC_NEAR | OC_L | OC_R | OC_T | OC_B)) continue;
        face_style(&st, t, dir, cz, sel);
        if (oo & (OC_NEAR | OC_GUARD)) {
            CV v[4];
            for (i = 0; i < 4; i++) { int k = fc[dir][i]; v[i].x = cx_[k]; v[i].y = cy_[k]; v[i].z = cz_[k]; v[i].u = fu[i]; v[i].v = fv[i]; }
            draw_cv(v, 4, &st, t == WATER ? (frame_no >> 2) : 0);
        } else {
            s32 xs[4], ys[4], ds[4], zmin;
            xs[0] = sx[k0]; xs[1] = sx[k1]; xs[2] = sx[k2]; xs[3] = sx[k3];
            ys[0] = sy[k0]; ys[1] = sy[k1]; ys[2] = sy[k2]; ys[3] = sy[k3];
            ds[0] = sd[k0]; ds[1] = sd[k1]; ds[2] = sd[k2]; ds[3] = sd[k3];
            zmin = cz_[k0]; if (cz_[k1] < zmin) zmin = cz_[k1]; if (cz_[k2] < zmin) zmin = cz_[k2]; if (cz_[k3] < zmin) zmin = cz_[k3];
            draw_proj(xs, ys, ds, fu, 4, zmin, ds[0] + ds[1] + ds[2] + ds[3], &st, t == WATER ? (frame_no >> 2) : 0);
        }
    }
    (void)have;
}

/* the columns round the camera in falling Manhattan distance (a valid back-to-front order for a grid of cubes) */
#define NRING 640
static s8 ring_dx[NRING], ring_dz[NRING];
static s16 ring_of[(2 * VIEW_R + 1) * (2 * VIEW_R + 1)];   /* (a column offset's place in the ring order) */
static int nring;

static void make_rings(void)
{
    int r, i;
    nring = 0;
    for (r = VIEW_R * 2; r >= 0; r--) {
        int n = r ? 4 * r : 1;
        for (i = 0; i < n; i++) {
            int q = r ? i / r : 0, j = r ? i % r : 0, dx, dz;
            switch (q) {
            case 0: dx = r - j; dz = j; break;
            case 1: dx = -j; dz = r - j; break;
            case 2: dx = -(r - j); dz = -j; break;
            default: dx = j; dz = -(r - j); break;
            }
            if (dx * dx + dz * dz > VIEW_R * VIEW_R || nring >= NRING) continue;
            ring_of[(dz + VIEW_R) * (2 * VIEW_R + 1) + dx + VIEW_R] = (s16)nring;
            ring_dx[nring] = (s8)dx; ring_dz[nring] = (s8)dz; nring++;
        }
    }
}

static int show(int n, int t)                     /* (does a face toward neighbour n show) */
{
    return !OPAQUE(n) && n != t;
}

/* the sea: one plane (the water blocks' tops and the endless ocean), drawn between the layers under it and over it */
static int sea_near;                               /* (sea-level water in the near ring: the plane is needed) */
static void water_plane(void)
{
    if (!sea_near) return;
    Style st;
    V3 w, c;
    CV v[4];
    int q;
    for (q = 0; q < 4; q++) {
        w.x = cam_x + ((q == 1 || q == 2) ? (VIEW_R + 1) * 256 : -(VIEW_R + 1) * 256); w.y = SEA * 256 - 32;
        w.z = cam_z + (q >= 2 ? -(VIEW_R + 1) * 256 : (VIEW_R + 1) * 256);
        cam_to(&w, &c);
        v[q].x = c.x; v[q].y = c.y; v[q].z = c.z; v[q].u = v[q].v = 0;
    }
    face_style(&st, WATER, 3, 5 * 256, 0);
    st.edge = 0;
    draw_cv(v, 4, &st, frame_no >> 2);
}

/* ---- the world, layer by layer.
   For horizontal layers wholly below the camera a lower layer can never hide a higher one (a ray to a point descends to it),
   and within a layer the top faces never hide its side faces. So: the layers below the camera bottom up, each one's side
   faces back to front (ring order) and then its top faces, merged into rectangles of one block type and distance band;
   the layers above top down with their bottom faces; the camera's own layer last. The traversal files cells by layer. */
#ifdef CINEMA
#define NREC 2400
#else
#define NREC 1400
#endif
u32 st_recfull;                                   /* (pictures that ran out of records) */
#define GW (2 * VIEW_R + 3)
static u8 rec_x[NREC], rec_z[NREC], rec_t[NREC], rec_f[NREC];
static s16 rec_next[NREC], lay_head[WH], lay_tail[WH], rec_ring[NREC];
static u8 tgrid[GW * GW];
static int nrec;
#define F_X0 1                                     /* side faces: -x, +x, -z, +z; top; bottom */
#define F_X1 2
#define F_Z0 4
#define F_Z1 8
#define F_TOP 16
#define F_BOT 32
#define F_BB 64                                    /* a plant: one picture standing in the cell, turned to the eye */

/* a plant: a picture 0.85 blocks high standing upright in its cell's middle, turned to face the eye (world up stays up) */
/* a plant faces the eye, so on screen it is an upright rectangle at one depth: its texture columns straight into both eyes
   (one disparity), no polygon setup. wx..: its foot (Q8) */
static void plant_quad(s32 wx, s32 wy, s32 wz, s32 half, s32 high, int t)
{
    V3 w, c0, c1;
    s32 inv0, inv1, x0, x1, ya, yb, d, n, u0;
    int var = tex_var[t];
    w.x = wx; w.y = wy; w.z = wz; cam_to(&w, &c0);
    w.y = wy + high; cam_to(&w, &c1);
    if (c0.z < NEAR + 32 || c1.z < NEAR + 32 || var < 0) return;
    inv0 = RECIP(c0.z); inv1 = RECIP(c1.z);
    x0 = CX4 + (((c0.x - half) * inv0) >> 12); x1 = CX4 + (((c0.x + half) * inv0) >> 12);
    yb = CY4 - ((c0.y * inv0) >> 12); ya = CY4 - ((c1.y * inv1) >> 12);
    if (x1 <= x0 || yb <= ya) return;
    d = (dcomp(((EYE * inv0) >> 12) - s_conv) + 8) >> 4;
    {
        int ca = (x0 + 7) >> 4, cb = (x1 + 7) >> 4;
        s32 du = (16 << 16) / ((x1 - x0) >> 4 ? (x1 - x0) >> 4 : 1);
        if (ca < d) ca = d;                         /* (the right eye at ca - d: exactly on screen) */
        if (cb > VW + d) cb = VW + d;
        if (ca < -d - GUARD + 4) ca = -d - GUARD + 4;
        if (cb > VW - d + GUARD - 4) cb = VW - d + GUARD - 4;
        n = cb - ca;
        if (n <= 0) return;
        u0 = ((ca * 16 + 8 - x0) * 16 << 12) / ((x1 - x0) ? (x1 - x0) : 1) << 4;
        seg_tex(fb_l + (ca + d) * 16, fb_r + (ca - d) * 16, n, (ya << 12) + (7 << 12), 0, (yb << 12) + (7 << 12), 0,
                u0, du, TEXB + var * TEX_PER, 1);
    }
}

static void draw_plant(int x, int y, int z, int t)
{
    plant_quad((x << 8) + 128, y << 8, (z << 8) + 128, 118, t == FLOWER ? 220 : 200, t);
}

static void collect(int under_r2)
{
    int ccx = cam_x >> 8, ccy = cam_y >> 8, ccz = cam_z >> 8, i, y;
    /* the frustum's side planes (wider for the eyes' shift), in world space: n . (p - cam) >= 0 inside */
    s32 lnx = (FOC * rx + 230 * fx) >> 8, lny = (230 * fy) >> 8, lnz = (FOC * rz + 230 * fz) >> 8;
    s32 rnx = (-FOC * rx + 230 * fx) >> 8, rny = (230 * fy) >> 8, rnz = (-FOC * rz + 230 * fz) >> 8;
    s32 tnx = (-FOC * ux + 140 * fx) >> 8, tny = (-FOC * uy + 140 * fy) >> 8, tnz = (-FOC * uz + 140 * fz) >> 8;
    s32 bnx = (FOC * ux + 140 * fx) >> 8, bny = (FOC * uy + 140 * fy) >> 8, bnz = (FOC * uz + 140 * fz) >> 8;
    s32 le = ((lnx < 0 ? -lnx : lnx) + (lnz < 0 ? -lnz : lnz)) << 7, re = ((rnx < 0 ? -rnx : rnx) + (rnz < 0 ? -rnz : rnz)) << 7;
    s32 te = ((tnx < 0 ? -tnx : tnx) + (tnz < 0 ? -tnz : tnz)) << 7, be = ((bnx < 0 ? -bnx : bnx) + (bnz < 0 ? -bnz : bnz)) << 7;
    nrec = 0; sea_near = 0;
    for (y = 0; y < WH; y++) lay_head[y] = lay_tail[y] = -1;
    for (i = 0; i < nring; i++) {
        int dx = ring_dx[i], dz = ring_dz[i], wx = ccx + dx, wz = ccz + dz, lo, hi, k, xf, zf, d2;
        s32 px, pz, ylo, yhi, e;
        const u8 *cl, *nx, *nz;
        if ((unsigned)wx >= WX || (unsigned)wz >= WZ) continue;
        k = (wx << 6) + wz;
        lo = col_lo[k]; hi = col_hi[k];
        d2 = dx * dx + dz * dz;
        if (d2 > under_r2 && lo < SEA - 1) lo = SEA - 1;   /* (deep water is murky: its floor only near) */
        if (lo > hi) continue;
        st_cells++;
        px = (wx << 8) + 128 - cam_x; pz = (wz << 8) + 128 - cam_z;
        ylo = (lo << 8) - cam_y; yhi = ((hi + 1) << 8) - cam_y;
        e = lnx * px + lnz * pz + le + (lny > 0 ? lny * yhi : lny * ylo);
        if (e < 0) continue;
        e = rnx * px + rnz * pz + re + (rny > 0 ? rny * yhi : rny * ylo);
        if (e < 0) continue;
        e = tnx * px + tnz * pz + te + (tny > 0 ? tny * yhi : tny * ylo);
        if (e < 0) continue;
        e = bnx * px + bnz * pz + be + (bny > 0 ? bny * yhi : bny * ylo);
        if (e < 0) continue;
        cl = &CELL(wx, 0, wz);
        xf = dx > 0 ? F_X0 : dx < 0 ? F_X1 : 0;
        zf = dz > 0 ? F_Z0 : dz < 0 ? F_Z1 : 0;
        if (dx == 0) { if (cam_x < (wx << 8)) xf = F_X0; else if (cam_x > (wx << 8) + 256) xf = F_X1; }
        if (dz == 0) { if (cam_z < (wz << 8)) zf = F_Z0; else if (cam_z > (wz << 8) + 256) zf = F_Z1; }
        nx = !xf ? 0 : (unsigned)(wx + (xf == F_X1 ? 1 : -1)) < WX ? &CELL(wx + (xf == F_X1 ? 1 : -1), 0, wz) : out_col;
        nz = !zf ? 0 : (unsigned)(wz + (zf == F_Z1 ? 1 : -1)) < WZ ? &CELL(wx, 0, wz + (zf == F_Z1 ? 1 : -1)) : out_col;
        for (y = lo; y <= hi; y++) {
            int t = cl[y], f = 0;
            if (!t) continue;
            if (t == WATER) {
                /* sea-level water is the plane; a pond above it shows its own surface */
                if (y == SEA - 1) sea_near = 1;
                if (y < SEA || y >= ccy || (y + 1 < WH && cl[y + 1] != AIR)) continue;
                f = F_TOP;
            } else if (PLANT(t)) { if (d2 <= VIEW_R * VIEW_R) f = F_BB; }
            else {
            st_colsv++;
            if (xf && show(nx[y], t)) f |= xf;
            if (zf && show(nz[y], t)) f |= zf;
            if (y < ccy) { if (y + 1 >= WH || show(cl[y + 1], t)) f |= F_TOP; }
            else if (y > ccy) { if (y > 0 && show(cl[y - 1], t)) f |= F_BOT; }
            }
            if (!f) continue;
            if (nrec >= NREC) { st_recfull++; continue; }
            rec_x[nrec] = (u8)wx; rec_z[nrec] = (u8)wz; rec_t[nrec] = (u8)t; rec_f[nrec] = (u8)f; rec_ring[nrec] = (s16)i;
            rec_next[nrec] = -1;
            if (lay_tail[y] < 0) lay_head[y] = (s16)nrec; else rec_next[lay_tail[y]] = (s16)nrec;
            lay_tail[y] = (s16)nrec;
            nrec++;
        }
    }
}

static void cell_base(int x, int y, int z, s32 *bx, s32 *by, s32 *bz)
{
    *bx = b22x + x * sxv[0] + y * syv[0] + z * szv[0];
    *by = b22y + x * sxv[1] + y * syv[1] + z * szv[1];
    *bz = b22z + x * sxv[2] + y * syv[2] + z * szv[2];
}

/* one layer: side faces in ring order, then the top (tb 1) or bottom (tb 2) faces, merged past 4 blocks */
/* ---- creatures: boxes turned by their heading, shaded, outlined, a face on the front of the head */
#define MOBREC 255
/* (u across the face 0-16, v down it 0-16; d: the level's change) */
static const Rect r_pigface[] = {{2, 6, 4, 8, 8}, {3, 5, 5, 8, -30}, {10, 14, 4, 8, 8}, {11, 13, 5, 8, -30}, {0, 255, 0, 0, 0}};
static const Rect r_snout[] = {{3, 6, 5, 12, -24}, {10, 13, 5, 12, -24}, {0, 255, 0, 0, 0}};
static const Rect r_sheepface[] = {{2, 6, 5, 8, 26}, {3, 5, 6, 8, 4}, {10, 14, 5, 8, 26}, {11, 13, 6, 8, 4}, {6, 10, 11, 14, 14}, {0, 255, 0, 0, 0}};
static const Rect r_chickface[] = {{2, 6, 3, 7, -30}, {10, 14, 3, 7, -30}, {6, 10, 12, 16, -26}, {0, 255, 0, 0, 0}};
static const Rect r_zombface[] = {{2, 6, 6, 9, -26}, {10, 14, 6, 9, -26}, {4, 12, 12, 14, -20}, {6, 8, 12, 14, -30}, {0, 255, 0, 0, 0}};
static const Rect r_stalkface[] = {{3, 6, 1, 6, -34}, {10, 13, 1, 6, -34}, {4, 12, 9, 11, -34}, {4, 6, 11, 13, -34}, {10, 12, 11, 13, -34},
    {7, 9, 11, 12, -34}, {0, 255, 0, 0, 0}};
static const Rect r_capspots[] = {{2, 5, 3, 9, 20}, {9, 13, 6, 12, 20}, {0, 255, 0, 0, 0}};
static const Rect r_wing[] = {{0, 16, 6, 8, -10}, {0, 16, 11, 13, -10}, {0, 255, 0, 0, 0}};
static const Rect r_shirt[] = {{0, 16, 13, 16, -12}, {0, 255, 0, 0, 0}};
/* lev, texture near, the front's marks, all 4 sides marked */
static const struct { u8 lev, tex; const Rect *face; u8 sides; } mob_look[NMP] = {
    {42, T_FLAT, 0, 0}, {44, T_FLAT, r_pigface, 0}, {36, T_FLAT, 0, 0}, {36, T_FLAT, r_snout, 0}, {34, T_FLAT, 0, 0}, {36, T_FLAT, 0, 0},  /* pig */
    {46, T_COBBLE, 0, 0}, {18, T_FLAT, r_sheepface, 0}, {16, T_FLAT, 0, 0},                                                         /* sheep */
    {46, T_FLAT, 0, 0}, {46, T_FLAT, r_chickface, 0}, {26, T_FLAT, 0, 0}, {34, T_FLAT, 0, 0}, {40, T_FLAT, r_wing, 1}, {20, T_FLAT, 0, 0},  /* chicken */
    {14, T_FLAT, 0, 0}, {24, T_STONE, r_shirt, 1}, {32, T_FLAT, r_zombface, 0}, {30, T_FLAT, 0, 0},                                   /* zombie */
    {40, T_FLAT, r_stalkface, 0}, {22, T_FLAT, r_capspots, 1}, {44, T_FLAT, 0, 0}, {20, T_FLAT, 0, 0},                                /* boomcap */
};

static void draw_box(s32 cx, s32 cy, s32 cz, s32 hx, s32 hy, s32 hz, u16 yaw, int part, int bright)
{
    CV v[8], q[4];
    V3 w, c, cc;
    s32 sy = SIN(yaw), cyw = COS(yaw);
    int i, f;
    w.x = cx; w.y = cy; w.z = cz; cam_to(&w, &cc);
    if (cc.z < NEAR + 64) return;
    for (i = 0; i < 8; i++) {
        s32 lx = (i & 1) ? hx : -hx, ly = (i & 2) ? hy : -hy, lz = (i & 4) ? hz : -hz;
        w.x = cx + ((lx * cyw + lz * sy) >> 14); w.z = cz + ((lz * cyw - lx * sy) >> 14); w.y = cy + ly;
        cam_to(&w, &c);
        v[i].x = c.x; v[i].y = c.y; v[i].z = c.z; v[i].u = v[i].v = 0;
    }
    for (f = 0; f < 6; f++) {
        Style st;
        s32 mx = 0, my = 0, mz = 0, lev;
        for (i = 0; i < 4; i++) { q[i] = v[fc[f][i]]; q[i].u = fu[i]; q[i].v = fv[i]; mx += q[i].x; my += q[i].y; mz += q[i].z; }
        mx >>= 2; my >>= 2; mz >>= 2;
        if ((mx - cc.x) * (mx >> 2) + (my - cc.y) * (my >> 2) + (mz - cc.z) * (mz >> 2) >= 0) continue;
        lev = (mob_look[part].lev * dir_shade[f]) >> 8;
        if (bright) lev += 14;
        if (lev > 48) lev = 48;
        st.lev = lev; st.tex = T_FLAT; st.deco = 0; st.lines_only = 0; st.texv = -1; st.key = 0; st.door = 0;
        st.pat = &lev_pat[cc.z < TEXD ? mob_look[part].tex : T_FLAT][lev][0];
        st.edge = &lev_pat[T_FLAT][lev > 18 ? lev - 18 : 0][0];
        st.rects = f == 5 || (mob_look[part].sides && f < 2) || (mob_look[part].sides && f == 4) ? mob_look[part].face : 0;
        draw_cv(q, 4, &st, 0);
    }
}

/* the boxes of creature i whose middle lies in layer y, far ones first (a head never over the body behind it) */
static void draw_mob_layer(int i, int y)
{
    const Mob *m = &mobs[i];
    int b, ty = m->type, n = 0, j, k2;
    s32 sy = SIN(m->yaw), cyw = COS(m->yaw), bx = m->x >> 4, by = m->y >> 4, bz = m->z >> 4, sw = SIN(m->anim), sc = 256;
    s32 px[12], py[12], pz[12], pd[12];
    u8 pb[12];
    int bright = (m->hurt & 4) || (m->alive > 1 && (m->alive & 4));
    if (m->type == M_BOOM && m->state == 2) {         /* (the fuse: swelling, flashing faster and faster) */
        sc = 256 + (60 - m->timer) * 3 / 4;
        bright = (m->timer > 30 ? (m->timer & 8) : m->timer > 12 ? (m->timer & 4) : (m->timer & 2)) != 0;
    }
    if (m->alive > 1) by -= (m->alive - 1) * 6;   /* (dying: it sinks away) */
    for (b = mob_first[ty]; b < mob_first[ty + 1] && n < 12; b++) {
        const MobBox *k = &mob_boxes[b];
        s32 lx = k->x * 16, lz = k->z * 16, wy = by + k->y * 16, ly = 0;
        if (k->leg) { s32 o = (sw * 3) >> 10; lz += k->leg == 1 ? o : -o; ly = (k->leg == 1 ? (o > 0 ? o : 0) : (o < 0 ? -o : 0)) >> 1; }
        if ((wy >> 8) != y && !(b == mob_first[ty] && (wy >> 8) < y && y == (by >> 8))) continue;
        if (sc != 256) { lx = lx * sc >> 8; lz = lz * sc >> 8; wy = by + ((k->y * 16 * sc) >> 8); }
        px[n] = bx + ((lx * cyw + lz * sy) >> 14); py[n] = wy + ly; pz[n] = bz + ((lz * cyw - lx * sy) >> 14); pb[n] = (u8)b;
        { s32 ex = px[n] - cam_x, ey = py[n] - cam_y, ez = pz[n] - cam_z; pd[n] = ex * ex + ey * ey + ez * ez; }
        n++;
    }
    for (j = 0; j < n; j++) {                       /* (the farthest left, each time) */
        int f = -1;
        for (k2 = 0; k2 < n; k2++) if (pd[k2] >= 0 && (f < 0 || pd[k2] > pd[f])) f = k2;
        {
            const MobBox *k = &mob_boxes[pb[f]];
            draw_box(px[f], py[f], pz[f], (k->hx * 16 * sc) >> 8, (k->hy * 16 * sc) >> 8, (k->hz * 16 * sc) >> 8, m->yaw, k->part, bright);
        }
        pd[f] = -1;
    }
}

/* a run of n side faces in one plane (dir 0 1: along z, 4 5: along x) from the cell at base, one quad, the texture
   repeating a block */
u32 st_runs, st_runcells;
static void draw_run(int t, int dir, s32 bx, s32 by, s32 bz, int n, s32 cz)
{
    st_runs++; st_runcells += n;
    CV v[4];
    Style st;
    int i, alx = dir >= 4;
    for (i = 0; i < 4; i++) {
        int k = fc[dir][i];
        s32 px = bx, py = by, pz = bz, m;
        m = (k & 1) ? (alx ? n : 1) : 0;
        px += m * sxv[0]; py += m * sxv[1]; pz += m * sxv[2];
        if (k & 2) { px += syv[0]; py += syv[1]; pz += syv[2]; }
        m = (k & 4) ? (alx ? 1 : n) : 0;
        px += m * szv[0]; py += m * szv[1]; pz += m * szv[2];
        v[i].x = px >> 14; v[i].y = py >> 14; v[i].z = pz >> 14;
        v[i].u = fu[i] * n; v[i].v = fv[i];
    }
    face_style(&st, t, dir, cz, 0);
    st.rects = 0;
    if (st.deco == D_GRASS && st.texv < 0) st.deco = 0;
    draw_cv(v, 4, &st, 0);
}

static s16 sgrid[GW * GW];

static void draw_layer(int y, int tb)
{
    int r, ccx = cam_x >> 8, ccz = cam_z >> 8, any = 0;
    /* (the layer's cells by place, for runs of faces) */
    for (r = lay_head[y]; r >= 0; r = rec_next[r]) if (rec_t[r] != MOBREC) sgrid[(rec_z[r] - ccz + VIEW_R + 1) * GW + rec_x[r] - ccx + VIEW_R + 1] = (s16)(r + 1);
    for (r = lay_head[y]; r >= 0; r = rec_next[r]) {
        int f = rec_f[r], dirs[3], nd = 0, x = rec_x[r], z = rec_z[r], sel, q;
        s32 bx, by, bz;
        if (rec_t[r] == MOBREC) { draw_mob_layer(x, y); continue; }
        sel = sel_on && x == sel_x && y == sel_y && z == sel_z;
        /* runs: a side face whose neighbours along its plane show the same face of the same block, all one band */
        for (q = 0; q < 4 && !sel; q++) {
            static const u8 bit[4] = {F_X0, F_X1, F_Z0, F_Z1}, dirq[4] = {0, 1, 4, 5};
            int b = bit[q], ax = q >= 2, gx = x - ccx + VIEW_R + 1, gz = z - ccz + VIEW_R + 1, lo = 0, hi = 0, band, k;
            s32 d2;
            if (!(f & b)) continue;
            d2 = (x - ccx) * (x - ccx) + (z - ccz) * (z - ccz);
            band = d2 < 30;
            for (k = 1; k < 8; k++) {
                int nx = gx + (ax ? -k : 0), nz = gz + (ax ? 0 : -k), rr, ex, ez;
                if (nx < 0 || nz < 0) break;
                rr = sgrid[nz * GW + nx] - 1;
                if (rr < 0 || rec_t[rr] != rec_t[r] || !(rec_f[rr] & b)) break;
                ex = rec_x[rr] - ccx; ez = rec_z[rr] - ccz;
                if ((ex * ex + ez * ez < 30) != band || (sel_on && rec_x[rr] == sel_x && y == sel_y && rec_z[rr] == sel_z)) break;
                lo = k;
            }
            for (k = 1; k < 8 - lo; k++) {
                int nx = gx + (ax ? k : 0), nz = gz + (ax ? 0 : k), rr, ex, ez;
                if (nx >= GW || nz >= GW) break;
                rr = sgrid[nz * GW + nx] - 1;
                if (rr < 0 || rec_t[rr] != rec_t[r] || !(rec_f[rr] & b)) break;
                ex = rec_x[rr] - ccx; ez = rec_z[rr] - ccz;
                if ((ex * ex + ez * ez < 30) != band || (sel_on && rec_x[rr] == sel_x && y == sel_y && rec_z[rr] == sel_z)) break;
                hi = k;
            }
            if (lo + hi == 0) continue;
            for (k = -lo; k <= hi; k++) {
                int rr = sgrid[(gz + (ax ? 0 : k)) * GW + gx + (ax ? k : 0)] - 1;
                if (rr != r) rec_f[rr] &= ~b;
            }
            f &= ~b;
            {
                s32 cz;
                cell_base(ax ? x - lo : x, y, ax ? z : z - lo, &bx, &by, &bz);
                cz = (bz + ((sxv[2] + syv[2] + szv[2]) >> 1)) >> 14;
                draw_run(rec_t[r], dirq[q], bx, by, bz, lo + hi + 1, cz);
            }
        }
        if (f & F_X0) dirs[nd++] = 0;
        if (f & F_X1) dirs[nd++] = 1;
        if (f & F_Z0) dirs[nd++] = 4;
        if (f & F_Z1) dirs[nd++] = 5;
        if (f & (F_TOP | F_BOT)) any = 1;
        if (f & F_BB) { draw_plant(x, y, z, rec_t[r]); continue; }
        if (!nd) continue;
        cell_base(x, y, z, &bx, &by, &bz);
        draw_faces(rec_t[r], dirs, nd, bx, by, bz, sel);
    }
    for (r = lay_head[y]; r >= 0; r = rec_next[r]) if (rec_t[r] != MOBREC) sgrid[(rec_z[r] - ccz + VIEW_R + 1) * GW + rec_x[r] - ccx + VIEW_R + 1] = 0;
    if (!any) return;
    for (r = lay_head[y]; r >= 0; r = rec_next[r]) {
        int f = rec_f[r], x = rec_x[r], z = rec_z[r], dx = x - ccx, dz = z - ccz, d2 = dx * dx + dz * dz, dir;
        if (!(f & (F_TOP | F_BOT))) continue;
        dir = f & F_TOP ? 3 : 2;
        if (d2 < TOP_SINGLE || tb != 1 || rec_t[r] == WATER || (sel_on && x == sel_x && y == sel_y && z == sel_z)) {
            s32 bx, by, bz;
            int sel = sel_on && x == sel_x && y == sel_y && z == sel_z;
            cell_base(x, y, z, &bx, &by, &bz);
            draw_faces(rec_t[r], &dir, 1, bx, by, bz, sel);
        } else {
            int band = d2 < 36 ? 1 : d2 < 64 ? 2 : 3;
            tgrid[(dz + VIEW_R + 1) * GW + dx + VIEW_R + 1] = (u8)(rec_t[r] | band << 5);
        }
    }
    if (tb != 1) return;
    {
        /* greedy rectangles over the grid */
        int gx, gz;
        for (gz = 0; gz < GW; gz++)
            for (gx = 0; gx < GW; gx++) {
                u8 key = tgrid[gz * GW + gx];
                int w, h, ok, i;
                if (!key) continue;
                for (w = 1; gx + w < GW && tgrid[gz * GW + gx + w] == key; w++) ;
                for (h = 1, ok = 1; gz + h < GW && ok; ) {
                    for (i = 0; i < w; i++) if (tgrid[(gz + h) * GW + gx + i] != key) { ok = 0; break; }
                    if (ok) h++;
                }
                for (i = 0; i < h; i++) { int j; for (j = 0; j < w; j++) tgrid[(gz + i) * GW + gx + j] = 0; }
                {
                    CV v[4];
                    V3 wv, c;
                    Style st;
                    int q, band = key >> 5;
                    s32 x0 = (ccx + gx - VIEW_R - 1) << 8, z0 = (ccz + gz - VIEW_R - 1) << 8;
                    for (q = 0; q < 4; q++) {
                        wv.x = x0 + ((q == 1 || q == 2) ? w << 8 : 0); wv.y = (y + 1) << 8; wv.z = z0 + (q < 2 ? h << 8 : 0);
                        cam_to(&wv, &c);
                        v[q].x = c.x; v[q].y = c.y; v[q].z = c.z; v[q].u = v[q].v = 0;
                    }
                    face_style(&st, key & 31, 3, (3 + 2 * band) * 256, 0);
                    st.edge = 0; st.rects = 0; st.texv = -1;
                    draw_cv(v, 4, &st, 0);
                }
            }
    }
}

/* the creatures in the near ring: a record in each layer their boxes reach, at their column's place in the ring order
   (behind the cells nearer than it, in front of the farther ones) */
#define MOBREC 255
static void file_mobs(void)
{
    int i, ccx = cam_x >> 8, ccz = cam_z >> 8;
    for (i = 0; i < NMOB; i++) {
        const Mob *m = &mobs[i];
        int mx, mz, dx, dz, ri, y0, y1, y;
        if (!m->alive) continue;
        mx = m->x >> 12; mz = m->z >> 12; dx = mx - ccx; dz = mz - ccz;
        if (dx * dx + dz * dz > VIEW_R * VIEW_R) continue;
        ri = ring_of[(dz + VIEW_R) * (2 * VIEW_R + 1) + dx + VIEW_R];
        y0 = m->y >> 12; y1 = (m->y + (m->type == M_ZOMBIE ? Q12 * 2 : m->type == M_BOOM ? Q12 * 3 / 2 : Q12)) >> 12;
        if (y0 < 0) y0 = 0;
        if (y1 >= WH) y1 = WH - 1;
        for (y = y0; y <= y1 && nrec < NREC; y++) {
            int r = lay_head[y], prev = -1;
            while (r >= 0 && rec_ring[r] <= ri) { prev = r; r = rec_next[r]; }
            rec_t[nrec] = MOBREC; rec_x[nrec] = (u8)i; rec_z[nrec] = 0; rec_f[nrec] = 0; rec_ring[nrec] = (s16)ri;
            rec_next[nrec] = (s16)r;
            if (prev < 0) lay_head[y] = (s16)nrec; else rec_next[prev] = (s16)nrec;
            if (r < 0) lay_tail[y] = (s16)nrec;
            nrec++;
        }
    }
}

static void prepare_world(void)
{
    int above = cam_y > SEA * 256 - 16;
    sxv[0] = rx << 8; sxv[1] = ux << 8; sxv[2] = fx << 8;
    syv[0] = 0; syv[1] = uy << 8; syv[2] = fy << 8;
    szv[0] = rz << 8; szv[1] = uz << 8; szv[2] = fz << 8;
    b22x = -(rx * cam_x + rz * cam_z);
    b22y = -(ux * cam_x + uy * cam_y + uz * cam_z);
    b22z = -(fx * cam_x + fy * cam_y + fz * cam_z);
#ifdef CINEMA
    collect(VIEW_R * VIEW_R);
#else
    collect(above ? 36 : VIEW_R * VIEW_R);
#endif
    file_mobs();
}

static void far_field(void);
static void far_trees(void);
static s16 far_yb[192 + 2];
static void draw_world(void)
{
    int ccy = cam_y >> 8, y, above = cam_y > SEA * 256 - 16;
    {
        u32 t0 = PT;
        u32 tf;
        if (!above) { tf = PT; far_field(); far_trees(); prof[7] += PT - tf; }
        for (y = 0; y < ccy && y < WH; y++) {
            if (above && y == SEA - 1) { water_plane(); tf = PT; far_field(); far_trees(); prof[7] += PT - tf; }
            draw_layer(y, 1);
        }
        if (above && ccy <= SEA - 1) { water_plane(); tf = PT; far_field(); far_trees(); prof[7] += PT - tf; }
        for (y = WH - 1; y > ccy; y--) draw_layer(y, 2);
        if (ccy >= 0 && ccy < WH) draw_layer(ccy, 0);
        prof[5] += PT - t0;
    }
}

/* ---- the far field: the world past the near ring as a height field (hmap, surf_t: the surface's height and block),
   marched front to back per pair of screen columns (192 a picture) with a horizon buffer per pair, so a pixel is written
   at most once. Per depth step the rows of all 29 heights come from one table (29 divides a step, not one a sample);
   a visible stretch goes to both eyes at that depth's disparity. Rises get a darker top row: the hills' contours. */
extern int surf_max;                               /* (the world's highest surface, src/world.c) */
#define maxh (surf_max < WH ? surf_max + 1 : WH)
#define FAR0 (2 * 256 + 128)
#ifdef CINEMA
#define FAR1 (46 * 256)
#else
#define FAR1 (30 * 256)
#endif
static s16 far_kq[192];
static u8 far_hp[192 + 2];

/* rows [a, b) of nc columns from col (pattern by column phase k), the top row in E if rim */
static int far_tex;                                /* (far_put's pattern set: T_FLAT, or T_LEAVES for far crowns) */
static void far_put(u32 *col, int nc, int k, int a, int b, int lv, int rim)
{
    int wa = a >> 4, wb = (b - 1) >> 4, w, i;
    u32 ma = 0xFFFFFFFFu << ((a & 15) << 1), mb = 0xFFFFFFFFu >> ((15 - ((b - 1) & 15)) << 1);
    u32 er = rim ? (3u << ((a & 15) << 1)) : 0;
    const u32 *pat = &lev_pat[far_tex][lv][0], *ep = &lev_pat[T_FLAT][lv > 12 ? lv - 12 : 0][0];
    for (i = 0; i < nc; i++, col += 16) {
        u32 P = pat[(k + i) & 7], E = ep[(k + i) & 7];
        if (wa == wb) { u32 m = ma & mb, v = (P & ~er) | (E & er); PUT(col, wa, v, m); continue; }
        { u32 v = (P & ~er) | (E & er); PUT(col, wa, v, ma); }
        for (w = wa + 1; w < wb; w++) col[w] = P;
        PUT(col, wb, P, mb);
    }
}

typedef struct { s32 x16, z16, dx16, dz16, pw; s16 *yb; u8 *hp; s16 *ytab; s32 ytop; const u8 *hm, *st; s32 ring, ccx, ccz, r2; u32 *ev; } FarStep;
static u32 far_ev[2 * 192];
#define FAR_NS 12
static u8 far_hist[FAR_NS][192];                   /* (the horizon per pair after each step: what hides a far tree) */
static s32 far_d[FAR_NS];
static int far_ns;

#ifdef HOST
static int far_step(FarStep *f)
{
    int p, n = 0;
    s32 x16 = f->x16, z16 = f->z16;
    for (p = 0; p < 192; p += f->pw, x16 += f->dx16, z16 += f->dz16) {
        int ix = x16 >> 16, iz = z16 >> 16, i, h, y;
        if (f->yb[p] <= f->ytop || (unsigned)ix >= WX || (unsigned)iz >= WZ) continue;
        if (f->ring && (ix - f->ccx) * (ix - f->ccx) + (iz - f->ccz) * (iz - f->ccz) <= f->r2) {
            /* (in the near ring: drawn by the polygons; its surface still hides what lies behind it) */
            y = f->ytab[f->hm[(ix << 6) | iz]];
            if (y < f->yb[p]) { f->yb[p] = (s16)y; if (f->pw == 2) f->yb[p + 1] = (s16)y; }
            continue;
        }
        i = (ix << 6) | iz; h = f->hm[i]; y = f->ytab[h];
        if (y < f->yb[p]) {
            f->ev[2 * n] = p | f->st[i] << 8 | (h > f->hp[p]) << 13 | (((i >> 6) ^ i) & 1) << 14;
            f->ev[2 * n + 1] = (y & 0xFFFF) | (u32)f->yb[p] << 16;
            n++;
            f->yb[p] = (s16)y; if (f->pw == 2) f->yb[p + 1] = (s16)y;
        }
        f->hp[p] = (u8)h; if (f->pw == 2) f->hp[p + 1] = (u8)h;
    }
    return n;
}
#else
int far_step(FarStep *f);                          /* src/seg.s */
#endif

#ifdef HOST
static void far_span(u32 *colL, u32 *colR, int nc, int k, int a, int b, const u32 *pat, u32 E, int rim)
{
    int i, wa = a >> 4, wb = (b - 1) >> 4, w;
    u32 ma = 0xFFFFFFFFu << ((a & 15) << 1), mb = 0xFFFFFFFFu >> ((15 - ((b - 1) & 15)) << 1), er = rim ? (3u << ((a & 15) << 1)) : 0;
    for (i = 0; i < nc; i++, colL += 16, colR += 16) {
        u32 P = pat[(k + i) & 7], v = (P & ~er) | (E & er);
        if (wa == wb) { PUT(colL, wa, v, ma & mb); PUT(colR, wa, v, ma & mb); continue; }
        PUT(colL, wa, v, ma); PUT(colR, wa, v, ma);
        for (w = wa + 1; w < wb; w++) { colL[w] = P; colR[w] = P; }
        PUT(colL, wb, P, mb); PUT(colR, wb, P, mb);
    }
}
#else
void far_span(u32 *colL, u32 *colR, int nc, int k, int a, int b, const u32 *pat, u32 E, int rim);   /* src/seg.s */
#endif
/* both eyes' columns of a far stretch: left at +s, right at -s */
static u32 far_dummy[4 * 16];                      /* (where right-eye columns off the screen go) */
#define RCOL(c, nc) ((c) >= 0 && (c) + (nc) <= VW ? fb_r + (c) * 16 : far_dummy)
#define FAR_SPAN(c, s, nc, a, b, lv, tex, rim) far_span(fb_l + ((c) + (s)) * 16, RCOL((c) - (s), nc), nc, (c), a, b, \
    &lev_pat[tex][lv][0], lev_pat[T_FLAT][(lv) > 12 ? (lv) - 12 : 0][(c) & 7], rim)

static void far_field(void)
{
    s32 d = FAR0, sp = fy, cp = uy, fhx = -rz, fhz = rx;
    int p, h, pitched = sp > 900 || sp < -900;
    s16 ytab[WH + 1];
    FarStep fs;
    fs.yb = far_yb; fs.hp = far_hp; fs.ytab = ytab; fs.hm = hmap; fs.st = surf_t;
    fs.ccx = cam_x >> 8; fs.ccz = cam_z >> 8; fs.r2 = VIEW_R * VIEW_R; fs.ev = far_ev;
    for (p = 0; p < 192; p++) { far_yb[p] = VH; far_hp[p] = (u8)(cam_y >> 8); }
    far_ns = 0;
    while (d < FAR1) {
        s32 dd = d >> 2, zc0 = (d * cp) >> 14, f, sh;
        int s, pw = d < 9 * 256 ? 1 : 2, nc = 2 * pw, n, e;
        if (dd < 256) dd = 256;
        if (zc0 < 64) { d += dd; continue; }
        for (h = 0; h <= WH; h++) {
            s32 dy = (h << 8) - cam_y, zc = (d * cp + dy * sp) >> 14, yc = (dy * cp - d * sp) >> 14;
            ytab[h] = (s16)(zc > 32 ? (CY4 - ((yc * RECIP(zc)) >> 12) + 8) >> 4 : (yc > 0 ? -1000 : 1000));
        }
        s = (disparity(zc0) + 8) >> 4;
        f = FOGI(d);
        fs.ytop = ytab[maxh];                      /* (no surface here reaches above this row) */
        fs.pw = pw; fs.ring = d < (VIEW_R + 2) * 256;
        /* the line of samples at this depth, sample p at the centre of its 2 or 4 columns; a pitched camera meets the
           ground's typical height (the camera's level - 2) a little aside */
        sh = pitched ? ((((-2 * 256) * sp) >> 14) * (zc0 > 0 ? 1 : 1)) : 0;
        fs.x16 = (cam_x << 8) + ((d * fhx) >> 6) + (((((zc0 + sh) * rx) / FOC) * (pw - 192)) >> 6);
        fs.z16 = (cam_z << 8) + ((d * fhz) >> 6) + (((((zc0 + sh) * rz) / FOC) * (pw - 192)) >> 6);
        fs.dx16 = ((2 * pw * (zc0 + sh) * rx) / FOC) >> 6; fs.dz16 = ((2 * pw * (zc0 + sh) * rz) / FOC) >> 6;
        n = far_step(&fs);
        for (e = 0; e < n; e++) {
            u32 ea = far_ev[2 * e], eb = far_ev[2 * e + 1];
            int pp = ea & 255, t = (ea >> 8) & 31, rise = (ea >> 13) & 1, y = (s16)(eb & 0xFFFF), b = (s16)(eb >> 16), a = y < 0 ? 0 : y, lv;
            lv = (rise ? lev_base[t][4] : lev_base[t][3]) + ((ea >> 14) & 1 ? 2 : -2);
            if (t == WATER) lv = 14 + ((((ea >> 14) & 1) ^ ((d >> 8) & 1)) ? 6 : 0) + ((SEA_HZ - 14) * f >> 8);   /* (the sea: dark with a glint) */
            else lv += ((FOG_L - lv) * f) >> 8;
            if (lv > 48) lv = 48;
            if (lv < 0) lv = 0;
            if (b > VH) b = VH;
            if (a < b) {
                int built = t == COBBLE || t == BRICK || t == PLANKS || t == LOG || t == GLASS || t == LAMP || t == WOOL || t == GOLD;
                FAR_SPAN(2 * pp, s, nc, a, b, lv, built ? (t == PLANKS || t == LOG ? T_WOOD : T_COBBLE) : T_FLAT, rise && y >= 0);
                if (built && b - a > 3 && f < 200) {
                    /* (a building far off: a dark seam at each block's top edge, so it still reads as blocks, not a slab) */
                    int hh, r, ls = lv > 14 ? lv - 14 : 0;
                    for (hh = WH; hh > 0; hh--) {
                        r = ytab[hh];
                        if (r >= b) break;
                        if (r > a && r - ytab[hh + (hh < WH)] > 2) FAR_SPAN(2 * pp, s, nc, r, r + 1, ls, T_FLAT, 0);
                    }
                }
            }
        }
        if (far_ns < FAR_NS) {
            u8 *hh = far_hist[far_ns];
            for (p = 0; p < 192; p++) { int y = far_yb[p]; hh[p] = (u8)(y < 0 ? 0 : y > VH ? VH : y); }
            far_d[far_ns++] = zc0;
        }
        d += dd;
    }
}

/* far trees: a round crown on a trunk for each tree past the near ring, far to near, each column clipped by the far
   field's horizon of the steps in front of it (far_hist) */
extern u8 tree_x[], tree_z[], tree_y[];
extern int ntree;
/* a far creature: its body and head as two upright boxes on screen (the head on the side it faces), clipped like trees */
static const u8 far_mob[NMOBTYPE][6] = {          /* body w, h, centre y; head size, head centre y; head part (1/16 blocks) */
    {15, 8, 9, 8, 11, MP_HEAD}, {15, 10, 11, 7, 14, MP_HEAD2}, {7, 6, 7, 5, 13, MP_CHEAD}, {8, 24, 12, 8, 28, MP_ZHEAD}, {6, 12, 9, 14, 18, MP_CAP}};
static void far_rect(int x0, int x1, int y0, int y1, int s, int step, int lv, int tex)
{
    int col;
    if (lv < 0) lv = 0;
    if (lv > 48) lv = 48;
    for (col = x0 & ~1; col < x1; col += 2) {
        int p = col >> 1, top, a = y0 < 0 ? 0 : y0, b;
        if (p < 0 || p >= 192) continue;
        top = step > 0 ? far_hist[step - 1][p] : VH;
        b = y1 > top ? top : y1;
        if (b > VH) b = VH;
        if (a < b) FAR_SPAN(col, s, 2, a, b, lv, tex, a == y0);
    }
}

static void far_mob_draw(const Mob *m, const V3 *c)
{
    const u8 *fm = far_mob[m->type];
    s32 inv = (FOC << 16) / c->z, sx = (CX4 + ((c->x * inv) >> 12)) >> 4, base = (CY4 - ((c->y * inv) >> 12)) >> 4;
    s32 px16 = (inv * 16) >> 16, s = (disparity(c->z) + 8) >> 4, f = FOGI(c->z);
    int step, bw = (fm[0] * px16) >> 5, bh = (fm[1] * px16) >> 5, by = base - ((fm[2] * px16) >> 4), hs = (fm[3] * px16) >> 5;
    int hy = base - ((fm[4] * px16) >> 4), side = ((rx * SIN(m->yaw) + rz * COS(m->yaw)) >> 14) > 0 ? 1 : -1, lv;
    if (m->alive != 1) return;
    for (step = 0; step < far_ns - 1 && far_d[step + 1] < c->z; step++) ;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;
    lv = mob_look[mob_boxes[mob_first[m->type]].part].lev; lv += ((FOG_L - lv) * f) >> 8;
    far_rect(sx - bw, sx + bw, by - bh, by + bh, s, step, lv, T_FLAT);
    lv = mob_look[fm[5]].lev; lv += ((FOG_L - lv) * f) >> 8;
    if (m->type >= M_ZOMBIE) far_rect(sx - hs, sx + hs, hy - (m->type == M_BOOM ? hs / 3 : hs), hy + (m->type == M_BOOM ? hs / 3 : hs), s, step, lv, T_FLAT);
    else far_rect(sx + side * bw - hs, sx + side * bw + hs, hy - hs, hy + hs, s, step, lv, T_FLAT);
}

static void far_trees(void)
{
    static s16 order[150];
    static s32 tz[128 + NMOB];
    int i, j, n = 0, ccx = cam_x >> 8, ccz = cam_z >> 8;
    for (i = 0; i < ntree; i++) {
        V3 w, c;
        int dx = tree_x[i] - ccx, dz = tree_z[i] - ccz;
        if (dx * dx + dz * dz <= VIEW_R * VIEW_R) continue;
        if (CELL(tree_x[i], tree_y[i], tree_z[i]) != LOG) continue;   /* (cut down) */
        w.x = (tree_x[i] << 8) + 128; w.z = (tree_z[i] << 8) + 128; w.y = (tree_y[i] << 8) + 256;
        cam_to(&w, &c);
        if (c.z < 3 * 256 || c.z > FAR1 + 512 || c.x > c.z || c.x < -c.z) continue;
        tz[i] = c.z;
        for (j = n; j > 0 && tz[order[j - 1]] < c.z; j--) order[j] = order[j - 1];   /* (far first) */
        order[j] = (s16)i; n++;
    }
    /* the creatures past the near ring join the same far-to-near order (index 128 + i) */
    for (i = 0; i < NMOB && n < 140; i++) {
        V3 w, c;
        const Mob *m = &mobs[i];
        int dx = (m->x >> 12) - ccx, dz = (m->z >> 12) - ccz;
        if (m->alive != 1 || dx * dx + dz * dz <= VIEW_R * VIEW_R) continue;
        w.x = m->x >> 4; w.y = m->y >> 4; w.z = m->z >> 4;
        cam_to(&w, &c);
        if (c.z < 3 * 256 || c.z > FAR1 || c.x > c.z || c.x < -c.z) continue;
        tz[128 + i] = c.z;
        for (j = n; j > 0 && tz[order[j - 1]] < c.z; j--) order[j] = order[j - 1];
        order[j] = (s16)(128 + i); n++;
    }
    for (j = 0; j < n; j++) {
        V3 w, c, g;
        s32 inv, sx, sy, gy, r, s, f, k;
        int lv, col, step;
        i = order[j];
        if (i >= 128) {
            const Mob *m = &mobs[i - 128];
            w.x = m->x >> 4; w.y = m->y >> 4; w.z = m->z >> 4;
            cam_to(&w, &c);
            far_mob_draw(m, &c);
            continue;
        }
        w.x = (tree_x[i] << 8) + 128; w.z = (tree_z[i] << 8) + 128; w.y = (tree_y[i] << 8) + 256;
        cam_to(&w, &c);
        w.y = (tree_y[i] - 4) << 8; cam_to(&w, &g);
        inv = (FOC << 16) / c.z;
        sx = (CX4 + ((c.x * inv) >> 12)) >> 4; sy = (CY4 - ((c.y * inv) >> 12)) >> 4;
        gy = g.z > 32 ? (CY4 - ((g.y * ((FOC << 16) / g.z)) >> 12)) >> 4 : sy;
        r = (470 * inv) >> 16;                     /* (a crown of 1.8 blocks, in pixels) */
        if (r < 2) r = 2;
        s = (disparity(c.z) + 8) >> 4;
        f = FOGI(c.z);
        lv = lev_base[LEAVES][3]; lv += ((FOG_L - lv) * f) >> 8;
        for (step = 0; step < far_ns - 1 && far_d[step + 1] < c.z; step++) ;
        {
            int tw = r / 5 + 2, e;
            for (col = (sx - r) & ~1; col <= sx + r; col += 2) {
                int p = col >> 1, a, b, dxp = col + 1 - sx, adx = dxp < 0 ? -dxp : dxp, top, kk, mid, trunk = adx < tw;
                if (p < 0 || p >= 192) continue;
                top = step > 0 ? far_hist[step - 1][p] : VH;   /* (what is nearer than the tree hides it below this row) */
                /* a rounded square: full height in the middle, the corners cut */
                kk = adx <= r - r / 4 ? r : r - r / 4;      /* (a block of leaves with its corners cut, as the real crowns) */
                kk = (kk * 7) >> 3;
                a = sy - kk; b = sy + kk + 1; mid = sy - (kk >> 2);
                {
                    int lo = a < 0 ? 0 : a, hi = b > top ? top : b, l2;
                    if (hi > VH) hi = VH;
                    if (lo < mid && lo < hi) {
                        l2 = lv + 6 > 48 ? 48 : lv + 6;
                        FAR_SPAN(col, s, 2, lo, mid < hi ? mid : hi, l2, T_LEAVES, lo == a && adx < r - 1);
                    }
                    if (mid < hi) {
                        int m0 = mid > lo ? mid : lo;
                        l2 = lv - 6 < 0 ? 0 : lv - 6;
                        if (adx >= r - 1) l2 = lv - 14 < 0 ? 0 : lv - 14;
                        if (m0 < hi) FAR_SPAN(col, s, 2, m0, hi, l2, T_LEAVES, 0);
                    }
                    if (trunk) {
                        int t0 = b < 0 ? 0 : b, t1 = gy > top ? top : gy;
                        if (t1 > VH) t1 = VH;
                        if (t0 < t1) FAR_SPAN(col, s, 2, t0, t1, lv - 12 < 0 ? 0 : lv - 12, T_FLAT, 0);
                    }
                }
            }
        }
    }
}

/* sparkles on the sea beyond the island: points on the water plane, twinkling */
static void sea_glints(void)
{
    int i;
    for (i = 0; i < 40; i++) {
        u32 h = hsh(i * 7 + 3);
        V3 w, c;
        s32 inv, sx, sy, d, l;
        int ph = (frame_no + (int)(h >> 20)) & 63;
        if (ph > 20) continue;
        w.x = (cam_x & ~4095) + (s32)(h & 0x3FFF) - 8192 + 2048;
        w.z = (cam_z & ~4095) + (s32)((h >> 14) & 0x3FFF) - 8192 + 2048;
        if ((unsigned)(w.x >> 8) < WX && (unsigned)(w.z >> 8) < WZ) continue;
        w.y = SEA * 256 - 32;
        cam_to(&w, &c);
        if (c.z < 512) continue;
        inv = (FOC << 16) / c.z;
        sx = (CX4 + ((c.x * inv) >> 12)) >> 4; sy = (CY4 - ((c.y * inv) >> 12)) >> 4;
        d = (dcomp(((EYE * inv) >> 12) - s_conv) + 8) >> 4;
        if (sy < 1 || sy >= VH - 1 || sx < 4 || sx >= VW - 4) continue;
        l = ph < 10 ? 3 : 2;
        {
            int e, len = 1 + (c.z < 3000 ? 2 : 1);
            for (e = 0; e < 2; e++) {
                u32 *fb = e ? fb_r : fb_l;
                int x = sx + (e ? -d : d), k;
                for (k = 0; k < len; k++) {
                    int xx = x + k;
                    if ((unsigned)xx < VW) {
                        u32 *w32 = fb + xx * 16 + (sy >> 4);
                        *w32 = (*w32 & ~(3u << ((sy & 15) * 2))) | ((u32)l << ((sy & 15) * 2));
                    }
                }
            }
        }
    }
}

/* ---- small cubes in camera space (particles, the held block) */
void draw_cube_cam(const V3 *c, s32 half, int mat, int lit)
{
    if (SPRITE(mat)) {
        CV v[4];
        Style st;
        int q;
        for (q = 0; q < 4; q++) {
            v[q].x = c->x + ((q == 1 || q == 2) ? half : -half); v[q].y = c->y + (q < 2 ? half : -half); v[q].z = c->z;
            v[q].u = q == 1 || q == 2 ? 256 : 0; v[q].v = 0;
        }
        st.pat = &lev_pat[T_FLAT][30][0]; st.door = 0; st.edge = 0; st.rects = 0; st.lev = 30; st.tex = T_FLAT;
        st.deco = 0; st.lines_only = 0; st.texv = tex_var[mat]; st.key = 1;
        draw_cv(v, 4, &st, 0);
        return;
    }
    static const s8 cs[8][3] = {{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}};
    CV v[8], q[4];
    int f, i;
    s32 cy = SIN(lit), cyc = COS(lit);
    for (i = 0; i < 8; i++) {
        s32 x = cs[i][0] * half, z = cs[i][2] * half;
        s32 zz = (x * cy + z * cyc) >> 14, yy = cs[i][1] * half;
        v[i].x = c->x + ((x * cyc - z * cy) >> 14);
        if (cube_tilt) {
            /* (tipped toward the viewer: the icons show their tops) */
            s32 st = SIN(cube_tilt), ct = COS(cube_tilt), y2 = (yy * ct - zz * st) >> 14;
            zz = (yy * st + zz * ct) >> 14; yy = y2;
        }
        v[i].z = c->z + zz;
        v[i].y = c->y + yy;
        v[i].u = 0; v[i].v = 0;
    }
    for (f = 0; f < 6; f++) {
        Style st;
        s32 nx, ny, nz, mx = 0, my = 0, mz = 0;
        for (i = 0; i < 4; i++) { q[i] = v[fc[f][i]]; q[i].u = fu[i]; q[i].v = fv[i]; mx += q[i].x; my += q[i].y; mz += q[i].z; }
        /* face normal ~ face centre - cube centre; visible if it points toward the eye */
        nx = mx / 4 - c->x; ny = my / 4 - c->y; nz = mz / 4 - c->z;
        if (nx * (mx / 4) + ny * (my / 4) + nz * (mz / 4) >= 0) continue;
        face_style(&st, mat, f, 0, 0);
        st.rects = half > 40 ? deco_rects[st.deco] : 0;
        if (half <= 40 && !cube_detail) st.edge = 0;
        draw_cv(q, 4, &st, 0);
    }
}

void render_init(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        int k;
        u32 d2 = 0, d4 = 0;
        for (k = 0; k < 4; k++) {
            u32 px = (i >> (2 * k)) & 3;
            d2 |= (px | px << 2) << (4 * k);
            d4 |= (px * 0x55u) << (8 * k);
        }
        spread2[i] = d2; spread4[i] = d4;
    }
    for (i = 0; i <= 16; i++) {
        mfrom[i] = i >= 16 ? 0 : ~((1u << (i * 2)) - 1);
        mto[i] = i >= 16 ? 0xFFFFFFFFu : ((1u << (i * 2)) - 1);
    }
    make_tex();
    world_tables();
    {
        int p;
        for (p = 0; p < 192; p++) far_kq[p] = (s16)(((2 * p + 1 - 192) << 8) / FOC);
    }
    {
        int h, o = 0;
        for (h = 0; h <= TEX_HMAX + 1; h++) { hoff[h] = (u16)o; if (h) o += ((h + 15) >> 4) + 1; }
    }
    make_rings();
}

/* what needs no framebuffer (the camera, the sky's column, the cells in their layers): done while the last picture
   waits for the display */
void render_prepare(void)
{
    u32 t = PT;
    setup_cam();
    make_sky();
    prepare_world();
    prof[0] += PT - t;
}

#ifdef VRAMCHK
/* (tools/check_vram.py: the tables in the character memory beside the framebuffers must come out of a picture untouched) */
u32 vram_bad;
static u32 vsum(void)
{
    const u32 *t[4];
    u32 s = 0;
    int i, j;
    t[0] = (const u32 *)hmap; t[1] = (const u32 *)surf_t; t[2] = (const u32 *)col_lo; t[3] = (const u32 *)col_hi;
    for (j = 0; j < 4; j++) for (i = 0; i < WX * WZ / 4; i++) s = s * 31 + t[j][i];
    return s;
}
#endif
void render_draw(void)
{
    u32 t = PT, t2;
#ifdef VRAMCHK
    u32 vs = vsum();
#endif
    sky_fill(fb_l);
    sky_fill(fb_r);
    t2 = PT; prof[1] += t2 - t; t = t2;
    if (!water_view) { sky_things(); sea_glints(); }
    t2 = PT; prof[2] += t2 - t; t = t2;
    draw_world();
    t2 = PT; prof[3] += t2 - t; t = t2;
#ifdef VRAMCHK
    if (vsum() != vs) vram_bad++;
#endif
}

void render_frame(void)
{
    render_prepare();
    render_draw();
}
