/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

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

#define LEN_FRAMES 267191

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

extern const uint8_t frames_enc[LEN_FRAMES];
static uint32_t frame_counter;

static lv_obj_t *video;

K_WORK_DEFINE(anim_work, anim_work_handler);

void anim_expiry_function() { k_work_submit(&anim_work); }
#define FRAME_SIZE (ANIM_SIZE * ANIM_SIZE / 8 + 8)
K_TIMER_DEFINE(anim_timer, anim_expiry_function, NULL);
uint8_t new_frame[FRAME_SIZE] = {
    0x00, 0x00, 0x00, 0xff, /*Color of index 0*/
    0xff, 0xff, 0xff, 0xff  /*Color of index 1*/
};
void anim_work_handler(struct k_work *work) {
    // Uncompressing the frames
    uint16_t f_idx = 8;    // Byte currently being written to in image
    uint8_t prev_byte = 0; // Partially filled byte from previous run
    uint8_t byte_idx = 0;  // How many bytes were filled from the last run
    // 0 Denotes end of current frame (since you can't have a run of zero 0's)
    for (uint8_t run = frames_enc[frame_counter]; run != 0; run = frames_enc[++frame_counter]) {
        int8_t bit = run >> 7;   // Fill bit is sign of stored int
        int8_t len = run & 0x7F; // Length is body of stored int
        uint8_t byte = 0;        // Byte to write to frame buffer
        uint8_t new_byte = (uint8_t)(-bit);
        // Last run left partially filled byte
        if (byte_idx) {
            byte = prev_byte | (new_byte >> byte_idx);
            int8_t bytes_remaining = 8 - byte_idx;
            // Case where current run is too short to fill up byte entirely
            if (bytes_remaining > len) {
                bytes_remaining -= len;
                prev_byte = (byte >> bytes_remaining) << bytes_remaining;
                byte_idx = byte_idx + len;
                continue;
            } else {
                len -= bytes_remaining;
                byte_idx = 0;
                new_frame[f_idx] = byte;
                f_idx++;
            }
        }
        // Write runs of 8 until length reached
        while (len >= 8) {
            new_frame[f_idx] = new_byte;
            len -= 8;
            f_idx++;
        }
        // If run did not end on byte boundary
        if (len) {
            byte_idx = len;
            // Runs of 0's require no additional writes
            if (bit) {
                uint8_t bytes_remaining = 8 - byte_idx;
                prev_byte = (new_byte >> bytes_remaining) << bytes_remaining;
            } else {
                prev_byte = new_byte; // Should be 0
            }
        }
    }
    lv_img_dsc_t frame = {
        .header.cf = LV_IMG_CF_INDEXED_1BIT,
        .header.always_zero = 0,
        .header.reserved = 0,
        .header.w = ANIM_SIZE,
        .header.h = ANIM_SIZE,
        .data_size = FRAME_SIZE,
        .data = new_frame,
    };
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);

    // Fill background
    lv_canvas_draw_rect(video, 0, 0, DISP_WIDTH, DISP_WIDTH, &rect_white_dsc);

    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    lv_canvas_draw_img(video, 2, 2, &frame, &img_dsc);
    frame_counter = (frame_counter + 1) % LEN_FRAMES;
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
    lv_canvas_set_buffer(video, widget->vbuf, DISP_WIDTH, DISP_WIDTH, LV_IMG_CF_TRUE_COLOR);
    frame_counter = 0;

    // Starting animation timer
    k_timer_start(&anim_timer, K_MSEC(10), K_MSEC(10));
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }