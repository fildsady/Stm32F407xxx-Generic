#ifndef __USBD_AUDIO_IF_H
#define __USBD_AUDIO_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_audio.h"

extern USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops;

/* Audio processing buffer and counter variables */
extern volatile uint32_t usb_audio_rx_count;
extern volatile uint8_t usb_audio_streaming;
extern volatile int32_t usb_audio_buffer[];

#ifdef __cplusplus
}
#endif

#endif /* __USBD_AUDIO_IF_H */
