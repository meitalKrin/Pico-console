#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "init.h"
//init 
//  PRIVATE HELPERS (static)  
static inline void cs_select(void)   { gpio_put(LCD_PIN_CS, 0); } //low active
static inline void cs_Deselect(void) { gpio_put(LCD_PIN_CS, 1); } //high non active

static inline void lcd_write_cmd(uint8_t cmd) {
    gpio_put(LCD_PIN_DC, 0);
    cs_select();
    spi_write_blocking(LCD_SPI_PORT,&cmd, 1);
    cs_Deselect();
}

static inline void lcd_write_data(const uint8_t *data, size_t len) {
    gpio_put(LCD_PIN_DC, 1);
    cs_select();
    spi_write_blocking(LCD_SPI_PORT,data, len);
    cs_Deselect();
}
// reset high -> low -> high 
static void lcd_reset(void){
    gpio_put(LCD_PIN_RST, 1); sleep_ms(50);
    gpio_put(LCD_PIN_RST, 0); sleep_ms(50);
    gpio_put(LCD_PIN_RST, 1); sleep_ms(150); 
}

//display
static void gpio_initiate(void){
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

static void spi_initiate(void){
    spi_init(LCD_SPI_PORT,62500 * 1000);
    spi_set_format(LCD_SPI_PORT,  8,SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST );
    gpio_set_function(LCD_PIN_DIN, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_CLK, GPIO_FUNC_SPI);
}
// PUBLIC API (NOT static)
//XY

 void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1){
    lcd_write_cmd(0x2A);
    uint8_t col_data[] = {
        (uint8_t)(x0 >> 8),   // X start High Byte
        (uint8_t)(x0 & 0xFF), // X start Low Byte
        (uint8_t)(x1 >> 8),   // X end High Byte
        (uint8_t)(x1 & 0xFF)  // X end Low Byte
    };

    lcd_write_data(col_data, sizeof(col_data));

        lcd_write_cmd(0x2B);
     uint8_t row_data[] = {
        (uint8_t)(x0 >> 8),   // Y start High Byte
        (uint8_t)(x0 & 0xFF), // Y start Low Byte
        (uint8_t)(x1 >> 8),   // Y end High Byte
        (uint8_t)(x1 & 0xFF)  // Y end Low Byte
    };

    lcd_write_data(col_data, sizeof(col_data));

    lcd_write_cmd(0x2C);
}


//drawing Func

void draw_pixel(uint16_t x, uint16_t y, uint16_t color){
    if (x>=LCD_WIDTH || y >=LCD_HEIGHT)return;
    set_window(x, y, x, y);
    uint8_t data[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    lcd_write_data(data, 2);
}
void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)  {
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
void fill_screen(uint16_t color){
    fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void init(void) {
    gpio_initiate();
    spi_initiate();
    
  
    uint8_t buttons[] = {BTN_A, BTN_B, BTN_X, BTN_Y};
    for (int i = 0; i < 4; i++) {
        gpio_init(buttons[i]);
        gpio_set_dir(buttons[i], GPIO_IN);
        gpio_pull_up(buttons[i]); 
        }

        uint joy_pins[] = {JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_CTRL};
        for (int i = 0; i < 5; i++) {
            gpio_init(joy_pins[i]);
            gpio_set_dir(joy_pins[i], GPIO_IN);
            gpio_pull_up(joy_pins[i]);
}
    lcd_reset();

    lcd_write_cmd(0x11); // Exit Sleep Mode
    sleep_ms(120);

    lcd_write_cmd(0x3A); // Interface Pixel Format
    uint8_t format = 0x05; // 16-bit color (RGB565)
    lcd_write_data(&format, 1);

    lcd_write_cmd(0x29); // Turn Display On
    sleep_ms(50);
}