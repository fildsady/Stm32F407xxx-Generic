#ifndef __CS43L22_H
#define __CS43L22_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx.h"

/* CS43L22 I2C 7-bit Address */
#define CS43L22_I2C_ADDR   0x4A

/* Public functions */
uint8_t CS43L22_Init(uint32_t sample_rate);
void CS43L22_SetVolume(uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif /* __CS43L22_H */
