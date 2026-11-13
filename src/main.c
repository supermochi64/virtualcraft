/* Virtualcraft: power-on, the title, the game loop. Pictures are drawn into the second framebuffers while the display
   shows the first; at the next frame start both eyes are copied over (the drawing unit stays off). */
#include "game.h"

#define VEC(n) (*(volatile u32 *)(0x0500FFC0 + (n) * 4))
volatile u32 ticks, shown;
/* the right eye's hand-over: the PCM interrupt copies cp_left columns from cp_src to cp_dst; r_pending starts at a frame */
volatile u32 cp_left, frame_irq;
u32 *volatile cp_src, *volatile cp_dst, *volatile r_pending;
volatile u32 pad_on;
volatile u16 pad_test;
extern volatile unsigned int pcm_time;
void pcm_wake(void);
void hud_init(u32 *scratch);
void game_camera(void);
void title_screen(void);

static const u8 col_table[128] = {
    0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
    0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
    0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
    0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xE0, 0xBC,
    0xA6, 0x96, 0x8A, 0x82, 0x7A, 0x74, 0x6E, 0x6A, 0x66, 0x62, 0x60, 0x5C, 0x5A, 0x58, 0x56, 0x54,
    0x52, 0x50, 0x50, 0x4E, 0x4C, 0x4C, 0x4A, 0x4A, 0x48, 0x48, 0x46, 0x46, 0x46, 0x44, 0x44, 0x44,
    0x42, 0x42, 0x42, 0x40, 0x40, 0x40, 0x40, 0x40, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3C,
    0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C, 0x3C};

#define BRIGHT_A 14
#define BRIGHT_B 54
#define BRIGHT_C 60

static void vip_isr(void)
{
    u16 p = VIP[INTPND];
    VIP[INTCLR] = p;
    if (p & FRAMESTART) {
        ticks++;
        frame_irq = pcm_time;
        sound_tick();
    }
}

static void display_on(void)
{
    int i;
    for (i = 0; i < 128; i++) {
        CLMN_TBL[i] = col_table[i]; CLMN_TBL[i + 256] = col_table[i];
        CLMN_TBL[i + 128] = col_table[127 - i]; CLMN_TBL[i + 384] = col_table[127 - i];
    }
    VIP[INTENB] = 0; VIP[INTCLR] = VIP[INTPND]; VIP[REST] = 0; VIP[XPCTRL] = 0;
    VIP[DPCTRL] = SYNCE | RE | DISP; VIP[FRMCYC] = 0;
    VIP[BRTA] = 0; VIP[BRTB] = 0; VIP[BRTC] = 0; VIP[BKCOL] = 0;
    for (i = 0; i < 100000 && !(VIP[DPSTTS] & SCANRDY); i++) ;
}

void bright(int k)       /* 0..16 */
{
    VIP[BRTA] = BRIGHT_A * k >> 4; VIP[BRTB] = BRIGHT_B * k >> 4; VIP[BRTC] = BRIGHT_C * k >> 4;
}

static u16 pad_last;
u16 read_pad(void)
{
    if (pad_on) return pad_test;
    if (!(HW[SCR] & 0x02)) {
        pad_last = (u16)((HW[SDHR] << 8) | HW[SDLR]);
        HW[SCR] = 0x84;
    }
    return pad_last;
}

void wait_tick(void)
{
    u32 t = ticks;
    while (ticks == t) ;
}

static void copy_left(void)
{
    u32 *__restrict__ a = (u32 *)L_FB1, *__restrict__ b = (u32 *)L_FB0;
    int x;
    for (x = 0; x < VW * 16; x += 16) {
        b[x] = a[x]; b[x + 1] = a[x + 1]; b[x + 2] = a[x + 2]; b[x + 3] = a[x + 3]; b[x + 4] = a[x + 4]; b[x + 5] = a[x + 5];
        b[x + 6] = a[x + 6]; b[x + 7] = a[x + 7]; b[x + 8] = a[x + 8]; b[x + 9] = a[x + 9]; b[x + 10] = a[x + 10];
        b[x + 11] = a[x + 11]; b[x + 12] = a[x + 12]; b[x + 13] = a[x + 13];
    }
}

/* a finished picture to the display with little waiting: a
   frame's picture is taken near its end (~19 ms), so the left eye copied now and the right one right after (by the PCM
   interrupt, 4 columns a tick, ~5 ms) show together if that is done by then: allowed up to 12 ms into a frame, later
   pictures wait for the next frame's start. The right eye alternates between two buffers, so the next picture can be
   drawn while it is still being copied. */
static void hand_over(u32 *r)
{
    u32 ph;
    while (cp_left) ;                              /* the last right eye still on its way: rare */
    for (;;) {
        ph = pcm_time - frame_irq;                 /* 0.1 ms since the frame began */
        if (ph <= 140 || ph > 400) break;          /* no frame start for long: never hang */
    }
    copy_left();
    cp_src = r; cp_dst = (u32 *)R_FB0; cp_left = VW;
    pcm_wake();
}

/* both eyes from the back buffers to the shown ones, right after the frame's start (title, pause) */
void present(void)
{
    u32 *__restrict__ a = (u32 *)L_FB1, *__restrict__ b = (u32 *)L_FB0;
    u32 *__restrict__ c = (u32 *)R_FB1, *__restrict__ d = (u32 *)R_FB0;
    int x;
    for (x = 0; x < VW * 16; x += 16) {
        b[x] = a[x]; b[x + 1] = a[x + 1]; b[x + 2] = a[x + 2]; b[x + 3] = a[x + 3]; b[x + 4] = a[x + 4]; b[x + 5] = a[x + 5];
        b[x + 6] = a[x + 6]; b[x + 7] = a[x + 7]; b[x + 8] = a[x + 8]; b[x + 9] = a[x + 9]; b[x + 10] = a[x + 10];
        b[x + 11] = a[x + 11]; b[x + 12] = a[x + 12]; b[x + 13] = a[x + 13];
    }
    for (x = 0; x < VW * 16; x += 16) {
        d[x] = c[x]; d[x + 1] = c[x + 1]; d[x + 2] = c[x + 2]; d[x + 3] = c[x + 3]; d[x + 4] = c[x + 4]; d[x + 5] = c[x + 5];
        d[x + 6] = c[x + 6]; d[x + 7] = c[x + 7]; d[x + 8] = c[x + 8]; d[x + 9] = c[x + 9]; d[x + 10] = c[x + 10];
        d[x + 11] = c[x + 11]; d[x + 12] = c[x + 12]; d[x + 13] = c[x + 13];
    }
}

volatile u32 seed_in = 1;
static u16 pad_prev = 0xFFFF;
static u32 *shown_r = (u32 *)R_FB1;

/* Start: the picture stays, dimmed, with the controls over it */
static void pause_screen(void)
{
    static const char *const lines[] = {"PAUSED", "", "LEFT PAD    WALK", "RIGHT PAD   LOOK", "A           JUMP / SWIM",
        "L TRIGGER   BREAK (HOLD)", "R TRIGGER   PLACE", "B / SELECT  CHOOSE BLOCK", "", "START       PLAY ON"};
    u16 k, prev = K_START;
    int i, e;
    while (cp_left) ;
    if (shown_r != (u32 *)R_FB1) { u32 *a = shown_r, *b = (u32 *)R_FB1; int i; for (i = 0; i < VW * 16; i++) b[i] = a[i]; }
    for (e = 0; e < 2; e++) {
        u32 *fb = e ? (u32 *)R_FB1 : (u32 *)L_FB1;
        int x, w;
        for (x = 60; x < 324; x++)
            for (w = 2; w < 12; w++) fb[x * 16 + w] &= 0x55555555u & (x & 1 ? 0x33333333u : 0xCCCCCCCCu);
        for (i = 0; i < 10; i++) text(fb, 96 + (e ? -3 : 3), 46 + i * 13, lines[i], 3);
    }
    music_on(0);
    wait_tick(); present();
    for (;;) {
        k = read_pad();
        if ((k & K_START) && !(prev & K_START)) break;
        prev = k;
        wait_tick();
    }
    music_on(1);
    for (i = 0; i < 6; i++) wait_tick();
}

int main(void)
{
    int i;
    u32 last;
    for (i = 0; i < 5; i++) VEC(i) = 0;
    HW[WCR] = 1;
    display_on();
    sound_init();
    VEC(4) = (u32)vip_isr;
    VIP[INTENB] = FRAMESTART;
    __asm__ volatile("cli");
    render_init();
    fb_l = (u32 *)L_FB1; fb_r = (u32 *)R_FB1;
    hud_init((u32 *)L_FB1);
    title_screen();
    game_init();
    last = ticks;
    fb_l = (u32 *)L_FB1; fb_r = (u32 *)R_FB1;
    game_camera();
    render_prepare();
    for (;;) {
        static int rb;
        u32 now, n;
        u16 k;
        /* draw the prepared picture, then prepare the next one (game steps, camera, cells) before waiting to show it */
        render_draw();
        game_draw();
        hud_draw();
        now = ticks; n = now - last; k = read_pad();
        if ((k & K_START) && !(pad_prev & K_START)) { pause_screen(); now = last = ticks; n = 0; k = read_pad(); }
        pad_prev = k;
        if (n > 4) { last = now - 4; n = 4; }
        while (last != now) { game_tick(k); last++; frame_no++; }
        game_camera();
        render_prepare();
        VIP[BRTA] = BRIGHT_A + hurt_flash * 5;   /* hurt: the dark shade glows red for a moment */
        hand_over(fb_r);
        shown_r = fb_r;
        shown++;
        rb ^= 1;
        fb_r = rb ? R_FB2 : (u32 *)R_FB1;
    }
    return 0;
}
