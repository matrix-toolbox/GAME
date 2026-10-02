#include <stdint.h>

#define FRAME_CYCLES 29830

void pokey_init(void);
void pokey_write(int t, int reg, uint8_t v);
void pokey_render(int16_t *out, int n);
