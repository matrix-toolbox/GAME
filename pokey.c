#include <string.h>
#include "pokey.h"

#define POLY4  15
#define POLY5  31
#define POLY9  511
#define POLY17 131071

static uint8_t p4[POLY4], p5[POLY5], p9[POLY9], p17[POLY17];
static uint8_t audf[4], audc[4], audctl;
static int cnt[4], out[4], hp[2], base_div;
static uint64_t poly_t;
static double dc_x, dc_y;

struct wr { int t; uint8_t reg, v; };
static struct wr queue[1024];
static int nqueue;

static void lfsr(uint8_t *bits, int n, int len, int tap)
{
    uint32_t s = (1u << len) - 1;
    for (int i = 0; i < n; i++) {
        bits[i] = s & 1;
        uint32_t fb = ((s >> 0) ^ (s >> tap)) & 1;
        s = (s >> 1) | fb << (len - 1);
    }
}

void pokey_init(void)
{
    lfsr(p4, POLY4, 4, 1);
    lfsr(p5, POLY5, 5, 2);
    lfsr(p9, POLY9, 9, 4);
    lfsr(p17, POLY17, 17, 5);
    memset(audf, 0, sizeof audf);
    memset(audc, 0, sizeof audc);
    audctl = 0;
    base_div = 28;
}

void pokey_write(int t, int reg, uint8_t v)
{
    if (nqueue < 1024) queue[nqueue++] = (struct wr){t, (uint8_t)reg, v};
}

static void reload(int ch)
{
    int fast1 = (audctl & 0x40) != 0, fast3 = (audctl & 0x20) != 0;
    int j12 = (audctl & 0x10) != 0, j34 = (audctl & 0x08) != 0;
    switch (ch) {
    case 0: cnt[0] = audf[0] + (fast1 ? 4 : 1); break;
    case 1: cnt[1] = j12 ? (audf[1] << 8 | audf[0]) + (fast1 ? 7 : 1) : audf[1] + 1; break;
    case 2: cnt[2] = audf[2] + (fast3 ? 4 : 1); break;
    case 3: cnt[3] = j34 ? (audf[3] << 8 | audf[2]) + (fast3 ? 7 : 1) : audf[3] + 1; break;
    }
}

static void apply(const struct wr *w)
{
    if (w->reg < 8) (w->reg & 1 ? audc : audf)[w->reg >> 1] = w->v;
    else if (w->reg == 8) audctl = w->v;
}

static void underflow(int ch)
{
    uint8_t c = audc[ch];
    if (!(c & 0x80) && !p5[poly_t % POLY5]) return;
    if (c & 0x20) out[ch] ^= 1;
    else if (c & 0x40) out[ch] = p4[poly_t % POLY4];
    else out[ch] = (audctl & 0x80) ? p9[poly_t % POLY9] : p17[poly_t % POLY17];
}

static inline int clock_ch(int ch)
{
    if (--cnt[ch] <= 0) { reload(ch); return 1; }
    return 0;
}

void pokey_render(int16_t *outbuf, int n)
{
    double per = (double)FRAME_CYCLES / n, acc = 0, next = per;
    int qi = 0, s = 0, samples_in = 0;
    for (int t = 0; t < FRAME_CYCLES && s < n; t++) {
        while (qi < nqueue && queue[qi].t <= t) apply(&queue[qi++]);
        int base = 0;
        if (--base_div <= 0) { base_div = (audctl & 1) ? 114 : 28; base = 1; }
        int j12 = audctl & 0x10, j34 = audctl & 0x08, u0 = 0, u1 = 0, u2 = 0, u3 = 0;
        if ((audctl & 0x40) || base) u0 = clock_ch(0);
        if (j12) { if ((audctl & 0x40) ? 1 : base) u1 = clock_ch(1); }
        else if (base) u1 = clock_ch(1);
        if ((audctl & 0x20) || base) u2 = clock_ch(2);
        if (j34) { if ((audctl & 0x20) ? 1 : base) u3 = clock_ch(3); }
        else if (base) u3 = clock_ch(3);
        if (u0 && !j12) underflow(0);
        if (u1) underflow(1);
        if (u2 && !j34) underflow(2);
        if (u3) underflow(3);
        if (u2 && (audctl & 4)) hp[0] = out[0];
        if (u3 && (audctl & 2)) hp[1] = out[1];
        poly_t++;
        int level = 0;
        for (int ch = 0; ch < 4; ch++) {
            uint8_t c = audc[ch];
            int vol = c & 15;
            if (!vol || (ch == 0 && j12) || (ch == 2 && j34)) continue;
            int o = out[ch];
            if (ch == 0 && (audctl & 4)) o ^= hp[0];
            if (ch == 1 && (audctl & 2)) o ^= hp[1];
            if ((c & 0x10) || o) level += vol;
        }
        acc += level;
        samples_in++;
        if (t + 1 >= next) {
            double x = acc / samples_in * 560.0, y = x - dc_x + 0.995 * dc_y;
            dc_x = x;
            dc_y = y;
            int v = (int)y;
            outbuf[s++] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            acc = 0;
            samples_in = 0;
            next += per;
        }
    }
    while (qi < nqueue) apply(&queue[qi++]);
    nqueue = 0;
    while (s < n) { outbuf[s] = s ? outbuf[s - 1] : 0; s++; }
}
