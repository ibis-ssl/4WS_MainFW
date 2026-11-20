/* OpenOCDからSWDで読み書きするSEGGER RTT互換のRAMリング。 */
#include "Board/board_debug_rtt.h"
#include "stm32g4xx_hal.h"
#include <string.h>

typedef struct {
    const char *name;
    uint8_t *data;
    uint32_t size;
    volatile uint32_t write_at, read_at;
    uint32_t flags;
} rtt_ring_t;

typedef struct {
    char id[16];
    uint32_t up_count, down_count;
    rtt_ring_t up, down;
} rtt_control_t;

static uint8_t up[1024], down[1024];
static volatile rtt_control_t main_rtt;

void board_debug_rtt_init(void)
{
    main_rtt.up_count = 1;
    main_rtt.down_count = 1;
    main_rtt.up = (rtt_ring_t){"Main status", up, sizeof(up), 0, 0, 0};
    main_rtt.down = (rtt_ring_t){"Main commands", down, sizeof(down), 0, 0, 0};
    __DMB();
    const char id[] = "SEGGER RTT";
    for (unsigned int i = 0; i < sizeof(id); ++i) { main_rtt.id[i] = id[i]; }
}

bool board_debug_rtt_read(uint8_t *value)
{
    uint32_t at = main_rtt.down.read_at;
    if (at == main_rtt.down.write_at) { return false; }
    __DMB();
    *value = down[at];
    main_rtt.down.read_at = (at + 1U) % sizeof(down);
    return true;
}

void board_debug_rtt_write(const char *line)
{
    uint32_t at = main_rtt.up.write_at;
    uint32_t read = main_rtt.up.read_at;
    uint32_t free = (read + sizeof(up) - at - 1U) % sizeof(up);
    size_t length = strlen(line);
    if (length > free) { return; }
    for (size_t n = 0; n < length; ++n) {
        up[at] = (uint8_t)line[n];
        at = (at + 1U) % sizeof(up);
    }
    __DMB();
    main_rtt.up.write_at = at;
}
