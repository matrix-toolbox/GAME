#include <SDL.h>
#include "game.h"
#include "pokey.h"

#define RATE 44100
#define SAMPLES (RATE / FPS)
#define ROW_CYCLES (7 * FRAME_CYCLES / CH)
#define SWEEP_STEP 2400

int sound_on = 1;
static int ready, paused;
static SDL_AudioDeviceID dev;
static int f1, d1, v1, f4, d4, v4;
static int f1_next;
static int sparkle = -1;
static const uint8_t SPARKLE[4] = {0x3C, 0x2F, 0x27, 0x1D};
static int step = 0x40, step_f = -1;
static int frames, rows_left, row_t, amoeba, amoeba_was, cover, cover_was, sweeps, sweep;
static unsigned seed = 1;

static uint8_t random8(void)
{
    seed = seed * 1103515245u + 12345u;
    return (uint8_t)(seed >> 16);
}

int sound_init(void)
{
    SDL_AudioSpec want = {0}, have;
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) return 0;
    SDL_PauseAudioDevice(dev, 0);
    return 1;
}

void sound_reset(void)
{
    if (!ready) { pokey_init(); ready = 1; }
    paused = 0;
    f1 = d1 = v1 = f4 = d4 = v4 = f1_next = 0;
    sparkle = -1;
    step = 0x40;
    rows_left = amoeba = cover = sweep = sweeps = 0;
    for (int r = 0; r <= 8; r++) pokey_write(0, r, 0);
}

void sound_play(int id)
{
    switch (id) {
    case SND_DIAMOND: sparkle = 0; v1 = 0; f1_next = 0; break;
    case SND_PUSH: f1 = 0x14; v1 = 0x0A; d1 = 0x00; f1_next = 0; sparkle = -1; break;
    case SND_CRUSH: f1 = 0x14 + (random8() & 0x0F); d1 = v1 = 0x0A; f1_next = 0; sparkle = -1; break;
    case SND_BIRTH: case SND_EXIT: d1 = 0x08; v1 = 0x0E; f1 = 0x15; f1_next = 0; sparkle = -1; break;
    case SND_EXPLODE: d4 = 0x00; v4 = 0x0F; f4 = 0x60; break;
    case SND_DIRT: step = 0x41; step_f = 0x38; break;
    case SND_SPACE: step = 0x41; step_f = 0x99; break;
    case SND_BONUS: sweep = 1; break;
    case SND_BONUS_START: sweeps = 0; break;
    }
}

void sound_time(int s)
{
    if (s <= 0 || s > 9) return;
    f1 = 0x22 + s;
    f1_next = 0;
    sparkle = -1;
    d1 = v1 = 0x0A;
}

void sound_tick(int amoeba_now)
{
    rows_left = CH;
    row_t = 0;
    amoeba = amoeba_now;
}

void sound_cover(int on)
{
    cover = on;
}

void sound_pause(int on)
{
    if (on && !paused) for (int r = 0; r <= 8; r++) pokey_write(0, r, 0);
    paused = on;
}

const int16_t *sound_frame(int *n)
{
    static int16_t buf[SAMPLES];
    if (paused) {
        pokey_render(buf, SAMPLES);
        *n = SAMPLES;
        if (dev) SDL_QueueAudio(dev, buf, sizeof buf);
        return buf;
    }
    if (step_f >= 0) { pokey_write(0, 2, (uint8_t)step_f); step_f = -1; }
    if ((++frames % 7) == 0) {
        if (sparkle < 0) {
            pokey_write(0, 0, (uint8_t)f1);
            pokey_write(0, 1, (uint8_t)(d1 << 4 | v1));
        }
        v1 >>= 1;
        if (f1_next) { f1 = f1_next; f1_next = 0; }
        pokey_write(0, 6, (uint8_t)f4);
        pokey_write(0, 7, (uint8_t)(d4 << 4 | v4));
        if (v4) v4--;
    }
    if (sparkle >= 0) {
        int k = sparkle / 3, vol = sparkle < 12 ? 10 - k : 7 - (sparkle - 12);
        if (vol <= 0) { pokey_write(0, 1, 0); sparkle = -1; }
        else {
            pokey_write(0, 0, SPARKLE[k < 4 ? k : 3]);
            pokey_write(0, 1, (uint8_t)(0xA0 | vol));
            sparkle++;
        }
    }
    if (cover) { pokey_write(0, 5, 0xA8); pokey_write(0, 4, 0x08 + (random8() & 0x0F)); }
    else if (cover_was || (amoeba_was && !amoeba)) pokey_write(0, 5, 0);
    cover_was = cover;
    amoeba_was = amoeba;
    for (; rows_left > 0 && row_t < FRAME_CYCLES; rows_left--, row_t += ROW_CYCLES) {
        if (step != 0x40) {
            step = (step + 1) & 0x4F;
            pokey_write(row_t, 3, (uint8_t)step);
        }
        if (amoeba && !cover) { pokey_write(row_t, 5, 0xA5); pokey_write(row_t, 4, random8() | 0x40); }
    }
    row_t -= FRAME_CYCLES;
    if (sweep) {
        pokey_write(0, 7, 0xA8);
        for (int x = 12; x > 1; x--) pokey_write((12 - x) * SWEEP_STEP, 6, (uint8_t)(x + sweeps));
        pokey_write(11 * SWEEP_STEP, 7, 0);
        sweeps++;
        sweep = 0;
    }
    pokey_render(buf, SAMPLES);
    *n = SAMPLES;
    if (dev) {
        if (!sound_on) memset(buf, 0, sizeof buf);
        SDL_QueueAudio(dev, buf, sizeof buf);
    }
    return buf;
}

void sound_wait(void)
{
    while (dev && SDL_GetQueuedAudioSize(dev) > 3 * SAMPLES * sizeof(int16_t)) SDL_Delay(1);
}
