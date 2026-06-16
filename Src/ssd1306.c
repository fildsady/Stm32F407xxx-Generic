#include "ssd1306.h"
#include "font.h"
#include "stm32f4xx_ll_i2c.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_utils.h"

static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

static void ssd1306_I2C_Write(uint8_t reg, uint8_t data) {
    uint32_t timeout = 100000;
    
    // Wait for bus to not be busy
    while(LL_I2C_IsActiveFlag_BUSY(I2C1) && --timeout) {}
    
    LL_I2C_GenerateStartCondition(I2C1);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
    if(timeout == 0) return;
    
    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout) {
        if(LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    if(timeout == 0) {
        LL_I2C_GenerateStopCondition(I2C1);
        return;
    }
    LL_I2C_ClearFlag_ADDR(I2C1);
    
    LL_I2C_TransmitData8(I2C1, reg);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {
        if(LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    if(timeout == 0) {
        LL_I2C_GenerateStopCondition(I2C1);
        return;
    }
    
    LL_I2C_TransmitData8(I2C1, data);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {
        if(LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    if(timeout == 0) {
        LL_I2C_GenerateStopCondition(I2C1);
        return;
    }
    
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout) {}
    LL_I2C_GenerateStopCondition(I2C1);
}

#if !OLED_IS_SH1106
static void ssd1306_I2C_WriteMulti(uint8_t reg, uint8_t* data, uint16_t count) {
    uint32_t timeout = 100000;
    
    while(LL_I2C_IsActiveFlag_BUSY(I2C1) && --timeout) {}
    
    LL_I2C_GenerateStartCondition(I2C1);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
    if(timeout == 0) return;
    
    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout) {
        if(LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    if(timeout == 0) {
        LL_I2C_GenerateStopCondition(I2C1);
        return;
    }
    LL_I2C_ClearFlag_ADDR(I2C1);
    
    LL_I2C_TransmitData8(I2C1, reg);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {
        if(LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    if(timeout == 0) {
        LL_I2C_GenerateStopCondition(I2C1);
        return;
    }
    
    for(uint16_t i=0; i<count; i++) {
        LL_I2C_TransmitData8(I2C1, data[i]);
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {
            if(LL_I2C_IsActiveFlag_AF(I2C1)) {
                LL_I2C_ClearFlag_AF(I2C1);
                LL_I2C_GenerateStopCondition(I2C1);
                return;
            }
        }
        if(timeout == 0) {
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout) {}
    LL_I2C_GenerateStopCondition(I2C1);
}
#endif

static void I2C_Bus_Recovery(void) {
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_6, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_6, LL_GPIO_PULL_UP);

    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_7, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_7, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_7, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_7, LL_GPIO_PULL_UP);
    
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_6);
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_7);
    LL_mDelay(1);
    
    for (int i = 0; i < 9; i++) {
        if (LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_7)) {
            break; 
        }
        
        LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_6);
        LL_mDelay(1);
        LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_6);
        LL_mDelay(1);
    }
    
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_7);
    LL_mDelay(1);
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_6);
    LL_mDelay(1);
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_6);
    LL_mDelay(1);
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_7);
    LL_mDelay(1);
}

static void SSD1306_I2C_Init(void) {
    I2C_Bus_Recovery();

    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_6, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOB, LL_GPIO_PIN_6, LL_GPIO_AF_4);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_6, LL_GPIO_PULL_UP);

    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_7, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOB, LL_GPIO_PIN_7, LL_GPIO_AF_4);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_7, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_7, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_7, LL_GPIO_PULL_UP);

    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C1);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C1);

    LL_I2C_Disable(I2C1);
    
    LL_I2C_InitTypeDef I2C_InitStruct = {0};
    I2C_InitStruct.PeripheralMode = LL_I2C_MODE_I2C;
    I2C_InitStruct.ClockSpeed = 400000;
    I2C_InitStruct.DutyCycle = LL_I2C_DUTYCYCLE_2;
    I2C_InitStruct.OwnAddress1 = 0;
    I2C_InitStruct.TypeAcknowledge = LL_I2C_ACK;
    I2C_InitStruct.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT;
    
    LL_I2C_Init(I2C1, &I2C_InitStruct);
    
    LL_I2C_Enable(I2C1);
}

uint8_t SSD1306_Init(void) {
    SSD1306_I2C_Init();

    // Give some time
    LL_mDelay(150);

    // Init Sequence (from known good HAL polling config)
    ssd1306_I2C_Write(0x00, 0xAE); // display off
    ssd1306_I2C_Write(0x00, 0x20); // Set Memory Addressing Mode   
    ssd1306_I2C_Write(0x00, 0x10); // Page Addressing Mode
    ssd1306_I2C_Write(0x00, 0xB0); // Set Page Start Address
    ssd1306_I2C_Write(0x00, 0xC8); // Set COM Output Scan Direction
    ssd1306_I2C_Write(0x00, 0x00); // set low column address
    ssd1306_I2C_Write(0x00, 0x10); // set high column address
    ssd1306_I2C_Write(0x00, 0x40); // set start line address
    ssd1306_I2C_Write(0x00, 0x81); // set contrast control register
    ssd1306_I2C_Write(0x00, 0xFF);
    ssd1306_I2C_Write(0x00, 0xA1); // set segment re-map
    ssd1306_I2C_Write(0x00, 0xA6); // set normal display
    ssd1306_I2C_Write(0x00, 0xA8); // set multiplex ratio
    ssd1306_I2C_Write(0x00, 0x3F); // multiplex ratio value
    ssd1306_I2C_Write(0x00, 0xA4); // output follows RAM content
    ssd1306_I2C_Write(0x00, 0xD3); // set display offset
    ssd1306_I2C_Write(0x00, 0x00); // not offset
    ssd1306_I2C_Write(0x00, 0xD5); // set display clock divide ratio
    ssd1306_I2C_Write(0x00, 0xF0); // set divide ratio
    ssd1306_I2C_Write(0x00, 0xD9); // set pre-charge period
    ssd1306_I2C_Write(0x00, 0x22); 
    ssd1306_I2C_Write(0x00, 0xDA); // set com pins hardware configuration
    ssd1306_I2C_Write(0x00, 0x12);
    ssd1306_I2C_Write(0x00, 0xDB); // set vcomh
    ssd1306_I2C_Write(0x00, 0x20); // 0.77xVcc
    ssd1306_I2C_Write(0x00, 0x8D); // set DC-DC enable
    ssd1306_I2C_Write(0x00, 0x14); 
    ssd1306_I2C_Write(0x00, 0xAF); // turn on SSD1306 panel

    SSD1306_Clear();
    SSD1306_UpdateScreen();

    return 1;
}

static void ssd1306_I2C_WritePageSH1106(uint8_t *data) {
    uint32_t timeout = 100000;
    
    while(LL_I2C_IsActiveFlag_BUSY(I2C1) && --timeout) {}
    
    LL_I2C_GenerateStartCondition(I2C1);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
    if(timeout == 0) return;
    
    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout) {
        if(LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    if(timeout == 0) {
        LL_I2C_GenerateStopCondition(I2C1);
        return;
    }
    LL_I2C_ClearFlag_ADDR(I2C1);
    
    LL_I2C_TransmitData8(I2C1, 0x40); // Control byte for data
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {
        if(LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    if(timeout == 0) {
        LL_I2C_GenerateStopCondition(I2C1);
        return;
    }
    
    // Send 4 bytes of 0x00 for column 0, 1, 2, and 3 (offset of 4)
    for (int i = 0; i < 4; i++) {
        LL_I2C_TransmitData8(I2C1, 0x00);
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {
            if(LL_I2C_IsActiveFlag_AF(I2C1)) {
                LL_I2C_ClearFlag_AF(I2C1);
                LL_I2C_GenerateStopCondition(I2C1);
                return;
            }
        }
        if(timeout == 0) {
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    
    // Send 128 bytes of screen data (columns 4 to 131)
    for (uint16_t i = 0; i < SSD1306_WIDTH; i++) {
        LL_I2C_TransmitData8(I2C1, data[i]);
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {
            if(LL_I2C_IsActiveFlag_AF(I2C1)) {
                LL_I2C_ClearFlag_AF(I2C1);
                LL_I2C_GenerateStopCondition(I2C1);
                return;
            }
        }
        if(timeout == 0) {
            LL_I2C_GenerateStopCondition(I2C1);
            return;
        }
    }
    
    timeout = 100000;
    while(!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout) {}
    LL_I2C_GenerateStopCondition(I2C1);
}

void SSD1306_UpdateScreen(void) {
    for (uint8_t m = 0; m < 8; m++) {
        // Send page and column address commands in one I2C transaction
        uint32_t timeout = 100000;
        while(LL_I2C_IsActiveFlag_BUSY(I2C1) && --timeout) {}
        
        LL_I2C_GenerateStartCondition(I2C1);
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
        if(timeout == 0) continue;
        
        LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout) {
            if(LL_I2C_IsActiveFlag_AF(I2C1)) {
                LL_I2C_ClearFlag_AF(I2C1);
                LL_I2C_GenerateStopCondition(I2C1);
                break;
            }
        }
        if(timeout == 0) {
            LL_I2C_GenerateStopCondition(I2C1);
            continue;
        }
        LL_I2C_ClearFlag_ADDR(I2C1);
        
        LL_I2C_TransmitData8(I2C1, 0x00); // Control byte for command stream
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
        
        LL_I2C_TransmitData8(I2C1, 0xB0 + m); // Set Page Address
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
        
        LL_I2C_TransmitData8(I2C1, 0x00); // Set Column Low Address to 0
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
        
        LL_I2C_TransmitData8(I2C1, 0x10); // Set Column High Address to 0
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
        
        timeout = 100000;
        while(!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout) {}
        LL_I2C_GenerateStopCondition(I2C1);
        
        // Write the data page
#if OLED_IS_SH1106
        ssd1306_I2C_WritePageSH1106(&SSD1306_Buffer[SSD1306_WIDTH * m]);
#else
        ssd1306_I2C_WriteMulti(0x40, &SSD1306_Buffer[SSD1306_WIDTH * m], SSD1306_WIDTH);
#endif
    }
}

void SSD1306_Clear(void) {
    for (uint16_t i = 0; i < sizeof(SSD1306_Buffer); i++) {
        SSD1306_Buffer[i] = 0x00;
    }
}

void SSD1306_DrawPixel(int16_t x, int16_t y, uint8_t color) {
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }

    if (color == SSD1306_COLOR_WHITE) {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= (1 << (y % 8));
    } else {
        SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}

void SSD1306_DrawChar(int16_t x, int16_t y, char ch, const FontDef *font, uint8_t color) {
    if (ch < 32 || ch > 126) {
        return;
    }

    uint16_t char_index = ch - 32;

    for (uint8_t col = 0; col < font->width - 1; col++) {
        uint8_t line = font->data[char_index * (font->width - 1) + col];
        
        for (uint8_t row = 0; row < font->height; row++) {
            if (line & (1 << row)) {
                SSD1306_DrawPixel(x + col, y + row, color);
            } else {
                SSD1306_DrawPixel(x + col, y + row, !color);
            }
        }
    }
}

void SSD1306_DrawString(int16_t x, int16_t y, const char *str, const FontDef *font, uint8_t color) {
    while (*str) {
        SSD1306_DrawChar(x, y, *str, font, color);
        x += font->width;
        str++;
    }
}

uint8_t SSD1306_IsBusy(void) {
    // Since this is polling mode, it's never "busy" in background
    return 0;
}
