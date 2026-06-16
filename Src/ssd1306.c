#include "ssd1306.h"
#include "stm32f4xx_ll_i2c.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_utils.h"

#define SSD1306_I2C_ADDR 0x78

// Double Buffer allocation
// Size = 1024 bytes (128x64/8) + 1 control byte at index 0
static uint8_t oled_buffer_1[SSD1306_BUFFER_SIZE];
static uint8_t oled_buffer_2[SSD1306_BUFFER_SIZE];

uint8_t *oled_front_buffer = oled_buffer_1;
uint8_t *oled_back_buffer  = oled_buffer_2;

static volatile uint8_t oled_dma_busy = 0;

static void I2C_Bus_Recovery(void)
{
    // 1. Enable GPIOB Clock
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
    
    // 2. Configure PB6 (SCL) and PB7 (SDA) as Output, Open-Drain, Pull-up temporarily
    // NOTE: If your hardware uses PB8/PB9 instead of PB6/PB7, change LL_GPIO_PIN_6/7 to LL_GPIO_PIN_8/9 here!
    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_6, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_6, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_6, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_6, LL_GPIO_PULL_UP);

    LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_7, LL_GPIO_MODE_OUTPUT);
    LL_GPIO_SetPinOutputType(GPIOB, LL_GPIO_PIN_7, LL_GPIO_OUTPUT_OPENDRAIN);
    LL_GPIO_SetPinSpeed(GPIOB, LL_GPIO_PIN_7, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_7, LL_GPIO_PULL_UP);
    
    // Set SCL and SDA high
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_6);
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_7);
    LL_mDelay(1);
    
    // Clock SCL 9 times to force slave to release SDA line
    for (int i = 0; i < 9; i++)
    {
        if (LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_7))
        {
            break; // SDA is high, bus is clear
        }
        
        LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_6);
        LL_mDelay(1);
        LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_6);
        LL_mDelay(1);
    }
    
    // Send manual START and STOP to reset slave state machine
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_7); // SDA Low
    LL_mDelay(1);
    LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_6); // SCL Low
    LL_mDelay(1);
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_6);   // SCL High
    LL_mDelay(1);
    LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_7);   // SDA High
    LL_mDelay(1);
}

static void SSD1306_I2C_Init(void)
{
    // 1. Perform I2C Bus Recovery (Clears stuck SDA line)
    I2C_Bus_Recovery();

    // 2. Configure PB6 (SCL) and PB7 (SDA) as Alternate Function (AF4), Open-Drain, Pull-up
    // NOTE: If using PB8/PB9, change to:
    // LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_8, LL_GPIO_MODE_ALTERNATE);
    // LL_GPIO_SetAFPin_8_15(GPIOB, LL_GPIO_PIN_8, LL_GPIO_AF_4);
    // ... and the same for PB9.
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

    // 3. Enable I2C1 clock
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

    // 4. Reset I2C1
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C1);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C1);

    // 5. Configure I2C1 using standard LL_I2C_Init (automatically sets speed, duty cycle, and TRISE)
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

static void SSD1306_DMA_Init(void)
{
    // 1. Enable DMA1 clock
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    // 2. Configure DMA1 Stream 7, Channel 1 for I2C1 TX
    LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_7);
    
    LL_DMA_ConfigTransfer(DMA1, LL_DMA_STREAM_7, 
        LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
        LL_DMA_PRIORITY_HIGH |
        LL_DMA_MODE_NORMAL |
        LL_DMA_MDATAALIGN_BYTE |
        LL_DMA_PDATAALIGN_BYTE |
        LL_DMA_MEMORY_INCREMENT |
        LL_DMA_PERIPH_NOINCREMENT
    );
    
    LL_DMA_SetChannelSelection(DMA1, LL_DMA_STREAM_7, LL_DMA_CHANNEL_1);
    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_STREAM_7, (uint32_t)&I2C1->DR);

    // 3. Enable Transfer Complete Interrupt
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_7);

    // 4. Configure NVIC for DMA1 Stream 7 Interrupt
    NVIC_SetPriority(DMA1_Stream7_IRQn, 6); // Safe priority for FreeRTOS
    NVIC_EnableIRQ(DMA1_Stream7_IRQn);
}

static void SSD1306_WriteCommand(uint8_t cmd)
{
    // 1. Generate Start
    LL_I2C_GenerateStartCondition(I2C1);
    while(!LL_I2C_IsActiveFlag_SB(I2C1)) {}

    // 2. Send slave address (Write)
    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    while(!LL_I2C_IsActiveFlag_ADDR(I2C1)) {}
    LL_I2C_ClearFlag_ADDR(I2C1);

    // 3. Send Control Byte (0x00 for command)
    LL_I2C_TransmitData8(I2C1, 0x00);
    while(!LL_I2C_IsActiveFlag_TXE(I2C1)) {}

    // 4. Send Command Byte
    LL_I2C_TransmitData8(I2C1, cmd);
    while(!LL_I2C_IsActiveFlag_TXE(I2C1)) {}

    // 5. Generate Stop
    while(!LL_I2C_IsActiveFlag_BTF(I2C1)) {}
    LL_I2C_GenerateStopCondition(I2C1);
}

uint8_t SSD1306_Init(void)
{
    // Initialize Hardware Peripherals
    SSD1306_I2C_Init();
    SSD1306_DMA_Init();

    // Small delay for OLED power up
    LL_mDelay(100);

    // SSD1306 Startup Sequence
    SSD1306_WriteCommand(0xAE); // Display Off
    SSD1306_WriteCommand(0xD5); // Set Display Clock Divide Ratio/Oscillator Frequency
    SSD1306_WriteCommand(0x80);
    SSD1306_WriteCommand(0xA8); // Set Multiplex Ratio
    SSD1306_WriteCommand(0x3F); // 64MUX
    SSD1306_WriteCommand(0xD3); // Set Display Offset
    SSD1306_WriteCommand(0x00);
    SSD1306_WriteCommand(0x40); // Set Display Start Line (0)
    SSD1306_WriteCommand(0x8D); // Charge Pump
    SSD1306_WriteCommand(0x14); // Enable Charge Pump
    SSD1306_WriteCommand(0x20); // Set Memory Addressing Mode
    SSD1306_WriteCommand(0x00); // Horizontal Addressing Mode
    SSD1306_WriteCommand(0xA1); // Set Segment Re-map (COL127 to SEG0)
    SSD1306_WriteCommand(0xC8); // Set COM Output Scan Direction (COM63 to COM0)
    SSD1306_WriteCommand(0xDA); // Set COM Pins Hardware Configuration
    SSD1306_WriteCommand(0x12);
    SSD1306_WriteCommand(0x81); // Contrast Control
    SSD1306_WriteCommand(0xCF);
    SSD1306_WriteCommand(0xD9); // Set Pre-charge Period
    SSD1306_WriteCommand(0xF1);
    SSD1306_WriteCommand(0xDB); // Set VCOMH Deselect Level
    SSD1306_WriteCommand(0x40);
    SSD1306_WriteCommand(0xA4); // Entire Display On (Resume to RAM)
    SSD1306_WriteCommand(0xA6); // Set Normal Display
    SSD1306_WriteCommand(0xAF); // Display On

    // Initialize index 0 to I2C Command control byte (0x40 for streaming data)
    oled_buffer_1[0] = 0x40;
    oled_buffer_2[0] = 0x40;

    // Clear both buffers
    SSD1306_Clear();
    
    // Swap and clear the other one
    uint8_t *temp = oled_front_buffer;
    oled_front_buffer = oled_back_buffer;
    oled_back_buffer = temp;
    
    SSD1306_Clear();

    return 1; // Success
}

void SSD1306_Clear(void)
{
    // Clear data bytes in back buffer (index 1 to 1024)
    for (uint16_t i = 1; i < SSD1306_BUFFER_SIZE; i++)
    {
        oled_back_buffer[i] = 0x00;
    }
}

void SSD1306_UpdateScreen(void)
{
    // If DMA is already busy sending a frame, wait for it to finish
    while (oled_dma_busy) {}

    // Swap pointers
    uint8_t *temp = oled_front_buffer;
    oled_front_buffer = oled_back_buffer;
    oled_back_buffer = temp;

    oled_dma_busy = 1;

    // 1. Reset SSD1306 column and page pointers to (0,0)
    SSD1306_WriteCommand(0x21); // Set Column Address
    SSD1306_WriteCommand(0);    // Start
    SSD1306_WriteCommand(127);  // End
    SSD1306_WriteCommand(0x22); // Set Page Address
    SSD1306_WriteCommand(0);    // Start
    SSD1306_WriteCommand(7);    // End

    // 2. Clear DMA Stream flags
    LL_DMA_ClearFlag_TC7(DMA1);
    LL_DMA_ClearFlag_TE7(DMA1);

    // 3. Set DMA source memory and length
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_7, (uint32_t)oled_front_buffer);
    LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_7, SSD1306_BUFFER_SIZE);

    // 4. Enable DMA Stream
    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_7);

    // 5. Generate I2C START Condition and Send Address
    LL_I2C_EnableDMAReq_TX(I2C1);
    LL_I2C_GenerateStartCondition(I2C1);
    while(!LL_I2C_IsActiveFlag_SB(I2C1)) {}

    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    while(!LL_I2C_IsActiveFlag_ADDR(I2C1)) {}
    LL_I2C_ClearFlag_ADDR(I2C1);
    
    // At this point, DMA is transferring in the background!
}

void SSD1306_DrawPixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT)
    {
        return; // Out of bounds
    }

    // Index calculation:
    // y / 8 gives the page index (0 to 7)
    // x is column index (0 to 127)
    // Since index 0 is control byte (0x40), actual buffer index is offset by 1
    uint16_t index = 1 + x + ((y / 8) * SSD1306_WIDTH);

    if (color == SSD1306_COLOR_WHITE)
    {
        oled_back_buffer[index] |= (1 << (y % 8));
    }
    else
    {
        oled_back_buffer[index] &= ~(1 << (y % 8));
    }
}

void SSD1306_DrawChar(int16_t x, int16_t y, char ch, const FontDef *font, uint8_t color)
{
    // Support ASCII characters between Space (32) and Tilde (126)
    if (ch < 32 || ch > 126)
    {
        return;
    }

    uint16_t char_index = ch - 32;

    for (uint8_t col = 0; col < font->width - 1; col++)
    {
        // 5 columns per character for our 6x8 font
        uint8_t line = font->data[char_index * (font->width - 1) + col];
        
        for (uint8_t row = 0; row < font->height; row++)
        {
            if (line & (1 << row))
            {
                SSD1306_DrawPixel(x + col, y + row, color);
            }
            else
            {
                SSD1306_DrawPixel(x + col, y + row, !color);
            }
        }
    }
}

void SSD1306_DrawString(int16_t x, int16_t y, const char *str, const FontDef *font, uint8_t color)
{
    while (*str)
    {
        SSD1306_DrawChar(x, y, *str, font, color);
        x += font->width;
        str++;
    }
}

uint8_t SSD1306_IsBusy(void)
{
    return oled_dma_busy;
}

// DMA1 Stream 7 ISR Handler
void DMA1_Stream7_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TC7(DMA1))
    {
        LL_DMA_ClearFlag_TC7(DMA1);

        // Wait for I2C last byte transfer to finish physically on the bus
        while (!LL_I2C_IsActiveFlag_BTF(I2C1)) {}

        // Generate Stop Condition
        LL_I2C_GenerateStopCondition(I2C1);

        // Disable DMA requests and stream
        LL_I2C_DisableDMAReq_TX(I2C1);
        LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_7);

        oled_dma_busy = 0; // Clear busy flag, buffer swap now safe
    }
}
