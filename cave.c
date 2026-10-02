#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

#define MAXB 12
static uint8_t used[CH][CW];
static uint8_t avoid[CH][CW];
static struct { int bx, by, cx, cy, tx, ty, p1x, p1y, p2x, p2y, bend, locked; } box_[MAXB];
static int nbox, nlocked, nvault, nthrow, throw_[MAXB][3], vault_diamonds, wanted_boxes, wanted_vaults;

static const uint8_t COLOURS[][5] = {
    {0xC4, 0x46, 0x4C, 0x04, 0x00}, {0x14, 0x56, 0x4C, 0x04, 0x00}, {0x04, 0x26, 0x2C, 0x00, 0x00},
    {0x34, 0x96, 0x7C, 0x04, 0x00}, {0x36, 0x76, 0x7C, 0x04, 0x00}, {0x26, 0x06, 0x0C, 0xD4, 0x00},
    {0x44, 0x76, 0xCC, 0xD4, 0x00}, {0x84, 0x46, 0x0C, 0xD4, 0x00}, {0x76, 0x36, 0x7C, 0xD4, 0x00},
    {0x14, 0x46, 0x3C, 0x36, 0x00}, {0xD4, 0x26, 0x5C, 0x72, 0x00}, {0x34, 0x76, 0x7C, 0x86, 0x00},
    {0xF4, 0xF6, 0x0C, 0xD4, 0x00}, {0x34, 0xA6, 0x4C, 0x36, 0x00}, {0x34, 0x06, 0xFC, 0xAA, 0x00},
};

static unsigned state;
static unsigned rnd(unsigned n)
{
    state = state * 1103515245u + 12345u;
    return ((state >> 16) & 0x7FFF) % n;
}

static int far_from(int x, int y, int x2, int y2, int d)
{
    return abs(x - x2) + abs(y - y2) >= d;
}

typedef struct { int x, y, e; } Cell;

static void turn(int *x, int *y, int rot)
{
    for (int r = 0; r < rot; r++) { int t = *x; *x = -*y; *y = t; }
}

static int put(Cave *c, const Cell *cell, int n, int bx, int by, int rot, int sx, int sy, int ex, int ey, int at[][2])
{
    for (int i = 0; i < n; i++) {
        int x = cell[i].x, y = cell[i].y;
        turn(&x, &y, rot);
        x += bx, y += by;
        if (x < 1 || y < 1 || x > CW - 2 || y > CH - 2 || used[y][x]) return 0;
        if (!far_from(x, y, sx, sy, 3) || !far_from(x, y, ex, ey, 2)) return 0;
        for (int k = 0; k < i; k++) if (at[k][0] == x && at[k][1] == y) return 0;
        at[i][0] = x, at[i][1] = y;
    }
    for (int i = 0; i < n; i++) {
        int x = at[i][0], y = at[i][1];
        c->el[y][x] = (uint8_t)cell[i].e;
        c->aux[y][x] = cell[i].e == FIREFLY ? LEFT : 0;
        used[y][x] = 1;
    }
    return 1;
}

static void margin(int bx, int by, int rot, int x0, int x1, int y0, int y1)
{
    for (int j = y0; j <= y1; j++)
        for (int i = x0; i <= x1; i++) {
            int x = i, y = j;
            turn(&x, &y, rot);
            x += bx, y += by;
            if (x >= 0 && y >= 0 && x < CW && y < CH) used[y][x] = 1;
        }
}

static int lock_cells(Cell *cell, int n)
{
    cell[n++] = (Cell){-2, -1, SPACE}; cell[n++] = (Cell){-2, 0, SPACE}; cell[n++] = (Cell){-2, 1, FIREFLY};
    cell[n++] = (Cell){-3, -1, DIRT}; cell[n++] = (Cell){-3, 0, DIRT}; cell[n++] = (Cell){-3, 1, DIRT};
    cell[n++] = (Cell){-2, 2, DIRT};
    cell[n++] = (Cell){-2, -2, STONE_R + (int)rnd(3)}; cell[n++] = (Cell){-2, -3, DIRT};
    return n;
}

static int place_free(Cave *c, int bx, int by, int rot, int sx, int sy, int ex, int ey)
{
    Cell cell[24];
    int at[24][2], n = 0, l1 = 2 + (int)rnd(4), l2 = 2 + (int)rnd(3), bend = rnd(2) ? (rnd(2) ? 1 : -1) : 0;
    cell[n++] = (Cell){0, 0, BOULDER};
    for (int i = 1; i <= l1; i++) cell[n++] = (Cell){i, 0, SPACE};
    int k1 = n, kt = l1, k2 = n;
    cell[n++] = (Cell){-1, 0, DIRT};
    if (bend) {
        for (int i = 1; i <= l2; i++) cell[n++] = (Cell){l1, bend * i, SPACE};
        kt = n - 1;
        k2 = n;
        cell[n++] = (Cell){l1, -bend, DIRT};
    }
    if (nbox >= MAXB || !put(c, cell, n, bx, by, rot, sx, sy, ex, ey, at)) return 0;
    typeof(box_[0]) *b = &box_[nbox++];
    b->bx = at[0][0], b->by = at[0][1], b->cx = at[l1][0], b->cy = at[l1][1];
    b->tx = at[kt][0], b->ty = at[kt][1], b->p1x = at[k1][0], b->p1y = at[k1][1];
    b->p2x = at[k2][0], b->p2y = at[k2][1], b->bend = bend != 0, b->locked = 0;
    c->target[b->ty][b->tx] = 1;
    return 1;
}

static int place_locked(Cave *c, int bx, int by, int rot, int sx, int sy, int ex, int ey)
{
    Cell cell[32];
    int at[32][2], n = 0, len = 2 + (int)rnd(4);
    cell[n++] = (Cell){0, 0, BOULDER};
    for (int i = 1; i <= len; i++) cell[n++] = (Cell){i, 0, SPACE};
    cell[n++] = (Cell){-1, 0, WALL}; cell[n++] = (Cell){0, -1, WALL};
    cell[n++] = (Cell){-1, -1, DIRT}; cell[n++] = (Cell){-1, 1, DIRT};
    int k0 = n;
    n = lock_cells(cell, n);
    if (nbox >= MAXB || nthrow >= MAXB || !put(c, cell, n, bx, by, rot, sx, sy, ex, ey, at)) return 0;
    for (int i = k0; i < n - 2; i++) avoid[at[i][1]][at[i][0]] = 1;
    margin(bx, by, rot, -3, -1, -2, 2);
    typeof(box_[0]) *b = &box_[nbox++];
    b->bx = at[0][0], b->by = at[0][1], b->cx = b->tx = at[len][0], b->cy = b->ty = at[len][1];
    b->locked = 1, b->bend = 0;
    c->target[b->ty][b->tx] = 1;
    throw_[nthrow][0] = at[n - 1][0], throw_[nthrow][1] = at[n - 1][1], throw_[nthrow][2] = rot, nthrow++;
    nlocked++;
    return 1;
}

static int place_vault(Cave *c, int bx, int by, int rot, int sx, int sy, int ex, int ey)
{
    Cell cell[40];
    int at[40][2], n = 0;
    for (int j = -1; j <= 2; j++)
        for (int i = -1; i <= 2; i++)
            cell[n++] = (Cell){i, j, i >= 0 && i <= 1 && j >= 0 && j <= 1 ? DIAMOND : WALL};
    int k0 = n;
    n = lock_cells(cell, n);
    if (nthrow >= MAXB || !put(c, cell, n, bx, by, rot, sx, sy, ex, ey, at)) return 0;
    for (int i = k0; i < n - 2; i++) avoid[at[i][1]][at[i][0]] = 1;
    margin(bx, by, rot, -3, 3, -3, 3);
    throw_[nthrow][0] = at[n - 1][0], throw_[nthrow][1] = at[n - 1][1], throw_[nthrow][2] = rot, nthrow++;
    vault_diamonds += 4;
    nvault++;
    return 1;
}

static void build(Cave *c, int *sx, int *sy)
{
    memset(c, 0, sizeof *c);
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            int r = (int)rnd(1000), e = DIRT;
            if (r < 100) e = SPACE;
            else if (r < 160) e = DIAMOND;
            else if (r < 295) e = WALL;
            else if (r < 315) e = STONE_R;
            else if (r < 330) e = STONE_G;
            else if (r < 340) e = STONE_B;
            c->el[y][x] = (uint8_t)e;
        }
    for (int n = 4 + rnd(5); n > 0; n--) {
        int x = 2 + rnd(CW - 4), y = 2 + rnd(CH - 4), len = 4 + rnd(12), across = rnd(2);
        for (int i = 0; i < len; i++, across ? x++ : y++)
            if (x > 0 && y > 0 && x < CW - 1 && y < CH - 1) c->el[y][x] = WALL;
    }
    *sx = 2 + rnd(6), *sy = 2 + rnd(CH - 4);
    int ex = CW - 3 - rnd(6), ey = 2 + rnd(CH - 4);
    for (int j = *sy - 1; j <= *sy + 1; j++)
        for (int i = *sx - 1; i <= *sx + 1; i++)
            if (c->el[j][i] != SPACE) c->el[j][i] = DIRT;
    c->el[*sy][*sx] = INBOX;
    c->el[ey][ex] = EXIT;
    nbox = nlocked = nvault = nthrow = vault_diamonds = 0;
    memset(used, 0, sizeof used);
    memset(avoid, 0, sizeof avoid);
    used[*sy][*sx] = used[ey][ex] = 1;
    wanted_boxes = 3 + (int)rnd(4);
    wanted_vaults = 1 + (int)rnd(2);
    int locked = 1 + (int)rnd(2);
    for (int tries = 0; tries < 3000 && nvault < wanted_vaults; tries++)
        place_vault(c, 3 + rnd(CW - 6), 3 + rnd(CH - 6), rnd(4), *sx, *sy, ex, ey);
    for (int tries = 0; tries < 3000 && nlocked < locked; tries++)
        place_locked(c, 2 + rnd(CW - 4), 2 + rnd(CH - 4), rnd(4), *sx, *sy, ex, ey);
    for (int tries = 0; tries < 3000 && nbox < wanted_boxes; tries++)
        place_free(c, 2 + rnd(CW - 4), 2 + rnd(CH - 4), rnd(4), *sx, *sy, ex, ey);
    for (int n = 3 + rnd(4); n > 0; n--) {
        int w = 3 + rnd(4), h = 1 + rnd(3), x = 2 + rnd(CW - 4 - w), y = 2 + rnd(CH - 4 - h), ok = 1;
        if (!far_from(x, y, *sx, *sy, 12)) continue;
        for (int j = y - 2; j < y + h + 2 && ok; j++)
            for (int i = x - 2; i < x + w + 2 && ok; i++)
                if (i >= 0 && j >= 0 && i < CW && j < CH && used[j][i]) ok = 0;
        if (!ok) continue;
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++) c->el[j][i] = SPACE;
        c->el[y][x] = FIREFLY;
        c->aux[y][x] = LEFT;
    }
    if (rnd(10) < 6) {
        int x = 2 + rnd(CW - 4), y = 2 + rnd(CH - 4), ok = far_from(x, y, *sx, *sy, 14);
        for (int j = y - 5; j <= y + 5 && ok; j++)
            for (int i = x - 5; i <= x + 5 && ok; i++)
                if (i >= 0 && j >= 0 && i < CW && j < CH && used[j][i] && abs(i - x) + abs(j - y) <= 5) ok = 0;
        if (ok) c->el[y][x] = AMOEBA;
    }
    for (int x = 0; x < CW; x++) c->el[0][x] = c->el[CH - 1][x] = STEEL;
    for (int y = 0; y < CH; y++) c->el[y][0] = c->el[y][CW - 1] = STEEL;
    c->time = 180 + 10 * rnd(8);
    c->amoeba_time = 30 + 10 * rnd(6);
    c->amoeba_limit = 200;
    memcpy(c->colors, COLOURS[rnd(sizeof COLOURS / sizeof COLOURS[0])], 5);
}

static int reach(const uint8_t el[CH][CW], int sx, int sy, uint8_t seen[CH][CW], int *diamonds)
{
    static int queue[CW * CH];
    int head = 0, tail = 0, exit = 0;
    *diamonds = 0;
    memset(seen, 0, CH * CW);
    seen[sy][sx] = 1;
    queue[tail++] = sy * CW + sx;
    while (head < tail) {
        int x = queue[head] % CW, y = queue[head++] / CW;
        static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d], e = el[ny][nx];
            if (seen[ny][nx]) continue;
            seen[ny][nx] = 1;
            if (e == EXIT) exit = 1;
            if ((e != SPACE && e != DIRT && e != DIAMOND) || avoid[ny][nx]) continue;
            if (e == DIAMOND) (*diamonds)++;
            queue[tail++] = ny * CW + nx;
        }
    }
    return exit;
}

static void boxes_at(uint8_t el[CH][CW], int skip, int sx_, int sy_)
{
    for (int i = 0; i < nbox; i++) {
        el[box_[i].by][box_[i].bx] = SPACE;
        if (i != skip) el[box_[i].ty][box_[i].tx] = BOULDER;
    }
    if (skip >= 0) el[sy_][sx_] = BOULDER;
}

static int check(Cave *c, int sx, int sy)
{
    static uint8_t seen[CH][CW], el[CH][CW];
    int d0, d;
    if (nbox != wanted_boxes || nvault != wanted_vaults || nlocked < 1) return 0;
    reach(c->el, sx, sy, seen, &d0);
    for (int i = 0; i < nthrow; i++) if (!seen[throw_[i][1]][throw_[i][0]]) return 0;
    for (int i = 0; i < nbox; i++) if (!box_[i].locked && !seen[box_[i].p1y][box_[i].p1x]) return 0;
    memcpy(el, c->el, sizeof el);
    boxes_at(el, -1, 0, 0);
    if (!reach(el, sx, sy, seen, &d)) return 0;
    for (int i = 0; i < nbox; i++) {
        if (box_[i].locked) continue;
        memcpy(el, c->el, sizeof el);
        boxes_at(el, i, box_[i].bx, box_[i].by);
        reach(el, sx, sy, seen, &d);
        if (!seen[box_[i].p1y][box_[i].p1x]) return 0;
        if (box_[i].bend) {
            memcpy(el, c->el, sizeof el);
            boxes_at(el, i, box_[i].cx, box_[i].cy);
            reach(el, sx, sy, seen, &d);
            if (!seen[box_[i].p2y][box_[i].p2x]) return 0;
        }
    }
    c->diamonds = d0 * 2 / 3 + vault_diamonds;
    return d0 >= 15;
}

void cave_random(Cave *c, unsigned seed)
{
    int sx, sy;
    for (unsigned k = 0;; k++) {
        state = seed * 7919u + k * 104729u + 1;
        build(c, &sx, &sy);
        if (check(c, sx, sy)) break;
    }
    c->seed = seed;
}

static const struct { char ch; uint8_t e, aux, target; } CHARS[] = {
    {' ', SPACE, 0, 0}, {'_', SPACE, 0, 1}, {'.', DIRT, 0, 0}, {'=', DIRT, 0, 1},
    {'w', WALL, 0, 0}, {'W', WALL, 0, 1},
    {'i', STEEL, 0, 0}, {'o', BOULDER, 0, 0}, {'O', BOULDER, 0, 1}, {'0', DIAMOND, 0, 0},
    {'1', DIAMOND, 1, 0}, {'2', DIAMOND, 2, 0}, {'3', DIAMOND, 3, 0}, {'*', INBOX, 0, 0},
    {'E', EXIT, 0, 0}, {'x', FIREFLY, LEFT, 0}, {'5', FIREFLY, UP, 0}, {'6', FIREFLY, RIGHT, 0},
    {'7', FIREFLY, DOWN, 0}, {'a', AMOEBA, 0, 0}, {'A', AMOEBA, 0, 1},
    {'r', STONE_R, 0, 0}, {'g', STONE_G, 0, 0}, {'b', STONE_B, 0, 0},
    {'R', STONE_R, 0, 1}, {'G', STONE_G, 0, 1}, {'B', STONE_B, 0, 1},
    {'y', STONE_RG, 0, 0}, {'m', STONE_RB, 0, 0}, {'c', STONE_GB, 0, 0},
    {'Y', STONE_RG, 0, 1}, {'M', STONE_RB, 0, 1}, {'C', STONE_GB, 0, 1},
};
#define NCHARS (int)(sizeof CHARS / sizeof *CHARS)

static int from_char(char ch, uint8_t *e, uint8_t *aux, uint8_t *target)
{
    for (int i = 0; i < NCHARS; i++)
        if (ch && CHARS[i].ch == ch) { *e = CHARS[i].e, *aux = CHARS[i].aux, *target = CHARS[i].target; return 1; }
    return 0;
}

char cave_char(int e, int aux, int target)
{
    if (e == EXPLOSION) return '%';
    if (e == ROCKFORD) e = INBOX;
    aux = e == DIAMOND ? aux : e == FIREFLY ? aux & 3 : 0;
    for (int t = target ? 1 : 0; t >= 0; t--)
        for (int i = 0; i < NCHARS; i++)
            if (CHARS[i].e == e && CHARS[i].aux == aux && CHARS[i].target == t) return CHARS[i].ch;
    return '?';
}

const char *cave_dir = ".", *cave_single;
int cave_count;

void cave_path(int n, char *path, int size)
{
    if (cave_single) snprintf(path, size, "%s", cave_single);
    else snprintf(path, size, "%s/C%02X.data", cave_dir, n);
}

void cave_count_files(void)
{
    char path[1024];
    if (cave_single) { cave_count = 1; return; }
    for (cave_count = 0; cave_count < 254; cave_count++) {
        cave_path(cave_count + 1, path, sizeof path);
        FILE *f = fopen(path, "r");
        if (!f) break;
        fclose(f);
    }
}

static int fail(char *where, char *why, const char *path, int line, const char *msg)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (line) snprintf(where, 64, "%s line %d:", base, line);
    else snprintf(where, 64, "%s:", base);
    snprintf(why, 160, "%s", msg);
    fprintf(stderr, "caves: %s %s\n", where, why);
    return 0;
}

int cave_load(Cave *c, const char *path, char *where, char *why)
{
    FILE *f = fopen(path, "r");
    if (!f) return fail(where, why, path, 0, "the file can not be read");
    memset(c, 0, sizeof *c);
    c->time = 150;
    c->amoeba_time = 60;
    c->amoeba_limit = 200;
    c->diamonds = -1;
    memcpy(c->colors, COLOURS[0], 5);
    char line[512], msg[160];
    int n = 0, row = -1, inboxes = 0, exits = 0, ok = 1;
    while (ok && fgets(line, sizeof line, f)) {
        n++;
        line[strcspn(line, "\r\n")] = 0;
        if (row >= 0) {
            if (row == CH) {
                for (char *p = line; *p; p++)
                    if (*p != ' ' && *p != '\t') { ok = fail(where, why, path, n, "text after the 22 map rows"); break; }
                continue;
            }
            int len = (int)strlen(line);
            for (int x = 0; x < len && ok; x++) {
                uint8_t e, aux, target;
                if (from_char(line[x], &e, &aux, &target)) continue;
                int l = 1;
                while (((unsigned char)line[x + l] & 0xC0) == 0x80) l++;
                snprintf(msg, sizeof msg, "map row %d, column %d: '%.*s' is not a map character", row + 1, x + 1, l, line + x);
                ok = fail(where, why, path, n, msg);
            }
            if (!ok) break;
            if (len > CW) {
                snprintf(msg, sizeof msg, "map row %d has %d characters, at most 40", row + 1, len);
                ok = fail(where, why, path, n, msg);
                break;
            }
            for (int x = 0; x < CW; x++) {
                uint8_t e = SPACE, aux = 0, target = 0;
                from_char(x < len ? line[x] : ' ', &e, &aux, &target);
                c->el[row][x] = e, c->aux[row][x] = aux, c->target[row][x] = target;
                inboxes += e == INBOX;
                exits += e == EXIT;
            }
            row++;
            continue;
        }
        char *h = strchr(line, '#');
        if (h) *h = 0;
        char key[32];
        if (sscanf(line, "%31s", key) != 1) continue;
        if (!strcmp(key, "map")) { row = 0; continue; }
        if (!strcmp(key, "colors")) {
            unsigned v[5];
            for (char *q = line; *q; q++) if (*q == '$') *q = ' ';
            if (sscanf(line, "%*s %x %x %x %x %x", &v[0], &v[1], &v[2], &v[3], &v[4]) != 5 ||
                v[0] > 255 || v[1] > 255 || v[2] > 255 || v[3] > 255 || v[4] > 255) {
                ok = fail(where, why, path, n, "colors must be 5 numbers $00-$FF (COLOR0-3, background)");
                break;
            }
            for (int i = 0; i < 5; i++) c->colors[i] = (uint8_t)v[i];
            continue;
        }
        static const struct { const char *name; int lo, hi; } SET[] = {
            {"diamonds", 0, 9999}, {"time", 1, 9999}, {"amoeba_time", 0, 9999}, {"amoeba_limit", 1, 880},
        };
        int k = 0, v;
        while (k < 4 && strcmp(key, SET[k].name)) k++;
        if (k == 4) { snprintf(msg, sizeof msg, "unknown setting '%s'", key); ok = fail(where, why, path, n, msg); break; }
        if (sscanf(line, "%*s %d", &v) != 1 || v < SET[k].lo || v > SET[k].hi) {
            snprintf(msg, sizeof msg, "%s must be %d-%d", key, SET[k].lo, SET[k].hi);
            ok = fail(where, why, path, n, msg);
            break;
        }
        int *to[] = {&c->diamonds, &c->time, &c->amoeba_time, &c->amoeba_limit};
        *to[k] = v;
    }
    fclose(f);
    if (!ok) return 0;
    if (c->diamonds < 0) return fail(where, why, path, 0, "the setting 'diamonds' is missing");
    if (row < 0) return fail(where, why, path, 0, "no 'map' line");
    if (row < CH) { snprintf(msg, sizeof msg, "the map has %d rows, it needs 22", row); return fail(where, why, path, 0, msg); }
    if (inboxes != 1) return fail(where, why, path, 0, inboxes ? "the map has more than one Rockford (*)" : "the map has no Rockford (*)");
    if (!exits) return fail(where, why, path, 0, "the map has no exit (E)");
    return 1;
}

int cave_save(const Cave *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# GAME - %s (the format: README.md)\n\n", c->seed ? "a random cave" : "a cave");
    if (c->seed) fprintf(f, "# random cave %u\n", c->seed);
    fprintf(f, "diamonds         %-8d # needed to open the exit\n", c->diamonds);
    fprintf(f, "time             %-8d # seconds\n", c->time);
    fprintf(f, "amoeba_time      %-8d # seconds until the amoeba grows fast\n", c->amoeba_time);
    fprintf(f, "amoeba_limit     %-8d # an amoeba this big turns into stones\n", c->amoeba_limit);
    fprintf(f, "colors           $%02X $%02X $%02X $%02X $%02X   # COLOR0 COLOR1 COLOR2 COLOR3 background\n\nmap\n",
            c->colors[0], c->colors[1], c->colors[2], c->colors[3], c->colors[4]);
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < CW; x++) fputc(cave_char(c->el[y][x], c->aux[y][x], c->target[y][x]), f);
        fputc('\n', f);
    }
    return fclose(f) == 0;
}

void cave_dump(const Game *g, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    int covered = 0;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) covered += g->covered[y][x];
    fprintf(f, "phase %d  score %ld  lives %d  diamonds %d/%d  targets left %d  time %d  exit %s  covered %d\n",
            g->phase, g->score, g->lives, g->collected, g->cave.diamonds, g->targets_left, g->time_left,
            g->exit_open ? "open" : "shut", covered);
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < CW; x++) fputc(cave_char(g->el[y][x], g->aux[y][x], g->cave.target[y][x]), f);
        fputc('\n', f);
    }
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            if (IS_STONE(g->el[y][x]) && g->aux[y][x])
                fprintf(f, "sliding %c at %d,%d %s\n", cave_char(g->el[y][x], 0, 0), x, y,
                        (const char *[]){"up", "right", "down", "left"}[g->aux[y][x] - 1]);
    fclose(f);
}
