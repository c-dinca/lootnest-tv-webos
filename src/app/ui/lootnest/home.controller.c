#include "home.controller.h"

#include <string.h>

#include <SDL.h>

#include "app.h"
#include "app_launch.h"
#include "lootnest.h"
#include "res.h"
#include "errors.h"
#include "logging.h"

#include "backend/pcmanager.h"
#include "backend/apploader/apploader.h"
#include "ui/root.h"
#include "ui/streaming/streaming.controller.h"
#include "ui/settings/settings.controller.h"

#include "lvgl/util/lv_app_utils.h"
#include "lv_sdl_img.h"
#include "util/i18n.h"
#include "util/user_event.h"

// Lootnest's colours (DESIGN.md in Lootnest): obsidian ground, ice grey
// text, Racing Red only for the one main key.
#define C_GROUND   0x111217
#define C_RAISED   0x181a21
#define C_KEY      0x1f222b
#define C_TEXT     0xDFE5EB
#define C_TEXT_2   0xa6aeb9
#define C_TEXT_3   0x8c94a0
#define C_RED      0xEF233C

typedef enum home_state_t {
    HOME_START,
    HOME_PAIRING,
    HOME_CONNECTING,
    HOME_WAKING,
    HOME_STARTING,
    HOME_READY,
    HOME_NO_PC,
    HOME_OFFLINE,
    HOME_UNPAIRED,
    HOME_NO_APP,
    HOME_ERROR,
} home_state_t;

// What the home screen knows lives here, not in the fragment: on TVs whose
// video takes the whole screen, the UI is closed for the stream and opened
// again afterwards, as a new fragment.
static struct {
    home_state_t state;
    bool has_pc;
    uuidstr_t pc;
    char pc_name[64];
    char pin[8];
    bool pin_pending;
    bool woke;
    Uint32 wake_until;
    bool streamed;
    char detail[256];
} H;

typedef struct home_fragment_t {
    lv_fragment_t base;
    app_t *global;
    const app_launch_params_t *params;

    lv_obj_t *root;
    lv_obj_t *body;
    lv_obj_t *pc_label;
    lv_obj_t *spinner;
    lv_obj_t *status;
    lv_obj_t *detail;
    lv_obj_t *keys;
    lv_obj_t *hint;
    lv_obj_t *pad;
    lv_group_t *group;
    lv_timer_t *timer;
    apploader_t *apploader;
    lv_img_dsc_t logo;
} home_fragment_t;

static home_fragment_t *home_view = NULL;
static bool started = false;

static void home_ctor(lv_fragment_t *self, void *args);

static lv_obj_t *home_create(lv_fragment_t *self, lv_obj_t *container);

static void home_created(lv_fragment_t *self, lv_obj_t *obj);

static void home_will_delete(lv_fragment_t *self, lv_obj_t *obj);

static bool home_event(lv_fragment_t *self, int code, void *userdata);

static void set_state(home_state_t state);

static void render(void);

static void begin(void);

static void connect_pc(const uuidstr_t *uuid);

static void evaluate(void);

static void on_pc_updated(const uuidstr_t *uuid, void *userdata);

const lv_fragment_class_t home_controller_class = {
        .constructor_cb = home_ctor,
        .create_obj_cb = home_create,
        .obj_created_cb = home_created,
        .obj_will_delete_cb = home_will_delete,
        .event_cb = home_event,
        .instance_size = sizeof(home_fragment_t),
};

static const pcmanager_listener_t listener = {
        .added = on_pc_updated,
        .updated = on_pc_updated,
};

/* ------------------------------------------------------------------------------ */
/*                                     Fonts                                      */
/* ------------------------------------------------------------------------------ */

// Tilt Warp for the name and the status, Atkinson Hyperlegible Next for the
// rest, as in Lootnest. Made once, at the TV's density, and kept.
static struct {
    lv_font_t *title, *status, *body, *key, *small;
} F;

static lv_font_t *font_mem(const char *name, const unsigned char *data, unsigned int size, int px) {
    lv_ft_info_t info = {.name = name, .mem = data, .mem_size = size, .style = FT_FONT_STYLE_NORMAL,
            .weight = px};
    if (!lv_ft_font_init(&info)) {
        return NULL;
    }
    return info.font;
}

static void fonts_init(app_ui_t *ui) {
    if (F.title != NULL) {
        return;
    }
    int dpi = ui->dpi;
    F.title = font_mem("TiltWarp", res_tilt_warp_data, res_tilt_warp_size, _LV_DPX_CALC(dpi, 26));
    F.status = font_mem("TiltWarp", res_tilt_warp_data, res_tilt_warp_size, _LV_DPX_CALC(dpi, 30));
    F.body = font_mem("Atkinson", res_atkinson_data, res_atkinson_size, _LV_DPX_CALC(dpi, 17));
    F.small = font_mem("Atkinson", res_atkinson_data, res_atkinson_size, _LV_DPX_CALC(dpi, 14));
    F.key = font_mem("AtkinsonBold", res_atkinson_bold_data, res_atkinson_bold_size, _LV_DPX_CALC(dpi, 17));
    // A font that can't load falls back to the app's own.
    if (!F.title) F.title = ui->fonts.fonts.large;
    if (!F.status) F.status = ui->fonts.fonts.large;
    if (!F.body) F.body = ui->fonts.fonts.normal;
    if (!F.small) F.small = ui->fonts.fonts.small;
    if (!F.key) F.key = ui->fonts.fonts.normal;
    // Words outside the Latin subset (a PC's name, an error) use the
    // system font.
    if (F.body != ui->fonts.fonts.normal) F.body->fallback = ui->fonts.fonts.normal;
    if (F.small != ui->fonts.fonts.small) F.small->fallback = ui->fonts.fonts.small;
    if (F.status != ui->fonts.fonts.large) F.status->fallback = ui->fonts.fonts.large;
}

/* ------------------------------------------------------------------------------ */
/*                                     Keys                                       */
/* ------------------------------------------------------------------------------ */

typedef enum key_action_t {
    KEY_PLAY,
    KEY_RETRY,
    KEY_WAKE,
    KEY_SIZE,
    KEY_PICTURE,
    KEY_SETTINGS,
    KEY_QUIT,
} key_action_t;

// The Size or Quality key was just pressed: it keeps the focus as it changes.
static int picture_pressed = -1;

static void key_clicked(lv_event_t *e);

static void key_nav(lv_event_t *e);

static void back_pressed(lv_event_t *e);

// A key changes over 160 ms, as the focus does on the PC.
static const lv_style_prop_t key_tr_props[] = {LV_STYLE_BG_COLOR, LV_STYLE_TEXT_COLOR, LV_STYLE_TRANSFORM_WIDTH,
                                               LV_STYLE_TRANSFORM_HEIGHT, 0};
static lv_style_transition_dsc_t key_tr;
static bool key_tr_ready = false;

static lv_obj_t *add_key(home_fragment_t *v, const char *text, key_action_t action, bool primary) {
    if (!key_tr_ready) {
        lv_style_transition_dsc_init(&key_tr, key_tr_props, lv_anim_path_ease_out, 160, 0, NULL);
        key_tr_ready = true;
    }
    lv_obj_t *key = lv_btn_create(v->keys);
    lv_obj_remove_style_all(key);
    lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
    // The focus, as in Lootnest's Big Picture on the PC: a key in focus
    // lights up (Ice Grey, dark words) and grows a little; the main key
    // keeps its red and takes a ring instead.
    lv_obj_set_style_bg_color(key, lv_color_hex(primary ? C_RED : C_KEY), 0);
    lv_obj_set_style_bg_color(key, lv_color_hex(primary ? C_RED : C_TEXT), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(key, lv_color_hex(primary ? C_GROUND : C_TEXT), 0);
    lv_obj_set_style_text_color(key, lv_color_hex(C_GROUND), LV_STATE_FOCUSED);
    lv_obj_set_style_transform_width(key, LV_DPX(3), LV_STATE_FOCUSED);
    lv_obj_set_style_transform_height(key, LV_DPX(2), LV_STATE_FOCUSED);
    lv_obj_set_style_transition(key, &key_tr, 0);
    lv_obj_set_style_transition(key, &key_tr, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(key, LV_DPX(8), 0);
    lv_obj_set_style_pad_hor(key, LV_DPX(26), 0);
    lv_obj_set_style_pad_ver(key, LV_DPX(12), 0);
    // A fine top light, as Lootnest's keys have.
    lv_obj_set_style_border_side(key, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(key, 1, 0);
    lv_obj_set_style_border_color(key, lv_color_white(), 0);
    lv_obj_set_style_border_opa(key, primary ? LV_OPA_30 : LV_OPA_10, 0);
    if (primary) {
        lv_obj_set_style_outline_width(key, LV_DPX(3), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_pad(key, LV_DPX(3), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_color(key, lv_color_hex(C_TEXT), LV_STATE_FOCUSED);
        lv_obj_set_style_outline_opa(key, LV_OPA_COVER, LV_STATE_FOCUSED);
    }
    lv_obj_set_style_translate_y(key, 1, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(key);
    lv_obj_set_style_text_font(label, F.key, 0); // its colour is the key's, so it changes with the focus
    lv_label_set_text(label, text);
    lv_obj_center(label);

    lv_obj_set_user_data(key, (void *) (intptr_t) action);
    lv_obj_add_event_cb(key, key_clicked, LV_EVENT_CLICKED, v);
    lv_obj_add_event_cb(key, key_nav, LV_EVENT_KEY, v);
    lv_obj_add_event_cb(key, back_pressed, LV_EVENT_CANCEL, v);
    lv_group_add_obj(v->group, key);
    return key;
}

// Left and right move along the keys and stop at the ends. The group's own
// next and previous went round through the screen itself, which takes no
// arrows or OK, so a press too many left nothing to come back with.
static void key_nav(lv_event_t *e) {
    home_fragment_t *v = lv_event_get_user_data(e);
    lv_obj_t *key = lv_event_get_current_target(e);
    int at = (int) lv_obj_get_index(key);
    int count = (int) lv_obj_get_child_cnt(v->keys);
    int to = at;
    switch (lv_event_get_key(e)) {
        case LV_KEY_LEFT:
        case LV_KEY_UP:
            to = at - 1;
            break;
        case LV_KEY_RIGHT:
        case LV_KEY_DOWN:
            to = at + 1;
            break;
        default:
            return;
    }
    if (at < 0 || to < 0 || to >= count) {
        return;
    }
    lv_group_focus_obj(lv_obj_get_child(v->keys, to));
}

// The screen has the focus only while it shows no key; any arrow or OK
// then goes to the first key, should one have come since.
static void root_key(lv_event_t *e) {
    home_fragment_t *v = lv_event_get_user_data(e);
    if (lv_obj_get_child_cnt(v->keys) > 0 && !lv_obj_has_flag(v->keys, LV_OBJ_FLAG_HIDDEN)) {
        lv_group_focus_obj(lv_obj_get_child(v->keys, 0));
    }
}

static void back_pressed(lv_event_t *e) {
    LV_UNUSED(e);
    app_quit_confirm();
}

static void play(home_fragment_t *v);

static void open_settings(home_fragment_t *v) {
    lv_fragment_t *fragment = lv_fragment_create(&settings_controller_cls, v->global);
    lv_obj_t *const *container = lv_fragment_get_container(lv_fragment_manager_get_top(v->global->ui.fm));
    lv_fragment_manager_push(v->global->ui.fm, fragment, container);
}

static void send_wol_cb(int result, const char *error, const uuidstr_t *uuid, void *userdata);

static void wake(void) {
    H.woke = true;
    H.wake_until = SDL_GetTicks() + 90000;
    set_state(HOME_WAKING);
    pcmanager_send_wol(pcmanager, &H.pc, send_wol_cb, NULL);
}

static void key_clicked(lv_event_t *e) {
    home_fragment_t *v = lv_event_get_user_data(e);
    lv_obj_t *key = lv_event_get_current_target(e);
    switch ((key_action_t) (intptr_t) lv_obj_get_user_data(key)) {
        case KEY_PLAY:
            play(v);
            break;
        case KEY_RETRY:
            begin();
            break;
        case KEY_WAKE:
            wake();
            break;
        case KEY_SIZE:
            // The next size the TV decodes, kept for the next stream.
            lootnest_size_next(&v->global->settings, (int) v->global->ss4s.video_cap.maxWidth,
                               (int) v->global->ss4s.video_cap.maxHeight);
            settings_save(&v->global->settings);
            picture_pressed = KEY_SIZE;
            render();
            break;
        case KEY_PICTURE:
            // The next quality, kept for the next stream.
            lootnest_picture_next(&v->global->settings);
            settings_save(&v->global->settings);
            picture_pressed = KEY_PICTURE;
            render();
            break;
        case KEY_SETTINGS:
            open_settings(v);
            break;
        case KEY_QUIT:
            app_quit_confirm();
            break;
    }
}

/* ------------------------------------------------------------------------------ */
/*                                    The view                                    */
/* ------------------------------------------------------------------------------ */

static void home_ctor(lv_fragment_t *self, void *args) {
    home_fragment_t *v = (home_fragment_t *) self;
    const home_fragment_args_t *a = args;
    v->global = a->app;
    v->params = a->params;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_static(l, "");
    return l;
}

static lv_obj_t *row(lv_obj_t *parent) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    return r;
}

// The screen in three bands, so nothing can sit over anything else: the
// mark and the name with the PC at the top, what's happening and the keys
// in the middle, and a foot with the controller, the hint and the version.
static lv_obj_t *home_create(lv_fragment_t *self, lv_obj_t *container) {
    home_fragment_t *v = (home_fragment_t *) self;
    app_ui_t *ui = &v->global->ui;
    fonts_init(ui);

    lv_obj_t *root = lv_obj_create(container);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(C_RAISED), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(C_GROUND), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(root, 0, 0);
    lv_obj_set_style_bg_grad_stop(root, 160, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(root, LV_DPX(48), 0);
    lv_obj_set_style_pad_ver(root, LV_DPX(32), 0);

    // Top: the brand mark (a ring over two arcs) and the name, the PC on the right.
    lv_obj_t *head = row(root);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, LV_DPX(12), 0);
    v->logo.header.w = LV_DPX(52);
    v->logo.header.h = LV_DPX(52);
    v->logo.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    v->logo.data_size = sizeof(lv_sdl_img_data_logo_96);
    v->logo.data = (const uint8_t *) &lv_sdl_img_data_logo_96;
    lv_obj_t *logo = lv_img_create(head);
    lv_img_set_src(logo, &v->logo);
    lv_obj_set_size(logo, LV_DPX(52), LV_DPX(52));
    lv_obj_t *title = label(head, F.title, C_TEXT);
    lv_label_set_text_static(title, "Lootnest");
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_LEFT, 0);
    v->pc_label = label(head, F.body, C_TEXT_2);
    lv_obj_set_style_text_align(v->pc_label, LV_TEXT_ALIGN_RIGHT, 0);

    // Middle: takes what the bands leave, its content centred in it.
    v->body = lv_obj_create(root);
    lv_obj_remove_style_all(v->body);
    lv_obj_clear_flag(v->body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(v->body, LV_PCT(100));
    lv_obj_set_flex_grow(v->body, 1);
    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(v->body, LV_DPX(12), 0);

    v->spinner = lv_spinner_create(v->body, 1000, 60);
    lv_obj_set_size(v->spinner, LV_DPX(40), LV_DPX(40));
    lv_obj_set_style_arc_width(v->spinner, LV_DPX(4), LV_PART_MAIN);
    lv_obj_set_style_arc_width(v->spinner, LV_DPX(4), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(v->spinner, lv_color_hex(C_KEY), LV_PART_MAIN);
    lv_obj_set_style_arc_color(v->spinner, lv_color_hex(C_TEXT), LV_PART_INDICATOR);
    lv_obj_clear_flag(v->spinner, LV_OBJ_FLAG_CLICKABLE);

    v->status = label(v->body, F.status, C_TEXT);
    v->detail = label(v->body, F.body, C_TEXT_3);
    lv_obj_set_width(v->detail, LV_PCT(70));
    lv_label_set_long_mode(v->detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(v->detail, LV_DPX(4), 0);

    v->keys = row(v->body);
    lv_obj_set_size(v->keys, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(v->keys, LV_DPX(14), 0);
    // Room for the focused key's growth and ring.
    lv_obj_set_style_pad_all(v->keys, LV_DPX(10), 0);
    lv_obj_set_style_pad_top(v->keys, LV_DPX(22), 0);

    // Foot: the controller on the left, the hint in the middle, the
    // version on the right, each in its own third.
    lv_obj_t *foot = row(root);
    lv_obj_set_size(foot, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(foot, LV_DPX(24), 0);

    // Which controller the TV has, or that it isn't recognised.
    v->pad = label(foot, F.small, C_TEXT_2);
    lv_obj_set_flex_grow(v->pad, 1);
    lv_obj_set_width(v->pad, 1);
    lv_label_set_long_mode(v->pad, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(v->pad, LV_TEXT_ALIGN_LEFT, 0);

    v->hint = label(foot, F.small, C_TEXT_3);
    lv_obj_set_flex_grow(v->hint, 1);
    lv_obj_set_width(v->hint, 1);
    lv_label_set_long_mode(v->hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text_static(v->hint, locstr("To come back here while you play, hold Start and Select for two seconds."));

    // Which version this is: what's on the TV is otherwise only known to
    // Lootnest on the PC.
    lv_obj_t *version = label(foot, F.small, C_TEXT_3);
    lv_obj_set_flex_grow(version, 1);
    lv_obj_set_width(version, 1);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_static(version, "Lootnest for TV " APP_VERSION);
    return root;
}

static void timer_cb(lv_timer_t *t);

static void home_created(lv_fragment_t *self, lv_obj_t *obj) {
    home_fragment_t *v = (home_fragment_t *) self;
    v->root = obj;
    v->group = lv_group_create();
    // With no key on screen, Back still reaches the screen.
    lv_group_add_obj(v->group, obj);
    lv_obj_add_event_cb(obj, back_pressed, LV_EVENT_CANCEL, v);
    lv_obj_add_event_cb(obj, root_key, LV_EVENT_KEY, v);
    app_input_set_group(&v->global->ui.input, v->group);
    pcmanager_register_listener(pcmanager, &listener, v);
    v->timer = lv_timer_create(timer_cb, 1000, v);
    home_view = v;

    // Settings may have changed the picture by hand, and a stream that just
    // ended may move Automatic a step.
    bool sync = lootnest_picture_sync(&v->global->settings);
    if (lootnest_picture_review(&v->global->settings) || sync) {
        settings_save(&v->global->settings);
    }

    if (!started) {
        started = true;
        begin();
    } else if (H.state == HOME_STARTING || H.streamed) {
        // Back from a stream: Play starts it again.
        H.streamed = false;
        set_state(HOME_READY);
    } else {
        render();
    }
}

static void home_will_delete(lv_fragment_t *self, lv_obj_t *obj) {
    LV_UNUSED(obj);
    home_fragment_t *v = (home_fragment_t *) self;
    if (home_view == v) {
        home_view = NULL;
    }
    if (v->apploader != NULL) {
        apploader_destroy(v->apploader);
        v->apploader = NULL;
    }
    lv_timer_del(v->timer);
    v->timer = NULL;
    pcmanager_unregister_listener(pcmanager, &listener);
    app_input_set_group(&v->global->ui.input, NULL);
    lv_group_del(v->group);
    v->group = NULL;
}

static bool home_event(lv_fragment_t *self, int code, void *userdata) {
    LV_UNUSED(userdata);
    home_fragment_t *v = (home_fragment_t *) self;
    if (code == USER_SIZE_CHANGED && self->obj != NULL) {
        lv_obj_set_size(self->obj, v->global->ui.width, v->global->ui.height);
    }
    return false;
}

static bool busy(home_state_t s) {
    return s == HOME_START || s == HOME_PAIRING || s == HOME_CONNECTING || s == HOME_WAKING || s == HOME_STARTING;
}

static void render_pad(home_fragment_t *v) {
    // Emptied rather than hidden: the foot's thirds stay where they are.
    const char *line = lootnest_controller_line();
    if (strcmp(lv_label_get_text(v->pad), line) != 0) {
        lv_label_set_text(v->pad, line);
    }
}

static void render(void) {
    home_fragment_t *v = home_view;
    if (v == NULL) {
        return;
    }
    render_pad(v);
    static char pc_line[96], status[128];
    const char *detail = "";
    const char *name = H.pc_name[0] ? H.pc_name : locstr("your PC");
    if (H.has_pc && H.pc_name[0]) {
        SDL_snprintf(pc_line, sizeof(pc_line), locstr("on %s"), H.pc_name);
        lv_label_set_text_static(v->pc_label, pc_line);
        lv_obj_clear_flag(v->pc_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(v->pc_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (H.state == HOME_READY || H.state == HOME_STARTING) {
        lv_label_set_text_static(v->hint, locstr("To come back here while you play, hold Start and Select for two seconds."));
    } else {
        lv_label_set_text_static(v->hint, "");
    }
    switch (H.state) {
        case HOME_START:
            SDL_strlcpy(status, locstr("Starting"), sizeof(status));
            break;
        case HOME_PAIRING:
            SDL_strlcpy(status, locstr("Pairing with your PC"), sizeof(status));
            detail = locstr("Lootnest on the PC finishes this by itself.");
            break;
        case HOME_CONNECTING:
            SDL_snprintf(status, sizeof(status), locstr("Connecting to %s"), name);
            break;
        case HOME_WAKING:
            SDL_snprintf(status, sizeof(status), locstr("Waking %s"), name);
            detail = locstr("This can take up to a minute.");
            break;
        case HOME_STARTING:
            SDL_strlcpy(status, locstr("Starting Lootnest"), sizeof(status));
            break;
        case HOME_READY: {
            // What the next stream asks for, and how the last one went.
            static char ready[320];
            const char *last = lootnest_last_stream_line();
            SDL_strlcpy(status, locstr("Ready"), sizeof(status));
            SDL_snprintf(ready, sizeof(ready), "%s%s%s", lootnest_picture_line(&v->global->settings), last[0] ? "\n" : "",
                         last);
            detail = ready;
            break;
        }
        case HOME_NO_PC:
            SDL_strlcpy(status, locstr("This TV isn't paired with a PC yet"), sizeof(status));
            detail = locstr("On your PC, open Lootnest, then Settings, TV, and Add an LG TV.");
            break;
        case HOME_OFFLINE:
            SDL_snprintf(status, sizeof(status), locstr("%s isn't answering"), name);
            detail = locstr("Turn the PC on, and check that it and the TV are on the same network.");
            break;
        case HOME_UNPAIRED:
            SDL_snprintf(status, sizeof(status), locstr("%s doesn't know this TV any more"), name);
            detail = locstr("On your PC, open Lootnest, then Settings, TV, and add this TV again.");
            break;
        case HOME_NO_APP:
            SDL_snprintf(status, sizeof(status), locstr("Lootnest isn't set up for the TV on %s"), name);
            detail = locstr("On your PC, open Lootnest, then Settings, TV.");
            break;
        case HOME_ERROR:
            SDL_strlcpy(status, locstr("Something went wrong"), sizeof(status));
            detail = H.detail;
            break;
    }
    lv_label_set_text_static(v->status, status);
    lv_label_set_text_static(v->detail, detail);
    if (detail[0]) {
        lv_obj_clear_flag(v->detail, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(v->detail, LV_OBJ_FLAG_HIDDEN);
    }
    if (busy(H.state)) {
        lv_obj_clear_flag(v->spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(v->spinner, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clean(v->keys);
    lv_obj_t *first = NULL;
    switch (H.state) {
        case HOME_READY: {
            first = add_key(v, locstr("Play"), KEY_PLAY, true);
            lv_obj_t *size = add_key(v, lootnest_size_key(&v->global->settings), KEY_SIZE, false);
            lv_obj_t *picture = add_key(v, lootnest_picture_key(&v->global->settings), KEY_PICTURE, false);
            if (picture_pressed == KEY_SIZE) {
                first = size;
            } else if (picture_pressed == KEY_PICTURE) {
                first = picture;
            }
            add_key(v, locstr("Settings"), KEY_SETTINGS, false);
            add_key(v, locstr("Quit"), KEY_QUIT, false);
            break;
        }
        case HOME_OFFLINE:
            first = add_key(v, locstr("Try again"), KEY_RETRY, true);
            add_key(v, locstr("Wake the PC"), KEY_WAKE, false);
            add_key(v, locstr("Settings"), KEY_SETTINGS, false);
            add_key(v, locstr("Quit"), KEY_QUIT, false);
            break;
        case HOME_NO_PC:
        case HOME_UNPAIRED:
        case HOME_NO_APP:
        case HOME_ERROR:
            first = add_key(v, locstr("Try again"), KEY_RETRY, true);
            add_key(v, locstr("Settings"), KEY_SETTINGS, false);
            add_key(v, locstr("Quit"), KEY_QUIT, false);
            break;
        default:
            break;
    }
    // The screen itself is in the group only while there's no key, so Back
    // still reaches it then; with keys, the focus never leaves them.
    if (first != NULL) {
        lv_group_remove_obj(v->root);
    } else if (lv_obj_get_group(v->root) == NULL) {
        lv_group_add_obj(v->group, v->root);
    }
    if (first != NULL) {
        lv_obj_clear_flag(v->keys, LV_OBJ_FLAG_HIDDEN);
        lv_group_focus_obj(first);
        if (app_ui_get_input_mode(&v->global->ui.input) & UI_INPUT_MODE_BUTTON_FLAG) {
            lv_obj_add_state(first, LV_STATE_FOCUS_KEY);
        }
    } else {
        lv_obj_add_flag(v->keys, LV_OBJ_FLAG_HIDDEN);
        lv_group_focus_obj(v->root);
    }
    picture_pressed = -1;
}

static void set_state(home_state_t state) {
    if (H.state != state) {
        commons_log_info("Lootnest", "Home: %d -> %d", H.state, state);
        static const char *names[] = {"start", "pairing", "connecting", "waking", "starting", "ready", "no PC",
                                      "offline", "unpaired", "no app", "error"};
        lootnest_note("Screen: %s%s%s", names[state], state == HOME_ERROR ? ": " : "",
                      state == HOME_ERROR ? H.detail : "");
    }
    H.state = state;
    render();
}

static void fail(const char *what, const char *error) {
    SDL_snprintf(H.detail, sizeof(H.detail), "%s%s%s", what, error && error[0] ? ": " : "", error ? error : "");
    set_state(HOME_ERROR);
}

/* ------------------------------------------------------------------------------ */
/*                                    The flow                                    */
/* ------------------------------------------------------------------------------ */

static void remember_pc(const uuidstr_t *uuid) {
    H.has_pc = true;
    H.pc = *uuid;
    const pclist_t *node = pcmanager_node(pcmanager, uuid);
    if (node != NULL && node->server != NULL && node->server->hostname != NULL) {
        SDL_strlcpy(H.pc_name, node->server->hostname, sizeof(H.pc_name));
    }
}

static void pair_cb(int result, const char *error, const uuidstr_t *uuid, void *userdata) {
    LV_UNUSED(userdata);
    lootnest_note("Pairing: %s", result == GS_OK ? "done" : (error ? error : "no answer"));
    if (result != GS_OK) {
        commons_log_warn("Lootnest", "Pairing failed: %s", error ? error : "no answer");
        fail(locstr("The PC didn't finish pairing"), error);
        return;
    }
    connect_pc(uuid);
}

static void pair(void) {
    H.pin_pending = false;
    set_state(HOME_PAIRING);
    if (!pcmanager_pair(pcmanager, &H.pc, H.pin, pair_cb, NULL)) {
        // Already paired, or the PC went away: look again.
        connect_pc(&H.pc);
    }
}

static void added_cb(int result, const char *error, const uuidstr_t *uuid, void *userdata) {
    LV_UNUSED(userdata);
    lootnest_note("Reaching the PC: %s", result == GS_OK && uuid != NULL ? "answered" : (error ? error : "no answer"));
    if (result != GS_OK || uuid == NULL) {
        commons_log_warn("Lootnest", "Couldn't reach the PC to pair: %s", error ? error : "no answer");
        fail(locstr("The TV couldn't reach your PC"), error);
        return;
    }
    remember_pc(uuid);
    pcmanager_select(pcmanager, uuid);
    const pclist_t *node = pcmanager_node(pcmanager, uuid);
    if (node != NULL && node->state.code == SERVER_STATE_NOT_PAIRED && H.pin_pending) {
        pair();
        return;
    }
    connect_pc(uuid);
}

static bool first_known(uuidstr_t *out, const app_launch_params_t *params) {
    if (params != NULL && !uuidstr_is_empty(&params->default_host_uuid) &&
        pcmanager_node(pcmanager, &params->default_host_uuid) != NULL) {
        *out = params->default_host_uuid;
        return true;
    }
    // A PC saved from an earlier run counts: it was paired then, and asking
    // it says whether it still is. (Saved PCs read as unpaired until asked.)
    const pclist_t *pick = NULL;
    for (const pclist_t *cur = pcmanager_servers(pcmanager); cur != NULL; cur = cur->next) {
        bool candidate = cur->known || (cur->server != NULL && cur->server->paired);
        if (!candidate) {
            continue;
        }
        if (cur->selected) {
            pick = cur;
            break;
        }
        if (pick == NULL) {
            pick = cur;
        }
    }
    if (pick == NULL) {
        return false;
    }
    *out = pick->id;
    return true;
}

static void begin(void) {
    H.woke = false;
    H.detail[0] = '\0';
    char host[128];
    // Launch parameters, else a request Lootnest left on the TV.
    bool asked = lootnest_take_pairing_host(host, sizeof(host));
    if (asked) {
        lootnest_mark_pair_file_used();
    } else if (lootnest_check_pair_file()) {
        asked = lootnest_take_pairing_host(host, sizeof(host));
    }
    if (asked) {
        H.pin_pending = lootnest_take_pin(H.pin);
        hostport_t *hp = hostport_parse(host);
        if (hp != NULL) {
            set_state(HOME_PAIRING);
            pcmanager_manual_add(pcmanager, hp, added_cb, NULL);
            return;
        }
    }
    uuidstr_t pc;
    if (H.has_pc && pcmanager_node(pcmanager, &H.pc) != NULL) {
        connect_pc(&H.pc);
    } else if (first_known(&pc, home_view ? home_view->params : NULL)) {
        connect_pc(&pc);
    } else {
        set_state(HOME_NO_PC);
    }
}

static void update_cb(int result, const char *error, const uuidstr_t *uuid, void *userdata) {
    LV_UNUSED(result);
    LV_UNUSED(error);
    LV_UNUSED(uuid);
    LV_UNUSED(userdata);
    evaluate();
}

static void connect_pc(const uuidstr_t *uuid) {
    remember_pc(uuid);
    pcmanager_select(pcmanager, uuid);
    set_state(HOME_CONNECTING);
    pcmanager_request_update(pcmanager, uuid, update_cb, NULL);
}

static void send_wol_cb(int result, const char *error, const uuidstr_t *uuid, void *userdata) {
    LV_UNUSED(uuid);
    LV_UNUSED(userdata);
    if (result != GS_OK) {
        commons_log_warn("Lootnest", "Wake-on-LAN failed: %s", error ? error : "");
    }
    evaluate();
}

static void apps_started(void *userdata) {
    LV_UNUSED(userdata);
}

static void apps_loaded(apploader_list_t *apps, void *userdata) {
    home_fragment_t *v = userdata;
    if (home_view != v || H.state != HOME_STARTING) {
        apploader_list_free(apps);
        return;
    }
    const apploader_item_t *found = NULL;
    for (size_t i = 0; i < apps->count; ++i) {
        if (apps->items[i].base.name != NULL && strcmp(apps->items[i].base.name, LOOTNEST_APP_NAME) == 0) {
            found = &apps->items[i];
            break;
        }
    }
    if (found == NULL) {
        apploader_list_free(apps);
        set_state(HOME_NO_APP);
        return;
    }
    streaming_scene_arg_t args = {
            .global = v->global,
            .uuid = H.pc,
            .app = found->base,
    };
    H.streamed = true;
    app_ui_t *ui = &v->global->ui;
    lv_fragment_t *fragment = lv_fragment_create(&streaming_controller_class, &args);
    apploader_list_free(apps);
    lv_obj_t *const *container = lv_fragment_get_container(lv_fragment_manager_get_top(ui->fm));
    lv_fragment_manager_push(ui->fm, fragment, container);
}

static void apps_errored(int code, const char *error, void *userdata) {
    LV_UNUSED(code);
    home_fragment_t *v = userdata;
    if (home_view != v || H.state != HOME_STARTING) {
        return;
    }
    fail(locstr("Your PC didn't list its apps"), error);
}

static const apploader_cb_t apps_cb = {
        .start = apps_started,
        .data = apps_loaded,
        .error = apps_errored,
};

static void play(home_fragment_t *v) {
    if (!H.has_pc) {
        begin();
        return;
    }
    set_state(HOME_STARTING);
    if (v->apploader != NULL) {
        apploader_destroy(v->apploader);
    }
    v->apploader = apploader_create(v->global, &H.pc, &apps_cb, v);
    apploader_load(v->apploader);
}

// evaluate moves on from the PC's state, as the engine reports it.
static void evaluate(void) {
    if (!H.has_pc || !(H.state == HOME_CONNECTING || H.state == HOME_WAKING)) {
        return;
    }
    const pclist_t *node = pcmanager_node(pcmanager, &H.pc);
    if (node == NULL) {
        set_state(HOME_NO_PC);
        return;
    }
    remember_pc(&H.pc);
    switch (node->state.code) {
        case SERVER_STATE_AVAILABLE:
            if (home_view != NULL) {
                play(home_view);
            }
            break;
        case SERVER_STATE_NOT_PAIRED:
            if (H.pin_pending) {
                pair();
            } else {
                set_state(HOME_UNPAIRED);
            }
            break;
        case SERVER_STATE_OFFLINE:
            if (!H.woke) {
                wake();
            } else if (H.state == HOME_WAKING && SDL_TICKS_PASSED(SDL_GetTicks(), H.wake_until)) {
                set_state(HOME_OFFLINE);
            }
            // While waking, the timer asks again.
            break;
        case SERVER_STATE_ERROR:
            fail(locstr("Your PC answered with an error"), node->state.error.errmsg);
            break;
        default:
            // Still asking.
            break;
    }
}

static void on_pc_updated(const uuidstr_t *uuid, void *userdata) {
    LV_UNUSED(userdata);
    if (H.has_pc && uuidstr_t_equals_t(uuid, &H.pc)) {
        evaluate();
    }
}

static void timer_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    if (home_view != NULL) {
        render_pad(home_view);
        // A stream's numbers can come in a moment after this screen is back.
        if (H.state == HOME_READY && lootnest_picture_review(&home_view->global->settings)) {
            settings_save(&home_view->global->settings);
            render();
        }
    }
    // A pairing request from Lootnest while this screen waits: the app was
    // already open when Lootnest asked.
    if (!busy(H.state) && lootnest_check_pair_file()) {
        begin();
        return;
    }
    if (H.state == HOME_WAKING && H.has_pc) {
        if (SDL_TICKS_PASSED(SDL_GetTicks(), H.wake_until)) {
            set_state(HOME_OFFLINE);
            return;
        }
        // Every third second: the PC is asked no more often than before.
        static int tick = 0;
        if (++tick % 3 == 0) {
            pcmanager_request_update(pcmanager, &H.pc, update_cb, NULL);
        }
    }
}
