#pragma bank 255

#include <gbdk/platform.h>
#include <gbdk/metasprites.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "systemhelpers.h"
#include "musicmanager.h"
#include "joy.h"
#include "gbcamera.h"
#include "gbprinter.h"
#include "linkcable.h"
#include "screen.h"
#include "states.h"
#include "vector.h"
#include "load_save.h"
#include "fade_manager.h"
#include "protected.h"
#include "vwf.h"

#include "state_camera.h"
#include "state_thumbnails.h"
#include "state_gallery.h"

#include "misc_assets.h"

// audio assets
#include "sound_ok.h"
#include "sound_error.h"
#include "sound_transmit.h"
#include "sound_menu_alter.h"

// menus
#include "menus.h"
#include "menu_codes.h"
#include "menu_main.h"
#include "menu_yesno.h"

BANKREF(state_thumbnails)

bool selected_images[CAMERA_MAX_IMAGE_SLOTS];

static uint8_t thumbnails_num_pages = 0, thumbnails_page_no = 0, cx = 0, cy = 0, cursor_anim = 0;

const metasprite_t gallery_cursor0[] = {
	METASPR_ITEM(16,  8, 0, 0), METASPR_ITEM(0,  24, 1, 0),
    METASPR_ITEM(24,  0, 3, 0), METASPR_ITEM(0, -24, 2, 0),
    METASPR_TERM
};
const metasprite_t gallery_cursor1[] = {
	METASPR_ITEM(17,  9, 0, 0), METASPR_ITEM(0,  22, 1, 0),
    METASPR_ITEM(22,  0, 3, 0), METASPR_ITEM(0, -22, 2, 0),
    METASPR_TERM
};
const metasprite_t gallery_cursor2[] = {
	METASPR_ITEM(18, 10, 0, 0), METASPR_ITEM(0,  20, 1, 0),
    METASPR_ITEM(20,  0, 3, 0), METASPR_ITEM(0, -20, 2, 0),
    METASPR_TERM
};
const metasprite_t * const gallery_cursor[] = {gallery_cursor0, gallery_cursor1, gallery_cursor2, gallery_cursor1};

const thumb_coord_t thumbnail_coords[MAX_PREVIEW_THUMBNAILS] = {
    {2,  1}, {6,  1}, {10,  1}, {14,  1},
    {2,  5}, {6,  5}, {10,  5}, {14,  5},
    {2,  9}, {6,  9}, {10,  9}, {14,  9},
    {2, 13}, {6, 13}, {10, 13}, {14, 13}

};

uint8_t onHelpThumbnailMenu(const struct menu_t * menu, const struct menu_item_t * selection);
uint8_t onTranslateSubResultThumbnailMenu(const struct menu_t * menu, const struct menu_item_t * self, uint8_t value);
const menu_item_t ThumbnailMenuItems[] = {
    {
        .sub = &YesNoMenu, .sub_params = "Delete selected?",
        .ofs_x = 1, .ofs_y = 1, .width = 11,
        .caption = " Delete selected",
        .helpcontext = " Delete selected images",
        .onPaint = NULL,
        .result = ACTION_DELETE_SELECTED
    }, {
        .sub = NULL, .sub_params = NULL,
        .ofs_x = 1, .ofs_y = 2, .width = 11,
        .caption = " Print selected",
        .helpcontext = " Print selected images",
        .onPaint = NULL,
        .result = ACTION_PRINT_SELECTED
    }, {
        .sub = NULL, .sub_params = NULL,
        .ofs_x = 1, .ofs_y = 3, .width = 11,
        .caption = " Transfer selected",
        .helpcontext = " Transfer selected images",
        .onPaint = NULL,
        .result = ACTION_TRANSFER_SELECTED
    }
};
const menu_t ThumbnailMenu = {
    .x = 1, .y = 4, .width = 13, .height = 5,
    .cancel_mask = J_B, .cancel_result = ACTION_NONE,
    .items = ThumbnailMenuItems, .last_item = LAST_ITEM(ThumbnailMenuItems),
    .onShow = NULL, .onHelpContext = onHelpThumbnailMenu,
    .onTranslateKey = NULL, .onTranslateSubResult = onTranslateSubResultThumbnailMenu
};

uint8_t onTranslateSubResultThumbnailMenu(const struct menu_t * menu, const struct menu_item_t * self, uint8_t value) {
    menu;
    if (self->sub == &YesNoMenu) {
        return (value == MENU_RESULT_YES) ? self->result : ACTION_NONE;
    }
    return value;
}
uint8_t onHelpThumbnailMenu(const struct menu_t * menu, const struct menu_item_t * selection) {
    menu;
    menu_text_out(0, 17, HELP_CONTEXT_WIDTH, SOLID_BLACK, selection->helpcontext);
    return 0;
}


inline uint8_t coords_to_index(uint8_t x, uint8_t y) {
    return (thumbnails_page_no << 4) | (y << 2) | x;
}
inline uint8_t coords_to_picture_no(uint8_t x, uint8_t y) {
    uint8_t image_count = VECTOR_LEN(used_slots);
    if (image_count) {
        uint8_t no = coords_to_index(x, y);
        return (no < image_count) ? no : (image_count - 1);
    } else return 0;
}

void tumbnail_refresh(uint8_t index) {
    uint8_t pos = index - (thumbnails_page_no * MAX_PREVIEW_THUMBNAILS);
    if (index < VECTOR_LEN(used_slots)) {
        uint8_t slot = VECTOR_GET(used_slots, index);
        SWITCH_RAM((slot >> 1) + 1);
        screen_load_thumbnail(thumbnail_coords[pos].x, thumbnail_coords[pos].y, ((slot & 1) ? image_second_thumbnail : image_first_thumbnail), 0xFF);
        if (selected_images[index]) menu_text_out(thumbnail_coords[pos].x + 3, thumbnail_coords[pos].y + 3, 0, SOLID_BLACK, ICON_CBX_CHECKED);
        screen_restore_rect(thumbnail_coords[pos].x, thumbnail_coords[pos].y, CAMERA_THUMB_TILE_WIDTH, CAMERA_THUMB_TILE_HEIGHT);
    } else {
        screen_clear_rect(thumbnail_coords[pos].x, thumbnail_coords[pos].y, CAMERA_THUMB_TILE_WIDTH, CAMERA_THUMB_TILE_HEIGHT, SOLID_BLACK);
    }
}

uint8_t tumbnails_diaplay(uint8_t start) {
    wait_vbl_done();
    screen_clear_rect(THUMBNAIL_DISPLAY_X, THUMBNAIL_DISPLAY_Y, THUMBNAIL_DISPLAY_WIDTH, THUMBNAIL_DISPLAY_HEIGHT, SOLID_BLACK);
    for (uint8_t i = start, j = 0; (i < VECTOR_LEN(used_slots)) && (j != MAX_PREVIEW_THUMBNAILS); i++, j++) {
        uint8_t slot = VECTOR_GET(used_slots, i);
        SWITCH_RAM((slot >> 1) + 1);
        screen_load_thumbnail(thumbnail_coords[j].x, thumbnail_coords[j].y, ((slot & 1) ? image_second_thumbnail : image_first_thumbnail), 0xFF);
        if (selected_images[i]) menu_text_out(thumbnail_coords[j].x + 3, thumbnail_coords[j].y + 3, 0, SOLID_BLACK, ICON_CBX_CHECKED);
        screen_restore_rect(thumbnail_coords[j].x, thumbnail_coords[j].y, CAMERA_THUMB_TILE_WIDTH, CAMERA_THUMB_TILE_HEIGHT);
    }
    return TRUE;
}

static void refresh_screen() {
    screen_clear_rect(0, 0, 20, 18, SOLID_BLACK);
    menu_text_out(0, 0, 20, SOLID_BLACK, " Thumbnail view");

    tumbnails_diaplay(thumbnails_page_no * MAX_PREVIEW_THUMBNAILS);

    sprintf(text_buffer, "%hd/%hd", (uint8_t)((OPTION(gallery_picture_idx) < images_taken()) ? (OPTION(gallery_picture_idx) + 1) : 0), (uint8_t)images_taken());
    menu_text_out(HELP_CONTEXT_WIDTH, 17, IMAGE_SLOTS_USED_WIDTH, SOLID_BLACK, text_buffer);
}

uint8_t INIT_state_thumbnails() BANKED {
    return 0;
}

uint8_t ENTER_state_thumbnails() BANKED {
    thumbnails_num_pages = VECTOR_LEN(used_slots) >> 4;
    if (VECTOR_LEN(used_slots) & 0x0f) thumbnails_num_pages++;

    thumbnails_page_no = (OPTION(gallery_picture_idx) >> 4);

    cx = OPTION(gallery_picture_idx) & 0x03, cy = (OPTION(gallery_picture_idx) >> 2) & 0x03;

    memset(selected_images, 0, sizeof(selected_images));

    gbprinter_set_handler(NULL, 0);

    refresh_screen();
    fade_in_modal();
    JOYPAD_RESET();
    return 0;
}

uint8_t UPDATE_state_thumbnails() BANKED {
    static uint8_t menu_result;
    PROCESS_INPUT();
    if (KEY_PRESSED(J_UP)) {
        if (cy) --cy, music_play_sfx(BANK(sound_menu_alter), sound_menu_alter, SFX_MUTE_MASK(sound_menu_alter), MUSIC_SFX_PRIORITY_MINIMAL);
    } else if (KEY_PRESSED(J_DOWN)) {
        if (cy < (THUMBS_COUNT_Y - 1)) ++cy, music_play_sfx(BANK(sound_menu_alter), sound_menu_alter, SFX_MUTE_MASK(sound_menu_alter), MUSIC_SFX_PRIORITY_MINIMAL);
    } else if (KEY_PRESSED(J_LEFT)) {
        if (!cx) {
            uint8_t old_page = thumbnails_page_no;
            if (thumbnails_page_no) --thumbnails_page_no; else thumbnails_page_no = thumbnails_num_pages - 1;
            cx = THUMBS_COUNT_X - 1;
            if (old_page != thumbnails_page_no) {
                hide_sprites_range(0, MAX_HARDWARE_SPRITES);
                tumbnails_diaplay(thumbnails_page_no * MAX_PREVIEW_THUMBNAILS);
            }
        } else --cx;
        music_play_sfx(BANK(sound_menu_alter), sound_menu_alter, SFX_MUTE_MASK(sound_menu_alter), MUSIC_SFX_PRIORITY_MINIMAL);
    } else if (KEY_PRESSED(J_RIGHT)) {
        if (++cx == THUMBS_COUNT_X) {
            uint8_t old_page = thumbnails_page_no;
            if (thumbnails_page_no < thumbnails_num_pages - 1) ++thumbnails_page_no; else thumbnails_page_no = 0;
            cx = 0;
            if (old_page != thumbnails_page_no) {
                hide_sprites_range(0, MAX_HARDWARE_SPRITES);
                tumbnails_diaplay(thumbnails_page_no * MAX_PREVIEW_THUMBNAILS);
            }
        };
        music_play_sfx(BANK(sound_menu_alter), sound_menu_alter, SFX_MUTE_MASK(sound_menu_alter), MUSIC_SFX_PRIORITY_MINIMAL);
    } else if (KEY_PRESSED(J_A)) {
        OPTION(gallery_picture_idx) = coords_to_picture_no(cx, cy);
        save_camera_state();
        music_play_sfx(BANK(sound_ok), sound_ok, SFX_MUTE_MASK(sound_ok), MUSIC_SFX_PRIORITY_MINIMAL);
        CHANGE_STATE(state_gallery);
        return 0;
    } else if (KEY_PRESSED(J_B)) {
        uint8_t idx = coords_to_index(cx, cy);
        if (idx < VECTOR_LEN(used_slots)) {
            selected_images[idx] = !selected_images[idx];
            tumbnail_refresh(idx);
        } else music_play_sfx(BANK(sound_error), sound_error, SFX_MUTE_MASK(sound_error), MUSIC_SFX_PRIORITY_MINIMAL);
    } else if (KEY_PRESSED(J_SELECT)) {
        switch (menu_result = menu_execute(&ThumbnailMenu, NULL, NULL)) {
            case ACTION_DELETE_SELECTED:
                for (int8_t i = CAMERA_MAX_IMAGE_SLOTS - 1; i >= 0; i--) {
                    if (selected_images[i]) {
                        uint8_t elem = VECTOR_GET(used_slots, i);
                        VECTOR_DEL(used_slots, i);
                        protected_modify_slot(elem, CAMERA_IMAGE_DELETED);
                        VECTOR_ADD(free_slots, elem);
                    }
                }
                protected_pack(used_slots);

                // update page count
                thumbnails_num_pages = VECTOR_LEN(used_slots) >> 4;
                if (VECTOR_LEN(used_slots) & 0x0f) thumbnails_num_pages++;
                // update page no
                thumbnails_page_no = (coords_to_picture_no(cx, cy) >> 4);

                music_play_sfx(BANK(sound_ok), sound_ok, SFX_MUTE_MASK(sound_ok), MUSIC_SFX_PRIORITY_MINIMAL);
                break;
            case ACTION_PRINT_SELECTED:
            case ACTION_TRANSFER_SELECTED: {
                uint8_t transfer_completion = 0, image_count = 0;
                // count selected images
                for (uint8_t i = 0; i != images_taken(); i++) image_count += (selected_images[i]);
                if (image_count == 0) {
                    music_play_sfx(BANK(sound_error), sound_error, SFX_MUTE_MASK(sound_error), MUSIC_SFX_PRIORITY_MINIMAL);
                    break;
                }
                // print or transfer selected images
                remote_activate(REMOTE_DISABLED);
                if (menu_result == ACTION_TRANSFER_SELECTED) {
                    linkcable_transfer_reset();
                    music_play_sfx(BANK(sound_transmit), sound_transmit, SFX_MUTE_MASK(sound_transmit), MUSIC_SFX_PRIORITY_MINIMAL);
                }
                gallery_show_progressbar(0, 0, PRN_MAX_PROGRESS);
                for (uint8_t i = 0, j = 0; i != images_taken(); i++) {
                    if (!(selected_images[i])) continue;
                    if (!((menu_result == ACTION_TRANSFER_SELECTED) ? gallery_transfer_picture(i) : gallery_print_picture(i, OPTION(print_frame_idx)))) {
                        music_play_sfx(BANK(sound_error), sound_error, SFX_MUTE_MASK(sound_error), MUSIC_SFX_PRIORITY_MINIMAL);
                        break;
                    }
                    uint8_t current_progress = (((uint16_t)++j * PRN_MAX_PROGRESS) / image_count);
                    if (transfer_completion != current_progress) {
                        transfer_completion = current_progress;
                        gallery_show_progressbar(0, current_progress, PRN_MAX_PROGRESS);
                    }
                }
                remote_activate(REMOTE_ENABLED);
                JOYPAD_RESET();
                break;
            }
            default:
                // error, must not get here
                music_play_sfx(BANK(sound_error), sound_error, SFX_MUTE_MASK(sound_error), MUSIC_SFX_PRIORITY_MINIMAL);
                break;
        }
        // clear selection
        memset(selected_images, 0, sizeof(selected_images));

        refresh_screen();
    } else if (KEY_PRESSED(J_START)) {
        // run Main Menu
        hide_sprites_range(0, MAX_HARDWARE_SPRITES);
        if (!menu_main_execute()) refresh_screen();
    }
    hide_sprites_range(move_metasprite(gallery_cursor[cursor_anim], 0xfa, 0, ((cx << 2) + 2) << 3, ((cy << 2) + 1) << 3), MAX_HARDWARE_SPRITES);
    if ((sys_time & 0x07) == 0) cursor_anim = ++cursor_anim & 0x03;
    return TRUE;
}

uint8_t LEAVE_state_thumbnails() BANKED {
    fade_out_modal();
    hide_sprites_range(0, MAX_HARDWARE_SPRITES);
    return 0;
}
