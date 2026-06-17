#include "i2s_audio.h"
#include "usbd_audio_if.h"

/* I2S3 and DMA Handles */
I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_spi3_tx;

/* I2S Double Transmit Buffer */
/* 48kHz Stereo 16-bit: 1ms = 48 samples * 2 channels = 96 half-words */
/* Double buffer total size = 96 * 2 = 192 half-words */
#define I2S_HALF_BUF_SIZE   96
#define I2S_TOTAL_BUF_SIZE  192
static int16_t i2s_tx_buffer[I2S_TOTAL_BUF_SIZE];

/* Read pointer for the USB Audio circular buffer */
#define AUDIO_BUFFER_SIZE   2048
static volatile uint32_t usb_audio_read_ptr = 0;

/* Global flag for I2S status */
static volatile uint8_t i2s_playing = 0;

/* HAL I2S MSP Initialization */
void HAL_I2S_MspInit(I2S_HandleTypeDef* i2sHandle) {
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(i2sHandle->Instance==SPI3) {
    /* 1. Enable peripheral clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SPI3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* 2. Configure I2S3 Pins */
    /* WS (PA4) */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* MCK (PC7), CK (PC10), SD (PC12) */
    GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_10|GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* 3. Configure DMA1 Stream 7 for SPI3/I2S3 TX */
    hdma_spi3_tx.Instance = DMA1_Stream7;
    hdma_spi3_tx.Init.Channel = DMA_CHANNEL_0;
    hdma_spi3_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_spi3_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_spi3_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_spi3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_spi3_tx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_spi3_tx.Init.Mode = DMA_CIRCULAR;
    hdma_spi3_tx.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_spi3_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    HAL_DMA_Init(&hdma_spi3_tx);

    /* Link DMA handle to I2S handle */
    __HAL_LINKDMA(i2sHandle, hdmatx, hdma_spi3_tx);

    /* 4. Configure DMA Interrupt priorities */
    HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
  }
}

void HAL_I2S_MspDeInit(I2S_HandleTypeDef* i2sHandle) {
  if(i2sHandle->Instance==SPI3) {
    /* Disable DMA1 Stream 7 Interrupt */
    HAL_NVIC_DisableIRQ(DMA1_Stream7_IRQn);

    /* De-initialize DMA */
    HAL_DMA_DeInit(&hdma_spi3_tx);

    /* De-initialize GPIO Pins */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4);
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_7|GPIO_PIN_10|GPIO_PIN_12);

    /* Disable I2S3 Clock */
    __HAL_RCC_SPI3_CLK_DISABLE();
  }
}

uint8_t I2S_Audio_Init(uint32_t sample_rate) {
  UNUSED(sample_rate);

  /* 1. Configure PLLI2S Clock for 48kHz audio */
  RCC_PeriphCLKInitTypeDef rcc_periph_clk;
  HAL_RCCEx_GetPeriphCLKConfig(&rcc_periph_clk);
  rcc_periph_clk.PeriphClockSelection = RCC_PERIPHCLK_I2S;
  rcc_periph_clk.PLLI2S.PLLI2SN = 258;
  rcc_periph_clk.PLLI2S.PLLI2SR = 3;
  if (HAL_RCCEx_PeriphCLKConfig(&rcc_periph_clk) != HAL_OK) {
    return 0; /* Clock config failed */
  }

  /* Enable PLLI2S */
  __HAL_RCC_PLLI2S_ENABLE();
  while(__HAL_RCC_GET_FLAG(RCC_FLAG_PLLI2SRDY) == RESET) { }

  /* 2. Configure I2S3 (SPI3) */
  hi2s3.Instance = SPI3;
  hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s3.Init.CPOL = I2S_CPOL_LOW;

  if (HAL_I2S_Init(&hi2s3) != HAL_OK) {
    return 0; /* I2S init failed */
  }

  /* Clear output buffers */
  for (int i = 0; i < I2S_TOTAL_BUF_SIZE; i++) {
    i2s_tx_buffer[i] = 0;
  }
  usb_audio_read_ptr = 0;

  return 1;
}

void I2S_Audio_Start(void) {
  if (i2s_playing == 0) {
    HAL_I2S_Transmit_DMA(&hi2s3, (uint16_t *)i2s_tx_buffer, I2S_TOTAL_BUF_SIZE);
    i2s_playing = 1;
  }
}

void I2S_Audio_Stop(void) {
  if (i2s_playing == 1) {
    HAL_I2S_DMAStop(&hi2s3);
    i2s_playing = 0;
  }
}

/* Local helper function to feed data from USB buffer to I2S buffer */
static void feed_audio_samples(int16_t *dest_buffer, uint32_t num_samples) {
  if (usb_audio_streaming) {
    /* Calculate current available samples in USB buffer */
    uint32_t write_ptr = usb_audio_rx_count % AUDIO_BUFFER_SIZE;
    uint32_t read_ptr = usb_audio_read_ptr;
    uint32_t available = 0;
    
    if (write_ptr >= read_ptr) {
      available = write_ptr - read_ptr;
    } else {
      available = (AUDIO_BUFFER_SIZE - read_ptr) + write_ptr;
    }

    if (available >= num_samples) {
      /* Copy from USB buffer to I2S DMA buffer */
      for (uint32_t i = 0; i < num_samples; i++) {
        dest_buffer[i] = usb_audio_buffer[usb_audio_read_ptr];
        usb_audio_read_ptr = (usb_audio_read_ptr + 1) % AUDIO_BUFFER_SIZE;
      }
    } else {
      /* Underflow: fill with zeros to avoid static noise */
      for (uint32_t i = 0; i < num_samples; i++) {
        dest_buffer[i] = 0;
      }
    }
  } else {
    /* Audio is not streaming: output silence */
    for (uint32_t i = 0; i < num_samples; i++) {
      dest_buffer[i] = 0;
    }
  }
}

/* I2S DMA Callbacks */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI3) {
    /* Feed the first half of the I2S DMA buffer */
    feed_audio_samples(&i2s_tx_buffer[0], I2S_HALF_BUF_SIZE);
  }
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI3) {
    /* Feed the second half of the I2S DMA buffer */
    feed_audio_samples(&i2s_tx_buffer[I2S_HALF_BUF_SIZE], I2S_HALF_BUF_SIZE);
  }
}
