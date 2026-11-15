#pragma once

// What Lootnest's fork adds to Moonlight TV (README.md, "What this fork
// changes"), kept here so the rest of the code changes in a few lines.

#include <stdbool.h>

#include "uuidstr.h"

// The host app Lootnest registers in the streaming engine.
#define LOOTNEST_APP_NAME "Lootnest"

// Launch parameters Lootnest passes when it installs or opens this app:
// {"host": "192.168.1.20", "pairPin": "1234"}.
void lootnest_set_pairing(const char *host, const char *pin);

// The PC to pair with, once: false when there's none (or it was taken).
bool lootnest_take_pairing_host(char *host, int size);

// The PIN Lootnest submits for that PC, once; a pair dialog uses it
// instead of making one. False when there's none.
bool lootnest_take_pin(char pin[8]);

// True the first time it's asked in this run: the first PC opened starts
// its app named "Lootnest" by itself, so the TV goes straight to Lootnest.
bool lootnest_take_autolaunch(void);

// The paired PC to open at start when the launch named none: the first
// one paired, or false when there's none.
bool lootnest_first_paired(uuidstr_t *out);

// Ends the stream (as Quit in its menu) when Start and Select are held
// together for two seconds; buttons is the gamepad's button flags.
void lootnest_gamepad_buttons(int buttons, int start_flag, int select_flag);
