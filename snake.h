#ifndef SNAKE_H
#define SNAKE_H

#include <stdint.h>
#include "hardware/spi.h"



// Public Function Prototypes 
void snake(void);
void snake_ui(void);
void SpawnApple(void);
static void courser_snake(uint16_t x, uint16_t y, uint16_t color);
#endif 