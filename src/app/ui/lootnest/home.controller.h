#pragma once

// Lootnest's home screen on the TV: the first and only screen before the
// stream. It does everything by itself (pairs with the PC Lootnest named
// in the launch, wakes the PC, opens its app named Lootnest) and shows
// what it's doing, in Lootnest's look. Moonlight TV's computer list and
// app grid are never shown.

#include "lvgl.h"

typedef struct app_t app_t;
typedef struct app_launch_params_t app_launch_params_t;

typedef struct home_fragment_args_t {
    app_t *app;
    const app_launch_params_t *params;
} home_fragment_args_t;

extern const lv_fragment_class_t home_controller_class;
