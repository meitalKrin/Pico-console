#include "pico/stdlib.h"
#include "init.h"
#include "paint.h"
#include <stdio.h>

#define CURSOR_SIZE 5
#define GRID_WIDTH  (LCD_WIDTH / CURSOR_SIZE)   // 240 / 5 = 48
#define GRID_HEIGHT (LCD_HEIGHT / CURSOR_SIZE)  // 240 / 5 = 48

// Color palette definition
uint16_t paint_colors[] = {
    0xF800, // Red
    0x07E0, // Green
    0x001F, // Blue
    0xFFE0, // Yellow
    0xF81F, // Magenta
    0x07FF  // Cyan
};

static uint8_t num_colors = sizeof(paint_colors) / sizeof(paint_colors[0]);
static uint8_t color_index = 0;

// Canvas grid buffer to remember pixel colors across the whole screen (~4.6 KB)
static uint16_t canvas_grid[GRID_WIDTH][GRID_HEIGHT];

// Button debounce state tracking
static bool btn_b_last = true, btn_y_last = true, btn_x_last = true;
static uint64_t btn_b_time = 0, btn_y_time = 0, btn_x_time = 0;

static void courser(uint8_t x, uint8_t y, uint16_t color) {
    fill_rect(x, y, CURSOR_SIZE, CURSOR_SIZE, color);
}

// Restores the actual canvas pixel color at (x, y) instead of forcing white
void LCD_Restore_Area(uint8_t x, uint8_t y) {
    uint8_t gx = x / CURSOR_SIZE;
    uint8_t gy = y / CURSOR_SIZE;

    if (gx < GRID_WIDTH && gy < GRID_HEIGHT) {
        // Redraw saved color from canvas memory
        fill_rect(x, y, CURSOR_SIZE, CURSOR_SIZE, canvas_grid[gx][gy]);
    } else {
        fill_rect(x, y, CURSOR_SIZE, CURSOR_SIZE, COLOR_WHITE);
    }
}

void paint_ui_draw_color_wheel(void) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT / 10, COLOR_BLACK);
    fill_rect(0, 0, LCD_WIDTH / 10, LCD_HEIGHT / 10, paint_colors[color_index]);
    draw_text(35, 6, "[B]Color [X]Paint [Y]Exit", COLOR_WHITE, COLOR_BLACK, 1);
}

void paint(void) {
    // 1. Initialize canvas grid to solid white
    for (int gx = 0; gx < GRID_WIDTH; gx++) {
        for (int gy = 0; gy < GRID_HEIGHT; gy++) {
            canvas_grid[gx][gy] = COLOR_WHITE;
        }
    }

    // 2. Clear display and render UI
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_WHITE);
    paint_ui_draw_color_wheel();

    uint8_t cursor_x = 120;
    uint8_t cursor_y = 120;

    btn_y_last = true;

    // Draw initial cursor preview
    courser(cursor_x, cursor_y, paint_colors[color_index]);

    while (true) {
        // Exit to main menu on BTN_Y press
        if (button_pressed(BTN_Y, &btn_y_last, &btn_y_time)) {
            return;
        }

        // Cycle Colors on BTN_B press
        if (button_pressed(BTN_B, &btn_b_last, &btn_b_time)) {
            color_index = (color_index + 1) % num_colors; 
            paint_ui_draw_color_wheel();                  
        }

        // READ DIGITAL JOYSTICK
        int8_t dx = 0, dy = 0;
        if (!gpio_get(JOY_RIGHT)) dx =  1;
        if (!gpio_get(JOY_LEFT))  dx = -1;
        if (!gpio_get(JOY_DOWN))  dy =  1;
        if (!gpio_get(JOY_UP))    dy = -1;

        // Check if X button is held down
        bool is_painting = !gpio_get(BTN_X); 

        // Move cursor in 5-pixel grid increments so grid coordinates align perfectly
        if (dx != 0 || dy != 0) {
            const int8_t CURSOR_SPEED = 5; 
            const uint8_t TOP_MARGIN = LCD_HEIGHT / 10;

            int16_t new_x = (int16_t)cursor_x + dx * CURSOR_SPEED;
            int16_t new_y = (int16_t)cursor_y + dy * CURSOR_SPEED;

            // Bounds checking
            if (new_x < 0) new_x = 0;
            if (new_x > LCD_WIDTH - CURSOR_SIZE) new_x = LCD_WIDTH - CURSOR_SIZE;
            if (new_y < TOP_MARGIN) new_y = TOP_MARGIN;
            if (new_y > LCD_HEIGHT - CURSOR_SIZE) new_y = LCD_HEIGHT - CURSOR_SIZE;

            if (cursor_x != (uint8_t)new_x || cursor_y != (uint8_t)new_y) {
                // Restore old spot using true saved background color
                LCD_Restore_Area(cursor_x, cursor_y);

                cursor_x = (uint8_t)new_x;
                cursor_y = (uint8_t)new_y;
            }
        }

        // If painting, write new color to both display and canvas grid
        if (is_painting) {
            uint8_t gx = cursor_x / CURSOR_SIZE;
            uint8_t gy = cursor_y / CURSOR_SIZE;
            if (gx < GRID_WIDTH && gy < GRID_HEIGHT) {
                canvas_grid[gx][gy] = paint_colors[color_index];
            }
        }

        // Always draw active cursor preview
        courser(cursor_x, cursor_y, paint_colors[color_index]);

        sleep_ms(20);
    }
}