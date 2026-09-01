#include "pico/stdlib.h"
#include "init.h"
#include "snake.h"
#include <stdio.h>
#include <stdlib.h>

#define TAIL_CAPACITY 200
#define CURSOR_SIZE 10 
// File-scoped static variables (prevents multiple definition linker errors)
static uint16_t apple_size = 10;
static uint16_t apple_x = 0;
static uint16_t apple_y = 0;
static uint32_t score = 0;
static int head = 0;

// Direction tracking
static int8_t dir_x = 1;
static int8_t dir_y = 0;

// History buffer to track tail positions
static uint16_t history_x[TAIL_CAPACITY];
static uint16_t history_y[TAIL_CAPACITY];

// Local score string buffer
static char score_buffer[32];

// Button debounce tracking
static bool btn_y_last = true;
static uint64_t btn_y_time = 0;

static void courser_snake(uint16_t x, uint16_t y, uint16_t color) {
    fill_rect(x, y, CURSOR_SIZE, CURSOR_SIZE, color);
}

void snake_ui(void) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT / 10, COLOR_BLACK);
    
    snprintf(score_buffer, sizeof(score_buffer), "SCORE: %lu", (unsigned long)score);
    draw_text(5, 5, score_buffer, COLOR_WHITE, COLOR_BLACK, 1);
    
    draw_text(LCD_WIDTH - 65, 5, "[Y] Exit", COLOR_WHITE, COLOR_BLACK, 1);
}

void GameOver(void) {
    fill_screen(COLOR_WHITE);
    draw_text(50, 80, "GAME OVER", COLOR_RED, COLOR_WHITE, 2);

    snprintf(score_buffer, sizeof(score_buffer), "SCORE: %lu", (unsigned long)score);
    draw_text(60, 120, score_buffer, COLOR_BLACK, COLOR_WHITE, 2);
    draw_text(40, 170, "Press [Y] to Exit", COLOR_BLACK, COLOR_WHITE, 1);

    bool btn_y_state = true;
    uint64_t btn_y_timer = 0;
    
    while (true) {
        if (button_pressed(BTN_Y, &btn_y_state, &btn_y_timer)) {
            break;
        }
        sleep_ms(20);
    }
}

void SpawnApple(void) {
    // Number of cells to pad away from the outer edges
    const uint16_t BORDER_PADDING = 5; 
    uint16_t top_margin = 20; 
    uint16_t playable_width = LCD_WIDTH;
    uint16_t playable_height = LCD_HEIGHT - top_margin;

    // Calculate maximum available grid cells in total
    uint16_t max_grid_x = playable_width / apple_size;
    uint16_t max_grid_y = playable_height / apple_size;

    // Ensure screen dimensions are large enough for the padding
    if (max_grid_x <= (BORDER_PADDING * 2) || max_grid_y <= (BORDER_PADDING * 2)) {
        return;
    }

    // Restrict random grid generation to the padded inner region
    uint16_t inner_grid_x = max_grid_x - (BORDER_PADDING * 2);
    uint16_t inner_grid_y = max_grid_y - (BORDER_PADDING * 2);

    // Calculate aligned screen position offset by the border padding
    apple_x = (BORDER_PADDING + (rand() % inner_grid_x)) * apple_size;
    apple_y = top_margin + (BORDER_PADDING + (rand() % inner_grid_y)) * apple_size;

    // Draw square apple
    fill_rect(apple_x, apple_y, apple_size, apple_size, COLOR_RED);
}

void snake(void) {
    srand(to_us_since_boot(get_absolute_time()));

    // Clear canvas and render initial UI
    fill_screen(COLOR_FIELD);
    
    score = 0;
    btn_y_last = true;

    // Reset starting direction (moving RIGHT)
    dir_x = 1;
    dir_y = 0;
    
    snake_ui();
    SpawnApple();

    uint16_t cursor_x = 120;
    uint16_t cursor_y = 120;
    const uint16_t TOP_MARGIN = LCD_HEIGHT / 10;

    // Initialize history buffer with initial head position
   for (int i = 0; i < TAIL_CAPACITY; i++) {
    history_x[i] = cursor_x;
    history_y[i] = cursor_y;
}

    // Draw initial cursor position
    courser_snake(cursor_x, cursor_y, COLOR_GREEN);

    while (true) {
        // Exit to main menu on BTN_Y press
        if (button_pressed(BTN_Y, &btn_y_last, &btn_y_time)) {
            return;
        }

        // READ JOYSTICK INPUT (Block 180-degree reversals)
        if (!gpio_get(JOY_RIGHT) && dir_x != -1) {
            dir_x = 1;
            dir_y = 0;
        } else if (!gpio_get(JOY_LEFT) && dir_x != 1) {
            dir_x = -1;
            dir_y = 0;
        } else if (!gpio_get(JOY_DOWN) && dir_y != -1) {
            dir_x = 0;
            dir_y = 1;
        } else if (!gpio_get(JOY_UP) && dir_y != 1) {
            dir_x = 0;
            dir_y = -1;
        }

        // ALWAYS MOVE 
        const int8_t CURSOR_SPEED = 5; // Movement step per frame

        int16_t new_x = (int16_t)cursor_x + dir_x * CURSOR_SPEED;
        int16_t new_y = (int16_t)cursor_y + dir_y * CURSOR_SPEED;

        // Wall Bounds Checking
        if (new_x < 0 || new_x > LCD_WIDTH - CURSOR_SIZE ||
            new_y < TOP_MARGIN || new_y > LCD_HEIGHT - CURSOR_SIZE) 
        {
            GameOver();
            return; // Exit game loop after Game Over
        }

        cursor_x = (uint16_t)new_x;
        cursor_y = (uint16_t)new_y;

        // Determine snake tail length based on score
        int tail_length = 5 + (score * 5);
        if (tail_length >= TAIL_CAPACITY) tail_length = TAIL_CAPACITY - 1;


       for (int i = 10; i < tail_length; i++) {
            if (cursor_x < history_x[(head + TAIL_CAPACITY - i) % TAIL_CAPACITY] + CURSOR_SIZE &&
             cursor_x + CURSOR_SIZE > history_x[(head + TAIL_CAPACITY - i) % TAIL_CAPACITY] &&
             cursor_y < history_y[(head + TAIL_CAPACITY - i) % TAIL_CAPACITY] + CURSOR_SIZE &&
             cursor_y + CURSOR_SIZE > history_y[(head + TAIL_CAPACITY - i) % TAIL_CAPACITY]) 
             {
        GameOver();
        return;
            }
        }
        // Erase the old tail segment from the screen
        courser_snake(history_x[(head + TAIL_CAPACITY - tail_length) % TAIL_CAPACITY],
              history_y[(head + TAIL_CAPACITY - tail_length) % TAIL_CAPACITY],
              COLOR_FIELD);

        // Shift position history right
         head = (head + 1) % TAIL_CAPACITY;
            history_x[head] = cursor_x;
            history_y[head] = cursor_y;


        // Insert new head position into history
   
        // Apple collision check 
        if (cursor_x < apple_x + apple_size &&
            cursor_x + CURSOR_SIZE > apple_x &&
            cursor_y < apple_y + apple_size &&
            cursor_y + CURSOR_SIZE > apple_y) 
        {
            // Erase eaten apple
            fill_rect(apple_x, apple_y, apple_size, apple_size, COLOR_FIELD);
            score++;
            snake_ui();
            SpawnApple();
        }

        // Draw active head position
        courser_snake(cursor_x, cursor_y, COLOR_GREEN);
        sleep_ms(25);
    }
}