#include <stdint.h>

#define CW 40
#define CH 22
#define CELL 18
#define VIEW_W 20
#define VIEW_H 12
#define STATUS_H 8
#define SCREEN_W (VIEW_W * CELL)
#define SCREEN_H (STATUS_H + VIEW_H * CELL)
#define FPS 60

enum { SPACE, DIRT, WALL, STEEL, BOULDER, DIAMOND, ROCKFORD, INBOX, EXIT, FIREFLY, AMOEBA,
       STONE_R, STONE_G, STONE_B, STONE_RG, STONE_RB, STONE_GB, EXPLOSION };
#define IS_STONE(e) ((e) >= STONE_R && (e) <= STONE_GB)
enum { UP, RIGHT, DOWN, LEFT };

typedef struct {
    uint8_t el[CH][CW];
    uint8_t aux[CH][CW];
    uint8_t target[CH][CW];
    int diamonds, time, amoeba_time, amoeba_limit;
    uint8_t colors[5];
    unsigned seed;
} Cave;

enum { PH_MENU, PH_UNCOVER, PH_BIRTH, PH_PLAY, PH_DEAD, PH_BONUS, PH_OVER, PH_INVALID, PH_COVER };

typedef struct {
    int number;
    unsigned seed;
    int start_number;
    unsigned start_seed;
    char where[64], why[160];
    Cave cave;
    uint8_t done[CH][CW];
    uint8_t covered[CH][CW];
    uint8_t cover_q[CH * 2][CW * 2];
    uint8_t el[CH][CW], aux[CH][CW];
    int passes;
    int flash_toggle;
    int cover_frame;
    int phase, phase_frames, frame, tick_frames;
    int rx, ry;
    int walking, facing_left, facing, blink, tap;
    int collected, exit_open, flash;
    int exit_x, exit_y, exit_frame;
    int time_left, second_frames, played_seconds;
    long score;
    int lives, birth_ticks;
    int noise;
    int amoeba_room;
    uint8_t glitter[4][2];
    int targets_left, loose_boulders;
    int paused, fire_was, latch_dir, latch_fire, tap_fire, pick;
    int cam_x, cam_y, scroll_x, scroll_y;
    unsigned rnd;
} Game;

extern const char *cave_dir;
extern int cave_count;
extern const char *cave_single;
extern unsigned game_luck;
extern int time_off;
void cave_count_files(void);
void cave_path(int n, char *path, int size);
void cave_random(Cave *c, unsigned seed);
int cave_load(Cave *c, const char *path, char *where, char *why);
int cave_save(const Cave *c, const char *path);
void cave_dump(const Game *g, const char *path);
char cave_char(int e, int aux, int target);

void game_new(Game *g, int number, unsigned seed);
void game_frame(Game *g, int dir, int fire);
int game_back(Game *g, int ticks);
void game_give_up(Game *g);
void game_press(Game *g, int dir);

void draw(const Game *g, uint32_t *fb);
void draw_map(const Game *g, uint32_t *fb);
int sprites_load(const char *path);

enum { SND_DIRT, SND_SPACE, SND_DIAMOND, SND_PUSH, SND_CRUSH, SND_EXPLODE, SND_EXIT, SND_BONUS,
       SND_BONUS_START, SND_BIRTH };
int sound_init(void);
void sound_reset(void);
void sound_play(int id);
void sound_time(int seconds_left);
void sound_tick(int amoeba);
void sound_cover(int on);
const int16_t *sound_frame(int *n);
void sound_wait(void);
void sound_pause(int on);
extern int sound_on;
