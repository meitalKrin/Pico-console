#include "pico/stdlib.h"
#include "init.h"
#include "paint.h"
#include <stdio.h>

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
//screen logic
const uint16_t center = 2048;
const uint16_t deadzone = 400;


// Button debounce state tracking
static bool btn_b_last = true,btn_y_last = true;
static uint64_t btn_b_time = 0,btn_y_time = 0;

void courser(uint8_t x, uint8_t y, uint16_t color){
    int courserSize=5;
    fill_rect(x, y, courserSize, courserSize, color);

}


uint16_t LCD_Restore_Area(uint8_t x, uint8_t y){


     

}


void paint_ui_draw_color_wheel(void) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT / 10, COLOR_BLACK);
    fill_rect(0, 0, LCD_WIDTH / 10, LCD_HEIGHT / 10, paint_colors[color_index]);
    draw_text(35, 6, "[B]Color [X]Paint [Y]Exit", COLOR_WHITE, COLOR_BLACK, 1);
}

void paint(void) {
    // Clear canvas and render UI
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_WHITE);
    paint_ui_draw_color_wheel();
    uint8_t cursor_x = 120;
    uint8_t cursor_y = 120;
    uint8_t prev_x = cursor_x;
    uint8_t prev_y = cursor_y;
    const uint16_t CENTER_X = 800;
    const uint16_t CENTER_Y = 690;
    const uint16_t DEADZONE = 200;
    
    while (true) {
        //  Cycle Colors
        if (button_pressed(BTN_B, &btn_b_last, &btn_b_time)) {
            color_index = (color_index + 1) % num_colors; 
            paint_ui_draw_color_wheel();                  
        }

        //READ ANALOG JOYSTICK
        uint16_t raw_x = joystick_read_x();
        uint16_t raw_y = joystick_read_y();

        prev_x = cursor_x;
        prev_y = cursor_y;

        // X-Axis Movement
        if (raw_x > CENTER_X + DEADZONE && cursor_x < LCD_WIDTH - 6) {
            cursor_x += 2; // Move Right
        } else if (raw_x < CENTER_X - DEADZONE && cursor_x > 2) {
            cursor_x -= 2; // Move Left
        }

        // Y-Axis Movement
        if (raw_y > CENTER_Y + DEADZONE && cursor_y < LCD_HEIGHT - 6) {
            cursor_y += 2; // Move Down
        } else if (raw_y < CENTER_Y - DEADZONE && cursor_y > (LCD_HEIGHT / 10) + 2) {
            cursor_y -= 2; // Move Up
        }

        //courser logic
        courser(cursor_x, cursor_y, paint_colors[color_index]);


        //  Exit App 
        if (button_pressed(BTN_Y, &btn_y_last, &btn_y_time)) {
           //
        }

        sleep_ms(10);
    }
}