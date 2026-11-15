#include "res.h"

#include "gen/lootnest.h"
#include "gen/fav_indicator.h"
#include "gen/defcover.h"
#include "gen/material_icons_regular_ttf.h"
#include "gen/tilt_warp_ttf.h"
#include "gen/atkinson_next_ttf.h"
#include "gen/atkinson_next_bold_ttf.h"

const lv_sdl_img_data_t lv_sdl_img_data_logo_96 = {
        .type = LV_SDL_IMG_TYPE_CONST_PTR,
        .data.constptr = res_lootnest_data,
        .data_len = res_lootnest_size,
};

const lv_sdl_img_data_t lv_sdl_img_data_fav_indicator = {
        .type = LV_SDL_IMG_TYPE_CONST_PTR,
        .data.constptr = res_fav_indicator_data,
        .data_len = res_fav_indicator_size,
};

const lv_sdl_img_data_t lv_sdl_img_data_defcover = {
        .type = LV_SDL_IMG_TYPE_CONST_PTR,
        .data.constptr = res_defcover_data,
        .data_len = res_defcover_size,
};

const unsigned char *res_mat_iconfont_data = ttf_material_icons_regular_data;
const unsigned int res_mat_iconfont_size = ttf_material_icons_regular_size;

const unsigned char *res_tilt_warp_data = res_tilt_warp_ttf_data;
const unsigned int res_tilt_warp_size = res_tilt_warp_ttf_size;
const unsigned char *res_atkinson_data = res_atkinson_next_ttf_data;
const unsigned int res_atkinson_size = res_atkinson_next_ttf_size;
const unsigned char *res_atkinson_bold_data = res_atkinson_next_bold_ttf_data;
const unsigned int res_atkinson_bold_size = res_atkinson_next_bold_ttf_size;
