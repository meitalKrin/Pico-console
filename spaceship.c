
#include "pico/stdlib.h"
#include "init.h"
#include "snake.h"
#include <stdio.h>

//vars
static uint32_t score = 0;
char score_buffer_spaceship[32];
// Button debounce tracking
static bool btn_y_last = true;
static uint64_t btn_y_time = 0;

void spaceship_ui(void) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT / 10, COLOR_BLACK);
    draw_text( LCD_WIDTH -50 ,  5, "[Y] Exit", COLOR_WHITE, COLOR_BLACK, 1);
}

void spaceship (void) {
    // Clear canvas and render UI
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_SPACE);
    spaceship_ui();
    btn_y_last = true;
    
    while (true) {
        
         // Exit to main menu on BTN_Y press
        if (button_pressed(BTN_Y, &btn_y_last, &btn_y_time)) {
            score = 0;
            return;
        }
        snprintf(score_buffer_spaceship, sizeof(score_buffer_spaceship), "SCORE: %lu", (unsigned long)score);
        draw_text(10, 5, score_buffer_spaceship, COLOR_WHITE, COLOR_BLACK, 1);
        score++;
       
    }
}