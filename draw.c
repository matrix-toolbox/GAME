#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "palette.h"
#include "font8x8_basic.h"

#define SHEET_W (12 * CELL)
#define SHEET_H (5 * CELL)
#define CLEAR 0x00FF00FFu
static uint32_t sheet[SHEET_W * SHEET_H];
static int own_colours;
static uint32_t gem_tone[3];
static uint8_t target_core[CELL][CELL];

int sprites_load(const char *path)
{
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) return 0;
    SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s);
    if (!c) return 0;
    int ok = c->w >= SHEET_W && c->h >= SHEET_H;
    for (int y = 0; ok && y < SHEET_H; y++)
        memcpy(sheet + y * SHEET_W, (uint8_t *)c->pixels + y * c->pitch, SHEET_W * 4);
    SDL_FreeSurface(c);
    for (int i = 0; i < SHEET_W * SHEET_H; i++) sheet[i] &= 0xFFFFFFu;
    own_colours = ok && sheet[11 * CELL] != CLEAR;
    if (ok) {
        int seen[CELL][CELL] = {{0}}, best = -1;
        double best_d = 1e9;
        memset(target_core, 0, sizeof target_core);
        for (int y0 = 0; y0 < CELL; y0++)
            for (int x0 = 0; x0 < CELL; x0++) {
                uint32_t c = sheet[y0 * SHEET_W + 6 * CELL + x0];
                if (c == CLEAR || c == 0 || seen[y0][x0]) continue;
                int stack[CELL * CELL][2], n = 0, id = y0 * CELL + x0 + 1;
                double d = 1e9;
                stack[n][0] = x0, stack[n][1] = y0, n++, seen[y0][x0] = id;
                while (n) {
                    n--;
                    int x = stack[n][0], y = stack[n][1];
                    double dd = (x - 8.5) * (x - 8.5) + (y - 8.5) * (y - 8.5);
                    if (dd < d) d = dd;
                    static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (int k = 0; k < 4; k++) {
                        int nx = x + D[k][0], ny = y + D[k][1];
                        if (nx < 0 || ny < 0 || nx >= CELL || ny >= CELL || seen[ny][nx]) continue;
                        uint32_t nc = sheet[ny * SHEET_W + 6 * CELL + nx];
                        if (nc == CLEAR || nc == 0) continue;
                        seen[ny][nx] = id, stack[n][0] = nx, stack[n][1] = ny, n++;
                    }
                }
                if (d < best_d) best_d = d, best = id;
            }
        for (int y = 0; y < CELL; y++)
            for (int x = 0; x < CELL; x++) target_core[y][x] = best > 0 && seen[y][x] == best;
    }
    for (int k = 0; ok && k < 3; k++) {
        double sum[3] = {0}; int n = 0;
        for (int y = 0; y < CELL; y++)
            for (int x = 0; x < CELL; x++) {
                uint32_t c = sheet[(1 * CELL + y) * SHEET_W + (15 + k - 12) * CELL + x];
                if (c == CLEAR || (c & 0xF0F0F0) == 0) continue;
                sum[0] += c >> 16; sum[1] += c >> 8 & 255; sum[2] += c & 255; n++;
            }
        gem_tone[k] = n ? (uint32_t)(sum[0] / n) << 16 | (uint32_t)(sum[1] / n) << 8 | (uint32_t)(sum[2] / n) : 0x808080;
    }
    return ok;
}

static uint32_t pixel(int sprite, int x, int y)
{
    return sheet[((sprite / 12) * CELL + y) * SHEET_W + (sprite % 12) * CELL + x];
}

static unsigned hash(unsigned a, unsigned b, unsigned c)
{
    unsigned h = a * 2654435761u ^ b * 2246822519u ^ c * 3266489917u;
    return h ^ h >> 15;
}

static double lum(uint32_t c) { return 0.299 * (c >> 16) + 0.587 * (c >> 8 & 255) + 0.114 * (c & 255); }

static uint32_t tint(uint32_t c, uint32_t own, uint32_t to)
{
    if (!c) return 0;
    double f = lum(c) / lum(own);
    int r = (int)((to >> 16) * f), g = (int)((to >> 8 & 255) * f), b = (int)((to & 255) * f);
    return (uint32_t)((r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b));
}

static uint32_t brighter(uint32_t c)
{
    int r = (int)(c >> 16) * 3 / 2, g = (int)(c >> 8 & 255) * 3 / 2, b = (int)(c & 255) * 3 / 2;
    return (uint32_t)((r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b));
}

static uint32_t grey(uint32_t c)
{
    int l = (int)(lum(c) + 0.5);
    l = l > 255 ? 255 : l;
    return (uint32_t)(l << 16 | l << 8 | l);
}

static uint32_t mixc(uint32_t a, uint32_t b, double t)
{
    int r = (int)((a >> 16) * (1 - t) + (b >> 16) * t), gg = (int)((a >> 8 & 255) * (1 - t) + (b >> 8 & 255) * t);
    int bb = (int)((a & 255) * (1 - t) + (b & 255) * t);
    return (uint32_t)(r << 16 | gg << 8 | bb);
}

static uint32_t at_brightness(uint32_t tone, double l)
{
    double k = l / (lum(tone) > 1 ? lum(tone) : 1);
    int r = (int)((tone >> 16) * k), gg = (int)((tone >> 8 & 255) * k), bb = (int)((tone & 255) * k);
    return (uint32_t)((r > 255 ? 255 : r) << 16 | (gg > 255 ? 255 : gg) << 8 | (bb > 255 ? 255 : bb));
}

static uint32_t role[5];
#define STEEL_COLOUR 0x7C8A9E

static int role_of(int s, uint32_t *own)
{
    if (own_colours) return -1;
    switch (s) {
    case 1: *own = 0x2C364A; return 0;
    case 3: *own = 0x787878; return 4;
    case 4: *own = 0x8B6800; return 1;
    case 2: *own = 0x787878; return 2;
    default:
        if (s >= 39 && s <= 41) { *own = 0xA600B7; return 3; }
        return -1;
    }
}

static int blast_stage(int aux)
{
    int st = (aux & 7) - !(aux & 8);
    return st > 4 ? 4 : st;
}

static int sprite(const Game *g, int e, int aux, int x, int y)
{
    int flash = g->flash_toggle & 1, k = (g->frame / 8) % 3;
    switch (e) {
    case DIRT: return 1;
    case WALL: return 2;
    case STEEL: return 3;
    case BOULDER: return 4;
    case STONE_R: case STONE_G: case STONE_B: return 12 + e - STONE_R;
    case STONE_RG: case STONE_RB: case STONE_GB: return 18 + e - STONE_RG;
    case DIAMOND: return 15 + (aux ? aux - 1 : (x * 7 + y * 3) % 3);
    case FIREFLY: return 36 + k;
    case AMOEBA: return 0;
    case EXIT: return g->exit_open && flash ? 5 : 3;
    case INBOX: return aux ? 48 + aux - 1 : flash ? 5 : 3;
    case EXPLOSION: {
        int st = blast_stage(aux);
        return st < 0 ? 0 : 48 + st;
    }
    case ROCKFORD: return (g->walking && (g->frame / 4) % 2 ? 28 : 24) + g->facing;
    default: return 0;
    }
}

static void put_cell(const Game *g, uint32_t *fb, int fw, int top, int bottom, int sx, int sy, int x, int y)
{
    int e = g->el[y][x];
    int covered = g->covered[y][x] || (g->phase == PH_COVER && g->cover_q[y * 2][x * 2]);
    int s = covered ? 3 : sprite(g, e, g->aux[y][x], x, y);
    int vanish = -1;
    if (g->exit_x == x && g->exit_y == y && !covered && (g->phase == PH_BONUS || g->phase == PH_COVER))
        vanish = g->frame - g->exit_frame, s = 5;
    uint32_t own = 0;
    int r0 = role_of(s, &own);
    int blink = covered && hash((unsigned)x, (unsigned)y, (unsigned)g->frame / 8) % 9 == 0;
    int target = g->cave.target[y][x] && !covered && e != FIREFLY;
    int boxed = target && e == BOULDER, core_on = (g->frame / 10) % 2 == 0;
    int piece[3][3] = {{0}};
    if (e == AMOEBA && !covered)
        for (int by = 0; by < 3; by++)
            for (int bx = 0; bx < 3; bx++) {
                unsigned h = hash((unsigned)x, (unsigned)y, (unsigned)(by * 3 + bx));
                int period = 6 + (int)(h % 7);
                piece[by][bx] = by == 1 && bx == 1 ? -1 : 39 + (int)((h / 7 + (unsigned)g->frame / (unsigned)period) % 3);
            }
    int gem = 0, reveal = 0;
    double sat = 0;
    if (e == EXPLOSION && !covered && (g->aux[y][x] >> 4 & 3)) {
        gem = 15 + (g->aux[y][x] >> 4 & 3) - 1;
        reveal = blast_stage(g->aux[y][x]) - 1;
        sat = blast_stage(g->aux[y][x]) / 4.0;
        sat = sat < 0 ? 0 : sat > 1 ? 1 : sat;
    }
    int rgb = !covered && (IS_STONE(e) || e == FIREFLY || e == DIAMOND);
    int glint = 0;
    if (e == DIAMOND && !covered) {
        int t = (g->frame + (int)(hash((unsigned)x, (unsigned)y, 7) % 97)) % 97;
        glint = t < 4 ? 7 : t < 8 ? 8 : t < 12 ? 7 : 0;
    }
    for (int r = 0; r < CELL; r++) {
        int py = sy + r;
        if (py < top || py >= bottom) continue;
        for (int p = 0; p < CELL; p++) {
            int px = sx + p;
            if (px < 0 || px >= fw) continue;
            uint32_t c = pixel(s, p, r);
            int void_px = e == SPACE && vanish < 0;
            if (vanish >= 0) {
                int ring = 9 - (abs(p - 9) > abs(r - 9) ? abs(p - 9) : abs(r - 9));
                double f = 1 - (vanish - ring * 2) / 4.0;
                f = f < 0 ? 0 : f > 1 ? 1 : f;
                c = c == CLEAR ? 0 : (uint32_t)((int)((c >> 16) * f) << 16 | (int)((c >> 8 & 255) * f) << 8 | (int)((c & 255) * f));
                void_px = f <= 0;
            }
            if (e == AMOEBA && !covered) {
                int pc = piece[r / 6][p / 6];
                c = pc < 0 ? 0 : pixel(pc, p % 6, r % 6);
                if (pc >= 0 && c != CLEAR && !own_colours) c = tint(c, 0xA600B7, role[3]);
            }
            int of_gem = 0;
            if (c == CLEAR && reveal > 0 && hash((unsigned)(x * CELL + p), (unsigned)(y * CELL + r), 3) % 3 < (unsigned)reveal)
                c = pixel(gem, p, r), of_gem = 1;
            if (c == CLEAR) c = 0;
            if (r0 >= 0) c = tint(c, own, role[r0]);
            if (blink) c = brighter(c);
            if (!covered) {
                if (void_px && g->noise && r % 3 == 1) {
                    uint32_t G[4] = {0, role[0], role[1], role[2]};
                    c = G[hash((unsigned)g->frame, (unsigned)r, (unsigned)p / 2) >> 30];
                }
                if (glint && pixel(glint, p, r) != CLEAR) c = pixel(glint, p, r);
                if (gem && of_gem) c = mixc(grey(c), c, sat);
                else if (gem && c) c = mixc(grey(c), at_brightness(gem_tone[gem - 15], lum(c)), sat);
                else if (!rgb) c = grey(c);
                uint32_t t = target ? pixel(6, p, r) : CLEAR;
                if (boxed && !(target_core[r][p] && core_on)) t = CLEAR;
                if (t != CLEAR) {
                    t = grey(t);
                    c = IS_STONE(e) && c ? ((c & 0xFEFEFE) >> 1) + ((t & 0xFEFEFE) >> 1) : t;
                }
                if (g->flash && c == 0) c = 0xC8C8C8;
            } else c = grey(c);
            fb[py * fw + px] = 0xFF000000u | c;
        }
    }
}

static void colours(const Game *g)
{
    if (own_colours) {
        for (int i = 0; i < 3; i++) role[i] = grey(pixel(11, 2 + i, 0));
        return;
    }
    for (int i = 0; i < 3; i++)
        role[i] = measured_palette[(g->cave.colors[i] & 0x0E) >> 1] & 0xFFFFFF;
    int l = g->cave.colors[3] & 0x0E;
    role[3] = measured_palette[(l < 0x0A ? 0x0A : l) >> 1] & 0xFFFFFF;
    role[4] = grey(STEEL_COLOUR);
}

static void line(uint32_t *fb, const char *text, const char *m)
{
    for (int i = 0; i < 20 && text[i]; i++) {
        unsigned char ch = (unsigned char)text[i] < 128 ? (unsigned char)text[i] : '?';
        int mark = m && i < (int)strlen(m) && m[i] == '1';
        uint32_t c = own_colours ? 0xFF000000u | grey(pixel(11, mark, 0)) : mark ? 0xFFFFFFFFu : 0xFFB4B4B4u;
        for (int r = 0; r < 8; r++) {
            uint8_t b = (uint8_t)font8x8_basic[ch][r];
            for (int p = 0; p < 8; p++)
                if (b & (1 << p)) fb[r * SCREEN_W + i * 18 + 1 + p * 2] = fb[r * SCREEN_W + i * 18 + 2 + p * 2] = c;
        }
    }
}

void draw_map(const Game *g, uint32_t *fb)
{
    colours(g);
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) put_cell(g, fb, CW * CELL, 0, CH * CELL, x * CELL, y * CELL, x, y);
}

static void say(uint32_t *fb, int row, const char *s)
{
    int n = (int)strlen(s) > 40 ? 40 : (int)strlen(s), x0 = (SCREEN_W - n * 8) / 2;
    for (int i = 0; i < n; i++)
        for (int r = 0; r < 8; r++)
            for (int p = 0; p < 8; p++)
                if ((unsigned char)s[i] < 128 && font8x8_basic[(unsigned char)s[i]][r] & (1 << p))
                    fb[(row * 8 + r) * SCREEN_W + x0 + i * 8 + p] = 0xFFE0E0E0u;
}

static void invalid(const Game *g, uint32_t *fb)
{
    char lines[4][41] = {{0}};
    const char *p = g->why;
    int n = 0;
    while (*p && n < 4) {
        int l = (int)strlen(p);
        if (l > 40) { l = 40; while (l > 10 && p[l] != ' ') l--; }
        snprintf(lines[n++], 41, "%.*s", l, p);
        for (p += l; *p == ' '; p++) ;
    }
    int row = 8;
    say(fb, row++, "INVALID CAVE!");
    say(fb, row++, g->where);
    for (int i = 0; i < n; i++) say(fb, row++, lines[i]);
    say(fb, row + 1, "CHECK SPECIFICATION!");
    say(fb, row + 4, "fire: try again  left/right: other caves");
}

void draw(const Game *g, uint32_t *fb)
{
    colours(g);
    if (g->phase == PH_INVALID) {
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = 0xFF000000u;
        invalid(g, fb);
        return;
    }
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) fb[i] = 0xFF000000u;
    int cx = g->cam_x < 0 ? 0 : g->cam_x, cy = g->cam_y;
    for (int y = cy / CELL; y <= (cy + VIEW_H * CELL - 1) / CELL && y < CH; y++)
        for (int x = cx / CELL; x <= (cx + SCREEN_W - 1) / CELL && x < CW; x++)
            put_cell(g, fb, SCREEN_W, STATUS_H, SCREEN_H, x * CELL - cx, STATUS_H + y * CELL - cy, x, y);
    char text[32];
    if (g->phase == PH_OVER) {
        snprintf(text, sizeof text, " GAME OVER    %04ld ", g->score % 10000);
        line(fb, text, NULL);
    } else if (g->paused) line(fb, "       PAUSED", NULL);
    else if (g->phase == PH_MENU) {
        if (g->number) snprintf(text, sizeof text, "C:%02X      PRESS FIRE", g->number);
        else snprintf(text, sizeof text, "CAVE:%04u PRESS FIRE", g->cave.seed % 10000);
        line(fb, text, NULL);
    } else if (g->phase <= PH_BIRTH) {
        if (g->number) snprintf(text, sizeof text, "C:%02X", g->number);
        else snprintf(text, sizeof text, "CAVE:%04u", g->cave.seed % 10000);
        line(fb, text, NULL);
    } else {
        int left = g->cave.diamonds - g->collected;
        left = left < 0 ? 0 : left > 99 ? 99 : left;
        snprintf(text, sizeof text, "%02d %02d %02d %04d %04ld %d", left, g->loose_boulders % 100,
                 g->targets_left % 100, g->time_left % 10000, g->score % 10000, g->lives);
        line(fb, text, "11 11 11");
    }
}
