/* Virtualcraft: the player. Walk, look with the right pad, jump, swim; aim at a block, break it (L, held), place one
   (R); pick blocks from the hotbar (B, Select). Positions Q12 (4096 a block), 50 steps a second. */
#include "game.h"

#define Q 4096
#define HW_ (Q * 3 / 10)                          /* half width */
#define HT (Q * 18 / 10)                          /* height */
#define EYE_H (Q * 162 / 100)
#define GRAV 52
#define JUMPV 760
#define WALK 330
#define REACH (Q * 5)

s32 player_x, player_y, player_z;
#define px player_x
#define py player_y
#define pz player_z
static s32 vx, vy, vz;                           /* Q12, Q12 a step */
s32 player_hp = 20, hurt_flash;
static s32 atk_cd, regen_t, death_t;
static s32 on_ground, in_water, head_water, walk_ph, walk_amt, step_acc;
static s32 yaw_v, pitch_v, mine_t, place_t, swing, last_sel_t, hold_r;
static s32 tgt_on, tgt_x, tgt_y, tgt_z, tgt_px, tgt_py, tgt_pz, tgt_dist;
static u16 pad_old;
static s32 yaw, pitch;                            /* 0..4095 x 16 */
s32 hand_sel;
s32 hud_msg_t;
const u8 hotbar[9] = {SWORD, GRASS, DIRT, COBBLE, PLANKS, LOG, BRICK, GLASS, LAMP};
extern s32 smoke_x, smoke_y, smoke_z, meadow_x, meadow_z;

/* ---- particles: little cubes of the broken block */
#define NPART 24
typedef struct { s32 x, y, z, vx, vy, vz, t, mat; } Part;
static Part parts[NPART];

static u32 rs = 0x1234567;
u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

/* the yaw of (x, z): 0 along +z, 1024 along +x (an octant by a ratio table) */
u16 iatan2(s32 x, s32 z)
{
    static const u16 at[33] = {0,20,41,61,81,101,121,140,160,179,197,216,234,252,269,286,302,318,334,349,364,379,393,406,419,432,445,457,469,480,491,502,512};   /* atan(i/32) in 1/4096 turns */   /* atan(i/32) in 1/4096 turns, i 0..32 */
    s32 ax = x < 0 ? -x : x, az = z < 0 ? -z : z, a;
    if (!ax && !az) return 0;
    if (ax <= az) a = at[(ax * 32) / az];          /* 0..512: from +z toward +x */
    else a = 1024 - at[(az * 32) / ax];
    if (a > 512 && ax <= az) a = 512;
    if (z < 0) a = 2048 - a;
    if (x < 0) a = 4096 - a;
    return (u16)(a & 4095);
}

/* hit by a zombie: hurt, knocked back, red; at no health back to the cabin */
void player_hurt(int dmg, s32 fromx, s32 fromz)
{
    s32 kx = px - fromx, kz = pz - fromz, kl = (kx < 0 ? -kx : kx) + (kz < 0 ? -kz : kz) + 1;
    player_hp -= dmg;
    hurt_flash = 8; regen_t = 0;
    vx = (kx * 700) / kl; vz = (kz * 700) / kl; vy = 520;
    sfx(S_HURT, 0);
    if (player_hp <= 0) death_t = 60;
}

void mob_poof(const Mob *m)
{
    int i, n = 0;
    for (i = 0; i < NPART && n < 8; i++) {
        Part *p = &parts[i];
        if (p->t > 0) continue;
        p->x = (m->x >> 4) + (s32)(rnd() & 127) - 64; p->y = (m->y >> 4) + 64 + (rnd() & 127); p->z = (m->z >> 4) + (s32)(rnd() & 127) - 64;
        p->vx = (s32)(rnd() & 15) - 8; p->vz = (s32)(rnd() & 15) - 8; p->vy = 6 + (s32)(rnd() & 7);
        p->t = 26 + (rnd() & 15); p->mat = WOOL;
        n++;
    }
}

/* a boomcap's burst: dirt flung out and a cloud of smoke (every free particle) */
void boom_fx(s32 x, s32 y, s32 z)
{
    int i, n = 0;
    hurt_flash = hurt_flash > 6 ? hurt_flash : 6;
    for (i = 0; i < NPART; i++) {
        Part *p = &parts[i];
        if (p->t > 0) continue;
        p->x = (x >> 4) + (s32)(rnd() & 255) - 128; p->y = (y >> 4) + (s32)(rnd() & 255) - 64; p->z = (z >> 4) + (s32)(rnd() & 255) - 128;
        p->vx = (s32)(rnd() & 63) - 32; p->vz = (s32)(rnd() & 63) - 32; p->vy = 8 + (s32)(rnd() & 31);
        p->t = 30 + (rnd() & 31); p->mat = (n & 1) ? DIRT : WOOL;
        n++;
    }
}

static void burst(int bx, int by, int bz, int mat)
{
    int i, n = 0;
    for (i = 0; i < NPART && n < 10; i++) {
        Part *p = &parts[i];
        if (p->t > 0) continue;
        p->x = (bx << 8) + 40 + (rnd() & 175); p->y = (by << 8) + 40 + (rnd() & 175); p->z = (bz << 8) + 40 + (rnd() & 175);
        p->vx = (s32)(rnd() & 31) - 16; p->vz = (s32)(rnd() & 31) - 16; p->vy = 10 + (s32)(rnd() & 15);
        p->t = 30 + (rnd() & 15); p->mat = mat;
        n++;
    }
}

/* smoke from the chimney: grey puffs that rise, drift with a breeze and grow */
#define NSMOKE 8
static Part smoke[NSMOKE];
static int smoke_t;

static void smoke_step(void)
{
    int i;
    if (++smoke_t >= 22) {
        smoke_t = 0;
        for (i = 0; i < NSMOKE; i++)
            if (smoke[i].t <= 0) {
                Part *p = &smoke[i];
                p->x = smoke_x + (s32)(rnd() & 63) - 32; p->y = smoke_y; p->z = smoke_z + (s32)(rnd() & 63) - 32;
                p->vx = 2 + (rnd() & 1); p->vz = 1; p->vy = 4; p->t = 150; p->mat = WOOL;
                break;
            }
    }
    for (i = 0; i < NSMOKE; i++) {
        Part *p = &smoke[i];
        if (p->t <= 0) continue;
        p->t--;
        p->y += p->vy; if ((p->t & 3) == 0) { p->x += p->vx; p->z += p->vz; }
    }
}

static void parts_step(void)
{
    int i;
    for (i = 0; i < NPART; i++) {
        Part *p = &parts[i];
        if (p->t <= 0) continue;
        p->t--;
        p->vy -= 2;
        p->x += p->vx; p->z += p->vz;
        if (SOLID(block_at(p->x >> 8, (p->y + p->vy - 12) >> 8, p->z >> 8))) { p->vy = 0; p->vx = (p->vx * 3) >> 2; p->vz = (p->vz * 3) >> 2; }
        else p->y += p->vy;
    }
}

/* ---- collision */
static int box_hits(s32 x, s32 y, s32 z)
{
    int x0 = (x - HW_) >> 12, x1 = (x + HW_ - 1) >> 12, y0 = y >> 12, y1 = (y + HT - 1) >> 12, z0 = (z - HW_) >> 12, z1 = (z + HW_ - 1) >> 12;
    int a, b, c;
    if (x0 < 1 || z0 < 1 || x1 >= WX - 1 || z1 >= WZ - 1) return 1;     /* the world's edge: the sea goes on, the swimmer may not */
    for (a = x0; a <= x1; a++)
        for (b = y0; b <= y1; b++)
            for (c = z0; c <= z1; c++)
                if (SOLID(block_at(a, b, c))) return 1;
    return 0;
}

static int move_axis(int ax, s32 d)
{
    s32 *p = ax == 0 ? &px : ax == 1 ? &py : &pz;
    s32 old = *p;
    if (!d) return 0;
    *p += d;
    if (!box_hits(px, py, pz)) return 0;
    /* back to the face of the block we hit */
    if (ax == 1) *p = d > 0 ? (((old + HT + d) >> 12) << 12) - HT - 1 : (((old + d) >> 12) + 1) << 12;
    else *p = d > 0 ? (((old + HW_ + d) >> 12) << 12) - HW_ - 1 : ((((old - HW_ + d) >> 12) + 1) << 12) + HW_;
    if (box_hits(px, py, pz)) *p = old;
    return 1;
}

/* ---- aiming: a voxel walk along the view ray */
static void aim(void)
{
    s32 sy = SIN(yaw >> 4), cy = COS(yaw >> 4), sp = SIN(pitch >> 4), cp = COS(pitch >> 4);
    s32 dx = (sy * cp) >> 14, dy = sp, dz = (cy * cp) >> 14;         /* Q14 */
    s32 ox = px, oy = py + EYE_H, oz = pz;
    int x = ox >> 12, y = oy >> 12, z = oz >> 12, i, lx = x, ly = y, lz = z;
    s32 stx = dx > 0 ? 1 : -1, sty = dy > 0 ? 1 : -1, stz = dz > 0 ? 1 : -1;
    /* t in Q12 blocks along the ray (the direction has length 1) */
    s32 adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, adz = dz < 0 ? -dz : dz;
    s32 ddx = adx ? (Q << 14) / adx : 1 << 30, ddy = ady ? (Q << 14) / ady : 1 << 30, ddz = adz ? (Q << 14) / adz : 1 << 30;
    s32 fx = dx > 0 ? Q - (ox & (Q - 1)) : (ox & (Q - 1)), fy = dy > 0 ? Q - (oy & (Q - 1)) : (oy & (Q - 1)), fz = dz > 0 ? Q - (oz & (Q - 1)) : (oz & (Q - 1));
    s32 tx = adx ? ((fx << 14) / adx) : 1 << 30;
    s32 ty = ady ? ((fy << 14) / ady) : 1 << 30;
    s32 tz = adz ? ((fz << 14) / adz) : 1 << 30;
    s32 t = 0;
    tgt_on = 0;
    for (i = 0; i < 16 && t <= REACH; i++) {
        int b = block_at(x, y, z);
        if (b && b != WATER && i > 0) {
            tgt_on = 1; tgt_x = x; tgt_y = y; tgt_z = z; tgt_px = lx; tgt_py = ly; tgt_pz = lz; tgt_dist = t;
            return;
        }
        lx = x; ly = y; lz = z;
        if (tx <= ty && tx <= tz) { x += stx; t = tx; tx += ddx; }
        else if (ty <= tz) { y += sty; t = ty; ty += ddy; }
        else { z += stz; t = tz; tz += ddz; }
    }
}

static int hardness(int t)
{
    switch (t) {
    case LEAVES: return 5;
    case FLOWER: case TALLGRASS: return 1;
    case GLASS: return 8;
    case SAND: case DIRT: case GRASS: return 14;
    case PLANKS: case LOG: case WOOL: return 22;
    case STONE: case COBBLE: case BRICK: case GOLD: case LAMP: return 30;
    case BEDROCK: return 0;
    default: return 14;
    }
}

static int dig_sound(int t)
{
    switch (t) {
    case STONE: case COBBLE: case BRICK: case GOLD: case LAMP: return S_DIG_STONE;
    case PLANKS: case LOG: return S_DIG_WOOD;
    case SAND: return S_DIG_SAND;
    case LEAVES: case WOOL: case FLOWER: case TALLGRASS: return S_DIG_LEAVES;
    case GLASS: return S_GLASS;
    default: return S_DIG_DIRT;
    }
}

static int step_sound(int t)
{
    switch (t) {
    case STONE: case COBBLE: case BRICK: case GOLD: case LAMP: case BEDROCK: case GLASS: return S_STEP_STONE;
    case PLANKS: case LOG: return S_STEP_WOOD;
    case SAND: return S_STEP_SAND;
    default: return S_STEP_GRASS;
    }
}

void game_init(void)
{
    px = spawn_x << 4; py = spawn_y << 4; pz = spawn_z << 4;
    vx = vy = vz = 0;
    yaw = spawn_yaw << 4; pitch = 0;
    hand_sel = 0;
    hud_msg_t = 120;
    player_hp = 20;
    {
        int i;
        for (i = 0; i < NPART; i++) parts[i].t = 0;
    }
    mobs_init();
}


void game_tick(u16 k)
{
    u16 press = k & ~pad_old;
    s32 fwd = 0, side = 0, sy, cy, spd, ax, az;
    pad_old = k;
    /* looking: the right pad, faster the longer it is held */
    if (k & (K_RL | K_RR)) { if (yaw_v < 9 * 16) yaw_v += 10; } else yaw_v = 26;
    if (k & (K_RU | K_RD)) { if (pitch_v < 7 * 16) pitch_v += 8; } else pitch_v = 20;
    if (k & K_RL) yaw -= yaw_v * 16 / 4;
    if (k & K_RR) yaw += yaw_v * 16 / 4;
    if (k & K_RU) pitch += pitch_v * 16 / 4;
    if (k & K_RD) pitch -= pitch_v * 16 / 4;
    if (pitch > 1000 * 16) pitch = 1000 * 16;
    if (pitch < -1000 * 16) pitch = -1000 * 16;
    yaw &= 0xFFFF;
    /* the hotbar */
    if (press & K_B) { hand_sel = hand_sel == 8 ? 0 : hand_sel + 1; hud_msg_t = 90; last_sel_t = 0; }
    if (press & K_SEL) { hand_sel = hand_sel == 0 ? 8 : hand_sel - 1; hud_msg_t = 90; last_sel_t = 0; }
    /* walking */
    if (k & K_LU) fwd += 1;
    if (k & K_LD) fwd -= 1;
    if (k & K_LL) side -= 1;
    if (k & K_LR) side += 1;
    {
        int b0 = block_at(px >> 12, py >> 12, pz >> 12), b1 = block_at(px >> 12, (py + EYE_H) >> 12, pz >> 12);
        in_water = b0 == WATER || block_at(px >> 12, (py + Q / 2) >> 12, pz >> 12) == WATER;
        head_water = b1 == WATER && ((py + EYE_H) & (Q - 1)) < Q * 7 / 8;
    }
    spd = in_water ? WALK / 2 : WALK;
    if (fwd && side) spd = spd * 181 >> 8;
    sy = SIN(yaw >> 4); cy = COS(yaw >> 4);
    ax = (fwd * sy + side * cy) * spd >> 14;
    az = (fwd * cy - side * sy) * spd >> 14;
    /* smooth speed changes */
    vx += (ax - vx) >> (on_ground ? 1 : 3);
    vz += (az - vz) >> (on_ground ? 1 : 3);
    if (in_water) {
        vy -= GRAV / 4;
        if (vy < -200) vy = -200;
        if (k & K_A) { vy += 40; if (vy > 260) vy = 260; }
    } else {
        vy -= GRAV;
        if (vy < -2600) vy = -2600;
        if ((k & K_A) && on_ground) vy = JUMPV;
    }
    {
        s32 ox = px, oz = pz;
        int hitx = move_axis(0, vx), hitz = move_axis(2, vz);
        /* auto-jump: walked into a single block with room above it */
        if ((hitx || hitz) && on_ground && (fwd || side)) {
            if (!box_hits(ox + vx, py + Q + 64, oz + vz)) vy = JUMPV;
        }
        if (in_water && (hitx || hitz) && (fwd || side)) vy = 300;    /* climb out of the water */
        if (hitx) vx = 0;
        if (hitz) vz = 0;
    }
    on_ground = 0;
    if (move_axis(1, vy)) { if (vy < 0) on_ground = 1; vy = 0; }
    if (py < -8 * Q) { px = spawn_x << 4; py = spawn_y << 4; pz = spawn_z << 4; vy = 0; }
    /* steps */
    {
        s32 sp2 = vx * vx + vz * vz;
        if (on_ground && sp2 > 100 * 100) {
            walk_ph += 150; walk_amt += (256 - walk_amt) >> 3;
            step_acc += 1;
            if (step_acc >= 17) {
                step_acc = 0;
                sfx(step_sound(block_at(px >> 12, (py - 64) >> 12, pz >> 12)), 1);
            }
        } else { walk_amt -= walk_amt >> 3; step_acc = 12; }
        if (in_water && sp2 > 100 * 100 && (frame_no & 31) == 0) sfx(S_STEP_WATER, 1);
    }
    /* aiming, breaking, placing */
    aim();
    /* the blow: a creature in reach is hit (the sword hits harder), else L breaks blocks */
    if (atk_cd) atk_cd--;
    if ((k & K_L) && !atk_cd && !death_t) {
        s32 sy2 = SIN(yaw >> 4), cy2 = COS(yaw >> 4), sp2 = SIN(pitch >> 4), cp2 = COS(pitch >> 4);
        s32 ddx = (sy2 * cp2) >> 14, ddy = sp2, ddz = (cy2 * cp2) >> 14;
        if (mobs_hit(px, py + EYE_H, pz, ddx, ddy, ddz, tgt_on ? tgt_dist : REACH, hotbar[hand_sel] == SWORD ? 5 : 2)) {
            atk_cd = 12; swing = 10; mine_t = 0; k &= ~K_L;
        } else if ((press & K_L) && hotbar[hand_sel] == SWORD) { swing = 10; atk_cd = 8; sfx(S_SWISH, 0); }
    }
    if (death_t) k &= ~(K_L | K_R);
    if (tgt_on && (k & K_L)) {
        int t = block_at(tgt_x, tgt_y, tgt_z), h = hardness(t);
        if (tgt_x != sel_x || tgt_y != sel_y || tgt_z != sel_z) mine_t = 0;
        if (h) {
            if ((mine_t & 7) == 0) { sfx(dig_sound(t), 0); swing = 10; }
            mine_t++;
            if (mine_t >= h) {
                world_set(tgt_x, tgt_y, tgt_z, AIR);
                if (PLANT(block_at(tgt_x, tgt_y + 1, tgt_z))) world_set(tgt_x, tgt_y + 1, tgt_z, AIR);   /* its ground gone */
                /* water flows in from beside or above */
                if (block_at(tgt_x, tgt_y + 1, tgt_z) == WATER || block_at(tgt_x - 1, tgt_y, tgt_z) == WATER ||
                    block_at(tgt_x + 1, tgt_y, tgt_z) == WATER || block_at(tgt_x, tgt_y, tgt_z - 1) == WATER ||
                    block_at(tgt_x, tgt_y, tgt_z + 1) == WATER)
                    world_set(tgt_x, tgt_y, tgt_z, WATER);
                burst(tgt_x, tgt_y, tgt_z, t);
                sfx(t == GLASS ? S_GLASS : S_DUG, 0);
                mine_t = 0;
                aim();
            }
        }
    } else mine_t = 0;
    if (place_t) place_t--;
    if (tgt_on && (k & K_R) && (!(hold_r) || !place_t) && hotbar[hand_sel] != SWORD) {
        int x = tgt_px, y = tgt_py, z = tgt_pz, b = block_at(x, y, z);
        if (PLANT(b) && !PLANT(hotbar[hand_sel])) { x = tgt_x; y = tgt_y; z = tgt_z; b = AIR; }   /* a plant gives way to a block */
        if (PLANT(hotbar[hand_sel]) && !SOLID(block_at(x, y - 1, z))) b = -1;                    /* plants want ground */
        if ((b == AIR || b == WATER) && (unsigned)y < WH && (unsigned)x < WX && (unsigned)z < WZ) {
            /* not inside the player */
            s32 x0 = (px - HW_) >> 12, x1 = (px + HW_ - 1) >> 12, y0 = py >> 12, y1 = (py + HT - 1) >> 12, z0 = (pz - HW_) >> 12, z1 = (pz + HW_ - 1) >> 12;
            if (PLANT(hotbar[hand_sel]) || !(x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1)) {
                int t = hotbar[hand_sel];
                world_set(x, y, z, t);
                sfx(t == STONE || t == COBBLE || t == BRICK || t == LAMP || t == GLASS ? S_PLACE_HARD : S_PLACE, 0);
                swing = 8;
                aim();
            }
        }
        place_t = hold_r ? 8 : 14;
    }
    hold_r = (k & K_R) != 0;
    if (!hold_r) place_t = 0;
    if (swing) swing--;
    if (hud_msg_t) hud_msg_t--;
    last_sel_t++;
    parts_step();
    smoke_step();
    mobs_tick();
    if (hurt_flash) hurt_flash--;
    if (death_t && !--death_t) {
        /* back at the cabin, healed */
        px = spawn_x << 4; py = spawn_y << 4; pz = spawn_z << 4; vx = vy = vz = 0; player_hp = 20;
    }
    if (++regen_t >= 250 && player_hp < 20 && !death_t) { player_hp++; regen_t = 150; }
    /* the camera */
    sel_on = tgt_on; sel_x = tgt_x; sel_y = tgt_y; sel_z = tgt_z;
    sel_crack = tgt_on && mine_t ? mine_t * 255 / hardness(block_at(tgt_x, tgt_y, tgt_z)) : 0;
}

/* where the camera is this picture (bobbing a little when walking) */
void game_camera(void)
{
    s32 bob = (SIN(walk_ph) * walk_amt) >> 16;     /* +-64 Q12 at full */
    cam_x = px >> 4; cam_z = pz >> 4;
    cam_y = (py + EYE_H + (bob < 0 ? -bob : bob) * 3 / 2) >> 4;
    cam_yaw = (u16)(yaw >> 4); cam_pitch = (u16)((pitch >> 4) & 4095);
    water_view = head_water;
}

s32 target_depth(void)
{
    return tgt_on ? tgt_dist >> 4 : 0;
}

/* butterflies over the meadow: a body and two wings that beat, at their depth in both eyes */
static void butterflies(void)
{
    int i, e;
    for (i = 0; i < 6; i++) {
        s32 a = frame_no * (9 + i) + i * 700, b = frame_no * (5 + i) + i * 1300;
        V3 w, c;
        s32 inv, sx, sy, d, flap = ((frame_no >> 1) + i) & 3;
        w.x = meadow_x + ((SIN(a) * (5 + i)) >> 6) + ((SIN(b * 3) * 2) >> 6);
        w.z = meadow_z + ((COS(b) * (4 + i)) >> 6);
        w.y = smoke_y - 7 * 256 + ((SIN(a * 2 + b) * 3) >> 9);
        cam_to(&w, &c);
        if (c.z < 96 || c.z > 9 * 256) continue;
        inv = (FOC << 16) / c.z;
        sx = (192 * 16 + ((c.x * inv) >> 12)) >> 4; sy = (112 * 16 - ((c.y * inv) >> 12)) >> 4;
        d = (disparity(c.z) + 8) >> 4;
        if (sy < 2 || sy > 190) continue;
        for (e = 0; e < 2; e++) {
            u32 *fb = e ? fb_r : fb_l;
            int x = sx + (e ? -d : d), wy = flap == 0 ? sy - 2 : flap == 2 ? sy + 1 : sy - 1;
            dot(fb, x, sy, 1); dot(fb, x, sy - 1, 1);
            if (c.z < 4 * 256) { dot(fb, x - 2, wy, 3); dot(fb, x - 1, sy - 1, 3); dot(fb, x + 1, sy - 1, 3); dot(fb, x + 2, wy, 3); dot(fb, x - 2, wy + 1, 2); dot(fb, x + 2, wy + 1, 2); }
            else { dot(fb, x - 1, wy, 3); dot(fb, x + 1, wy, 3); }
        }
    }
}

void game_draw(void)
{
    int i;
    V3 w, c;
    for (i = 0; i < NPART; i++) {
        Part *p = &parts[i];
        if (p->t <= 0) continue;
        w.x = p->x; w.y = p->y; w.z = p->z;
        cam_to(&w, &c);
        if (c.z < 60) continue;
        draw_cube_cam(&c, p->t > 8 ? 13 : p->t + 5, p->mat, i * 300);
    }
    for (i = 0; i < NSMOKE; i++) {
        Part *p = &smoke[i];
        if (p->t <= 0) continue;
        w.x = p->x; w.y = p->y; w.z = p->z;
        cam_to(&w, &c);
        if (c.z < 200) continue;
        draw_cube_cam(&c, 34 - p->t / 6, WOOL, p->t * 20);
    }
    butterflies();
    /* the held block: low right, swinging when used, bobbing with the steps */
    {
        s32 bob = (SIN(walk_ph) * walk_amt) >> 16, bx = (COS(walk_ph >> 1) * walk_amt) >> 17;
        s32 sw = swing ? SIN(swing * 205) >> 7 : 0;
        c.x = 150 + bx - sw / 2; c.y = -86 + (bob < 0 ? -bob : bob) / 2 - sw / 3; c.z = 300 + sw;
        if (hotbar[hand_sel] == SWORD) { c.x += 18; c.y += 22; }   /* the sword: twice the size, its blade up into the picture */
        draw_cube_cam(&c, hotbar[hand_sel] == SWORD ? 60 : 30, hotbar[hand_sel], 480 + sw * 4);
    }
}
