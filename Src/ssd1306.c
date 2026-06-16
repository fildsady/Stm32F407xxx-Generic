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
static uint8_t oled_buffer_1[SSD1306_BUFFER_SIZE];
static uint8_t oled_buffer_2[SSD1306_BUFFER_SIZE];

uint8_t *oled_front_buffer = oled_buffer_1;
uint8_t *oled_back_buffer  = oled_buffer_2;

static volatile uint8_t oled_dma_busy = 0;
static volatile uint8_t current_page = 0;

static void I2C_Bus_Recovery(void)
{
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
    
    for (int i = 0; i < 9; i++)
    {
        if (LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_7))
        {
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

static void SSD1306_I2C_Init(void)
{
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
    I2C_InitStruct.ClockSpeed = 400000; // 400kHz Fast Mode for smoother and faster UI updates
    I2C_InitStruct.DutyCycle = LL_I2C_DUTYCYCLE_2;
    I2C_InitStruct.OwnAddress1 = 0;
    I2C_InitStruct.TypeAcknowledge = LL_I2C_ACK;
    I2C_InitStruct.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT;
    
    LL_I2C_Init(I2C1, &I2C_InitStruct);
    
    LL_I2C_Enable(I2C1);
}

static void SSD1306_DMA_Init(void)
{
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

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

    LL_DMA_EnableIT_TC(DMA1, LL_DMA_STREAM_7);

    NVIC_SetPriority(DMA1_Stream7_IRQn, 6);
    NVIC_EnableIRQ(DMA1_Stream7_IRQn);
}

static uint8_t SSD1306_WriteCommand(uint8_t cmd)
{
    uint32_t timeout;

    // 1. Generate Start
    LL_I2C_GenerateStartCondition(I2C1);
    timeout = 10000;
    while(!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
    if (timeout == 0) return 0;

    // 2. Send slave address (Write)
    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    timeout = 10000;
    while(!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout) {
        if (LL_I2C_IsActiveFlag_AF(I2C1)) {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            return 0;
        }
    }
    if (timeout == 0) return 0;
    LL_I2C_ClearFlag_ADDR(I2C1);

    // 3. Send Control Byte (0x00 for command)
    LL_I2C_TransmitData8(I2C1, 0x00);
    timeout = 10000;
    while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0) return 0;

    // 4. Send Command Byte
    LL_I2C_TransmitData8(I2C1, cmd);
    timeout = 10000;
    while(!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0) return 0;

    // 5. Generate Stop
    timeout = 10000;
    while(!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout) {}
    LL_I2C_GenerateStopCondition(I2C1);
    
    return 1;
}

static void SSD1306_ClearDisplayRAM(void)
{
    uint32_t timeout;
    for (uint8_t page = 0; page < 8; page++)
    {
        // Set page address (0xB0 + page)
        SSD1306_WriteCommand(0xB0 + page);
        // Set column address to 0
        SSD1306_WriteCommand(0x00);
        SSD1306_WriteCommand(0x10);

        // Start I2C Start condition
        LL_I2C_GenerateStartCondition(I2C1);
        timeout = 10000;
        while (!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
        if (timeout == 0) continue;

        // Send slave address
        LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
        timeout = 10000;
        while (!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout)
        {
            if (LL_I2C_IsActiveFlag_AF(I2C1))
            {
                LL_I2C_ClearFlag_AF(I2C1);
                LL_I2C_GenerateStopCondition(I2C1);
                break;
            }
        }
        if (timeout == 0) continue;
        LL_I2C_ClearFlag_ADDR(I2C1);

        // Send control byte (0x40 for data stream)
        LL_I2C_TransmitData8(I2C1, 0x40);
        timeout = 10000;
        while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
        if (timeout == 0)
        {
            LL_I2C_GenerateStopCondition(I2C1);
            continue;
        }

        // Transmit 132 bytes of 0x00 to clear the entire row
        for (uint8_t col = 0; col < 132; col++)
        {
            LL_I2C_TransmitData8(I2C1, 0x00);
            timeout = 10000;
            while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
        }

        // Wait for last byte to transmit and generate STOP
        timeout = 10000;
        while (!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout) {}
        LL_I2C_GenerateStopCondition(I2C1);

        // Wait for bus to be free
        timeout = 100000;
        while (LL_I2C_IsActiveFlag_BUSY(I2C1) && --timeout) {}
    }
}

uint8_t SSD1306_Init(void)
{
    SSD1306_I2C_Init();
    SSD1306_DMA_Init();

    LL_mDelay(150);

    // SH1106 Startup Sequence (Page Addressing Mode)
    SSD1306_WriteCommand(0xAE); // display off
    SSD1306_WriteCommand(0x20); // Set Memory Addressing Mode   
    SSD1306_WriteCommand(0x10); // Page Addressing Mode
    SSD1306_WriteCommand(0xB0); // Set Page Start Address
    SSD1306_WriteCommand(0xC8); // Set COM Output Scan Direction
    SSD1306_WriteCommand(0x00); // set low column address
    SSD1306_WriteCommand(0x10); // set high column address
    SSD1306_WriteCommand(0x40); // set start line address
    SSD1306_WriteCommand(0x81); // set contrast control register
    SSD1306_WriteCommand(0xFF);
    SSD1306_WriteCommand(0xA1); // set segment re-map
    SSD1306_WriteCommand(0xA6); // set normal display
    SSD1306_WriteCommand(0xA8); // set multiplex ratio
    SSD1306_WriteCommand(0x3F); // multiplex ratio value
    SSD1306_WriteCommand(0xA4); // output follows RAM content
    SSD1306_WriteCommand(0xD3); // set display offset
    SSD1306_WriteCommand(0x00); // not offset
    SSD1306_WriteCommand(0xD5); // set display clock divide ratio
    SSD1306_WriteCommand(0xF0); // set divide ratio
    SSD1306_WriteCommand(0xD9); // set pre-charge period
    SSD1306_WriteCommand(0x22); 
    SSD1306_WriteCommand(0xDA); // set com pins hardware configuration
    SSD1306_WriteCommand(0x12);
    SSD1306_WriteCommand(0xDB); // set vcomh
    SSD1306_WriteCommand(0x20); // 0.77xVcc
    SSD1306_WriteCommand(0x8D); // set DC-DC enable
    SSD1306_WriteCommand(0x14); 
    SSD1306_WriteCommand(0xAF); // turn on panel

    // Clear the entire 132-column RAM of the display to prevent power-on garbage in unused regions
    SSD1306_ClearDisplayRAM();

    SSD1306_Clear();
    SSD1306_UpdateScreen();
    while (SSD1306_IsBusy()) {}

    return 1;
}

void SSD1306_Clear(void)
{
    for (uint16_t i = 0; i < SSD1306_BUFFER_SIZE; i++)
    {
        oled_back_buffer[i] = 0x00;
    }
}

static void SSD1306_StartPageTransfer(uint8_t page)
{
    uint32_t timeout;

    // 1. Wait until I2C is not busy
    timeout = 100000;
    while (LL_I2C_IsActiveFlag_BUSY(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        oled_dma_busy = 0;
        return;
    }

    // 2. Start I2C Command phase: Set page and column address (start at column 2)
    LL_I2C_GenerateStartCondition(I2C1);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        oled_dma_busy = 0;
        return;
    }

    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout)
    {
        if (LL_I2C_IsActiveFlag_AF(I2C1))
        {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            oled_dma_busy = 0;
            return;
        }
    }
    if (timeout == 0)
    {
        oled_dma_busy = 0;
        return;
    }
    LL_I2C_ClearFlag_ADDR(I2C1);

    // Control byte: Command Stream (0x00)
    LL_I2C_TransmitData8(I2C1, 0x00);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        LL_I2C_GenerateStopCondition(I2C1);
        oled_dma_busy = 0;
        return;
    }

    // Set Page Address (0xB0 + page)
    LL_I2C_TransmitData8(I2C1, 0xB0 + page);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        LL_I2C_GenerateStopCondition(I2C1);
        oled_dma_busy = 0;
        return;
    }

    // Set Column Low Address to 2 (0x02) for SH1106
    LL_I2C_TransmitData8(I2C1, 0x02);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        LL_I2C_GenerateStopCondition(I2C1);
        oled_dma_busy = 0;
        return;
    }

    // Set Column High Address to 0 (0x10)
    LL_I2C_TransmitData8(I2C1, 0x10);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        LL_I2C_GenerateStopCondition(I2C1);
        oled_dma_busy = 0;
        return;
    }

    // Wait for Byte Transfer Finished to ensure command bytes are sent
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout) {}
    
    // Generate STOP condition
    LL_I2C_GenerateStopCondition(I2C1);

    // Wait for I2C to be not busy
    timeout = 100000;
    while (LL_I2C_IsActiveFlag_BUSY(I2C1) && --timeout) {}

    // 3. Start I2C Data phase: Send data header (0x40) only
    LL_I2C_GenerateStartCondition(I2C1);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_SB(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        oled_dma_busy = 0;
        return;
    }

    LL_I2C_TransmitData8(I2C1, SSD1306_I2C_ADDR);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_ADDR(I2C1) && --timeout)
    {
        if (LL_I2C_IsActiveFlag_AF(I2C1))
        {
            LL_I2C_ClearFlag_AF(I2C1);
            LL_I2C_GenerateStopCondition(I2C1);
            oled_dma_busy = 0;
            return;
        }
    }
    if (timeout == 0)
    {
        oled_dma_busy = 0;
        return;
    }
    LL_I2C_ClearFlag_ADDR(I2C1);

    // Control byte: Data Stream (0x40)
    LL_I2C_TransmitData8(I2C1, 0x40);
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        LL_I2C_GenerateStopCondition(I2C1);
        oled_dma_busy = 0;
        return;
    }

    // Wait for I2C DR to be empty before activating DMA
    timeout = 10000;
    while (!LL_I2C_IsActiveFlag_TXE(I2C1) && --timeout) {}
    if (timeout == 0)
    {
        LL_I2C_GenerateStopCondition(I2C1);
        oled_dma_busy = 0;
        return;
    }

    // 4. Configure and Enable DMA Transfer for 128 bytes of page data
    LL_DMA_ClearFlag_TC7(DMA1);
    LL_DMA_ClearFlag_TE7(DMA1);

    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_STREAM_7, (uint32_t)&oled_front_buffer[page * 128]);
    LL_DMA_SetDataLength(DMA1, LL_DMA_STREAM_7, 128);

    // Enable I2C DMA request and enable DMA Stream
    LL_I2C_EnableDMAReq_TX(I2C1);
    LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_7);
}

void SSD1306_UpdateScreen(void)
{
    uint32_t timeout = 100000;

    // Wait if previous DMA transfer is still busy
    while (oled_dma_busy && --timeout) {}
    if (timeout == 0)
    {
        oled_dma_busy = 0; // Force clear if hung
    }

    // Swap pointers
    uint8_t *temp = oled_front_buffer;
    oled_front_buffer = oled_back_buffer;
    oled_back_buffer = temp;

    oled_dma_busy = 1;
    current_page = 0;

    // Start transfer of page 0
    SSD1306_StartPageTransfer(0);
}

void SSD1306_DrawPixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT)
    {
        return;
    }

    uint16_t index = x + ((y / 8) * SSD1306_WIDTH);

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
    if (ch < 32 || ch > 126)
    {
        return;
    }

    uint16_t char_index = ch - 32;

    for (uint8_t col = 0; col < font->width - 1; col++)
    {
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

        // 1. Wait for I2C last byte transfer to finish physically on the bus
        uint32_t timeout = 20000;
        while (!LL_I2C_IsActiveFlag_BTF(I2C1) && --timeout)
        {
            if (LL_I2C_IsActiveFlag_AF(I2C1))
            {
                LL_I2C_ClearFlag_AF(I2C1);
                break;
            }
        }

        // 2. Generate STOP Condition
        LL_I2C_GenerateStopCondition(I2C1);

        // 3. Disable DMA requests and stream
        LL_I2C_DisableDMAReq_TX(I2C1);
        LL_DMA_DisableStream(DMA1, LL_DMA_STREAM_7);

        // 4. Cascade to next page
        current_page++;
        if (current_page < 8)
        {
            SSD1306_StartPageTransfer(current_page);
        }
        else
        {
            oled_dma_busy = 0; // All pages sent, swap now safe
        }
    }
}
