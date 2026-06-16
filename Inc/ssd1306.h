#ifndef __SSD1306_H__
#define __SSD1306_H__

#include <stdint.h>
#include "font.h"

#define SSD1306_I2C_ADDR 0x78
#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

#define SSD1306_COLOR_BLACK 0
#define SSD1306_COLOR_WHITE 1

// Controller Type: Set to 1 for SH1106 (Page Mode, Column offset 2), 0 for SSD1306 (Horizontal Mode)
#define OLED_IS_SH1106 1

// Function prototypes
uint8_t SSD1306_Init(void);
void SSD1306_Clear(void);
void SSD1306_UpdateScreen(void);
void SSD1306_DrawPixel(int16_t x, int16_t y, uint8_t color);
void SSD1306_DrawChar(int16_t x, int16_t y, char ch, const FontDef *font, uint8_t color);
void SSD1306_DrawString(int16_t x, int16_t y, const char *str, const FontDef *font, uint8_t color);
uint8_t SSD1306_IsBusy(void);

#endif /* __SSD1306_H__ */
