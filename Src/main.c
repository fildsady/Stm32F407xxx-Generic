#include "stm32f4xx.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_utils.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_tim.h"
#include <string.h>

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

/* Global variables to share DSP results with OLED task */
volatile float32_t dsp_mult_result[4] = {0.0f, 0.0f, 0.0f, 0.0f};
volatile float32_t dsp_sin_val = 0.0f;

/* Timer 5 Configuration for FreeRTOS Run-time Stats */
void Timer_Init(void)
{
    // Enable TIM5 clock
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM5);
    
    // Configure prescaler: TIM5 input clock is 84MHz on APB1.
    // To run timer at 20 kHz (resolution of 50us):
    // Prescaler = 84000000 / 20000 - 1 = 4199
    LL_TIM_SetPrescaler(TIM5, 4199);
    
    // Set auto-reload to max 32-bit value
    LL_TIM_SetAutoReload(TIM5, 0xFFFFFFFF);
    
    // Enable counter
    LL_TIM_EnableCounter(TIM5);
}

uint32_t Timer_GetCounter(void)
{
    return LL_TIM_GetCounter(TIM5);
}

/* Calculate FreeRTOS CPU Usage */
float32_t calculate_cpu_usage(void)
{
    static TaskStatus_t pxTaskStatusArray[10];
    static uint32_t ulTotalRunTimeBefore = 0;
    static uint32_t ulIdleRunTimeBefore = 0;
    
    volatile UBaseType_t uxArraySize;
    uint32_t ulTotalRunTime = 0;
    
    uxArraySize = uxTaskGetNumberOfTasks();
    if (uxArraySize > 10) {
        uxArraySize = 10;
    }
    
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
    
    uint32_t ulIdleRunTime = 0;
    for (UBaseType_t i = 0; i < uxArraySize; i++) {
        if (strcmp(pxTaskStatusArray[i].pcTaskName, "IDLE") == 0) {
            ulIdleRunTime = pxTaskStatusArray[i].ulRunTimeCounter;
            break;
        }
    }
    
    float32_t cpu_usage = 0.0f;
    if (ulTotalRunTime > ulTotalRunTimeBefore) {
        uint32_t total_diff = ulTotalRunTime - ulTotalRunTimeBefore;
        uint32_t idle_diff = ulIdleRunTime - ulIdleRunTimeBefore;
        if (total_diff > 0) {
            cpu_usage = 100.0f * (1.0f - ((float32_t)idle_diff / (float32_t)total_diff));
        }
    }
    
    ulTotalRunTimeBefore = ulTotalRunTime;
    ulIdleRunTimeBefore = ulIdleRunTime;
    
    if (cpu_usage < 0.0f) cpu_usage = 0.0f;
    if (cpu_usage > 100.0f) cpu_usage = 100.0f;
    
    return cpu_usage;
}

#define ENABLE_LOAD_TEST 1

#if ENABLE_LOAD_TEST
static void load_test_task(void *args)
{
    (void)args;
    while (1) {
        // Sleep for 2 seconds
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Busy loop for 1 second (1000 ms)
        uint32_t start_tick = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(1000)) {
            // Keep CPU busy with a dummy operation
            __asm__ volatile("nop");
        }
    }
}
#endif

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
        for (int i = 0; i < 4; i++) {
            dsp_mult_result[i] = result[i];
        }

        /* Test Fast Math Sine (utilizes FPU) */
        dsp_sin_val = arm_sin_f32(PI/4);

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
    float32_t cpu_usage = 0.0f;
    
    while (1)
    {
        // 1. Clear Back Buffer
        SSD1306_Clear();
        
        // Calculate CPU usage every ~1 second (every 30 frames)
        if (frame_count % 30 == 0) {
            cpu_usage = calculate_cpu_usage();
        }
        
        // 2. Draw Text Info on Left Side
        SSD1306_DrawString(0, 2,  "STM32F407 RTOS", &Font_6x8, SSD1306_COLOR_WHITE);
        
        sprintf(str_buf, "CPU: %.1f%%", cpu_usage);
        SSD1306_DrawString(0, 12, str_buf, &Font_6x8, SSD1306_COLOR_WHITE);
        
        // Draw DSP calculation values
        sprintf(str_buf, "sin: %.4f", dsp_sin_val);
        SSD1306_DrawString(0, 22, str_buf, &Font_6x8, SSD1306_COLOR_WHITE);
        
        sprintf(str_buf, "R0,1: %.1f,%.1f", dsp_mult_result[0], dsp_mult_result[1]);
        SSD1306_DrawString(0, 32, str_buf, &Font_6x8, SSD1306_COLOR_WHITE);
        
        sprintf(str_buf, "R2,3: %.1f,%.1f", dsp_mult_result[2], dsp_mult_result[3]);
        SSD1306_DrawString(0, 42, str_buf, &Font_6x8, SSD1306_COLOR_WHITE);
        
        // Draw frame count / mode
        sprintf(str_buf, "Frames: %lu", frame_count++);
        SSD1306_DrawString(0, 52, str_buf, &Font_6x8, SSD1306_COLOR_WHITE);
        
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
        
        // Commented out the vertical divider line (column 88) to remove it from the display
        /*
        for (uint8_t y_line = 0; y_line < 64; y_line++) {
            SSD1306_DrawPixel(88, y_line, SSD1306_COLOR_WHITE);
        }
        */
        
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

#if ENABLE_LOAD_TEST
    /* Create CPU Load Test task at priority 1 (same as OLED UI to allow time-slicing and screen updates) */
    xTaskCreate(load_test_task, "Load_Test", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
#endif

	/* Create DSP test task */
	xTaskCreate(dsp_test_task, "DSP_Test", configMINIMAL_STACK_SIZE + 128, NULL, 2, NULL);

    /* Create OLED UI task */
    xTaskCreate(oled_ui_task, "OLED_UI", configMINIMAL_STACK_SIZE + 512, NULL, 1, NULL);

	/* Start FreeRTOS scheduler */
	vTaskStartScheduler();

	while (1) {
        /* Should never reach here */
	}
	return 0;
}
