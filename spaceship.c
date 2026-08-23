#include "pico/stdlib.h"
#include "init.h"
#include "snake.h"
#include <stdio.h>
#include <stdlib.h>

#define CURSOR_SIZE 5

// External function prototypes
extern void EraseArea(uint16_t x, uint16_t y);

// Vars
static uint32_t score = 0;
static char score_buffer_spaceship[32];

struct Node {
    uint16_t x;
    uint16_t y;
    struct Node* next;
};

static struct Node *starList = NULL;
static const uint8_t starsize = 5;

// Button debounce tracking
static bool btn_y_last = true;
static uint64_t btn_y_time = 0;

static void courser_spaceship(uint16_t x, uint16_t y, uint16_t color) {
    fill_rect(x, y, CURSOR_SIZE, CURSOR_SIZE, color);
}

// Free memory when exiting game loop
void cleanup_stars(void) {
    struct Node *current = starList;
    while (current != NULL) {
        struct Node *next = current->next;
        free(current);
        current = next;
    }
    starList = NULL;
}
void EraseArea(uint16_t x, uint16_t y) {
    
    fill_rect(x, y, 5, 5, COLOR_SPACE);
}
void GameOver_spaceship(void) {
    fill_screen(COLOR_WHITE);
    draw_text(50, 80, "GAME OVER", COLOR_RED, COLOR_WHITE, 2);

    snprintf(score_buffer_spaceship, sizeof(score_buffer_spaceship), "SCORE: %lu", (unsigned long)score);
    draw_text(60, 120, score_buffer_spaceship, COLOR_BLACK, COLOR_WHITE, 2);
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

// Star logic
struct Node* createStar(uint16_t startrandomX) {
    struct Node *newNode = (struct Node *)malloc(sizeof(struct Node));
    if (newNode == NULL) return NULL;
    newNode->y = LCD_HEIGHT / 10; 
    newNode->x = startrandomX;
    newNode->next = NULL;
    return newNode;
}

void movingStars(void) {
    // 1 in 10 chance per frame to spawn a star
    if ((rand() % 10) == 0) {
        uint16_t spawn_x = rand() % (LCD_WIDTH - starsize);
        struct Node *newStar = createStar(spawn_x);
        if (newStar != NULL) {
            newStar->next = starList;
            starList = newStar;
        }
    }
}

void moveStar(void) {
    struct Node *temp = starList;
    while (temp != NULL) {
        // Erase old position before moving Y
        EraseArea(temp->x, temp->y);
        
        temp->y += 5;

        // Reset to top if star goes off bottom of screen
        if (temp->y >= LCD_HEIGHT - starsize) {
            temp->y = LCD_HEIGHT / 10;
            temp->x = rand() % (LCD_WIDTH - starsize);
        }
        temp = temp->next;
    }
}

bool PrintStarsAndCheckCollision(uint16_t player_x, uint16_t player_y) {
    struct Node *temp = starList;

    while (temp != NULL) {
        // Draw star block directly using Pico UI driver
        fill_rect(temp->x, temp->y, starsize, starsize, COLOR_WHITE);

        // AABB Collision Detection (Player vs Star)
        if (player_x < temp->x + starsize &&
            player_x + CURSOR_SIZE > temp->x &&
            player_y < temp->y + starsize &&
            player_y + CURSOR_SIZE > temp->y) {
            
            return true; // Collision detected
        }

        temp = temp->next;
    }
    return false;
}

void spaceship_ui(void) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT / 10, COLOR_BLACK);
    draw_text(LCD_WIDTH - 50, 5, "[Y] Exit", COLOR_WHITE, COLOR_BLACK, 1);
}

void spaceship(void) {
    // Ensure clean star list state before starting
    cleanup_stars();
    
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_SPACE);
    spaceship_ui();
    btn_y_last = true;
    
    uint16_t cursor_x = LCD_WIDTH / 2;
    uint16_t cursor_y = LCD_HEIGHT - 20;
    
    uint16_t prev_x = cursor_x;
    uint16_t prev_y = cursor_y;

    while (true) {
        // Exit on BTN_Y
        if (button_pressed(BTN_Y, &btn_y_last, &btn_y_time)) {
            cleanup_stars();
            score = 0;
            return;
        }

        snprintf(score_buffer_spaceship, sizeof(score_buffer_spaceship), "SCORE: %lu", (unsigned long)score);
        draw_text(10, 5, score_buffer_spaceship, COLOR_WHITE, COLOR_BLACK, 1);
        score++;

        // Read Joystick
        int8_t dx = 0, dy = 0;
        if (!gpio_get(JOY_RIGHT)) dx =  1;
        if (!gpio_get(JOY_LEFT))  dx = -1;
        if (!gpio_get(JOY_DOWN))  dy =  1;
        if (!gpio_get(JOY_UP))    dy = -1;

        if (dx != 0 || dy != 0) {
            const int8_t CURSOR_SPEED = 5; 
            const uint16_t TOP_MARGIN = LCD_HEIGHT / 10;

            int32_t new_x = (int32_t)cursor_x + dx * CURSOR_SPEED;
            int32_t new_y = (int32_t)cursor_y + dy * CURSOR_SPEED;

            if (new_x < 0) new_x = 0;
            if (new_x > LCD_WIDTH - CURSOR_SIZE) new_x = LCD_WIDTH - CURSOR_SIZE;
            if (new_y < TOP_MARGIN) new_y = TOP_MARGIN;
            if (new_y > LCD_HEIGHT - CURSOR_SIZE) new_y = LCD_HEIGHT - CURSOR_SIZE;

            cursor_x = (uint16_t)new_x;
            cursor_y = (uint16_t)new_y;
        }

        // Erase old ship frame
        if (cursor_x != prev_x || cursor_y != prev_y) {
            courser_spaceship(prev_x, prev_y, COLOR_SPACE);
            prev_x = cursor_x;
            prev_y = cursor_y;
        }

        //STAR PIPELINE
        movingStars();                                       
        moveStar();                                           
        if (PrintStarsAndCheckCollision(cursor_x, cursor_y)) { 
            cleanup_stars();
            GameOver_spaceship();
            score = 0;
            return;
        }

        // Draw active player ship
        courser_spaceship(cursor_x, cursor_y, COLOR_RED);

        sleep_ms(20); 
    }
}