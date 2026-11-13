/* Virtualcraft : the title. The island turns slowly behind the name; Start plays. */
#include "game.h"
extern volatile u32 ticks, pad_on, shown;
u16 read_pad(void);
void wait_tick(void);
void present(void);
void bright(int k);
extern volatile u32 seed_in;
extern s32 meadow_x, meadow_z;

/* the name as a block logo: the 5 x 7 font's dots as chunky stone (DX x DY pixels a dot), speckled like cobble, lit from the
   top left (bright top rows, dark bottom rows), pushed out down-right into a dark solid side, a black rim round it all; a step
   in front of the screen (d: the eyes' shift) */
#define DX 4
#define DY 6
#define LOGO_W (12 * 6 * DX)
#define LMASK ((u8 *)R_FB2 + 12 * 1024)                /* the letters' pixels, a byte each, x major: 288 x 42 */
static int logo_dot(const char *s, int n, int x, int y)
{
    (void)s; (void)n;
    if ((unsigned)x >= LOGO_W || (unsigned)y >= 7 * DY) return 0;
    return LMASK[x * (7 * DY) + y];
}

static void mask_make(const char *s, int n)
{
    extern const u8 *font_glyph(int ch);
    int k, c, r, i, j;
    for (i = 0; i < LOGO_W * 7 * DY; i++) LMASK[i] = 0;
    for (k = 0; k < n && k < 12; k++) {
        const u8 *g = font_glyph(s[k]);
        for (c = 0; c < 5; c++)
            for (r = 0; r < 7; r++)
                if ((g[c] >> r) & 1)
                    for (i = 0; i < DX; i++)
                        for (j = 0; j < DY; j++) LMASK[(k * 6 * DX + c * DX + i) * (7 * DY) + r * DY + j] = 1;
    }
}

/* made once into the spare right-eye buffer (unused on the title): per column 4 words of shades, 4 of the mask, rows 16-79 */
#define LOGO ((u32 *)R_FB2)
static int logo_w;
static void logo_make(const char *s)
{
    int n = 0, w, x, y;
    while (s[n]) n++;
    w = n * 6 * DX - DX;
    logo_w = w + 9;
    mask_make(s, n);
    for (x = 0; x < logo_w; x++) {
        u32 *col = LOGO + x * 8;
        for (y = 0; y < 8; y++) col[y] = 0;
        for (y = 0; y < 64; y++) {
            int lx = x - 2, ly = y - 2, sh = -1, k;
            if (logo_dot(s, n, lx, ly)) {
                u32 hs = (u32)((lx / 2) * 7919 + (ly / 2) * 104729 + (lx / DX) * 31) * 2654435761u >> 28;
                sh = hs < 5 ? 3 : hs > 12 ? 1 : 2;                                          /* stone: speckles */
                if (!logo_dot(s, n, lx, ly - 1) || !logo_dot(s, n, lx, ly - 2)) sh = 3;      /* the lit top */
                else if (!logo_dot(s, n, lx - 1, ly)) sh = sh > 2 ? 3 : sh + 1;            /* the lit left */
                if (!logo_dot(s, n, lx, ly + 1) || !logo_dot(s, n, lx + 1, ly)) sh = 1;     /* the shaded bottom, right */
            } else {
                for (k = 1; k <= 5 && sh < 0; k++)                                         /* the pushed-out side */
                    if (logo_dot(s, n, lx - (k * 2) / 3, ly - k)) sh = (k == 5 || !logo_dot(s, n, lx - (k * 2) / 3, ly - k + 1)) ? 0 : 1;
                if (sh < 0) {                                                               /* the rim */
                    static const s8 o[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-2, 0}, {2, 0}, {0, -2}, {0, 2}};
                    for (k = 0; k < 8 && sh < 0; k++) if (logo_dot(s, n, lx + o[k][0], ly + o[k][1])) sh = 0;
                    for (k = 1; k <= 6 && sh < 0; k++)
                        if (logo_dot(s, n, lx - (k * 2) / 3 - 1, ly - k) || logo_dot(s, n, lx - (k * 2) / 3, ly - k - 1)) sh = 0;
                }
            }
            if (sh < 0) continue;
            col[y >> 4] |= (u32)sh << ((y & 15) * 2);
            col[4 + (y >> 4)] |= 3u << ((y & 15) * 2);
        }
    }
}

static void big_text(int d)
{
    int e, x, j;
    for (e = 0; e < 2; e++) {
        u32 *fb = e ? fb_r : fb_l;
        int x0 = 192 - logo_w / 2 + (e ? -d : d);
        for (x = 0; x < logo_w; x++) {
            const u32 *col = LOGO + x * 8;
            u32 *f;
            if ((unsigned)(x0 + x) >= VW) continue;
            f = fb + (x0 + x) * 16 + 1;
            for (j = 0; j < 4; j++) f[j] = (f[j] & ~col[4 + j]) | col[j];
        }
    }
}

void title_screen(void)
{
    u32 t = 0, a = 0, t0 = ticks;
    int fade = 0, go = 0;
    world_gen(1);
    logo_make("VIRTUALCRAFT");
    for (;;) {
        u16 k = read_pad();
        if (!go && (k & K_START) && ticks - t0 > 20) go = 1;
        if (pad_on) break;
        /* a slow look across the meadow from its south side: the cabin, the flowers, the pond */
        a = (ticks - t0) * 3;
        cam_x = meadow_x + 256; cam_z = meadow_z - 3 * 256; cam_y = spawn_y + 3 * 256;
        cam_yaw = (u16)((150 + (SIN(a) >> 5)) & 4095); cam_pitch = (u16)(4096 - 230);
        frame_no = t;
        fb_l = (u32 *)L_FB1; fb_r = (u32 *)R_FB1;
        render_frame();
        big_text(6);
        {
            extern void text(u32 *fb, int x, int y, const char *s, int shade);
            static const char *const sub = "BUILD IN 3D";
            text_eyes(192 - text_w(sub) / 2, 76, sub, 3, 4);
        }
        if ((t & 32) && !go) {
            extern void text(u32 *fb, int x, int y, const char *s, int shade);
            text_eyes(192 - text_w("PRESS START") / 2, 170, "PRESS START", 3, 2);
        }
        wait_tick();
        present();
        shown++;
        if (go) { fade -= 4; if (fade <= 0) break; }
        else if (fade < 16) fade++;
        bright(fade);
        t++;
    }
    seed_in = 1;
    bright(16);
}
