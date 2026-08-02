#include "pico/stdlib.h"
#include "init.h"
#include <stdio.h>
typedef enum { MENU, SNAKE, SPACESHIP, PAINT } app_state_t;

#define NUM_ITEMS 3
static int selected = 0;


#define DEBOUNCE_MS 150

static bool button_pressed(uint pin, bool *last_state, uint64_t *last_change_time) {
    bool current = gpio_get(pin); // HIGH = not pressed, LOW = pressed
    uint64_t now = to_ms_since_boot(get_absolute_time());

    if (current != *last_state && (now - *last_change_time) > DEBOUNCE_MS) {
        *last_change_time = now;
        *last_state = current;
        return (!current); 
    }
    return false;
}

void draw_menu(void) {
    fill_screen(COLOR_BLACK);
    draw_text(60, 10, "MAIN MENU", COLOR_WHITE, 2);

    const char* labels[] = {"SNAKE", "SHIP", "PAINT"};

    for (int i = 0; i < NUM_ITEMS; i++) {
        int y = 60 + i * 55;

        if (i == selected) {
         
            fill_rect(20, y, 200, 40, COLOR_YELLOW);
        } else {
           
            fill_rect(20, y, 200, 40, COLOR_WHITE);
            fill_rect(22, y + 2, 196, 36, COLOR_BLACK);
        }

        char display_label[16];
        snprintf(display_label, sizeof(display_label), "%s %s",
                 (i == selected) ? ">" : " ", labels[i]);

        uint16_t text_color = (i == selected) ? COLOR_BLACK : COLOR_WHITE;
        draw_text(35, y + 12, display_label, text_color, 2);
    }
}

int main() {
    stdio_init_all();
    init();
    app_state_t state = MENU;
    draw_menu();

    bool joy_up_last = true, joy_down_last = true, btn_a_last = true;
    uint64_t up_time = 0, down_time = 0, btn_time = 0;

    while (true) {
        switch (state) {
            case MENU: {
                bool up = button_pressed(JOY_UP, &joy_up_last, &up_time);
                bool down = button_pressed(JOY_DOWN, &joy_down_last, &down_time);
                bool select = button_pressed(BTN_A, &btn_a_last, &btn_time);

                if (up)   { selected = (selected - 1 + NUM_ITEMS) % NUM_ITEMS; draw_menu(); }
                if (down) { selected = (selected + 1) % NUM_ITEMS; draw_menu(); }

                if (select) {
                    if (selected == 0) state = SNAKE;
                    if (selected == 1) state = SPACESHIP;
                    if (selected == 2) state = PAINT;
                }
                sleep_ms(10); 
                break;
            }

            case SNAKE:
                state = MENU;
                draw_menu();
                break;

            case SPACESHIP:
                state = MENU;
                draw_menu();
                break;

            case PAINT:
                state = MENU;
                draw_menu();
                break;
        }
    }
}