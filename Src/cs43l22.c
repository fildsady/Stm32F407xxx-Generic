#include "cs43l22.h"
#include "usbd_conf.h"
#include "stm32f4xx_ll_utils.h"

/* I2C1 Handle */
I2C_HandleTypeDef hi2c1;

/* Private helper functions */
static void codec_write_reg(uint8_t reg, uint8_t value) {
  uint8_t data[2] = {reg, value};
  HAL_I2C_Master_Transmit(&hi2c1, CS43L22_I2C_ADDR << 1, data, 2, HAL_MAX_DELAY);
}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(i2cHandle->Instance==I2C1) {
    /* Enable GPIOB Clock */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Configure I2C1 SCL (PB6) and SDA (PB9) pins */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Enable I2C1 Clock */
    __HAL_RCC_I2C1_CLK_ENABLE();
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle) {
  if(i2cHandle->Instance==I2C1) {
    /* Disable I2C1 Clock */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /* Disable SCL and SDA GPIOs */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6|GPIO_PIN_9);
  }
}

uint8_t CS43L22_Init(uint32_t sample_rate) {
  UNUSED(sample_rate);

  /* 1. Configure Reset Pin (PD4) */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* Reset the codec */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_RESET);
  LL_mDelay(10);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_SET);
  LL_mDelay(10);

  /* 2. Initialize I2C1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    return 0; /* Init failed */
  }

  /* 3. Configure CS43L22 Registers */
  codec_write_reg(0x02, 0x01); /* Power Control 1: Power Down */
  codec_write_reg(0x04, 0xAF); /* Power Control 2: Headphone always on, speaker off */
  codec_write_reg(0x05, 0x81); /* Clocking Control: Auto-detect clock speed */
  codec_write_reg(0x06, 0x05); /* Interface Control: I2S Philips 24-bit format */
  
  /* Select analog routing path */
  codec_write_reg(0x0A, 0x00); /* Analog routing: DAC to output */
  
  /* Unmute */
  codec_write_reg(0x0E, 0x04); /* Mute Control: Headphone unmuted, speaker muted */
  
  /* Power up the chip */
  codec_write_reg(0x02, 0x9E); /* Power Control 1: Power Up */
  
  /* Set Volumes (Default: 0dB) */
  codec_write_reg(0x1F, 0x00); /* Master Volume */
  codec_write_reg(0x20, 0x18); /* Headphone A Volume (+24dB) */
  codec_write_reg(0x21, 0x18); /* Headphone B Volume (+24dB) */
  
  return 1; /* Success */
}

void CS43L22_SetVolume(uint8_t volume) {
  /* Volume range: 0..100 */
  /* Map 0..100 to CS43L22 register value (0x00 is 0dB, 0x18 is +24dB, 0xE0 is -100dB) */
  uint8_t reg_val = 0;
  if (volume == 0) {
    reg_val = 0xE0; /* Mute */
  } else {
    /* Map 1..100 to -40dB (0xD0) to +24dB (0x18) */
    int32_t val = ((int32_t)volume * 72 / 100) - 40;
    if (val < 0) {
      reg_val = (uint8_t)(256 + val);
    } else {
      reg_val = (uint8_t)val;
    }
  }
  codec_write_reg(0x20, reg_val);
  codec_write_reg(0x21, reg_val);
}
