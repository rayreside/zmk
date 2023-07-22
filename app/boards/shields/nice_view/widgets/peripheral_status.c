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
#include <zephyr/random/rand32.h>
#include <zmk/display.h>
#include "peripheral_status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zephyr/random/rand32.h>

#define LEN_FRAMES 94310
#define LEN_DICT 2048
#define FPS 15
#define UPDATES_PER_FRAME 5
#define BOARD_R 68
#define BOARD_C 136
#define FRAME_L (BOARD_R * BOARD_C / 8 + 8)
static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static const uint16_t ms_per_frame = 1000 / FPS;
struct peripheral_status_state {
    bool connected;
};

static uint8_t board_bits[FRAME_L] = {
    0xff, 0xff, 0xff, 0xff, /*Color of index 1*/
    0x00, 0x00, 0x00, 0xff, /*Color of index 0*/
};
static uint32_t frame_counter;
static uint32_t lcg;
static lv_draw_img_dsc_t img_dsc;
static lv_obj_t *video;

K_WORK_DEFINE(anim_work, anim_work_handler);

void anim_expiry_function() { k_work_submit(&anim_work); }
#define FRAME_SIZE (ANIM_SIZE * ANIM_SIZE / 8 + 8)
K_TIMER_DEFINE(anim_timer, anim_expiry_function, NULL);
#define MAZE_WIDTH 68
#define MAZE_HEIGHT 34
#define MAZE_AREA (MAZE_WIDTH * MAZE_HEIGHT / 4)
static uint8_t maze[MAZE_HEIGHT][MAZE_WIDTH];
static int16_t stack[MAZE_AREA][2];
static uint16_t stack_top = 0;
typedef enum { NORTH = 1, SOUTH = 2, EAST = 4, WEST = 8, WALL = 16, VISITED = 32 } Direction;
static uint8_t perms[24][4] = {
    {NORTH, SOUTH, EAST, WEST}, {SOUTH, NORTH, EAST, WEST}, {EAST, NORTH, SOUTH, WEST},
    {NORTH, EAST, SOUTH, WEST}, {SOUTH, EAST, NORTH, WEST}, {EAST, SOUTH, NORTH, WEST},
    {EAST, SOUTH, WEST, NORTH}, {SOUTH, EAST, WEST, NORTH}, {WEST, EAST, SOUTH, NORTH},
    {EAST, WEST, SOUTH, NORTH}, {SOUTH, WEST, EAST, NORTH}, {WEST, SOUTH, EAST, NORTH},
    {WEST, NORTH, EAST, SOUTH}, {NORTH, WEST, EAST, SOUTH}, {EAST, WEST, NORTH, SOUTH},
    {WEST, EAST, NORTH, SOUTH}, {NORTH, EAST, WEST, SOUTH}, {EAST, NORTH, WEST, SOUTH},
    {SOUTH, NORTH, WEST, EAST}, {NORTH, SOUTH, WEST, EAST}, {WEST, SOUTH, NORTH, EAST},
    {SOUTH, WEST, NORTH, EAST}, {NORTH, WEST, SOUTH, EAST}, {WEST, NORTH, SOUTH, EAST}};
void set_block_at(int16_t row, int16_t col) {
    row = row * 2;
    col = col * 2;
    set_pixel_at(row, col);
    set_pixel_at(row + 1, col);
    set_pixel_at(row, col + 1);
    set_pixel_at(row + 1, col + 1);
}
void set_pixel_at(int16_t row, uint16_t col) {
    int16_t temp = row * BOARD_C + col;
    int16_t byte_idx = (temp >> 3) + 8;
    int8_t bit_idx = temp & 0x7;
    board_bits[byte_idx] |= (0x80 >> bit_idx);
}
void push(int16_t row, int16_t col) {
    stack[stack_top][0] = row;
    stack[stack_top][1] = col;
    maze[row][col] |= VISITED;
    set_block_at(row, col);
    stack_top++;
}
void pop(int16_t *row, int16_t *col) {
    *row = stack[stack_top - 1][0];
    *col = stack[stack_top - 1][1];
    stack_top--;
}
void peek(int16_t *row, int16_t *col) {
    *row = stack[stack_top - 1][0];
    *col = stack[stack_top - 1][1];
}
int8_t x_offset(uint8_t dir) {
    switch (dir) {
    case EAST:
        return 1;
    case WEST:
        return -1;
    default:
        return 0;
    }
}
int8_t y_offset(uint8_t dir) {
    switch (dir) {
    case SOUTH:
        return 1;
    case NORTH:
        return -1;
    default:
        return 0;
    }
}
uint8_t opposite(uint8_t dir) {
    switch (dir) {
    case NORTH:
        return SOUTH;
    case EAST:
        return WEST;
    case SOUTH:
        return NORTH;
    case WEST:
        return EAST;
    }
    return 0;
}
uint8_t make_maze_step() {
    if (stack_top == 0) {
        return 0;
    }
    int16_t row, col;
    uint8_t *order = perms[lcg % 24];
    lcg = lcg * 22695477 + 1;
    do {
        peek(&row, &col);
        uint8_t val = maze[row][col];
        for (int i = 0; i < 4; i++) {
            uint8_t dir = order[i];
            int16_t r_offset = y_offset(dir);
            int16_t c_offset = x_offset(dir);
            int16_t new_r = row + r_offset * 2;
            int16_t new_c = (int16_t)col + c_offset * 2;
            if (new_r < 0 || new_r >= MAZE_HEIGHT || new_c < 0 || new_c >= MAZE_WIDTH) {
                continue;
            }
            if ((val & dir) || maze[new_r][new_c] & VISITED) {
                continue;
            }
            maze[row][col] |= (dir | VISITED);
            maze[row + r_offset][col + c_offset] |= VISITED;
            maze[new_r][new_c] |= opposite(dir);
            set_block_at(row, col);
            set_block_at(row + r_offset, col + c_offset);
            push(new_r, new_c);
            return 1;
        }
        stack_top--;
    } while (stack_top > 0);
    return 0;
}
void init_maze() {
    for (int r = 0; r < MAZE_HEIGHT; r++) {
        for (int c = 0; c < MAZE_WIDTH; c++) {
            maze[r][c] = WALL;
        }
    }
    for (int i = 8; i < FRAME_L; i++) {
        board_bits[i] = 0x0;
    }
    uint16_t start_r = lcg % MAZE_HEIGHT;
    uint16_t start_c = (lcg >> 16) % MAZE_WIDTH;
    push(start_r, start_c);
}
lv_img_dsc_t frame = {.header.cf = LV_IMG_CF_INDEXED_1BIT,
                      .header.always_zero = 0,
                      .header.reserved = 0,
                      .header.w = BOARD_C,
                      .header.h = BOARD_R,
                      .data_size = FRAME_L,
                      .data = board_bits};
void anim_work_handler(struct k_work *work) {
    for (int i = 0; i < UPDATES_PER_FRAME; i++) {
        if (!make_maze_step()) {
            frame_counter++;
            if (frame_counter == FPS) {
                frame_counter = 0;
                init_maze();
            }
            break;
        }
    }
    lv_canvas_draw_img(video, 0, 0, &frame, &img_dsc);
    // Set timer to go off when animation finishes
    k_timer_start(&anim_timer, K_MSEC(ms_per_frame), K_MSEC(ms_per_frame));
}

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], struct status_state state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, DISP_WIDTH, BATTERY_HEIGHT + 3, &rect_black_dsc);

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
    lv_canvas_set_buffer(top, widget->cbuf, DISP_WIDTH, BATTERY_HEIGHT + 3, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();

    video = lv_canvas_create(widget->obj);
    lv_obj_align(video, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_set_buffer(video, widget->vbuf, 136, DISP_WIDTH, LV_IMG_CF_TRUE_COLOR);
    lv_draw_img_dsc_init(&img_dsc);
    frame_counter = 0;
    lcg = sys_rand32_get();
    init_maze();
    //  Starting animation timer
    k_timer_start(&anim_timer, K_MSEC(10), K_MSEC(10));
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }