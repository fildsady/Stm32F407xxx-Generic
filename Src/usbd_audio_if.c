#include "usbd_audio_if.h"

/* Global audio state variables */
volatile uint32_t usb_audio_rx_count = 0;
volatile uint8_t usb_audio_streaming = 0;

/* Circular audio buffer for user processing */
#define AUDIO_BUFFER_SIZE 2048
volatile int16_t usb_audio_buffer[AUDIO_BUFFER_SIZE];
static volatile uint32_t usb_audio_write_ptr = 0;

/* Private function prototypes */
static int8_t AUDIO_Init_FS(uint32_t AudioFreq, uint32_t Volume, uint32_t options);
static int8_t AUDIO_DeInit_FS(uint32_t options);
static int8_t AUDIO_AudioCmd_FS(uint8_t *pbuf, uint32_t size, uint8_t cmd);
static int8_t AUDIO_VolumeCtl_FS(uint8_t vol);
static int8_t AUDIO_MuteCtl_FS(uint8_t cmd);
static int8_t AUDIO_PeriodicTC_FS(uint8_t *pbuf, uint32_t size, uint8_t cmd);
static int8_t AUDIO_GetState_FS(void);

/* Audio interface operations structure */
USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops = {
  AUDIO_Init_FS,
  AUDIO_DeInit_FS,
  AUDIO_AudioCmd_FS,
  AUDIO_VolumeCtl_FS,
  AUDIO_MuteCtl_FS,
  AUDIO_PeriodicTC_FS,
  AUDIO_GetState_FS,
};

static int8_t AUDIO_Init_FS(uint32_t AudioFreq, uint32_t Volume, uint32_t options) {
  UNUSED(AudioFreq);
  UNUSED(Volume);
  UNUSED(options);
  
  usb_audio_rx_count = 0;
  usb_audio_write_ptr = 0;
  usb_audio_streaming = 1;
  return USBD_OK;
}

static int8_t AUDIO_DeInit_FS(uint32_t options) {
  UNUSED(options);
  usb_audio_streaming = 0;
  return USBD_OK;
}

static int8_t AUDIO_AudioCmd_FS(uint8_t *pbuf, uint32_t size, uint8_t cmd) {
  UNUSED(pbuf);
  UNUSED(size);
  
  switch (cmd) {
    case AUDIO_CMD_START:
      usb_audio_streaming = 1;
      break;
    case AUDIO_CMD_PLAY:
      usb_audio_streaming = 1;
      break;
    case AUDIO_CMD_STOP:
      usb_audio_streaming = 0;
      break;
    default:
      break;
  }
  return USBD_OK;
}

static int8_t AUDIO_VolumeCtl_FS(uint8_t vol) {
  UNUSED(vol);
  return USBD_OK;
}

static int8_t AUDIO_MuteCtl_FS(uint8_t cmd) {
  UNUSED(cmd);
  return USBD_OK;
}

static int8_t AUDIO_PeriodicTC_FS(uint8_t *pbuf, uint32_t size, uint8_t cmd) {
  /* This is called every time a new packet of audio data is received */
  if (cmd == AUDIO_OUT_TC && pbuf != NULL && size > 0) {
    uint32_t samples_received = size / 2; /* 16-bit samples */
    int16_t *src = (int16_t *)pbuf;
    
    for (uint32_t i = 0; i < samples_received; i++) {
      usb_audio_buffer[usb_audio_write_ptr] = src[i];
      usb_audio_write_ptr = (usb_audio_write_ptr + 1) % AUDIO_BUFFER_SIZE;
    }
    
    usb_audio_rx_count += samples_received;
  }
  return USBD_OK;
}

static int8_t AUDIO_GetState_FS(void) {
  return USBD_OK;
}
