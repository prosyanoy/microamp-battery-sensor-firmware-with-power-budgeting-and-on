#include <string.h>

#include "app.h"

/* polling : every sample is fetched by a 100 Hz periodic task, tick always on.
 * fifo    : sensor FIFO batches 16 samples, MCU stop-sleeps until watermark IRQ.
 * wom     : like fifo, but the MCU also sleeps through quiet periods and is
 *           woken by the sensor's motion interrupt (FIFO keeps pre-roll). */
static const design_t designs[] = {
    {"polling", false, false, false, 100, 1},
    {"fifo", true, true, false, 100, 16},
    {"wom", true, true, true, 100, 16},
};

int design_count(void) { return (int)(sizeof designs / sizeof designs[0]); }
const design_t *design_at(int i) { return &designs[i]; }

const design_t *design_by_name(const char *name)
{
    for (int i = 0; i < design_count(); i++)
        if (strcmp(designs[i].name, name) == 0) return &designs[i];
    return 0;
}
