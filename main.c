#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "game.h"

#ifndef SHEET
#define SHEET "sprites_1.bmp"
#endif

static Game game;
static uint32_t fb[SCREEN_W * SCREEN_H];
static int held[4], order[4], order_n, fire_key, back_key, back_frames, quit_req, headless;
static long frame_no, max_frames = -1;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static FILE *wav, *rec;
static long wav_samples;
static unsigned pending_seed;

static void wav_header(void)
{
    uint32_t data = (uint32_t)(wav_samples * 2), rate = 44100;
    uint8_t h[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\0\0\0\0\0\0\0\0\x02\0\x10\0data";
    uint32_t v[4] = {36 + data, rate, rate * 2, data};
    memcpy(h + 4, &v[0], 4);
    memcpy(h + 24, &v[1], 4);
    memcpy(h + 28, &v[2], 4);
    memcpy(h + 40, &v[3], 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
}

static unsigned random_seed(void)
{
    unsigned s = pending_seed ? pending_seed : 1 + (unsigned)(time(NULL) ^ (time(NULL) >> 7)) % 9999;
    pending_seed = 0;
    if (rec) fprintf(rec, "%ld seed %u\n", frame_no, s);
    return s;
}

static int direction(void)
{
    for (int i = order_n - 1; i >= 0; i--)
        if (held[order[i]]) return order[i];
    return -1;
}

static void arrow(int d, int down)
{
    held[d] = down;
    if (!down) return;
    game_press(&game, d);
    int n = 0;
    for (int i = 0; i < order_n; i++) if (order[i] != d) order[n++] = order[i];
    order[n++] = d;
    order_n = n;
}

static void key(SDL_Keycode k, int down)
{
    switch (k) {
    case SDLK_UP: arrow(UP, down); break;
    case SDLK_RIGHT: arrow(RIGHT, down); break;
    case SDLK_DOWN: arrow(DOWN, down); break;
    case SDLK_LEFT: arrow(LEFT, down); break;
    case SDLK_LCTRL: case SDLK_RCTRL:
        if (down && !fire_key) game_press(&game, -1);
        fire_key = down;
        break;
    case SDLK_z: back_key = down; break;
    }
    if (!down) return;
    switch (k) {
    case SDLK_SPACE: game.paused ^= 1; sound_pause(game.paused); break;
    case SDLK_ESCAPE:
        if (game.phase == PH_OVER || game.phase == PH_INVALID || game.phase == PH_MENU) quit_req = 1;
        else game_give_up(&game);
        break;
    case SDLK_F2: game_new(&game, cave_count ? 1 : 0, random_seed()); break;
    case SDLK_F3: game_new(&game, 0, random_seed()); break;
    case SDLK_m: sound_on ^= 1; break;
    case SDLK_t: time_off ^= 1; break;
    case SDLK_F11:
        if (win) SDL_SetWindowFullscreen(win, (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        break;
    }
}

static FILE *script;
static long script_frame = -1;
static char script_cmd[32], script_arg[256];

static void script_next(void)
{
    char buf[512];
    script_frame = -1;
    while (script && fgets(buf, sizeof buf, script)) {
        script_arg[0] = 0;
        if (sscanf(buf, "%ld %31s %255s", &script_frame, script_cmd, script_arg) >= 2) return;
        script_frame = -1;
    }
}

static const struct { const char *name; SDL_Keycode k; } K[] = {
    {"up", SDLK_UP}, {"down", SDLK_DOWN}, {"left", SDLK_LEFT}, {"right", SDLK_RIGHT}, {"fire", SDLK_LCTRL}, {"fire", SDLK_RCTRL},
    {"z", SDLK_z}, {"space", SDLK_SPACE}, {"esc", SDLK_ESCAPE}, {"f2", SDLK_F2}, {"f3", SDLK_F3}, {"m", SDLK_m}, {"t", SDLK_t},
};

static void record(SDL_Keycode k, int down)
{
    for (unsigned i = 0; rec && i < sizeof K / sizeof K[0]; i++)
        if (K[i].k == k) { fprintf(rec, "%ld %s %s\n", frame_no, down ? "down" : "up", K[i].name); return; }
}

static SDL_Keycode key_by_name(const char *n)
{
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++)
        if (!strcmp(n, K[i].name)) return K[i].k;
    fprintf(stderr, "script: no key '%s'\n", n);
    return SDLK_UNKNOWN;
}

static void save_shot(const char *name)
{
    draw(&game, fb);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(fb, SCREEN_W, SCREEN_H, 32, SCREEN_W * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(s, name);
    SDL_FreeSurface(s);
}

static void save_map(const char *name)
{
    static uint32_t big[CW * CELL * CH * CELL];
    draw_map(&game, big);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(big, CW * CELL, CH * CELL, 32, CW * CELL * 4, SDL_PIXELFORMAT_ARGB8888);
    SDL_SaveBMP(s, name);
    SDL_FreeSurface(s);
}

static void run_script(void)
{
    while (script_frame >= 0 && script_frame <= frame_no) {
        if (!strcmp(script_cmd, "down")) key(key_by_name(script_arg), 1);
        else if (!strcmp(script_cmd, "up")) key(key_by_name(script_arg), 0);
        else if (!strcmp(script_cmd, "key")) { key(key_by_name(script_arg), 1); key(key_by_name(script_arg), 0); }
        else if (!strcmp(script_cmd, "shot")) save_shot(script_arg);
        else if (!strcmp(script_cmd, "dump")) cave_dump(&game, script_arg);
        else if (!strcmp(script_cmd, "map")) save_map(script_arg);
        else if (!strcmp(script_cmd, "quit")) quit_req = 1;
        else if (!strcmp(script_cmd, "seed")) pending_seed = (unsigned)strtoul(script_arg, NULL, 10);
        script_next();
    }
}

static void usage(void)
{
    printf("usage: GAME [options]\n"
           "  --caves DIR      the cave files C01.data ... (default: next to the program)\n"
           "  --start N        begin with cave CNN (hex, as its file: --start 0C)\n"
           "  --cave FILE      this cave file alone, again and again (to try one out)\n"
           "  --seed N         random caves, beginning with number N (1-9999)\n"
           "  --export FILE    write the random cave --seed N as a cave file, to edit, and stop\n"
           "  --scale N        window scale (default 3)\n"
           "  --fullscreen     start full screen (F11 toggles)\n"
           "  --nosound        no sound\n"
           "  --record FILE    save the keys pressed and the luck: --script FILE plays it again\n"
           "keys: arrows = move, Control = fire, Z = back in time (held), Space = pause,\n"
           "      Esc = give the cave up (a life;"
 " after GAME OVER: quit), F2 = a new game (C01),\n"
           "      F3 = a new game in a random cave,"
 " M = sound, T = time on/off, F11 = full screen;\n"
           "      the menu (a game's first cave): left/right, fire\n"
           "testing: --headless --frames N --script FILE --shot FILE.bmp --map FILE.bmp (the whole cave)\n"
           "         --wav FILE.wav (the sound)\n");
}

int main(int argc, char **argv)
{
    unsigned seed = 0;
    int scale = 3, fullscreen = 0, nosound = 0, start = 1, audio = 0;
    const char *shot = NULL, *map = NULL, *export = NULL;
    static char dir[1024];
    snprintf(dir, sizeof dir, "%s", argv[0]);
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bs = strrchr(dir, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
#endif
    if (slash) *slash = 0;
    else snprintf(dir, sizeof dir, ".");
    cave_dir = dir;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--seed") && v) seed = (unsigned)atoi(argv[++i]);
        else if (!strcmp(a, "--cave") && v) cave_single = argv[++i];
        else if (!strcmp(a, "--caves") && v) cave_dir = argv[++i];
        else if (!strcmp(a, "--start") && v) start = (int)strtol(argv[++i], NULL, 16);
        else if (!strcmp(a, "--export") && v) export = argv[++i];
        else if (!strcmp(a, "--scale") && v) scale = atoi(argv[++i]);
        else if (!strcmp(a, "--fullscreen")) fullscreen = 1;
        else if (!strcmp(a, "--nosound")) nosound = 1;
        else if (!strcmp(a, "--headless")) headless = 1;
        else if (!strcmp(a, "--frames") && v) max_frames = atol(argv[++i]);
        else if (!strcmp(a, "--script") && v) script = fopen(argv[++i], "r");
        else if (!strcmp(a, "--record") && v) { rec = fopen(argv[++i], "w"); if (!rec) { perror(argv[i]); return 1; } }
        else if (!strcmp(a, "--shot") && v) shot = argv[++i];
        else if (!strcmp(a, "--map") && v) map = argv[++i];
        else if (!strcmp(a, "--wav") && v) { wav = fopen(argv[++i], "wb"); if (wav) wav_header(); }
        else { usage(); return strcmp(a, "--help") ? 1 : 0; }
    }
    char sprites[1100];
    snprintf(sprites, sizeof sprites, "%s/%s", dir, SHEET);
    if (!sprites_load(sprites)) { fprintf(stderr, "%s: can not be read (the pictures, 200 x 100)\n", sprites); return 1; }
    if (export) {
        Cave c;
        cave_random(&c, seed ? seed : random_seed());
        if (!cave_save(&c, export)) { fprintf(stderr, "%s: can not be written\n", export); return 1; }
        printf("%s: random cave %u\n", export, c.seed);
        return 0;
    }
    cave_count_files();
    if (cave_single) fprintf(stderr, "caves: %s alone\n", cave_single);
    else fprintf(stderr, "caves: %d in %s%s\n", cave_count, cave_dir, cave_count ? "" : " - random caves only");
    if (start < 1 || start > cave_count) start = 1;
    if (!headless) game_luck = (unsigned)time(NULL) * 2654435761u ^ (unsigned)clock();
    script_next();
    while (script_frame == 0 && (!strcmp(script_cmd, "luck") || !strcmp(script_cmd, "seed"))) {
        if (script_cmd[0] == 'l') game_luck = (unsigned)strtoul(script_arg, NULL, 10);
        else pending_seed = (unsigned)strtoul(script_arg, NULL, 10);
        script_next();
    }
    if (rec) fprintf(rec, "0 luck %u\n", game_luck);
    game_new(&game, cave_count && !seed ? start : 0, seed ? seed : random_seed());

    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
            fprintf(stderr, "SDL: %s\n", SDL_GetError());
            return 1;
        }
        win = SDL_CreateWindow("GAME", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               SCREEN_W * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
        if (!nosound) audio = sound_init();
    }
    uint64_t freq = headless ? 1 : SDL_GetPerformanceFrequency(), next = headless ? 0 : SDL_GetPerformanceCounter();
    while (!quit_req && (max_frames < 0 || frame_no < max_frames)) {
        if (!headless) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) quit_req = 1;
                else if ((e.type == SDL_KEYDOWN && !e.key.repeat) || e.type == SDL_KEYUP) {
                    key(e.key.keysym.sym, e.type == SDL_KEYDOWN);
                    record(e.key.keysym.sym, e.type == SDL_KEYDOWN);
                }
            }
        }
        run_script();
        int fire = fire_key;
        if (back_key) back_frames++;
        else back_frames = 0;
        int back = back_frames == 1 ? 1 : back_frames < 16 ? 0 : back_frames < 136 ? back_frames % 2 : 2;
        if (!back_key || !game_back(&game, back)) game_frame(&game, direction(), fire);
        frame_no++;
        if (!headless || wav) {
            int n;
            const int16_t *s = sound_frame(&n);
            if (wav) { fwrite(s, 2, (size_t)n, wav); wav_samples += n; }
        }
        if (headless) continue;
        draw(&game, fb);
        SDL_UpdateTexture(tex, NULL, fb, SCREEN_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        if (audio) sound_wait();
        else {
            uint64_t now = SDL_GetPerformanceCounter();
            if (now > next + freq / 4) next = now;
            next += freq / FPS;
            while ((now = SDL_GetPerformanceCounter()) < next) {
                uint64_t ms = (next - now) * 1000 / freq;
                if (ms > 1) SDL_Delay((Uint32)(ms - 1));
            }
        }
    }
    if (shot) save_shot(shot);
    if (map) save_map(map);
    if (wav) { wav_header(); fclose(wav); }
    if (rec) { fprintf(rec, "%ld quit\n", frame_no > 0 ? frame_no - 1 : 0); fclose(rec); }
    if (!headless) SDL_Quit();
    return 0;
}
