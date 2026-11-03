#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t unveil_sound_init(void);
esp_err_t unveil_sound_command(const char *kind,int volume,bool muted);
