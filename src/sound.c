/* Virtualcraft: sound. The music (tools/assets.py: an original piece) on wave channels 1-4 from the frame interrupt,
   with long rests between plays; the block sounds (Minetest Game's) as 10 kHz PCM on channel 5 (src/pcm.s). */
#include "game.h"

typedef struct { const s8 *p; s32 n; } PcmStream;
volatile PcmStream pcm[3];
volatile unsigned int pcm_irqs, pcm_time, pcm_step = 1;
/* back to 10 kHz (the PCM interrupt slows to 1 kHz when idle) */
void pcm_wake(void)
{
    if (pcm_step != 1) { HW[TLR] = 5; pcm_step = 1; }
}
volatile unsigned char pcm_dummy;
#include "sound_data.h"

#define CH_PCM 4
static u32 mframe, mi, mus_on = 1;
static s32 rest = 150;                            /* frames before the music starts again */
static u8 step_slot;

void sound_init(void)
{
    int w, i;
    SSTOP = 1;
    for (w = 0; w < 5; w++)
        for (i = 0; i < 32; i++) WAVE(w)[i * 4] = snd_waves[w][i];
    for (i = 0; i < 6; i++) { SND(i, SxLRV) = 0; SND(i, SxEV1) = 0; }
    for (i = 0; i < 4; i++) SND(i, SxRAM) = i;
    SND(CH_PCM, SxRAM) = 4;
    SND(CH_PCM, SxFQL) = 0x00; SND(CH_PCM, SxFQH) = 0x07;
    SND(CH_PCM, SxEV0) = 0x00; SND(CH_PCM, SxEV1) = 0x00; SND(CH_PCM, S5SWP) = 0x00;
    SND(CH_PCM, SxINT) = 0x80;
    SSTOP = 0;
    pcm[0].n = pcm[1].n = pcm[2].n = 0;
    HW[TCR] = 0x00; HW[TLR] = 5; HW[THR] = 0;
    HW[TCR] = 0x10; HW[TCR] = 0x19;
}

void music_on(int on) { mus_on = on; if (!on) { int i; for (i = 0; i < 4; i++) SND(i, SxLRV) = 0; } }

void sound_tick(void)
{
    if (!mus_on) return;
    if (rest > 0) { rest--; if (!rest) { mframe = 0; mi = 0; } return; }
    while (mus[mi].t <= mframe) {
        const Note *n = &mus[mi];
        int ch = n->ch;
        SND(ch, SxFQL) = n->fq & 0xFF;
        SND(ch, SxFQH) = n->fq >> 8;
        SND(ch, SxLRV) = n->lrv;
        SND(ch, SxEV0) = n->ev0;
        SND(ch, SxEV1) = 0x01;
        SND(ch, SxINT) = 0x80;
        mi++;
    }
    mframe++;
    if (mframe > MUS_LEN + 150) rest = 50 * 40;  /* (then quiet for 40 seconds, as a calm game does) */
}

/* a sound: slot 0 for digging and placing, 1-2 for steps in turn */
#ifdef DEMO
volatile s32 demo_sfx[32], demo_sfx_n;           /* (tools/video: the sounds started since the director last looked) */
#endif
void sfx(int id, int vol)
{
    int s;
#ifdef DEMO
    if (demo_sfx_n < 32) demo_sfx[demo_sfx_n++] = id | (vol << 8);
#endif
    if (id < 0 || id >= NSFX) return;
    if (vol == 0) s = 0;
    else { step_slot ^= 1; s = 1 + step_slot; }
    pcm[s].n = 0;
    pcm[s].p = pcm_data[id];
    pcm[s].n = pcm_len[id];
    pcm_wake();
}
