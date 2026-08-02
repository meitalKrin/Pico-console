#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>
#include "hardware/spi.h"


#define LCD_SPI_PORT   spi1

#define LCD_PIN_DIN    11   // MOSI
#define LCD_PIN_CLK    10   // SCK
#define LCD_PIN_CS      9
#define LCD_PIN_DC      8   
#define LCD_PIN_RST    12   
#define LCD_PIN_BL     13

// Buttons
#define BTN_A          15
#define BTN_B          17
#define BTN_X          19
#define BTN_Y          21

// Joystick
#define JOY_UP          2
#define JOY_DOWN       18
#define JOY_LEFT       16
#define JOY_RIGHT      20
#define JOY_CTRL        3

#define LCD_WIDTH     240
#define LCD_HEIGHT    240

// 16-bit RGB565 helper
#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

// Public Function Prototypes 
void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void fill_screen(uint16_t color);
void init(void);
void draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t scale);
void draw_text(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t scale);

#endif 
