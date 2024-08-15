#ifndef HAL_POSIX_SIM_H
#define HAL_POSIX_SIM_H

#include "app.h"

int hal_posix_start(const app_config_t *cfg);   /* spawn simulator, open radio log */
void hal_posix_stop(void);

#endif
