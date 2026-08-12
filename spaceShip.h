#ifndef SPACESHIP_H
#define SPACESHIP_H

#include <stdint.h>
#include "hardware/spi.h"



// Public Function Prototypes 
void spaceship(void);
void spaceship_ui(void);
void GameOver_spaceship(void) ;
void movingStars(void);
void EraseArea(uint16_t x, uint16_t y);
struct Node* createStar( uint32_t startrandomX, uint32_t endrandomX);
bool PrintStarsAndCheckCollision(uint8_t player_x, uint8_t player_y);
struct Node* moveStar(struct Node *head);
void EraseArea(uint16_t x, uint16_t y);
#endif 