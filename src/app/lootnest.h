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

// The picture Lootnest asks the PC for, chosen on its home screen with two
// keys. Size is how large it is (1080p, 1440p, 4K or 720p, up to what the
// TV decodes) and is only ever changed by the person. Quality is how many
// bits a second it gets at that size: Sharp for a TV on a cable, Light for
// Wi-Fi that drops frames (a lighter picture arrives whole and on time,
// which is what makes a game feel quick), Balanced between. Automatic
// starts in the middle and, after each stream, takes a step down when the
// network lost frames or the TV couldn't keep up, and carefully back up
// after long clean streams; it never changes the size. Custom leaves what
// Settings says alone.
typedef enum lootnest_picture_t {
    LOOTNEST_PICTURE_AUTO = 0,
    LOOTNEST_PICTURE_LIGHT,
    LOOTNEST_PICTURE_BALANCED,
    LOOTNEST_PICTURE_SHARP,
    LOOTNEST_PICTURE_CUSTOM,
} lootnest_picture_t;

struct app_settings_t;

// Puts Lootnest's picture in place of Moonlight's defaults or an older
// version's, once (lootnest_tuned). True when the settings changed.
bool lootnest_tune_picture(struct app_settings_t *settings);

// The next quality in turn, put into the stream's settings.
void lootnest_picture_next(struct app_settings_t *settings);

// The next size in turn, among those no larger than the TV decodes
// (max_width and max_height; 0 when the TV doesn't say, then up to 1080p),
// with the quality's bits for it.
void lootnest_size_next(struct app_settings_t *settings, int max_width, int max_height);

// Settings were changed by hand since: a new size keeps the quality, with
// its bits for that size; other bits make the quality Custom. True when
// anything changed.
bool lootnest_picture_sync(struct app_settings_t *settings);

// After a stream, once: Automatic moves a step by how the stream went.
// True when the settings changed.
bool lootnest_picture_review(struct app_settings_t *settings);

// "Quality: Automatic" and "Size: 1080p", for the home screen's keys.
const char *lootnest_picture_key(const struct app_settings_t *settings);
const char *lootnest_size_key(const struct app_settings_t *settings);

// What that asks the PC for: "1080p at 60, 14 Mbps".
const char *lootnest_picture_line(const struct app_settings_t *settings);

// How the last stream of this run went, for the home screen; "" before one.
const char *lootnest_last_stream_line(void);

// A stream's picture, counted a second at a time, and summed up in
// lootnest-tv.log when the stream ends: how many frames arrived and were
// shown, how many the network lost and in how many seconds, and how long
// the PC, the network and the TV each took.
void lootnest_stream_begin(const char *format, int width, int height, int fps);
void lootnest_stream_second(unsigned frames, unsigned received, unsigned lost, unsigned shown, unsigned host_tenths_ms,
                            unsigned reassembly_us, unsigned submit_us, unsigned rtt_ms, unsigned rtt_var_ms);
void lootnest_stream_end(void);
