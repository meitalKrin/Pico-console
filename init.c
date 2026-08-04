#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "init.h"
#include "font.h"
#include "hardware/adc.h"

// PRIVATE HELPERS (static)
static inline void cs_select(void)   { gpio_put(LCD_PIN_CS, 0); } // Active Low
static inline void cs_Deselect(void) { gpio_put(LCD_PIN_CS, 1); } // Inactive High

static inline void lcd_write_cmd(uint8_t cmd) {
    gpio_put(LCD_PIN_DC, 0);
    cs_select();
    spi_write_blocking(LCD_SPI_PORT, &cmd, 1);
    cs_Deselect();
}

static inline void lcd_write_data(const uint8_t *data, size_t len) {
    gpio_put(LCD_PIN_DC, 1);
    cs_select();
    spi_write_blocking(LCD_SPI_PORT, data, len);
    cs_Deselect();
}

// Reset sequence: High -> Low -> High
static void lcd_reset(void) {
    gpio_put(LCD_PIN_RST, 1); sleep_ms(50);
    gpio_put(LCD_PIN_RST, 0); sleep_ms(50);
    gpio_put(LCD_PIN_RST, 1); sleep_ms(150); 
}

// Display initialization
static void gpio_initiate(void) {
    gpio_init(LCD_PIN_CS);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);

    gpio_init(LCD_PIN_DC);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_put(LCD_PIN_DC, 1);

    gpio_init(LCD_PIN_RST);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_put(LCD_PIN_RST, 1);

    gpio_init(LCD_PIN_BL);
    gpio_set_dir(LCD_PIN_BL, GPIO_OUT);
    gpio_put(LCD_PIN_BL, 1);
}

static void spi_initiate(void) {
    spi_init(LCD_SPI_PORT, 62500 * 1000);
    spi_set_format(LCD_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(LCD_PIN_DIN, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_CLK, GPIO_FUNC_SPI);
}
void joystick_adc_init(void) {
    adc_init();
    adc_gpio_init(26); // ADC0 (Joystick X)
    adc_gpio_init(27); // ADC1 (Joystick Y)
}

// Reads X axis (ADC0 / GPIO 26)
uint16_t joystick_read_x(void) {
    adc_select_input(0);
    return adc_read(); // Returns 0 to 4095
}

// Reads Y axis (ADC1 / GPIO 27)
uint16_t joystick_read_y(void) {
    adc_select_input(1);
    return adc_read(); // Returns 0 to 4095
}
// PUBLIC API
void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    lcd_write_cmd(0x2A);
    uint8_t col_data[] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
    };
    lcd_write_data(col_data, sizeof(col_data));

    lcd_write_cmd(0x2B);
    uint8_t row_data[] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
    };
    lcd_write_data(row_data, sizeof(row_data));

    lcd_write_cmd(0x2C);
}

// DRAWING FUNCTIONS
void draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    set_window(x, y, x, y);
    uint8_t data[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    lcd_write_data(data, 2);
}

void draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg_color, uint16_t scale) {
    if (c < ' ' || c > '~') c = ' ';

    uint8_t index = c - ' ';

  
    for (uint8_t col = 0; col < 6; col++) {
        uint8_t line = (col < 5) ? font5x7[index][col] : 0x00;

        for (uint8_t row = 0; row < 7; row++) {
            uint16_t pixel_color = (line & (1 << row)) ? color : bg_color;

            if (scale == 1) {
                draw_pixel(x + col, y + row, pixel_color);
            } else {
                fill_rect(x + col * scale, y + row * scale, scale, scale, pixel_color);
            }
        }
    }
}

void draw_text(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color, uint16_t scale) {
    uint16_t cursor_x = x;
    while (*str) {
        draw_char(cursor_x, y, *str, color, bg_color, scale);
        cursor_x += 6 * scale; 
        str++;
    }
}
void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
    if (x + w > LCD_WIDTH)  w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    set_window(x, y, x + w - 1, y + h - 1);

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);

    uint8_t line_buf[240 * 2];
    for (uint16_t i = 0; i < w; i++) {
        line_buf[2 * i]     = hi;
        line_buf[2 * i + 1] = lo;
    }
    gpio_put(LCD_PIN_DC, 1); // Data mode
    cs_select();
    for (uint16_t row = 0; row < h; row++) {
        spi_write_blocking(LCD_SPI_PORT, line_buf, w * 2);
    }
    cs_Deselect();
}

void fill_screen(uint16_t color) {
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}
// final setup
void init(void) {
    gpio_initiate();
    spi_initiate();
    joystick_adc_init();
    lcd_reset(); // Resets display registers before configuration

    lcd_write_cmd(0x11); // Exit Sleep Mode
    sleep_ms(120);

    lcd_write_cmd(0x3A); // Interface Pixel Format (RGB565)
    uint8_t format = 0x05;
    lcd_write_data(&format, 1);

    lcd_write_cmd(0x21); // Display Inversion ON

    lcd_write_cmd(0x36); // MADCTL — rotation/mirroring
    uint8_t madctl = 0x60;
    lcd_write_data(&madctl, 1);

    // Initialize Push Buttons
    uint buttons[] = {BTN_A, BTN_B, BTN_X, BTN_Y};
    for (int i = 0; i < 4; i++) {
        gpio_init(buttons[i]);
        gpio_set_dir(buttons[i], GPIO_IN);
        gpio_pull_up(buttons[i]);
    }

    // Initialize Joystick Pins
    uint joy_pins[] = {JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_CTRL};
    for (int i = 0; i < 5; i++) {
        gpio_init(joy_pins[i]);
        gpio_set_dir(joy_pins[i], GPIO_IN);
        gpio_pull_up(joy_pins[i]);
    }

    lcd_write_cmd(0x29); // Turn Display On
    sleep_ms(50);
}

//game and btn logic
bool button_pressed(uint pin, bool *last_state, uint64_t *last_change_time) {
    bool current = gpio_get(pin); // HIGH = not pressed, LOW = pressed
    uint64_t now = to_ms_since_boot(get_absolute_time());

    if (current != *last_state && (now - *last_change_time) > DEBOUNCE_MS) {
        *last_change_time = now;
        *last_state = current;
        return (!current); 
    }
    return false;
}



