#ifndef __I2S_AUDIO_H
#define __I2S_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx.h"

/* Public functions */
uint8_t I2S_Audio_Init(uint32_t sample_rate);
void I2S_Audio_Start(void);
void I2S_Audio_Stop(void);
void I2S_Audio_ResetBuffer(void);

#ifdef __cplusplus
}
#endif

#endif /* __I2S_AUDIO_H */
