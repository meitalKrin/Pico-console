#ifndef PAINT_H
#define PAINT_H

#include <stdint.h>
#include "hardware/spi.h"



// Public Function Prototypes 
void paint(void);
void paint_ui_draw_color_wheel(void);
void courser(uint8_t x, uint8_t y, uint16_t color);
uint16_t LCD_Restore_Area(uint8_t x, uint8_t y);

#endif 