#include "lootnest.h"

#include <ctype.h>
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
