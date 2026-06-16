#include "stm32f4xx.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_utils.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "arm_math.h"
#include "ssd1306.h"
#include <stdio.h>

void SystemClock_Config(void)
{
  /* Enable HSE (High Speed External Clock) */
  LL_RCC_HSE_Enable();
  while(LL_RCC_HSE_IsReady() != 1) { }

  /* Set FLASH latency */
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);
  if(LL_FLASH_GetLatency() != LL_FLASH_LATENCY_5) {
    while(1);
  }

  /* Enable power clock and set voltage scaling to scale 1 (for max frequency) */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);

  /* Configure PLL: HSE(8MHz) / 8 * 336 / 2 = 168MHz */
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_8, 336, LL_RCC_PLLP_DIV_2);
  LL_RCC_PLL_Enable();
  while(LL_RCC_PLL_IsReady() != 1) { }

  /* Set AHB/APB prescalers */
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_4);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);

  /* Switch system clock to PLL */
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) { }

  /* Update SystemCoreClock variable */
  LL_SetSystemCoreClock(168000000);
}

void GPIO_Init(void)
{
  /* Enable GPIOD clock (LED on STM32F407 Discovery is PD12) */
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOD);

  /* Configure PD12 as Output */
  LL_GPIO_SetPinMode(GPIOD, LL_GPIO_PIN_12, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinSpeed(GPIOD, LL_GPIO_PIN_12, LL_GPIO_SPEED_FREQ_LOW);
  LL_GPIO_SetPinOutputType(GPIOD, LL_GPIO_PIN_12, LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinPull(GPIOD, LL_GPIO_PIN_12, LL_GPIO_PULL_NO);
}

static void dsp_test_task(void *args)
{
    (void)args;

    /* CMSIS-DSP Test Data */
    float32_t a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float32_t b[4] = {2.0f, 2.0f, 2.0f, 2.0f};
    float32_t result[4];

    while (1) {
        /* Test Vector Multiplication (utilizes FPU/SIMD) */
        arm_mult_f32(a, b, result, 4);

        /* Test Fast Math Sine (utilizes FPU) */
        float32_t sin_val = arm_sin_f32(PI/4);
        (void)sin_val; // suppress unused warning

        LL_GPIO_TogglePin(GPIOD, LL_GPIO_PIN_12);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void oled_ui_task(void *args)
{
    (void)args;
    
    // Bouncing box variables on the right partition of screen
    int16_t ball_x = 95;
    int16_t ball_y = 10;
    int16_t ball_dx = 1;
    int16_t ball_dy = 1;
    const uint8_t ball_size = 4;
    
    uint32_t frame_count = 0;
    char str_buf[32];
    
    while (1)
    {
        // 1. Clear Back Buffer
        SSD1306_Clear();
        
        // 2. Draw Text Info on Left Side
        SSD1306_DrawString(0, 2,  "STM32F407 PLAYER", &Font_6x8, SSD1306_COLOR_WHITE);
        SSD1306_DrawString(0, 14, "DSP Core: Active",  &Font_6x8, SSD1306_COLOR_WHITE);
        SSD1306_DrawString(0, 24, "I2C DMA: Running",  &Font_6x8, SSD1306_COLOR_WHITE);
        
        // Draw frame count
        sprintf(str_buf, "Frames: %lu", frame_count++);
        SSD1306_DrawString(0, 38, str_buf, &Font_6x8, SSD1306_COLOR_WHITE);
        SSD1306_DrawString(0, 48, "Double Buffer 2K",  &Font_6x8, SSD1306_COLOR_WHITE);
        
        // 3. Update Bouncing Box physics
        ball_x += ball_dx;
        ball_y += ball_dy;
        
        // Check collisions with boundary
        // Boundaries are: x between 91 and 123 (excluding boundaries and box size), y between 0 and 59
        if (ball_x <= 91 || ball_x >= (128 - ball_size)) {
            ball_dx = -ball_dx;
            ball_x += ball_dx;
        }
        if (ball_y <= 0 || ball_y >= (64 - ball_size)) {
            ball_dy = -ball_dy;
            ball_y += ball_dy;
        }
        
        // Draw the bouncing box
        for (uint8_t i = 0; i < ball_size; i++) {
            for (uint8_t j = 0; j < ball_size; j++) {
                SSD1306_DrawPixel(ball_x + i, ball_y + j, SSD1306_COLOR_WHITE);
            }
        }
        
        // Draw divider line (column 88)
        for (uint8_t y_line = 0; y_line < 64; y_line++) {
            SSD1306_DrawPixel(88, y_line, SSD1306_COLOR_WHITE);
        }
        
        // 4. Update screen (swaps buffers and starts DMA transfer in background)
        SSD1306_UpdateScreen();
        
        // 5. Sleep for ~33ms (targeting 30 FPS)
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

int main(void)
{
	/* Configure the system clock to 168 MHz */
	SystemClock_Config();

	/* Initialize SysTick for LL_mDelay usage before FreeRTOS starts */
	LL_Init1msTick(168000000);

	/* Initialize GPIO */
	GPIO_Init();

    /* Initialize SSD1306 OLED (I2C1 + DMA1 Stream 7) */
    SSD1306_Init();

	/* Create DSP test task */
	xTaskCreate(dsp_test_task, "DSP_Test", configMINIMAL_STACK_SIZE + 128, NULL, 2, NULL);

    /* Create OLED UI task */
    xTaskCreate(oled_ui_task, "OLED_UI", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);

	/* Start FreeRTOS scheduler */
	vTaskStartScheduler();

	while (1) {
        /* Should never reach here */
	}
	return 0;
}
