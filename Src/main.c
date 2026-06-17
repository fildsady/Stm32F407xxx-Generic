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
#include <stdio.h>

/* USB Audio Device headers */
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_audio.h"
#include "usbd_audio_if.h"

USBD_HandleTypeDef hUsbDeviceFS;
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* Audio Codec & I2S headers */
#include "cs43l22.h"
#include "i2s_audio.h"

extern DMA_HandleTypeDef hdma_spi3_tx;

void DMA1_Stream7_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_spi3_tx);
}

void OTG_FS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

/* Override weak HAL_GetTick and HAL_Delay to prevent hangs before/after FreeRTOS starts */
uint32_t HAL_GetTick(void)
{
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    return xTaskGetTickCount();
  } else {
    static uint32_t initialized = 0;
    if (!initialized) {
      CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
      DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
      initialized = 1;
    }
    return DWT->CYCCNT / 168000;
  }
}

void HAL_Delay(uint32_t Delay)
{
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    vTaskDelay(pdMS_TO_TICKS(Delay));
  } else {
    uint32_t tickstart = HAL_GetTick();
    uint32_t wait = Delay;
    if (wait < HAL_MAX_DELAY) {
      wait += 1U;
    }
    while((HAL_GetTick() - tickstart) < wait) {
      __asm__ volatile("nop");
    }
  }
}

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

  /* Configure PLL: HSE(8MHz) / 8 * 336 / 2 = 168MHz, and PLLQ = 7 for 48MHz USB clock */
  LL_RCC_PLL_ConfigDomain_48M(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_8, 336, LL_RCC_PLLQ_DIV_7);
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
        
        // Allocate 20 KB (20480 bytes) from the FreeRTOS Heap
        void *temp_ptr = pvPortMalloc(20480);
        
        // Busy loop for 1 second (1000 ms)
        uint32_t start_tick = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(1000)) {
            // Keep CPU busy with a dummy operation
            __asm__ volatile("nop");
        }
        
        // Free the allocated memory
        if (temp_ptr != NULL) {
            vPortFree(temp_ptr);
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

        vTaskDelay(pdMS_TO_TICKS(500));
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

    /* Initialize USB Device Library, register AUDIO class and start the driver */
    if (USBD_Init(&hUsbDeviceFS, &FS_Desc, 0) == USBD_OK) {
        if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_AUDIO) == USBD_OK) {
            if (USBD_AUDIO_RegisterInterface(&hUsbDeviceFS, &USBD_AUDIO_fops) == USBD_OK) {
                USBD_Start(&hUsbDeviceFS);
                
                /* Initialize I2S3 Audio Stream & DMA first so MCLK is active for the codec */
                I2S_Audio_Init(48000);
                I2S_Audio_Start();
                
                /* Initialize CS43L22 Audio Codec after clocks are active */
                CS43L22_Init(48000);
            }
        }
    }

#if ENABLE_LOAD_TEST
    /* Create CPU Load Test task at priority 1 */
    xTaskCreate(load_test_task, "Load_Test", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
#endif

	/* Create DSP test task */
	xTaskCreate(dsp_test_task, "DSP_Test", configMINIMAL_STACK_SIZE + 128, NULL, 2, NULL);

	/* Start FreeRTOS scheduler */
	vTaskStartScheduler();

	while (1) {
        /* Should never reach here */
	}
	return 0;
}
