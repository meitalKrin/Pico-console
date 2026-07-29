#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "init.h"

int main(void) {
    stdio_init_all();

    init();

    fill_screen(0x0000); 

    while (1) {
        if (!gpio_get(BTN_A)) {
            fill_screen(0x1133);
            sleep_ms(200); 
        } 
        else if (!gpio_get(BTN_B)) {
            fill_screen(0x5115);
            sleep_ms(200);
        } 
        else if (!gpio_get(BTN_X)) {
            fill_screen(0x3355);
            sleep_ms(200);
        } 
        else if (!gpio_get(BTN_Y)) {
            fill_screen(0x7445);
            sleep_ms(200);
        }
        else if (!gpio_get(JOY_UP)) {
            fill_screen(0x1133);
            sleep_ms(200); 
        } 
        else if (!gpio_get(JOY_DOWN)) {
            fill_screen(0x5115);
            sleep_ms(200);
        } 
        else if (!gpio_get(JOY_LEFT)) {
            fill_screen(0x3315);
            sleep_ms(200);
        } 
        else if (!gpio_get(JOY_RIGHT)) {
            fill_screen(0x6645);
            sleep_ms(200);
        }

        sleep_ms(10); 
    }

    return 0;
}