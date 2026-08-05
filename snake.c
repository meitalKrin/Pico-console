
#include "pico/stdlib.h"
#include "init.h"
#include "snake.h"
#include <stdio.h>

//vars
static int score = 0;
// Button debounce tracking
static bool btn_y_last = true;
static uint64_t btn_y_time = 0;

void snake_ui(void) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT / 10, COLOR_BLACK);
    draw_text(5, 5, "SCORE", COLOR_WHITE, COLOR_BLACK, 1);
    draw_text( LCD_WIDTH -50 ,  5, "[Y] Exit", COLOR_WHITE, COLOR_BLACK, 1);
   
}

void snake (void) {
    // Clear canvas and render UI
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_FIELD);
    snake_ui();
    btn_y_last = true;
    
    while (true) {
        // Exit to main menu on BTN_Y press
        if (button_pressed(BTN_Y, &btn_y_last, &btn_y_time)) {
            return;
        }
    }
}