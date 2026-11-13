/* Virtualcraft: the world. An island in an endless sea, made from a seed: hills, beaches, a lake or two, trees, gold in
   the rock, and a small cabin by the spawn point. col_lo / col_hi say which part of a column can show a face. */
#include "game.h"

static int gen_done;
int surf_max;
s32 spawn_x, spawn_y, spawn_z;
u16 spawn_yaw;
static u32 seed_now;

int block_at(int x, int y, int z)
{
    if (y < 0) return BEDROCK;
    if (y >= WH) return AIR;
    if ((unsigned)x >= WX || (unsigned)z >= WZ) return y < 3 ? BEDROCK : y < SEA ? WATER : AIR;
    return CELL(x, y, z);
}

static u32 hash3(int x, int z, int k)
{
    u32 h = (u32)x * 374761393u + (u32)z * 668265263u + (u32)k * 2246822519u + seed_now * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* value noise 0..255, grid of s blocks (a power of two), smoothed */
static int vnoise(int x, int z, int sh, int k)
{
    int s = 1 << sh, gx = x >> sh, gz = z >> sh, fx = x & (s - 1), fz = z & (s - 1);
    int a = hash3(gx, gz, k) & 255, b = hash3(gx + 1, gz, k) & 255, c = hash3(gx, gz + 1, k) & 255, d = hash3(gx + 1, gz + 1, k) & 255;
    int tx = (fx << 8) >> sh, tz = (fz << 8) >> sh;          /* 0..255 */
    tx = (tx * tx * (768 - 2 * tx)) >> 16;                     /* smoothstep */
    tz = (tz * tz * (768 - 2 * tz)) >> 16;
    a = a + (((b - a) * tx) >> 8);
    c = c + (((d - c) * tx) >> 8);
    return a + (((c - a) * tz) >> 8);
}

/* tree_x/z/y: each tree's crown: src/render.c draws far ones as sprites */               /* after the generation: the surface's height and block, src/render.c far field */
static void put(int x, int y, int z, int t);
u8 tree_x[128], tree_z[128], tree_y[128];
int ntree;

/* ---- the island: an irregular coast with bays and a few islets, a mountain in the north, a lake in the east; biomes from
   broad noise: forest, rocky hills, dunes on the coast, flower plains */
enum { B_PLAINS, B_FOREST, B_ROCKY, B_DUNES };
#define BIOME(x, z) col_lo[((x) << 6) + (z)]          /* during the generation only */
#define MOUNT_X 30
#define MOUNT_Z 52
#define LAKE_X 47
#define LAKE_Z 33

static int isqrt(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }

static int land_at(int x, int z)                    /* > 0 on land, the more inland the larger (256 ~ 12 blocks in) */
{
    int dx = x * 2 - 63, dz = z * 2 - 63, r2 = dx * dx + dz * dz, l;
    static const s8 islets[3][3] = {{9, 10, 3}, {55, 13, 2}, {10, 52, 3}};   /* x, z, radius */
    int i;
    l = 256 - (r2 * 256) / (46 * 46) + ((vnoise(x, z, 3, 7) - 128) * 5) / 4 + (vnoise(x + 3, z + 40, 4, 12) - 128) / 2;
    for (i = 0; i < 3; i++) {
        int ex = x - islets[i][0], ez = z - islets[i][1], q = islets[i][2] * islets[i][2] - ex * ex - ez * ez;
        if (q > 0 && q * 40 > l) l = q * 40;
    }
    return l;
}

int biome_at(int x, int z)
{
    int b = vnoise(x + 50, z + 20, 4, 8), v = vnoise(x + 9, z + 77, 3, 9), l = land_at(x, z);
    int mx = x - MOUNT_X, mz = z - MOUNT_Z;
    if (mx * mx + mz * mz < 13 * 13) return B_ROCKY;
    if (l < 60 && v > 170) return B_DUNES;
    if (b < 105) return B_FOREST;
    if (b > 200) return B_ROCKY;
    return B_PLAINS;
}

static int height_at(int x, int z, int bi)
{
    int l = land_at(x, z), h, mx = x - MOUNT_X, mz = z - MOUNT_Z, md = mx * mx + mz * mz;
    if (l <= 0) {
        h = SEA - 1 + l / 50;                         /* the sea floor falls away from the coast */
        if (h < 2) h = 2;
        return h;
    }
    h = SEA + (l + 40) / 100;                         /* low coasts, rising slowly inland */
    if (bi == B_ROCKY) h += (vnoise(x, z, 2, 13) * 4) >> 8;
    else if (bi == B_FOREST) h += (vnoise(x, z, 3, 14) * 3) >> 8;
    else h += (vnoise(x, z, 3, 15) * 2) >> 8;
    if (md < 16 * 16) h += ((16 * 16 - md) * 9) / (16 * 16);   /* the mountain */
    {
        int lx = x - LAKE_X, lz = z - LAKE_Z, ld = lx * lx * 2 + lz * lz;
        if (ld < 9 * 9) h = SEA - 1 - (9 * 9 - ld) / 40;       /* the lake, at sea level */
        else if (ld < 11 * 11 && h > SEA) h = SEA;
    }
    if (h > WH - 7) h = WH - 7;
    return h;
}

/* a spruce: a tall trunk and a cone of leaves */
static void spruce(int x, int y, int z, int tall)
{
    int k, i, j;
    for (k = 2; k <= tall; k++) {
        int r = k == tall ? 0 : ((tall - k) & 1) ? 2 : 1;
        if (k >= tall - 1) r = k == tall ? 0 : 1;
        for (i = -r; i <= r; i++)
            for (j = -r; j <= r; j++) {
                if (r == 2 && (i == -2 || i == 2) && (j == -2 || j == 2)) continue;
                if ((unsigned)(x + i) < WX && (unsigned)(z + j) < WZ && y + k < WH && !CELL(x + i, y + k, z + j))
                    CELL(x + i, y + k, z + j) = LEAVES;
            }
    }
    if (y + tall + 1 < WH) put(x, y + tall + 1, z, LEAVES);
    for (k = 0; k < tall; k++) put(x, y + k, z, LOG);
    if (ntree < 128) { tree_x[ntree] = (u8)x; tree_z[ntree] = (u8)z; tree_y[ntree] = (u8)(y + tall - 2); ntree++; }
}

static void put(int x, int y, int z, int t)
{
    if ((unsigned)x < WX && (unsigned)z < WZ && (unsigned)y < WH) CELL(x, y, z) = (u8)t;
}

static void tree(int x, int y, int z, int tall)
{
    int i, j, k;
    for (k = tall - 3; k <= tall; k++) {
        int r = k >= tall - 1 ? 1 : 2;
        for (i = -r; i <= r; i++)
            for (j = -r; j <= r; j++) {
                if (r == 2 && (i == -2 || i == 2) && (j == -2 || j == 2) && (hash3(x + i, z + j, k) & 1)) continue;
                if (k == tall && r == 1 && i && j) continue;
                if ((unsigned)(x + i) < WX && (unsigned)(z + j) < WZ && y + k < WH && !CELL(x + i, y + k, z + j))
                    CELL(x + i, y + k, z + j) = LEAVES;
            }
    }
    for (k = 0; k < tall; k++) put(x, y + k, z, LOG);
    if (ntree < 128) { tree_x[ntree] = (u8)x; tree_z[ntree] = (u8)z; tree_y[ntree] = (u8)(y + tall - 1); ntree++; }
}

/* the cabin: 7 x 6, planks on cobble, log corners, glass windows, a doorway to the south (-z), a brick roof */
static void cabin(int x0, int z0, int y0)
{
    int x, y, z;
    for (x = x0 - 1; x <= x0 + 7; x++)
        for (z = z0 - 2; z <= z0 + 6; z++) {
            for (y = y0 - 3; y < y0; y++) put(x, y, z, y == y0 - 1 ? GRASS : DIRT);
            if (x >= x0 && x <= x0 + 6 && z >= z0 && z <= z0 + 5) put(x, y0 - 1, z, COBBLE);
            for (y = y0; y < WH; y++) put(x, y, z, AIR);
        }
    for (x = x0; x <= x0 + 6; x++)
        for (z = z0; z <= z0 + 5; z++) {
            int edge = x == x0 || x == x0 + 6 || z == z0 || z == z0 + 5;
            int corner = (x == x0 || x == x0 + 6) && (z == z0 || z == z0 + 5);
            for (y = y0; y < y0 + 4; y++) {
                if (!edge) continue;
                if (corner) put(x, y, z, LOG);
                else if (y == y0 + 3) put(x, y, z, LOG);
                else put(x, y, z, PLANKS);
            }
            if (!edge) put(x, y0 - 1, z, PLANKS);
        }
    /* windows */
    put(x0 + 2, y0 + 1, z0 + 5, GLASS); put(x0 + 4, y0 + 1, z0 + 5, GLASS);
    put(x0 + 2, y0 + 2, z0 + 5, GLASS); put(x0 + 4, y0 + 2, z0 + 5, GLASS);
    put(x0, y0 + 1, z0 + 2, GLASS); put(x0, y0 + 1, z0 + 3, GLASS); put(x0 + 6, y0 + 1, z0 + 2, GLASS); put(x0 + 6, y0 + 1, z0 + 3, GLASS);
    put(x0 + 1, y0 + 1, z0, GLASS); put(x0 + 5, y0 + 1, z0, GLASS);
    /* doorway */
    put(x0 + 3, y0, z0, AIR); put(x0 + 3, y0 + 1, z0, AIR);
    /* roof: brick, stepping in */
    for (y = 0; y < 3; y++)
        for (x = x0 - 1 + y; x <= x0 + 7 - y; x++)
            for (z = z0 - 1; z <= z0 + 6; z++)
                if (x == x0 - 1 + y || x == x0 + 7 - y || y == 2) put(x, y0 + 4 + y, z, BRICK);
    /* gables */
    for (y = 0; y < 2; y++)
        for (x = x0 + y; x <= x0 + 6 - y; x++) { put(x, y0 + 4 + y, z0, PLANKS); put(x, y0 + 4 + y, z0 + 5, PLANKS); }
    put(x0 + 3, y0 + 2, z0 + 4, LAMP);                        /* a lamp on the back wall's inside */
    put(x0 + 3, y0 + 2, z0 + 5, PLANKS);
    put(x0 + 5, y0, z0 + 4, GOLD);                             /* a treasure in the corner */
}

void world_set(int x, int y, int z, int t)
{
    if ((unsigned)x >= WX || (unsigned)z >= WZ || (unsigned)y >= WH) return;
    CELL(x, y, z) = (u8)t;
    col_update(x, z);
    if (x > 0) col_update(x - 1, z);
    if (x < WX - 1) col_update(x + 1, z);
    if (z > 0) col_update(x, z - 1);
    if (z < WZ - 1) col_update(x, z + 1);
}

/* the part of a column that can show a face: from the lowest block with a see-through neighbour to the highest block */
void col_update(int x, int z)
{
    int y, lo = WH, hi = -1, i = (x << 6) + z;
    const u8 *c = &CELL(x, 0, z);
    for (y = WH - 1; y >= 0; y--)
        if (c[y]) { hi = y; break; }
    for (y = 0; y <= hi; y++) {
        int t = c[y];
        if (!t) continue;
        if (y + 1 >= WH || !OPAQUE(c[y + 1]) || (y > 0 && !OPAQUE(c[y - 1])) ||
            !OPAQUE(block_at(x - 1, y, z)) || !OPAQUE(block_at(x + 1, y, z)) ||
            !OPAQUE(block_at(x, y, z - 1)) || !OPAQUE(block_at(x, y, z + 1))) { lo = y; break; }
    }
    if (hi < 0) { lo = 1; hi = 0; }
    col_lo[i] = (u8)lo; col_hi[i] = (u8)hi;
    if (gen_done) {
        for (y = hi; y > 0 && (!c[y] || PLANT(c[y]) || c[y] == LEAVES || c[y] == LOG || c[y] == LAMP); y--) ;
        hmap[i] = (u8)(y + 1); surf_t[i] = c[y];
        if (y + 1 > surf_max) surf_max = y + 1;
    }
}

s32 smoke_x, smoke_y, smoke_z, meadow_x, meadow_z;                    /* Q8: the chimney's top, src/game.c puffs from it */

/* a big round tree for the meadow's edge: a trunk of 5-6, a crown of 3 layers of radius 2 and a cap */
static void round_tree(int x, int y, int z, int tall)
{
    int i, j, k;
    for (k = tall - 3; k <= tall + 1; k++) {
        int r = k == tall + 1 ? 1 : 2;
        for (i = -r - (k == tall - 1); i <= r + (k == tall - 1); i++)
            for (j = -r - (k == tall - 1); j <= r + (k == tall - 1); j++) {
                int d = i * i + j * j;
                if (d > (k == tall - 1 ? 9 : r == 2 ? 6 : 2)) continue;
                if ((unsigned)(x + i) < WX && (unsigned)(z + j) < WZ && y + k < WH && !CELL(x + i, y + k, z + j))
                    CELL(x + i, y + k, z + j) = LEAVES;
            }
    }
    for (k = 0; k < tall; k++) put(x, y + k, z, LOG);
    if (ntree < 128) { tree_x[ntree] = (u8)x; tree_z[ntree] = (u8)z; tree_y[ntree] = (u8)(y + tall - 1); ntree++; }
}

void world_gen(u32 seed)
{
    int x, z, y, cx, cz, ch = SEA + 3, best = -1, mx, mz, px, pz;
    seed_now = seed;
    gen_done = 0; surf_max = 0; ntree = 0;
    /* each column's biome once, kept in col_lo until the end: the noise is slow */
    for (x = 0; x < WX; x++)
        for (z = 0; z < WZ; z++) { int bi = biome_at(x, z); col_lo[(x << 6) + z] = (u8)bi; hmap[(x << 6) + z] = (u8)height_at(x, z, bi); }
    /* the cabin: a flat, low spot in the south with the sea a few blocks away */
    cx = 28; cz = 14;
    {
        int ax, az;
        for (ax = 14; ax < 44; ax += 2)
            for (az = 14; az < 30; az += 2) {
                int lo = 99, hi = 0, sc, sea = 99, i, j;
                for (x = ax - 1; x <= ax + 7; x++)
                    for (z = az - 3; z <= az + 6; z++) {
                        int h = hmap[(x << 6) + z];
                        if (h < lo) lo = h;
                        if (h > hi) hi = h;
                    }
                if (lo < SEA || hi > SEA + 5) continue;
                for (i = -14; i <= 14; i += 2)
                    for (j = -14; j <= 14; j += 2) {
                        int qx = ax + 3 + i, qz = az + j, d;
                        if ((unsigned)qx >= WX || (unsigned)qz >= WZ) continue;
                        if (hmap[(qx << 6) + qz] < SEA) { d = isqrt(i * i + j * j); if (d < sea) sea = d; }
                    }
                if (sea < 8 || sea > 16) continue;     /* the shore in sight, not in the yard */
                sc = 100 - (hi - lo) * 4 - (hi - SEA) * 12 - (sea - 10) * (sea - 10);
                if (BIOME(ax + 3, az) == B_ROCKY) sc -= 40;
                if (sc > best) { best = sc; ch = (lo + hi + 1) / 2 + 1; cx = ax; cz = az; }
            }
    }
    ch = SEA + 2;                                 /* the cabin's ground 1 above the beach: lowland by the shore, the land rising behind */
    /* the meadow: the ground round the cabin eased to its level, soft at the rim (12 blocks) */
    mx = cx + 3; mz = cz - 4;
    meadow_x = mx * 256 + 128; meadow_z = mz * 256 + 128;
    for (x = 0; x < WX; x++)
        for (z = 0; z < WZ; z++) {
            int dx = x - mx, dz = z - mz, r2 = dx * dx + dz * dz, w, h = hmap[(x << 6) + z];
            if (r2 >= 18 * 18) continue;
            w = r2 <= 9 * 9 ? 256 : (18 * 18 - r2) * 256 / (18 * 18 - 9 * 9);
            if (h < SEA) continue;                    /* the meadow keeps its beach and sea */
            if (h < ch - 1 && r2 > 7 * 7) continue;   /* and slopes down to them: only the cabin's yard is filled up */
            h = (h * (256 - w) + (ch - 1) * w + 128) >> 8;
            hmap[(x << 6) + z] = (u8)h;
        }
    /* round the start no step is higher than 1: the land goes down to the beach in single terraces (no plateau, no cliff) */
    {
        int pass;
        for (pass = 0; pass < 12; pass++)
            for (x = 1; x < WX - 1; x++)
                for (z = 1; z < WZ - 1; z++) {
                    int i = pass & 1 ? (WX - 1 - x) : x, j = pass & 2 ? (WZ - 1 - z) : z;
                    int dx = i - mx, dz = j - mz, h = hmap[(i << 6) + j], m;
                    if (dx * dx + dz * dz >= 26 * 26 || h < SEA) continue;
                    m = 99;                           /* the lowest land beside it: the sea's edge may stay steep, the islets keep their shape */
                    if (hmap[((i - 1) << 6) + j] >= SEA - 1 && hmap[((i - 1) << 6) + j] < m) m = hmap[((i - 1) << 6) + j];
                    if (hmap[((i + 1) << 6) + j] >= SEA - 1 && hmap[((i + 1) << 6) + j] < m) m = hmap[((i + 1) << 6) + j];
                    if (hmap[(i << 6) + j - 1] >= SEA - 1 && hmap[(i << 6) + j - 1] < m) m = hmap[(i << 6) + j - 1];
                    if (hmap[(i << 6) + j + 1] >= SEA - 1 && hmap[(i << 6) + j + 1] < m) m = hmap[(i << 6) + j + 1];
                    if (h > m + 1) hmap[(i << 6) + j] = (u8)(m + 1);
                }
    }
    for (x = 0; x < WX; x++)
        for (z = 0; z < WZ; z++) {
            int h = hmap[(x << 6) + z];
            u8 *c = &CELL(x, 0, z);
            int bi = (x - mx) * (x - mx) + (z - mz) * (z - mz) < 17 * 17 ? B_PLAINS : BIOME(x, z);
            int rocky = (bi == B_ROCKY && (h >= SEA + 3 || (hash3(x, z, 21) & 3) == 0)) || h >= 15;
            int dune = bi == B_DUNES && h <= SEA + 3;
            for (y = 0; y < WH; y++) {
                int t = AIR;
                if (y == 0) t = BEDROCK;
                else if (y < h - 3) t = (hash3(x, z, y) % 61) == 0 && y < 11 ? GOLD : STONE;
                else if (y < h) t = rocky ? STONE : (h <= SEA || dune ? SAND : DIRT);
                else if (y == h) t = rocky ? STONE : (h <= SEA || dune ? SAND : GRASS);
                else if (y < SEA) t = WATER;
                c[y] = (u8)t;
            }
            if (h == SEA + 1 && (hash3(x, z, 9) & 3)) c[h] = SAND;    /* beaches */
        }
    cabin(cx, cz, ch);
    /* the chimney, with smoke */
    for (y = ch + 3; y <= ch + 7; y++) put(cx + 5, y, cz + 3, COBBLE);
    smoke_x = (cx + 5) * 256 + 128; smoke_y = (ch + 8) * 256; smoke_z = (cz + 3) * 256 + 128;
    /* the pond beside the cabin's east wall: water 2 deep over sand, a sandy rim, a plank dock reaching in */
    px = cx + 11; pz = cz + 1;
    for (x = px - 5; x <= px + 5; x++)
        for (z = pz - 5; z <= pz + 5; z++) {
            int dx = x - px, dz = z - pz, r2 = dx * dx * 4 + dz * dz * 6;
            if (r2 <= 4 * 4 * 4) { put(x, ch - 1, z, WATER); put(x, ch - 2, z, WATER); put(x, ch - 3, z, DIRT); put(x, ch, z, AIR); }
            else if (r2 <= 5 * 5 * 5 && CELL(x, ch - 1, z) == GRASS) put(x, ch - 1, z, SAND);
        }
    for (x = px - 4; x <= px - 1; x++) { put(x, ch - 1, pz, PLANKS); put(x, ch, pz, AIR); }
    put(px - 1, ch - 2, pz, LOG); put(px - 1, ch - 3, pz, LOG);
    /* the path: stepping stones from the door south, and east to the pond's edge */
    for (z = cz - 1; z >= cz - 3; z--) put(cx + 3, ch - 1, z, (z & 1) ? COBBLE : SAND);
    for (x = cx + 4; x <= cx + 7; x++) put(x, ch - 1, cz - 1, (x & 1) ? COBBLE : SAND);
    /* a lamp post by the path, a flower bed along the front */
    put(cx + 1, ch, cz - 2, LOG); put(cx + 1, ch + 1, cz - 2, LAMP);
    for (x = cx; x <= cx + 6; x++) if (x != cx + 3) put(x, ch, cz - 1, (x & 1) ? FLOWER : TALLGRASS);
    /* round trees on the meadow's rim */
    {
        static const s8 ring[8][2] = {{-12, 3}, {-11, -8}, {-5, -15}, {6, -16}, {17, 3}, {15, 12}, {-9, 12}, {19, -5}};
        int i;
        for (i = 0; i < 8; i++) {
            int tx = mx + ring[i][0], tz = mz + ring[i][1], h;
            if ((unsigned)tx >= WX || (unsigned)tz >= WZ) continue;
            h = hmap[(tx << 6) + tz];
            if (CELL(tx, h, tz) != GRASS) continue;
            round_tree(tx, h + 1, tz, 5 + (i & 1));
        }
    }
    /* trees elsewhere: scattered, not on the meadow, not touching each other */
    for (x = 3; x < WX - 3; x++)
        for (z = 3; z < WZ - 3; z++) {
            int h = hmap[(x << 6) + z], dx = x - mx, dz = z - mz;
            int bi = BIOME(x, z), v = hash3(x, z, 5) & 255;
            if (CELL(x, h, z) != GRASS || dx * dx + dz * dz < 17 * 17) continue;
            if (v > (bi == B_FOREST ? 34 : bi == B_PLAINS ? 6 : 0)) continue;
            if (CELL(x - 2, h + 3, z) || CELL(x + 2, h + 3, z) || CELL(x, h + 3, z - 2) || CELL(x, h + 3, z + 2)) continue;
            if (bi == B_FOREST && (hash3(x, z, 7) & 1)) spruce(x, h + 1, z, 5 + (hash3(x, z, 6) & 1));
            else tree(x, h + 1, z, 4 + (hash3(x, z, 6) & 1));
        }
    /* flowers and tall grass: thick on the meadow, sparse elsewhere */
    for (x = 1; x < WX - 1; x++)
        for (z = 1; z < WZ - 1; z++) {
            int h = hmap[(x << 6) + z], dx = x - mx, dz = z - mz, r2 = dx * dx + dz * dz, v = hash3(x, z, 11) & 255;
            int bi = r2 < 16 * 16 ? B_PLAINS : BIOME(x, z);
            int fl = r2 < 16 * 16 ? 16 : bi == B_PLAINS ? 10 : bi == B_FOREST ? 3 : 0, gr = r2 < 16 * 16 ? 40 : bi == B_PLAINS ? 30 : bi == B_FOREST ? 18 : 5;
            if (h + 1 >= WH || CELL(x, h, z) != GRASS || CELL(x, h + 1, z)) continue;
            if (dx >= -1 && dx <= 1 && dz >= 1 && dz <= 3) continue;      /* the yard before the door */
            if (v < fl) put(x, h + 1, z, FLOWER);
            else if (v < fl + gr) put(x, h + 1, z, TALLGRASS);
        }
    /* boulders on the rocky ground: little piles of cobble and stone */
    for (x = 2; x < WX - 2; x++)
        for (z = 2; z < WZ - 2; z++) {
            int h = hmap[(x << 6) + z];
            if (BIOME(x, z) != B_ROCKY || (hash3(x, z, 31) & 255) > 6 || h + 2 >= WH || CELL(x, h + 1, z)) continue;
            put(x, h + 1, z, COBBLE); put(x + 1, h + 1, z, STONE); put(x, h + 1, z + 1, STONE);
            if (hash3(x, z, 32) & 1) put(x, h + 2, z, COBBLE);
        }
    gen_done = 1;
    for (x = 0; x < WX; x++)
        for (z = 0; z < WZ; z++) col_update(x, z);
    /* spawn: on the meadow south-west of the door, looking toward the cabin and the pond */
    spawn_x = (cx + 7) * 256 + 128;
    spawn_z = (cz - 6) * 256 + 128;
    spawn_y = ch * 256;
    while (spawn_y < (WH - 2) * 256 && (SOLID(block_at(spawn_x >> 8, spawn_y >> 8, spawn_z >> 8)) || SOLID(block_at(spawn_x >> 8, (spawn_y >> 8) + 1, spawn_z >> 8))))
        spawn_y += 256;
    while (spawn_y > 256 && !SOLID(block_at(spawn_x >> 8, (spawn_y >> 8) - 1, spawn_z >> 8))) spawn_y -= 256;
    spawn_yaw = 0;
    (void)best;
}
