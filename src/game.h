/* Virtualcraft: a block-building game for the Virtual Boy. Shared declarations. */
#ifndef GAME_H
#define GAME_H
#include "hw.h"

/* ---- the world: 64 x 64 columns of 23 blocks, a byte a block, y fastest. On the VB it lives in the VIP's BG map memory
   (unused: the drawing unit is off), 92 KB at 0x20000; after it the spare right eye (src/main.c) */
#define WX 64
#define WZ 64
#define WH 23
#define SEA 8
#define WORLD ((u8 *)0x00020000)
#define CELL(x, y, z) WORLD[(((x) << 6) + (z)) * WH + (y)]
/* in the character memory beside the right framebuffer: the right eye is clipped exactly, so nothing spills there */
#define col_lo ((u8 *)0x00016000)
#define col_hi ((u8 *)0x00017000)
#define hmap ((u8 *)0x0001E000)
#define surf_t ((u8 *)0x0001F000)

enum { AIR, GRASS, DIRT, STONE, COBBLE, PLANKS, LOG, LEAVES, GLASS, BRICK, SAND, WATER, BEDROCK, LAMP, GOLD, WOOL, FLOWER, TALLGRASS, SWORD, NBLOCK };
#define SPRITE(t) ((t) == FLOWER || (t) == TALLGRASS || (t) == SWORD)   /* drawn as a picture facing the eye */
#define PLANT(t) ((t) == FLOWER || (t) == TALLGRASS)
#define OPAQUE(t) ((t) && (t) != LEAVES && (t) != GLASS && (t) != WATER && !PLANT(t))
#define SOLID(t) ((t) && (t) != WATER && !PLANT(t))
int block_at(int x, int y, int z);                /* outside the world: bedrock, then sea, then air */
void world_gen(u32 seed);
void world_set(int x, int y, int z, int t);
void col_update(int x, int z);
extern s32 spawn_x, spawn_y, spawn_z;            /* Q8 */
extern u16 spawn_yaw;

/* ---- the renderer */
#define VW 384
#define VH 224
#define FOC 232                                   /* focal length, pixels */
#define VIEW_R 7                                 /* blocks drawn round the camera */
typedef struct { s32 x, y, z; } V3;
extern s32 cam_x, cam_y, cam_z;                   /* Q8 (256 a block) */
extern u16 cam_yaw, cam_pitch;                    /* 0..4095 a turn */
extern s32 sel_x, sel_y, sel_z, sel_on, sel_crack;    /* the block in the cross hairs; crack 0..255 */
extern s32 frame_no, water_view;
extern u32 *fb_l, *fb_r;
#define R_FB2 ((u32 *)0x00037000)                  /* the second right-eye picture: 384 columns of 64 bytes up to 0x3D000 */                          /* where this picture is drawn */
void render_init(void);
void render_frame(void);
void render_prepare(void);
void render_draw(void);
void cam_to(const V3 *w, V3 *c);                 /* world Q8 -> camera Q8 */
void draw_cube_cam(const V3 *c, s32 half, int mat, int lit);   /* a small cube in camera space (particles, the held block) */
s32 disparity(s32 z);                             /* Q4 pixels of each eye's shift for camera depth z (Q8) */
extern const s16 sin_tab[1024 + 256];             /* Q14, a quarter-wave longer: cos = sin_tab[a + 1024 / 4] */
#define SIN(a) sin_tab[((a) >> 2) & 1023]
#define COS(a) sin_tab[(((a) >> 2) & 1023) + 256]

/* ---- the game */
void game_init(void);
void game_tick(u16 pad);                          /* 50 a second */
void game_draw(void);                             /* overlays after the world: particles, the hand */
extern s32 hand_sel;                              /* hotbar slot */
extern const u8 hotbar[9];
extern s32 hud_msg_t;

/* ---- the HUD (src/hud.c) */
void hud_draw(void);
void text(u32 *fb, int x, int y, const char *s, int shade);
int text_w(const char *s);
void text_eyes(int x, int y, const char *s, int shade, int d);   /* both eyes, a black rim, d columns out */
void blit_icon(u32 *fb, int x, int y, int blk);
void dot(u32 *fb, int x, int y, int s);

/* ---- sound (src/sound.c) */
void sound_init(void);
void sound_tick(void);                            /* in the frame interrupt */
void sfx(int id, int vol);
enum { S_DIG_DIRT, S_DIG_STONE, S_DIG_WOOD, S_DIG_SAND, S_DIG_LEAVES, S_DUG, S_PLACE, S_PLACE_HARD, S_GLASS,
       S_STEP_GRASS, S_STEP_STONE, S_STEP_WOOD, S_STEP_SAND, S_STEP_WATER, S_HURT, S_SQUEAL, S_GROAN, S_SWISH, S_CLUCK, S_HISS, S_BOOM, NSFX };
void music_on(int on);
void game_camera(void);
s32 target_depth(void);
extern s32 cube_detail, cube_tilt;

/* pad bits (the VB's SDHR << 8 | SDLR) */
#define K_A 0x0004
#define K_B 0x0008
#define K_R 0x0010
#define K_L 0x0020
#define K_RU 0x0040
#define K_RR 0x0080
#define K_LR 0x0100
#define K_LL 0x0200
#define K_LD 0x0400
#define K_LU 0x0800
#define K_START 0x1000
#define K_SEL 0x2000
#define K_RL 0x4000
#define K_RD 0x8000

u32 rnd(void);
u16 iatan2(s32 x, s32 z);                          /* the yaw (0..4095) of a direction: forward = (sin, cos) */

/* ---- creatures (src/mobs.c) */
#define Q12 4096
enum { M_PIG, M_SHEEP, M_CHICKEN, M_ZOMBIE, M_BOOM, NMOBTYPE };
enum { MP_BODY, MP_HEAD, MP_LEG, MP_SNOUT, MP_EAR, MP_TAIL, MP_WOOL, MP_HEAD2, MP_LEG2, MP_FEATHER, MP_CHEAD, MP_LEG3, MP_BEAK, MP_WING,
       MP_COMB, MP_PANTS, MP_SHIRT, MP_ZHEAD, MP_ARMS, MP_STALK, MP_CAP, MP_SPOT, MP_FOOT, NMP };
typedef struct { s8 x, y, z, hx, hy, hz; u8 part, leg; } MobBox;   /* 1/16 block; leg: 0, or the walk's phase 1 / 2 */
typedef struct { s32 x, y, z, vx, vy, vz, anim; u16 yaw; u8 type, hp, hurt, state, alive, onground; s16 timer, flee; } Mob;
#define NMOB 16
extern Mob mobs[NMOB];
extern const MobBox mob_boxes[];
extern const u8 mob_first[];
extern s32 meadow_x, meadow_z;
void mobs_init(void);
void mobs_tick(void);
int mobs_hit(s32 ox, s32 oy, s32 oz, s32 dx, s32 dy, s32 dz, s32 block_t, int dmg);
void mob_poof(const Mob *m);
void boom_fx(s32 x, s32 y, s32 z);               /* src/game.c: the burst's dust and smoke, Q12 */
extern s32 player_hp, hurt_flash;
#endif
