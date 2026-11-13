/* Virtualcraft: animals and enemies. Pigs, sheep and chickens wander the grassland and flee when hit; zombies roam the
   wilderness, chase the player within 12 blocks and strike when close. Everything blocky: a model is a few boxes
   (src/render.c draw_mob_layer draws the boxes of one height layer at the creature's place in the back-to-front order).
   Positions Q12 like the player; 50 steps a second. */
#include "game.h"

Mob mobs[NMOB];
int biome_at(int x, int z);

/* the models: boxes in 1/16 block, local axes x right, y up, z forward (the way it faces) */
const MobBox mob_boxes[] = {
    /* pig: a long pink body, a square head with a snout and floppy ears, four legs (crosswise in step), a curl of tail */
    {0, 9, -1, 5, 4, 7, MP_BODY, 0}, {0, 11, 9, 4, 4, 3, MP_HEAD, 0}, {0, 9, 13, 2, 1, 1, MP_SNOUT, 0},
    {-3, 15, 10, 1, 1, 1, MP_EAR, 0}, {3, 15, 10, 1, 1, 1, MP_EAR, 0},
    {-3, 3, 4, 2, 3, 2, MP_LEG, 1}, {3, 3, 4, 2, 3, 2, MP_LEG, 2}, {-3, 3, -6, 2, 3, 2, MP_LEG, 2}, {3, 3, -6, 2, 3, 2, MP_LEG, 1},
    {0, 11, -9, 1, 1, 1, MP_TAIL, 0},
    /* sheep: a big woolly body, a dark face under a wool cap, thin dark legs */
    {0, 12, -1, 6, 5, 8, MP_WOOL, 0}, {0, 14, 10, 3, 3, 3, MP_HEAD2, 0}, {0, 18, 9, 4, 1, 3, MP_WOOL, 0},
    {-3, 4, 5, 1, 4, 1, MP_LEG2, 1}, {3, 4, 5, 1, 4, 1, MP_LEG2, 2}, {-3, 4, -6, 1, 4, 1, MP_LEG2, 2}, {3, 4, -6, 1, 4, 1, MP_LEG2, 1},
    /* chicken: a plump body with wings, a head with a beak and a red comb, two thin legs */
    {0, 7, -1, 3, 3, 4, MP_FEATHER, 0}, {0, 13, 3, 2, 3, 2, MP_CHEAD, 0}, {0, 13, 6, 1, 1, 1, MP_BEAK, 0}, {0, 17, 3, 1, 1, 2, MP_COMB, 0},
    {-4, 8, -1, 1, 2, 3, MP_WING, 0}, {4, 8, -1, 1, 2, 3, MP_WING, 0},
    {-1, 2, 0, 1, 2, 1, MP_LEG3, 1}, {1, 2, 0, 1, 2, 1, MP_LEG3, 2},
    /* zombie: two legs, a shirt, a head, both arms held out */
    {0, 18, 0, 4, 6, 2, MP_SHIRT, 0}, {-2, 6, 0, 2, 6, 2, MP_PANTS, 1}, {2, 6, 0, 2, 6, 2, MP_PANTS, 2}, {0, 28, 0, 4, 4, 4, MP_ZHEAD, 0},
    {-6, 22, 5, 2, 2, 6, MP_ARMS, 0}, {6, 22, 5, 2, 2, 6, MP_ARMS, 0},
    /* boomcap: a pale stalk with a grim face under a broad dark cap with pale spots, two stubby feet */
    {0, 9, 0, 3, 6, 3, MP_STALK, 0}, {0, 18, 0, 7, 3, 7, MP_CAP, 0}, {0, 22, 0, 5, 1, 5, MP_CAP, 0},
    {-2, 23, -2, 2, 1, 2, MP_SPOT, 0}, {3, 23, 2, 1, 1, 2, MP_SPOT, 0},
    {-2, 1, 1, 2, 1, 3, MP_FOOT, 1}, {2, 1, 1, 2, 1, 3, MP_FOOT, 2},
};
const u8 mob_first[NMOBTYPE + 1] = {0, 10, 17, 25, 31, 38};
static const s16 mob_hw[NMOBTYPE] = {Q12 * 4 / 10, Q12 * 4 / 10, Q12 / 5, Q12 * 3 / 10, Q12 * 4 / 10};    /* half widths */
static const s16 mob_ht[NMOBTYPE] = {Q12 * 9 / 10, Q12 * 12 / 10, Q12 * 7 / 10, Q12 * 19 / 10, Q12 * 13 / 10};
static const u8 mob_maxhp[NMOBTYPE] = {10, 8, 4, 20, 10};
static const s16 mob_speed[NMOBTYPE] = {120, 110, 100, 170, 110};

static s32 respawn_t, tick_n;
extern s32 player_x, player_y, player_z;          /* Q12, src/game.c */
void player_hurt(int dmg, s32 fromx, s32 fromz);

static int mob_box_hits(const Mob *m, s32 x, s32 y, s32 z)
{
    s32 hw = mob_hw[m->type], ht = mob_ht[m->type];
    int x0 = (x - hw) >> 12, x1 = (x + hw - 1) >> 12, y0 = y >> 12, y1 = (y + ht - 1) >> 12, z0 = (z - hw) >> 12, z1 = (z + hw - 1) >> 12, a, b, c;
    if (x0 < 1 || z0 < 1 || x1 >= WX - 1 || z1 >= WZ - 1) return 1;
    for (a = x0; a <= x1; a++)
        for (b = y0; b <= y1; b++)
            for (c = z0; c <= z1; c++)
                if (SOLID(block_at(a, b, c))) return 1;
    return 0;
}

static int mob_move(Mob *m, int ax, s32 d)
{
    s32 *p = ax == 0 ? &m->x : ax == 1 ? &m->y : &m->z, old = *p;
    if (!d) return 0;
    *p += d;
    if (!mob_box_hits(m, m->x, m->y, m->z)) return 0;
    *p = old;
    if (ax == 1 && d < 0) {                        /* land on the block's top */
        s32 fy = ((old + d) >> 12) + 1;
        if (!mob_box_hits(m, m->x, fy << 12, m->z)) *p = fy << 12;
    }
    return 1;
}

/* the ground's top under a column (the highest solid block + 1), 0 if none */
static int ground_at(int x, int z, int from)
{
    int y;
    for (y = from; y > 0; y--) if (SOLID(block_at(x, y - 1, z))) return y;
    return 0;
}

static int spawn_one(int type, int near_ok)
{
    int i, tries;
    for (i = 0; i < NMOB && mobs[i].alive; i++) ;
    if (i == NMOB) return 0;
    for (tries = 0; tries < 40; tries++) {
        int x = 3 + (rnd() % (WX - 6)), z = 3 + (rnd() % (WZ - 6)), g = ground_at(x, z, WH - 1), b = block_at(x, g - 1, z), bi;
        s32 dx = (x << 12) - player_x, dz = (z << 12) - player_z;
        if (g <= SEA || b == WATER || b == LEAVES || g >= WH - 3) continue;
        if (!near_ok && (dx >> 12) * (dx >> 12) + (dz >> 12) * (dz >> 12) < 14 * 14) continue;
        bi = biome_at(x, z);
        if (type == M_ZOMBIE || type == M_BOOM) {
            /* the wilderness: forest and rock, away from the cabin's meadow */
            s32 mx = (x << 8) - meadow_x, mz = (z << 8) - meadow_z;
            if ((mx >> 8) * (mx >> 8) + (mz >> 8) * (mz >> 8) < 20 * 20) continue;
        } else if (b != GRASS || bi == 2) continue;
        mobs[i].alive = 1; mobs[i].type = (u8)type; mobs[i].hp = mob_maxhp[type];
        mobs[i].x = (x << 12) + Q12 / 2; mobs[i].z = (z << 12) + Q12 / 2; mobs[i].y = g << 12;
        mobs[i].vx = mobs[i].vy = mobs[i].vz = 0; mobs[i].yaw = (u16)(rnd() & 4095);
        mobs[i].hurt = 0; mobs[i].timer = (s16)(rnd() & 127); mobs[i].state = 0; mobs[i].anim = 0; mobs[i].flee = 0;
        return 1;
    }
    return 0;
}

void mobs_init(void)
{
    int i;
    for (i = 0; i < NMOB; i++) mobs[i].alive = 0;
    for (i = 0; i < 4; i++) spawn_one(M_PIG, 1);
    for (i = 0; i < 4; i++) spawn_one(M_SHEEP, 1);
    for (i = 0; i < 3; i++) spawn_one(M_CHICKEN, 1);
    for (i = 0; i < 4; i++) spawn_one(M_ZOMBIE, 0);
    for (i = 0; i < 2; i++) spawn_one(M_BOOM, 0);
}

static int count(int type)
{
    int i, n = 0;
    for (i = 0; i < NMOB; i++) if (mobs[i].alive && mobs[i].type == type) n++;
    return n;
}

/* a boomcap bursts: a round hole in the ground (no bedrock, water runs in), the player and creatures near it knocked away */
static void explode(Mob *m)
{
    int cx = m->x >> 12, cy = m->y >> 12, cz = m->z >> 12, i, j, k;
    s32 dx, dz, dy, d;
    for (k = -3; k <= 3; k++)
        for (i = -3; i <= 3; i++)
            for (j = -3; j <= 3; j++) {
                int r2 = i * i + j * j + k * k, b = block_at(cx + i, cy + k, cz + j);
                if (r2 > 5 + (int)(rnd() & 3) || b == AIR || b == BEDROCK || b == WATER) continue;
                world_set(cx + i, cy + k, cz + j, AIR);
            }
    /* plants whose ground went, water into the hole from its rim */
    for (k = -3; k <= 4; k++)
        for (i = -4; i <= 4; i++)
            for (j = -4; j <= 4; j++) {
                int x = cx + i, y = cy + k, z = cz + j, b = block_at(x, y, z);
                if (PLANT(b) && !SOLID(block_at(x, y - 1, z))) world_set(x, y, z, AIR);
                else if (b == AIR && y <= SEA && (block_at(x - 1, y, z) == WATER || block_at(x + 1, y, z) == WATER ||
                         block_at(x, y, z - 1) == WATER || block_at(x, y, z + 1) == WATER || block_at(x, y + 1, z) == WATER))
                    world_set(x, y, z, WATER);
            }
    sfx(S_BOOM, 0);
    boom_fx(m->x, m->y + Q12 / 2, m->z);
    dx = player_x - m->x; dz = player_z - m->z; dy = player_y - m->y;
    d = ((dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz) + (dy < 0 ? -dy : dy)) >> 8;   /* 1/16 blocks, roughly */
    if (d < 5 * 16) player_hurt(14 - d * 13 / (5 * 16), m->x, m->z);
    for (i = 0; i < NMOB; i++) {
        Mob *o = &mobs[i];
        if (o == m || o->alive != 1) continue;
        dx = o->x - m->x; dz = o->z - m->z;
        if ((dx >> 8) * (dx >> 8) + (dz >> 8) * (dz >> 8) < 48 * 48) { o->hp = o->hp > 8 ? o->hp - 8 : 0; o->vy = 900; o->hurt = 12; if (!o->hp) { o->alive = 2; mob_poof(o); } }
    }
    m->alive = 0;
}

void mobs_tick(void)
{
    int i;
    tick_n++;
    /* the population comes back, out of sight */
    if (++respawn_t >= 50 * 15) {
        respawn_t = 0;
        if (count(M_ZOMBIE) < 4) spawn_one(M_ZOMBIE, 0);
        else if (count(M_BOOM) < 2) spawn_one(M_BOOM, 0);
        else if (count(M_PIG) < 4) spawn_one(M_PIG, 0);
        else if (count(M_SHEEP) < 4) spawn_one(M_SHEEP, 0);
        else if (count(M_CHICKEN) < 3) spawn_one(M_CHICKEN, 0);
    }
    for (i = 0; i < NMOB; i++) {
        Mob *m = &mobs[i];
        s32 sp, wx, wz, dxp, dzp, d2;
        int hitx, hitz, inwater;
        int step = 1;
        if (!m->alive) continue;
        if (m->alive > 1) { if (++m->alive > 40) m->alive = 0; continue; }   /* dying: it falls over and fades */
        dxp = player_x - m->x; dzp = player_z - m->z;
        d2 = (dxp >> 8) * (dxp >> 8) + (dzp >> 8) * (dzp >> 8);           /* in 1/16 blocks, squared */
        if (d2 > 12 * 12 * 256) {
            /* far from the player: a step every 4th tick, 4 times as long, staggered */
            if (((tick_n + i) & 3) != 0) continue;
            step = 4;
        }
        if (m->hurt) m->hurt--;
        sp = mob_speed[m->type];
        if (m->type == M_BOOM && m->state == 2) {
            /* the fuse: it stands, hisses, swells and flashes; the player getting away puts it out */
            sp = 0;
            if (d2 > 5 * 5 * 256) { m->state = 1; m->timer = 0; }
            else if (--m->timer <= 0) { explode(m); continue; }
        } else if (m->type == M_BOOM && d2 < 10 * 10 * 256 && player_y > -(Q12 * 4)) {
            /* creep up on the player; close enough: light the fuse */
            u16 want = iatan2(dxp >> 6, dzp >> 6);
            s16 dy = (s16)((want - m->yaw) << 4) >> 4;
            m->yaw = (u16)((m->yaw + (dy > 30 ? 30 : dy < -30 ? -30 : dy)) & 4095);
            m->state = 1;
            if (d2 < 30 * 30 && (player_y - m->y) < Q12 * 2 && (m->y - player_y) < Q12 * 2) { m->state = 2; m->timer = 60; sfx(S_HISS, 0); sp = 0; }
        } else if (m->type == M_ZOMBIE && d2 < 12 * 12 * 256 && player_y > -(Q12 * 4)) {
            /* chase: turn toward the player, strike when close */
            u16 want = iatan2(dxp >> 6, dzp >> 6);
            s16 dy = (s16)((want - m->yaw) << 4) >> 4;
            m->yaw = (u16)((m->yaw + (dy > 40 ? 40 : dy < -40 ? -40 : dy)) & 4095);
            m->state = 1;
            if (d2 < (20 * 20) && m->timer <= 0 && (player_y - m->y) < Q12 * 2 && (m->y - player_y) < Q12 * 2) {
                player_hurt(3, m->x, m->z);
                m->timer = 50;
                sfx(S_GROAN, 2);
            }
            if (d2 < 14 * 14) sp = 0;
        } else if (m->flee) {
            /* run from the player */
            m->flee--;
            m->yaw = (u16)((iatan2(-dxp >> 6, -dzp >> 6) + ((m->flee & 32) ? 300 : -300)) & 4095);
            sp = sp * 2;
            m->state = 1;
        } else if (m->timer <= 0) {
            /* wander: walk a while in some direction, then stand */
            m->state = m->state ? 0 : 1;
            m->timer = (s16)(m->state ? 60 + (rnd() & 127) : 40 + (rnd() & 255));
            if (m->state) m->yaw = (u16)((m->yaw + (rnd() & 2047) - 1024) & 4095);
            if (m->type < M_ZOMBIE && (rnd() & 7) == 0) sfx(m->type == M_CHICKEN ? S_CLUCK : S_SQUEAL, 2);
        }
        if (m->timer > 0 && m->state != 2) m->timer -= step;
        if (!m->state) sp = 0;
        /* look ahead: no walking into water or off a drop */
        wx = (SIN(m->yaw) * sp) >> 14; wz = (COS(m->yaw) * sp) >> 14;
        if (sp && m->type < M_ZOMBIE) {
            int ax = (m->x + wx * 6) >> 12, az = (m->z + wz * 6) >> 12, fy = m->y >> 12, g = ground_at(ax, az, fy + 2);
            if (block_at(ax, g - 1, az) == WATER || g < fy - 2) { m->yaw = (u16)((m->yaw + 2048) & 4095); wx = -wx; wz = -wz; }
        }
        m->vx += (wx - m->vx) >> 2; m->vz += (wz - m->vz) >> 2;
        inwater = block_at(m->x >> 12, (m->y >> 12), m->z >> 12) == WATER;
        m->vy -= inwater ? 10 : 52;
        if (inwater && m->vy < 60) m->vy += 30;          /* they swim */
        if (m->vy < -2000) m->vy = -2000;
        hitx = mob_move(m, 0, m->vx * step);
        hitz = mob_move(m, 2, m->vz * step);
        if ((hitx || hitz) && m->onground) m->vy = 700;   /* a step up: hop */
        if (hitx) m->vx = 0;
        if (hitz) m->vz = 0;
        m->onground = 0;
        if (step > 1) m->vy -= 52 * (step - 1);
        if (mob_move(m, 1, m->vy * step > -3000 ? m->vy * step : -3000)) { if (m->vy < 0) m->onground = 1; m->vy = 0; }
        if (m->y < 0) m->alive = 0;
        if (m->vx || m->vz) m->anim += 3 + (sp >> 6);
    }
}

/* the player's blow: the first creature the view ray meets within reach, nearer than block_t (Q12 along the ray).
   Returns 1 if one was hit. */
int mobs_hit(s32 ox, s32 oy, s32 oz, s32 dx, s32 dy, s32 dz, s32 block_t, int dmg)
{
    int i, best = -1;
    s32 bt = block_t < Q12 * 3 ? block_t : Q12 * 3;
    for (i = 0; i < NMOB; i++) {
        Mob *m = &mobs[i];
        s32 hw = mob_hw[m->type] + Q12 * 3 / 10, ht = mob_ht[m->type] + Q12 / 4, t;   /* a little forgiving: aiming with a pad */
        if (m->alive != 1) continue;
        /* the ray against the box, coarsely: step along it */
        for (t = Q12 / 4; t <= bt; t += Q12 / 8) {
            s32 px = ox + ((dx * (t >> 2)) >> 12), py = oy + ((dy * (t >> 2)) >> 12), pz = oz + ((dz * (t >> 2)) >> 12);
            if (px > m->x - hw && px < m->x + hw && pz > m->z - hw && pz < m->z + hw && py > m->y && py < m->y + ht) {
                if (t < bt) { bt = t; best = i; }
                break;
            }
        }
    }
    if (best < 0) return 0;
    {
        Mob *m = &mobs[best];
        s32 kx = m->x - ox, kz = m->z - oz, kl = (kx < 0 ? -kx : kx) + (kz < 0 ? -kz : kz) + 1;
        m->hp = m->hp > dmg ? m->hp - dmg : 0;
        m->hurt = 12;
        m->vx = (kx * 500) / kl; m->vz = (kz * 500) / kl; m->vy = 500;
        sfx(m->type >= M_ZOMBIE ? S_GROAN : m->type == M_CHICKEN ? S_CLUCK : S_SQUEAL, 0);
        if (m->type < M_ZOMBIE) { m->flee = 150; m->state = 1; }
        if (m->type == M_BOOM && m->state == 2) m->timer += 20;   /* a blow puts the fuse back a little */
        if (!m->hp) { m->alive = 2; mob_poof(m); }
    }
    return 1;
}
