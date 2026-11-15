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
