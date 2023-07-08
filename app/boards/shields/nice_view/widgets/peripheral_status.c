/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/random/rand32.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include "peripheral_status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

LV_IMG_DECLARE(dvd_img);

#define NUM_FRAMES 1250
#define NUM_CHUNKS 14961

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

static lv_anim_t dvd;

extern const uint8_t chunks[NUM_CHUNKS][8];
extern const uint16_t frames[NUM_FRAMES][64];

static uint16_t frame_counter;
static lv_obj_t *video;

K_WORK_DEFINE(anim_work, anim_work_handler);

void anim_expiry_function() { k_work_submit(&anim_work); }

K_TIMER_DEFINE(anim_timer, anim_expiry_function, NULL);

void anim_work_handler(struct k_work *work) {
    const uint16_t *frame_compressed = frames[frame_counter];
    // Each frame is 64x64/8 = 512 bytes
    uint8_t new_frame[520] = {
        0x00, 0x00, 0x00, 0xff, /*Color of index 0*/
        0xff, 0xff, 0xff, 0xff  /*Color of index 1*/
    };
    // Uncompressing the frames
    uint8_t chunk_idx = 0;
    // Iterating through each row of chunks, 1 chunk = 8x8 pixels or 8 bytes
    for (int c_row = 0; c_row < 8; c_row++) {
        // Iterating through each column of chunks
        for (int c_col = 0; c_col < 8; c_col++) {
            const uint8_t *chunk_data = chunks[frame_compressed[chunk_idx]];
            for (int i = 0; i < 8; i++) {
                // Calculating the byte index
                // 8 bytes per row, 64 bytes per 8 rows/1 chunk row
                // 1 byte per 8 columns/1 chunk column
                // 8 bytes reserved for header
                new_frame[c_row * 64 + i * 8 + c_col + 8] = chunk_data[i];
            }
            chunk_idx++;
        }
    }
    lv_img_dsc_t frame = {
        .header.cf = LV_IMG_CF_INDEXED_1BIT,
        .header.always_zero = 0,
        .header.reserved = 0,
        .header.w = 64,
        .header.h = 64,
        .data_size = 520,
        .data = new_frame,
    };
    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    lv_canvas_draw_img(video, 0, 0, &frame, &img_dsc);
    frame_counter = (frame_counter + 1) % NUM_FRAMES;
    // Set timer to go off when animation finishes
    k_timer_start(&anim_timer, K_MSEC(67), K_MSEC(67));
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], struct status_state state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, DISP_WIDTH, BATTERY_HEIGHT, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    lv_canvas_draw_text(canvas, 0, 0, DISP_WIDTH, &label_dsc,
                        state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state) {
        .level = bt_bas_get_battery_level(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_status *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, widget->cbuf, widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 160, 68);
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, BATTERY_OFFSET, 0);
    lv_canvas_set_buffer(top, widget->cbuf, DISP_WIDTH, BATTERY_HEIGHT, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

    video = lv_canvas_create(widget->obj);
    lv_obj_align(video, LV_ALIGN_TOP_LEFT, BATTERY_OFFSET, 0);
    lv_canvas_set_buffer(video, widget->vbuf, ANIM_SIZE, ANIM_SIZE, LV_IMG_CF_TRUE_COLOR);
    frame_counter = 0;

    // Starting animation timer
    k_timer_start(&anim_timer, K_MSEC(10), K_MSEC(10));
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }