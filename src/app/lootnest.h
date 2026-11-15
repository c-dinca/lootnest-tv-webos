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

// The folder developer mode shares between the TV's developer sessions and
// its developer apps; Lootnest on the PC writes there over SSH. The
// LOOTNEST_TEMP environment variable replaces it (desktop tests).
const char *lootnest_temp_dir(void);

// A pairing request Lootnest left in lootnest-pair.json, for an app that
// was already open or didn't get the launch parameters: true when there's
// one not used yet in this run, which then becomes the pairing to do.
bool lootnest_check_pair_file(void);

// Marks the request in lootnest-pair.json as used, when the same pairing
// came with the launch parameters.
void lootnest_mark_pair_file_used(void);

// Notes a step in lootnest-tv.log, which Lootnest on the PC reads when a
// pairing doesn't finish. The first note of a run starts the file again.
void lootnest_note(const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));

// A controller SDL has no mapping for (older TVs' SDL knows few): a
// mapping by the usual button order, Sony's for a Sony pad, else the
// Linux xpad order. True when one was added for this device.
bool lootnest_guess_mapping(int device_index);

// Notes a controller that came or went, for the home screen's line.
void lootnest_controller_seen(const char *name, bool recognized, bool guessed);
void lootnest_controller_gone(void);

// The home screen's controller line: "" before any was seen.
const char *lootnest_controller_line(void);
