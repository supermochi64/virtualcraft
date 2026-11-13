/* Virtualcraft: the HUD. The cross hairs sit at the aimed block's depth (no double image), the hotbar floats a little in
   front of the screen; its icons are little cubes drawn by the renderer itself at power-on. */
#include "game.h"
#include "assets.h"
static void pset(u32 *fb, int x, int y, int s);

/* icons: 9 x 24 columns x 2 words (rows 192..223), with their masks */
static u32 icon_w[9][20][2], icon_m[9][20][2];


void dot(u32 *fb, int x, int y, int s)
{
    pset(fb, x, y, s);
}

static void pset(u32 *fb, int x, int y, int s)
{
    u32 *w;
    if ((unsigned)x >= VW || (unsigned)y >= VH) return;
    w = fb + x * 16 + (y >> 4);
    *w = (*w & ~(3u << ((y & 15) * 2))) | ((u32)s << ((y & 15) * 2));
}

static void vline(u32 *fb, int x, int y0, int y1, int s)
{
    int y;
    for (y = y0; y < y1; y++) pset(fb, x, y, s);
}

int text_w(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n * 6 - 1;
}

/* 5 x 7 glyphs, a byte a column (bit 0 at the top) */
void text(u32 *fb, int x, int y, const char *s, int shade)
{
    for (; *s; s++, x += 6) {
        int ch = *s, c, r;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) continue;
        for (c = 0; c < 5; c++) {
            u8 b = font5x7[ch - 32][c];
            for (r = 0; r < 7; r++) if (b & (1 << r)) pset(fb, x + c, y + r, shade);
        }
    }
}

/* text with a black edge round it, both eyes, d columns toward the viewer */
void text_eyes(int x, int y, const char *s, int shade, int d)
{
    int e, dx, dy;
    for (e = 0; e < 2; e++) {
        u32 *fb = e ? fb_r : fb_l;
        int xx = x + (e ? -d : d);
        for (dx = -1; dx <= 1; dx++)
            for (dy = -1; dy <= 1; dy++)
                if (dx || dy) text(fb, xx + dx, y + dy, s, 0);
        text(fb, xx, y, s, shade);
    }
}

/* the hotbar: 9 slots of 22 columns from HB_X, rows 198..222. Icons 20 x 20 at rows 201..220 (word rows 12 and 13) */
#define HB_X 93
#define HB_W (9 * 22 + 2)
#define IC 20
static u32 bar_w[HB_W][2], bar_m[HB_W][2];

void blit_icon(u32 *fb, int x, int y, int k)
{
    int c;
    (void)y;
    for (c = 0; c < IC; c++) {
        u32 *w;
        if ((unsigned)(x + c) >= VW) continue;
        w = fb + (x + c) * 16 + 12;
        w[0] = (w[0] & ~icon_m[k][c][0]) | icon_w[k][c][0];
        w[1] = (w[1] & ~icon_m[k][c][1]) | icon_w[k][c][1];
    }
}

static int gpix(const u32 *fb, int x, int y) { return (fb[x * 16 + (y >> 4)] >> ((y & 15) * 2)) & 3; }

/* a pixel into the two words of rows 192..223 (with its mask) */
static void put2(u32 *w, u32 *m, int y, int s)
{
    int k = y - 192;
    if ((unsigned)k >= 32) return;
    w[k >> 4] = (w[k >> 4] & ~(3u << ((k & 15) * 2))) | ((u32)s << ((k & 15) * 2));
    m[k >> 4] |= 3u << ((k & 15) * 2);
}

/* the icons: each hotbar block a cube turned 45 degrees and tipped toward the viewer, drawn into a spare buffer by the
   renderer and kept (20 x 20); the bar's frame and inset slots as a template */
void hud_init(u32 *scratch)
{
    int k, c, r;
    u32 *ol = fb_l, *orr = fb_r;
    cam_x = cam_y = cam_z = 0; cam_yaw = 0; cam_pitch = 0;
    for (k = 0; k < 9; k++) {
        V3 p;
        for (c = 0; c < VW * 16; c++) scratch[c] = 0;
        fb_l = scratch; fb_r = (u32 *)R_FB1;
        p.x = 0; p.y = 0; p.z = 768;
        cube_detail = 1; cube_tilt = 4096 - 520;
        draw_cube_cam(&p, SPRITE(hotbar[k]) ? 30 : 19, hotbar[k], 512);
        cube_detail = 0; cube_tilt = 0;
        for (c = 0; c < IC; c++) {
            icon_w[k][c][0] = icon_w[k][c][1] = icon_m[k][c][0] = icon_m[k][c][1] = 0;
            for (r = 0; r < IC; r++) {
                int s = gpix(scratch, 186 + c, 102 + r);
                if (s) put2(icon_w[k][c], icon_m[k][c], 201 + r, s);
            }
        }
        /* a black rim round the icon, so it stands off the slot */
        for (c = 0; c < IC; c++)
            for (r = 0; r < IC; r++) {
                int s = gpix(scratch, 186 + c, 102 + r);
                if (s) continue;
                if ((c > 0 && gpix(scratch, 185 + c, 102 + r)) || (c < IC - 1 && gpix(scratch, 187 + c, 102 + r)) ||
                    (r > 0 && gpix(scratch, 186 + c, 101 + r)) || (r < IC - 1 && gpix(scratch, 186 + c, 103 + r)))
                    put2(icon_w[k][c], icon_m[k][c], 201 + r, 0);
            }
    }
    for (c = 0; c < HB_W; c++) {
        int sc = (c - 1) % 22, edge = c == 0 || c == HB_W - 1;
        bar_w[c][0] = bar_w[c][1] = bar_m[c][0] = bar_m[c][1] = 0;
        for (r = 197; r <= 223; r++) {
            int s = 0;
            if (edge || r == 197 || r == 223) s = 2;                       /* the bar's frame */
            else if (r == 198 || r == 222 || c == 1 || c == HB_W - 2) s = 0;
            else if (sc >= 1 && sc <= 20 && r >= 200 && r <= 220) {
                /* an inset slot: shadow top and left, light bottom and right, a dark dithered floor */
                if (r == 200 || sc == 1) s = 0;
                else if (r == 220 || sc == 20) s = 2;
                else s = ((sc + r) & 1) ? 1 : 0;
            }
            put2(bar_w[c], bar_m[c], r, s);
        }
    }
    fb_l = ol; fb_r = orr;
}

static const char *const names[NBLOCK] = {"AIR", "GRASS", "DIRT", "STONE", "COBBLESTONE", "PLANKS", "LOG", "LEAVES",
    "GLASS", "BRICKS", "SAND", "WATER", "BEDROCK", "LAMP", "GOLD ORE", "WOOL", "FLOWER", "TALL GRASS", "SWORD"};

extern s32 target_depth(void);

void hud_draw(void)
{
    int e, i;
    s32 d = 0, z = target_depth();
    d = (disparity(z ? z : 4 * 256) + 8) >> 4;
    /* the cross hairs */
    for (e = 0; e < 2; e++) {
        u32 *fb = e ? fb_r : fb_l;
        int x = 192 + (e ? -d : d), y = 112;
        vline(fb, x - 1, y - 6, y + 7, 0); vline(fb, x + 1, y - 6, y + 7, 0);
        for (i = -6; i <= 6; i++) { pset(fb, x + i, y - 1, 0); pset(fb, x + i, y + 1, 0); }
        vline(fb, x, y - 5, y + 6, 3);
        for (i = -5; i <= 5; i++) pset(fb, x + i, y, 3);
    }
    /* the hotbar, 2 columns in front of the screen; the chosen block's icon 4 columns, in a bright frame */
    for (e = 0; e < 2; e++) {
        u32 *fb = e ? fb_r : fb_l;
        int x0 = HB_X + (e ? -2 : 2), c, k;
        for (c = 0; c < HB_W; c++) {
            u32 *w = fb + (x0 + c) * 16 + 12;
            w[0] = (w[0] & ~bar_m[c][0]) | bar_w[c][0];
            w[1] = (w[1] & ~bar_m[c][1]) | bar_w[c][1];
        }
        for (k = 0; k < 9; k++) if (k != hand_sel) blit_icon(fb, x0 + 2 + k * 22, 0, k);
        {
            int sx = x0 + hand_sel * 22 + (e ? -2 : 2), i;
            for (i = -1; i <= 22; i++) { pset(fb, sx + i, 197, 3); pset(fb, sx + i, 198, 3); pset(fb, sx + i, 222, 3); pset(fb, sx + i, 223, 3); }
            for (i = 197; i <= 223; i++) { pset(fb, sx - 1, i, 3); pset(fb, sx, i, 3); pset(fb, sx + 21, i, 3); pset(fb, sx + 22, i, 3); }
            blit_icon(fb, sx + 1, 0, hand_sel);
        }
    }
    /* health: ten hearts over the hotbar's left, blinking when hurt (a strip of words, rebuilt when it changes) */
    {
        static u32 hw_[80][2], hm_[80][2];
        static s32 last_state = -1;
        static const u8 heart[6] = {0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};   /* (7 columns a row, bit 6 leftmost) */
        s32 state = player_hp * 2 + (hurt_flash && (hurt_flash & 2));
        int hi, row, col;
        if (state != last_state) {
            last_state = state;
            for (col = 0; col < 80; col++) hw_[col][0] = hw_[col][1] = hm_[col][0] = hm_[col][1] = 0;
            for (hi = 0; hi < 10; hi++) {
                int full = player_hp >= hi * 2 + 2, half = player_hp == hi * 2 + 1, blink = hurt_flash && (hurt_flash & 2);
                for (row = 0; row < 6; row++)
                    for (col = 0; col < 7; col++) {
                        int on = (heart[row] >> (6 - col)) & 1, edge, sh, y = 189 + row - 176, c = hi * 8 + col;
                        if (!on) continue;
                        edge = row == 0 || col == 0 || col == 6 || !((heart[row - 1] >> (6 - col)) & 1) || !((heart[row] >> (7 - col)) & 1) || !((heart[row] >> (5 - col)) & 1);
                        sh = edge ? 0 : (full || (half && col < 3)) ? (blink ? 2 : 3) : 1;
                        hw_[c][y >> 4] |= (u32)sh << ((y & 15) * 2);
                        hm_[c][y >> 4] |= 3u << ((y & 15) * 2);
                    }
            }
        }
        for (e = 0; e < 2; e++) {
            u32 *fb = e ? fb_r : fb_l;
            int x0 = 95 + (e ? -2 : 2);
            for (col = 0; col < 80; col++) {
                u32 *w = fb + (x0 + col) * 16 + 11;
                w[0] = (w[0] & ~hm_[col][0]) | hw_[col][0];
                w[1] = (w[1] & ~hm_[col][1]) | hw_[col][1];
            }
        }
    }
    if (player_hp <= 0) text_eyes(192 - text_w("KNOCKED OUT") / 2, 100, "KNOCKED OUT", 3, 4);
    if (hud_msg_t) {
        const char *s = names[hotbar[hand_sel]];
        text_eyes(192 - text_w(s) / 2, 178, s, hud_msg_t > 20 || (hud_msg_t & 2) ? 3 : 2, 3);
    }
}

const u8 *font_glyph(int ch)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95) ch = 32;
    return font5x7[ch - 32];
}
