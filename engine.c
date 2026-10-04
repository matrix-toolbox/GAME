#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

#define TICK 7
#define BIRTH_TICKS 14
#define PASSES 120
#define PASSES_5_FRAMES 12
#define ROW_FRAMES 2
#define COVER_FRAMES 10
#define GLITTER 128
#define LIFE_POINTS 512
#define MAX_LIVES 9

static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};

#define PAST_MAX 32768
#define PART (sizeof(Game) - offsetof(Game, el))
static uint8_t *past;
static int past_n, past_cap;
unsigned game_luck;

static void camera(Game *g);

static unsigned rnd(Game *g)
{
    g->rnd = g->rnd * 1103515245u + 12345u;
    return (g->rnd >> 16) & 0x7FFF;
}

static void points(Game *g, int n)
{
    for (; n > 0; n--)
        if (++g->score % LIFE_POINTS == 0) {
            if (g->lives < MAX_LIVES) g->lives++;
            g->noise = GLITTER;
        }
}

static void start_cave(Game *g)
{
    past_n = 0;
    if (g->number) {
        char path[1024];
        cave_path(g->number, path, sizeof path);
        if (!cave_load(&g->cave, path, g->where, g->why)) {
            g->phase = PH_INVALID;
            g->phase_frames = 0;
            g->latch_fire = g->tap_fire = 0;
            sound_reset();
            return;
        }
    } else if (g->cave.seed != g->seed) cave_random(&g->cave, g->seed);
    memcpy(g->el, g->cave.el, sizeof g->el);
    memcpy(g->aux, g->cave.aux, sizeof g->aux);
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            if (g->el[y][x] == DIAMOND && !g->aux[y][x]) g->aux[y][x] = (uint8_t)(1 + rnd(g) % 3);
    g->rx = g->ry = 1;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            if (g->el[y][x] == INBOX) g->rx = x, g->ry = y;
    memset(g->covered, 1, sizeof g->covered);
    g->passes = 0;
    g->phase = PH_UNCOVER;
    g->phase_frames = g->tick_frames = 0;
    sound_reset();
    sound_cover(1);
    g->birth_ticks = BIRTH_TICKS;
    g->collected = g->exit_open = g->flash = 0;
    g->time_left = g->cave.time;
    g->second_frames = g->played_seconds = 0;
    g->amoeba_room = 0;
    g->exit_x = g->exit_y = g->exit_frame = -1;
    g->walking = g->facing_left = 0;
    g->facing = RIGHT;
    g->cam_x = -1;
    camera(g);
    g->latch_dir = -1;
    g->latch_fire = g->tap_fire = 0;
    g->rnd = g->rnd * 2654435761u + game_luck + 1;
}

static void menu(Game *g)
{
    if (g->phase != PH_UNCOVER) return;
    g->phase = PH_MENU;
    sound_cover(0);
}

void game_new(Game *g, int number, unsigned seed)
{
    memset(g, 0, sizeof *g);
    g->number = g->start_number = number;
    g->seed = g->start_seed = seed;
    g->latch_dir = -1;
    g->lives = 3;
    start_cave(g);
    menu(g);
}

static void cover_screen(Game *g)
{
    sound_reset();
    sound_cover(1);
    memset(g->cover_q, 0, sizeof g->cover_q);
    g->passes = 0;
    g->phase = PH_COVER;
    g->phase_frames = 0;
}

static void covering(Game *g)
{
    int passes = (g->phase_frames * CW * 2) / COVER_FRAMES;
    for (; g->passes < passes && g->passes < CW * 2; g->passes++)
        for (int y = 0; y < CH * 2; y++) {
            int x = rnd(g) % (CW * 2);
            while (g->cover_q[y][x]) x = (x + 1) % (CW * 2);
            g->cover_q[y][x] = 1;
        }
    if (g->passes >= CW * 2) start_cave(g);
}

static void next_cave(Game *g)
{
    if (cave_single) ;
    else if (g->number && g->number < cave_count) g->number++;
    else if (g->number) g->number = 0;
    else g->seed = g->seed % 9999 + 1;
    cover_screen(g);
}

static void kill_rockford(Game *g)
{
    if (g->phase != PH_PLAY) return;
    g->lives--;
    g->phase = g->lives > 0 ? PH_DEAD : PH_OVER;
    g->phase_frames = 0;
}

static void explode_into(Game *g, int x, int y, int diamonds)
{
    for (int j = y - 1; j <= y + 1; j++)
        for (int i = x - 1; i <= x + 1; i++) {
            if (i < 0 || j < 0 || i >= CW || j >= CH) continue;
            int e = g->el[j][i];
            if (e == STEEL || e == INBOX) continue;
            if (e == ROCKFORD) kill_rockford(g);
            g->el[j][i] = EXPLOSION;
            g->aux[j][i] = (uint8_t)((i == x && j == y ? 8 : 0) |
                                     (diamonds && !g->cave.target[j][i] ? (1 + rnd(g) % 3) << 4 : 0));
            g->done[j][i] = 1;
        }
    sound_play(SND_EXPLODE);
}

static void explode(Game *g, int x, int y) { explode_into(g, x, y, 0); }

void game_give_up(Game *g)
{
    if (g->phase == PH_PLAY) explode(g, g->rx, g->ry);
}

static void collect(Game *g)
{
    g->collected++;
    points(g, 1);
    sound_play(SND_DIAMOND);
}

static void check_exit(Game *g)
{
    g->targets_left = g->loose_boulders = 0;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            if (g->cave.target[y][x] && g->el[y][x] != BOULDER) g->targets_left++;
            if (!g->cave.target[y][x] && g->el[y][x] == BOULDER) g->loose_boulders++;
        }
    int open = g->collected >= g->cave.diamonds && !g->targets_left && !g->loose_boulders;
    if (open && !g->exit_open) {
        g->flash = 6;
        sound_play(SND_EXIT);
    }
    g->exit_open = open;
}

static void enter_exit(Game *g, int x, int y, int nx, int ny, int snapped)
{
    g->exit_x = nx, g->exit_y = ny, g->exit_frame = g->frame;
    if (snapped) g->el[ny][nx] = SPACE;
    else {
        g->el[y][x] = SPACE;
        g->el[ny][nx] = ROCKFORD;
        g->done[ny][nx] = 1;
        g->rx = nx, g->ry = ny;
    }
    g->phase = PH_BONUS;
    g->phase_frames = 0;
    sound_play(SND_BONUS_START);
}

static int push(Game *g, int x, int y, int dir)
{
    int nx = x + DX[dir], ny = y + DY[dir];
    if (IS_STONE(g->el[y][x]) && g->aux[y][x]) return 0;
    if (g->el[ny][nx] != SPACE || rnd(g) % 4) return 0;
    g->el[ny][nx] = g->el[y][x];
    g->aux[ny][nx] = 0;
    g->done[ny][nx] = 1;
    g->el[y][x] = SPACE;
    g->aux[y][x] = 0;
    sound_play(SND_PUSH);
    return 1;
}

static void move_rockford(Game *g, int x, int y, int nx, int ny)
{
    g->el[y][x] = SPACE;
    g->el[ny][nx] = ROCKFORD;
    g->done[ny][nx] = 1;
    g->rx = nx, g->ry = ny;
}

static void rockford(Game *g, int x, int y, int dir, int fire)
{
    if (dir < 0) return;
    int nx = x + DX[dir], ny = y + DY[dir], e = g->el[ny][nx];
    if (fire) {
        if (e == DIRT) { g->el[ny][nx] = SPACE; sound_play(SND_DIRT); }
        else if (e == DIAMOND) { g->el[ny][nx] = SPACE; collect(g); }
        else if (e == EXIT && g->exit_open) enter_exit(g, x, y, nx, ny, 1);
        else if (e == BOULDER || e == EXIT) push(g, nx, ny, dir);
        else if (IS_STONE(e) && !g->aux[ny][nx]) {
            g->aux[ny][nx] = (uint8_t)(1 + dir);
            g->done[ny][nx] = 1;
            sound_play(SND_PUSH);
        }
        return;
    }
    switch (e) {
    case DIRT: sound_play(SND_DIRT); move_rockford(g, x, y, nx, ny); break;
    case SPACE: sound_play(SND_SPACE); move_rockford(g, x, y, nx, ny); break;
    case DIAMOND: collect(g); move_rockford(g, x, y, nx, ny); break;
    case BOULDER: case STONE_R: case STONE_G: case STONE_B:
        if (push(g, nx, ny, dir)) move_rockford(g, x, y, nx, ny);
        break;
    case EXIT:
        if (g->exit_open) enter_exit(g, x, y, nx, ny, 0);
        else if (push(g, nx, ny, dir)) move_rockford(g, x, y, nx, ny);
        break;
    }
}

static void firefly(Game *g, int x, int y)
{
    for (int d = 0; d < 4; d++) {
        int e = g->el[y + DY[d]][x + DX[d]];
        if (e == ROCKFORD || e == AMOEBA) { explode_into(g, x, y, 1); return; }
    }
    int dir = g->aux[y][x], turn = (dir + 3) & 3, other = (dir + 1) & 3;
    int go = g->el[y + DY[turn]][x + DX[turn]] == SPACE ? turn : g->el[y + DY[dir]][x + DX[dir]] == SPACE ? dir : -1;
    if (go < 0) { g->aux[y][x] = (uint8_t)other; return; }
    int nx = x + DX[go], ny = y + DY[go];
    g->el[ny][nx] = g->el[y][x];
    g->aux[ny][nx] = (uint8_t)go;
    g->done[ny][nx] = 1;
    g->el[y][x] = SPACE;
}

static int rings_of(int e)
{
    static const int R[] = {1, 2, 4, 3, 5, 6};
    return IS_STONE(e) ? R[e - STONE_R] : 0;
}

static int stone_of(int rings)
{
    static const int E[8] = {0, STONE_R, STONE_G, STONE_RG, STONE_B, STONE_RB, STONE_GB, FIREFLY};
    return E[rings & 7];
}

static int beats(int mover, int e)
{
    int a = rings_of(mover), b = rings_of(e);
    return a && b && (a & -a) > b;
}

static void stone(Game *g, int x, int y)
{
    int m = g->aux[y][x];
    if (!m) return;
    int dir = m - 1, s = g->el[y][x], nx = x + DX[dir], ny = y + DY[dir], e = g->el[ny][nx];
    if (e == SPACE || beats(s, e)) {
        if (e != SPACE) {
            int ax = nx + DX[dir], ay = ny + DY[dir], beyond = g->el[ay][ax];
            if (beyond != SPACE && !beats(s, beyond)) {
                int j = stone_of(rings_of(s) | rings_of(e));
                g->el[ny][nx] = (uint8_t)j;
                g->aux[ny][nx] = (uint8_t)(j == FIREFLY ? dir : 0);
                g->done[ny][nx] = 1;
                g->el[y][x] = SPACE;
                g->aux[y][x] = 0;
                sound_play(j == FIREFLY ? SND_BIRTH : SND_PUSH);
                return;
            }
            sound_play(SND_CRUSH);
        }
        g->el[ny][nx] = (uint8_t)s;
        g->aux[ny][nx] = (uint8_t)m;
        g->done[ny][nx] = 1;
        g->el[y][x] = SPACE;
        g->aux[y][x] = 0;
    } else if (e == FIREFLY) explode_into(g, nx, ny, 1);
    else {
        g->aux[y][x] = 0;
        sound_play(SND_PUSH);
    }
}

static void amoeba(Game *g, int x, int y, int *cells, int *room)
{
    (*cells)++;
    for (int d = 0; d < 4 && !*room; d++) {
        int e = g->el[y + DY[d]][x + DX[d]];
        if (e == SPACE || e == DIRT) *room = 1;
    }
    int fast = g->played_seconds >= g->cave.amoeba_time;
    if ((int)(rnd(g) % 128) >= (fast ? 32 : 4)) return;
    int d = rnd(g) % 4, nx = x + DX[d], ny = y + DY[d], e = g->el[ny][nx];
    if (e != SPACE && e != DIRT) return;
    g->el[ny][nx] = AMOEBA;
    g->done[ny][nx] = 1;
}

static void tick(Game *g, int dir, int fire)
{
    int cells = 0, room = 0;
    memset(g->done, 0, sizeof g->done);
    g->flash_toggle++;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            if (g->done[y][x]) continue;
            switch (g->el[y][x]) {
            case ROCKFORD: if (g->phase == PH_PLAY) rockford(g, x, y, dir, fire); break;
            case FIREFLY: firefly(g, x, y); break;
            case AMOEBA: amoeba(g, x, y, &cells, &room); break;
            case STONE_R: case STONE_G: case STONE_B: case STONE_RG: case STONE_RB: case STONE_GB:
                stone(g, x, y);
                break;
            case EXPLOSION:
                if ((++g->aux[y][x] & 7) >= 5) {
                    int colour = g->aux[y][x] >> 4 & 3;
                    g->el[y][x] = colour ? DIAMOND : SPACE;
                    g->aux[y][x] = (uint8_t)colour;
                }
                break;
            case INBOX:
                if (g->birth_ticks > 0) { g->birth_ticks--; break; }
                if (g->aux[y][x] == 0) sound_play(SND_BIRTH);
                if (++g->aux[y][x] > 3) {
                    g->el[y][x] = ROCKFORD;
                    g->aux[y][x] = 0;
                    g->done[y][x] = 1;
                    g->phase = PH_PLAY;
                }
                break;
            }
        }
    if (cells && (cells >= g->cave.amoeba_limit || (!room && g->amoeba_room))) {
        int big = cells >= g->cave.amoeba_limit;
        for (int y = 0; y < CH; y++)
            for (int x = 0; x < CW; x++)
                if (g->el[y][x] == AMOEBA) {
                    g->el[y][x] = (uint8_t)(big ? STONE_R + rnd(g) % 3 : DIAMOND);
                    g->aux[y][x] = big ? 0 : (uint8_t)(1 + rnd(g) % 3);
                }
        cells = 0;
    }
    g->amoeba_room = room;
    check_exit(g);
    sound_tick(cells > 0);
}

static void uncover(Game *g)
{
    int f = ++g->phase_frames, passes = f * PASSES_5_FRAMES / 5;
    for (; g->passes < passes && g->passes < PASSES; g->passes++)
        for (int y = 0; y < CH; y++) g->covered[y][rnd(g) % CW] = 0;
    if (g->passes < PASSES) return;
    int rows = (f - PASSES * 5 / PASSES_5_FRAMES) / ROW_FRAMES;
    for (int y = 0; y < CH && y <= rows; y++) memset(g->covered[y], 0, CW);
    if (rows < CH) return;
    sound_cover(0);
    g->phase = PH_BIRTH;
    g->phase_frames = 0;
    check_exit(g);
}

static void camera(Game *g)
{
    int px = g->rx * CELL + CELL / 2, py = g->ry * CELL + CELL / 2, vh = VIEW_H * CELL;
    int wx = px - SCREEN_W / 2, wy = py - vh / 2, mx = CW * CELL - SCREEN_W, my = CH * CELL - vh;
    wx = wx < 0 ? 0 : wx > mx ? mx : wx;
    wy = wy < 0 ? 0 : wy > my ? my : wy;
    if (g->cam_x < 0) {
        g->cam_x = (wx + CELL / 2) / CELL * CELL, g->cam_y = (wy + CELL / 2) / CELL * CELL;
        g->scroll_x = g->scroll_y = 0;
        return;
    }
    if (px - g->cam_x < 5 * CELL || px - g->cam_x > SCREEN_W - 5 * CELL) g->scroll_x = 1;
    if (py - g->cam_y < 3 * CELL || py - g->cam_y > vh - 3 * CELL) g->scroll_y = 1;
    if (g->scroll_x) {
        int d = wx - g->cam_x;
        g->cam_x += d > 4 ? 4 : d < -4 ? -4 : d;
        if (g->cam_x == wx) g->scroll_x = 0;
    }
    if (g->scroll_y) {
        int d = wy - g->cam_y;
        g->cam_y += d > 4 ? 4 : d < -4 ? -4 : d;
        if (g->cam_y == wy) g->scroll_y = 0;
    }
}

static void past_keep(const Game *g)
{
    if (past_n == PAST_MAX) {
        memmove(past, past + PAST_MAX / 2 * PART, PAST_MAX / 2 * PART);
        past_n = PAST_MAX / 2;
    }
    if (past_n == past_cap) {
        int cap = past_cap ? past_cap * 2 : 1024;
        uint8_t *p = realloc(past, (size_t)cap * PART);
        if (!p) return;
        past = p, past_cap = cap;
    }
    memcpy(past + (size_t)past_n++ * PART, (const uint8_t *)g + offsetof(Game, el), PART);
}

int game_back(Game *g, int ticks)
{
    if (g->phase != PH_PLAY || g->paused) return 0;
    int fire_was = g->fire_was, tap_fire = g->tap_fire, pick = g->pick;
    unsigned luck = g->rnd;
    for (; ticks > 0 && past_n > 0; ticks--)
        memcpy((uint8_t *)g + offsetof(Game, el), past + (size_t)--past_n * PART, PART);
    g->fire_was = fire_was, g->tap_fire = tap_fire, g->pick = pick, g->rnd = luck;
    g->latch_dir = -1, g->latch_fire = 0;
    return 1;
}

void game_press(Game *g, int dir)
{
    if (dir >= 0) g->latch_dir = dir;
    else g->latch_fire = g->tap_fire = 1;
    if (dir == LEFT || dir == RIGHT) g->pick = dir == LEFT ? -1 : 1;
}

void game_frame(Game *g, int dir, int fire)
{
    int pressed = (fire && !g->fire_was) || g->tap_fire;
    g->fire_was = fire;
    g->tap_fire = 0;
    if (fire) g->latch_fire = 1;
    if (g->paused) return;
    if (g->phase == PH_PLAY && g->tick_frames == TICK - 1) past_keep(g);
    g->frame++;
    g->cover_frame++;
    if (g->phase != PH_UNCOVER) g->phase_frames++;
    if (g->noise) {
        g->noise--;
        for (int q = 0; q < 4; q++)
            for (int r = 0; r < 2; r++) g->glitter[q][r] = g->noise ? (uint8_t)rnd(g) : 0;
    }
    if (g->flash) g->flash--;
    if (g->phase == PH_PLAY) {
        g->walking = dir >= 0;
        if (dir >= 0) g->facing = dir;
        if (dir == LEFT) g->facing_left = 1;
        if (dir == RIGHT) g->facing_left = 0;
    } else if (g->phase != PH_BONUS) g->walking = 0;
    if ((g->frame & 1) == 0) {
        g->blink = rnd(g) % 4 == 0;
        if (rnd(g) % 16 == 0) g->tap ^= 1;
    }
    int pick = g->pick;
    g->pick = 0;
    if (pick && (g->phase == PH_MENU || g->phase == PH_INVALID)) {
        if (g->number) g->number = (g->number - 1 + pick + cave_count) % cave_count + 1;
        else g->seed = (g->seed - 1 + 9999 + (unsigned)pick) % 9999 + 1;
        g->start_number = g->number, g->start_seed = g->seed;
        start_cave(g);
        menu(g);
        return;
    }
    switch (g->phase) {
    case PH_COVER:
        covering(g);
        return;
    case PH_MENU:
        camera(g);
        if (pressed) { g->phase = PH_UNCOVER; g->phase_frames = 0; sound_cover(1); }
        return;
    case PH_INVALID:
        if (pressed) start_cave(g);
        return;
    case PH_UNCOVER:
        uncover(g);
        camera(g);
        return;
    case PH_BONUS:
        if (g->time_left > 0) {
            sound_time(--g->time_left);
            points(g, 1);
            sound_play(SND_BONUS);
            if (!g->time_left) g->phase_frames = 0;
        } else if (g->phase_frames > 44) next_cave(g);
        camera(g);
        return;
    case PH_DEAD:
        if (pressed && g->phase_frames > FPS) { cover_screen(g); return; }
        break;
    case PH_OVER:
        if (pressed && g->phase_frames > FPS) {
            game_new(g, g->start_number, g->start_seed);
            return;
        }
        break;
    case PH_PLAY:
        if (++g->second_frames >= FPS) {
            g->second_frames = 0;
            g->played_seconds++;
            if (g->time_left > 0) sound_time(--g->time_left);
            if (g->time_left == 0) explode(g, g->rx, g->ry);
        }
        break;
    }
    if (++g->tick_frames >= TICK) {
        g->tick_frames = 0;
        tick(g, dir >= 0 ? dir : g->latch_dir, fire || g->latch_fire);
        g->latch_dir = -1;
        g->latch_fire = 0;
    }
    camera(g);
}
