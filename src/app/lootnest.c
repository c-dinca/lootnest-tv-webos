#include "lootnest.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "app.h"
#include "backend/pcmanager.h"
#include "logging.h"
#include "util/bus.h"
#include "util/user_event.h"

static char pair_host[128];
static char pair_pin[8];
static bool autolaunch_done = false;
static SDL_TimerID quit_timer = 0;

static bool valid_pin(const char *pin) {
    if (pin == NULL || strlen(pin) != 4) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!isdigit((unsigned char) pin[i])) {
            return false;
        }
    }
    return true;
}

void lootnest_set_pairing(const char *host, const char *pin) {
    if (host == NULL || host[0] == '\0' || !valid_pin(pin)) {
        return;
    }
    SDL_strlcpy(pair_host, host, sizeof(pair_host));
    SDL_strlcpy(pair_pin, pin, sizeof(pair_pin));
    commons_log_info("Lootnest", "Asked to pair with %s", pair_host);
    lootnest_note("Asked to pair with %s", pair_host);
}

bool lootnest_take_pairing_host(char *host, int size) {
    if (pair_host[0] == '\0') {
        return false;
    }
    SDL_strlcpy(host, pair_host, size);
    pair_host[0] = '\0';
    return true;
}

bool lootnest_take_pin(char pin[8]) {
    if (!valid_pin(pair_pin)) {
        return false;
    }
    SDL_strlcpy(pin, pair_pin, 8);
    pair_pin[0] = '\0';
    return true;
}

bool lootnest_take_autolaunch(void) {
    if (autolaunch_done) {
        return false;
    }
    autolaunch_done = true;
    return true;
}

bool lootnest_first_paired(uuidstr_t *out) {
    for (const pclist_t *cur = pcmanager_servers(pcmanager); cur != NULL; cur = cur->next) {
        if (cur->server != NULL && cur->server->paired) {
            *out = cur->id;
            return true;
        }
    }
    return false;
}

static Uint32 quit_timer_cb(Uint32 interval, void *param) {
    (void) interval;
    (void) param;
    quit_timer = 0;
    bus_pushevent(USER_LOOTNEST_QUIT_STREAM, NULL, NULL);
    return 0;
}

void lootnest_gamepad_buttons(int buttons, int start_flag, int select_flag) {
    bool held = buttons == (start_flag | select_flag);
    if (held && quit_timer == 0) {
        quit_timer = SDL_AddTimer(2000, quit_timer_cb, NULL);
    } else if (!held && quit_timer != 0) {
        SDL_RemoveTimer(quit_timer);
        quit_timer = 0;
    }
}

const char *lootnest_temp_dir(void) {
    const char *dir = getenv("LOOTNEST_TEMP");
    return dir != NULL && dir[0] ? dir : "/media/developer/temp";
}

// value finds "key": "value" in the small JSON Lootnest writes.
static bool json_value(const char *json, const char *key, char *out, size_t size) {
    char needle[40];
    SDL_snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *at = strstr(json, needle);
    if (at == NULL) {
        return false;
    }
    at = strchr(at + strlen(needle), '"');
    if (at == NULL) {
        return false;
    }
    at++;
    const char *end = strchr(at, '"');
    if (end == NULL || (size_t) (end - at) >= size) {
        return false;
    }
    memcpy(out, at, end - at);
    out[end - at] = '\0';
    return true;
}

static char used_request[64];

static bool read_pair_file(char *id, size_t id_size, char *host, size_t host_size, char *pin, size_t pin_size);

void lootnest_mark_pair_file_used(void) {
    char id[64], host[128], pin[8];
    if (read_pair_file(id, sizeof(id), host, sizeof(host), pin, sizeof(pin))) {
        SDL_strlcpy(used_request, id, sizeof(used_request));
    }
}

static bool read_pair_file(char *id, size_t id_size, char *host, size_t host_size, char *pin, size_t pin_size) {
    char path[256];
    SDL_snprintf(path, sizeof(path), "%s/lootnest-pair.json", lootnest_temp_dir());
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    char json[512];
    size_t n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';
    return json_value(json, "id", id, id_size) && json_value(json, "host", host, host_size) &&
           json_value(json, "pairPin", pin, pin_size);
}

bool lootnest_check_pair_file(void) {
    char path[256];
    SDL_snprintf(path, sizeof(path), "%s/lootnest-pair.json", lootnest_temp_dir());
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    char json[512];
    size_t n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';
    char id[64], host[128], pin[8];
    if (!json_value(json, "id", id, sizeof(id)) || !json_value(json, "host", host, sizeof(host)) ||
        !json_value(json, "pairPin", pin, sizeof(pin))) {
        lootnest_note("lootnest-pair.json can't be read");
        return false;
    }
    if (strcmp(id, used_request) == 0) {
        return false;
    }
    SDL_strlcpy(used_request, id, sizeof(used_request));
    lootnest_note("Pairing request %s from lootnest-pair.json", id);
    lootnest_set_pairing(host, pin);
    return pair_host[0] != '\0';
}

void lootnest_note(const char *fmt, ...) {
    static bool started_log = false;
    char path[256];
    SDL_snprintf(path, sizeof(path), "%s/lootnest-tv.log", lootnest_temp_dir());
    FILE *f = fopen(path, started_log ? "ab" : "wb");
    if (f == NULL) {
        return;
    }
    started_log = true;
    fprintf(f, "[%u] ", (unsigned) (SDL_GetTicks() / 1000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static char controller_line[160];
static int controllers_on = 0;

bool lootnest_guess_mapping(int device_index) {
    SDL_Joystick *js = SDL_JoystickOpen(device_index);
    if (js == NULL) {
        lootnest_note("Controller %d couldn't be opened: %s", device_index, SDL_GetError());
        return false;
    }
    int buttons = SDL_JoystickNumButtons(js), axes = SDL_JoystickNumAxes(js), hats = SDL_JoystickNumHats(js);
    Uint16 vendor = SDL_JoystickGetVendor(js), product = SDL_JoystickGetProduct(js);
    const char *name = SDL_JoystickName(js);
    char guid[33];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), guid, sizeof(guid));
    lootnest_note("Controller without a mapping: %s, %04x:%04x, GUID %s, %d buttons, %d axes, %d hats",
                  name ? name : "?", vendor, product, guid, buttons, axes, hats);
    char safe[64];
    SDL_strlcpy(safe, name ? name : "Controller", sizeof(safe));
    for (char *c = safe; *c; c++) {
        if (*c == ',') {
            *c = ' ';
        }
    }
    SDL_JoystickClose(js);
    if (buttons < 4 || axes < 2) {
        return false;
    }
    char mapping[640];
    const char *dpad = hats > 0 ? "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8," : "";
    if (vendor == 0x054c) {
        // Sony's HID order: square, cross, circle, triangle, L1, R1, L2,
        // R2, share or create, options, L3, R3, PS; axes X, Y, Z (right
        // stick X), Rx (L2), Ry (R2), Rz (right stick Y).
        SDL_snprintf(mapping, sizeof(mapping),
                     "%s,%s,a:b1,b:b2,x:b0,y:b3,leftshoulder:b4,rightshoulder:b5,back:b8,start:b9,guide:b12,"
                     "leftstick:b10,rightstick:b11,%sleftx:a0,lefty:a1,rightx:a2,righty:a5,lefttrigger:a3,"
                     "righttrigger:a4,", guid, safe, dpad);
    } else {
        // The Linux xpad order most pads follow.
        SDL_snprintf(mapping, sizeof(mapping),
                     "%s,%s,a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,rightshoulder:b5,back:b6,start:b7,guide:b8,"
                     "leftstick:b9,rightstick:b10,%sleftx:a0,lefty:a1,lefttrigger:a2,rightx:a3,righty:a4,"
                     "righttrigger:a5,", guid, safe, dpad);
    }
    int added = SDL_GameControllerAddMapping(mapping);
    lootnest_note("Guessed a mapping for %s (%s): %s", safe, vendor == 0x054c ? "Sony order" : "xpad order",
                  added >= 0 ? "added" : SDL_GetError());
    return added >= 0;
}

void lootnest_controller_seen(const char *name, bool recognized, bool guessed) {
    if (recognized) {
        controllers_on++;
    }
    lootnest_note("Controller %s: %s", name ? name : "?",
                  !recognized ? "not recognised" : guessed ? "buttons guessed" : "recognised");
    if (!recognized) {
        SDL_snprintf(controller_line, sizeof(controller_line), "%s isn't recognised as a controller yet",
                     name ? name : "This controller");
    } else if (guessed) {
        SDL_snprintf(controller_line, sizeof(controller_line), "Controller: %s (buttons guessed)",
                     name ? name : "connected");
    } else {
        SDL_snprintf(controller_line, sizeof(controller_line), "Controller: %s", name ? name : "connected");
    }
}

void lootnest_controller_gone(void) {
    if (controllers_on > 0) {
        controllers_on--;
    }
    if (controllers_on == 0) {
        SDL_strlcpy(controller_line, "Controller disconnected", sizeof(controller_line));
    }
}

const char *lootnest_controller_line(void) {
    return controller_line;
}

bool lootnest_tune_picture(struct app_settings_t *settings) {
    if (settings->lootnest_tuned >= 1) {
        return false;
    }
    lootnest_note("Picture asked for was %dx%d at %d, %d kbps; now 1920x1080 at 60, 25000 kbps",
                  settings->stream.width, settings->stream.height, settings->stream.fps, settings->stream.bitrate);
    settings->stream.width = 1920;
    settings->stream.height = 1080;
    settings->stream.fps = 60;
    settings->stream.bitrate = 25000;
    settings->hevc = true;
    settings->lootnest_tuned = 1;
    return true;
}

// One stream's counts. A second is "bad" when the network lost a frame in it.
static struct {
    bool on;
    char format[24];
    int width, height, fps;
    unsigned seconds, bad_seconds;
    unsigned long frames, received, lost, shown;
    unsigned long long host_tenths_ms, reassembly_us, submit_us;
    unsigned worst_lost, rtt_max, rtt_var_max;
    unsigned long long rtt_sum;
} stream_sum;

void lootnest_stream_begin(const char *format, int width, int height, int fps) {
    memset(&stream_sum, 0, sizeof(stream_sum));
    stream_sum.on = true;
    snprintf(stream_sum.format, sizeof(stream_sum.format), "%s", format ? format : "?");
    stream_sum.width = width;
    stream_sum.height = height;
    stream_sum.fps = fps;
    lootnest_note("Stream: started, %s %dx%d at %d", stream_sum.format, width, height, fps);
}

void lootnest_stream_second(unsigned frames, unsigned received, unsigned lost, unsigned shown, unsigned host_tenths_ms,
                            unsigned reassembly_us, unsigned submit_us, unsigned rtt_ms, unsigned rtt_var_ms) {
    if (!stream_sum.on) {
        return;
    }
    stream_sum.seconds++;
    stream_sum.frames += frames;
    stream_sum.received += received;
    stream_sum.lost += lost;
    stream_sum.shown += shown;
    stream_sum.host_tenths_ms += host_tenths_ms;
    stream_sum.reassembly_us += reassembly_us;
    stream_sum.submit_us += submit_us;
    stream_sum.rtt_sum += rtt_ms;
    if (lost > 0) {
        stream_sum.bad_seconds++;
    }
    if (lost > stream_sum.worst_lost) {
        stream_sum.worst_lost = lost;
    }
    if (rtt_ms > stream_sum.rtt_max) {
        stream_sum.rtt_max = rtt_ms;
    }
    if (rtt_var_ms > stream_sum.rtt_var_max) {
        stream_sum.rtt_var_max = rtt_var_ms;
    }
}

void lootnest_stream_end(void) {
    if (!stream_sum.on) {
        return;
    }
    stream_sum.on = false;
    if (stream_sum.seconds == 0 || stream_sum.received == 0) {
        lootnest_note("Stream: ended before a second of picture");
        return;
    }
    double secs = (double) stream_sum.seconds, got = (double) stream_sum.received;
    double shown = stream_sum.shown > 0 ? (double) stream_sum.shown : 1;
    lootnest_note("Stream: ended after %u s, %s %dx%d at %d. Frames a second: %.1f arrived, %.1f shown. "
                  "The network lost %lu of %lu frames (%.2f%%), in %u of those seconds, %u in the worst. "
                  "A frame took the PC %.1f ms, the TV %.1f ms to put together and %.1f ms to hand to its decoder. "
                  "Round trip %.0f ms on average, %u at most, varying by up to %u",
                  stream_sum.seconds, stream_sum.format, stream_sum.width, stream_sum.height, stream_sum.fps,
                  got / secs, (double) stream_sum.shown / secs,
                  stream_sum.lost, stream_sum.frames, stream_sum.frames ? 100.0 * (double) stream_sum.lost / (double) stream_sum.frames : 0.0,
                  stream_sum.bad_seconds, stream_sum.worst_lost,
                  (double) stream_sum.host_tenths_ms / 10.0 / got, (double) stream_sum.reassembly_us / 1000.0 / got,
                  (double) stream_sum.submit_us / 1000.0 / shown,
                  (double) stream_sum.rtt_sum / secs, stream_sum.rtt_max, stream_sum.rtt_var_max);
}
