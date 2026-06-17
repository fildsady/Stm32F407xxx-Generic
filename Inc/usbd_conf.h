#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#define USE_HAL_DRIVER
#include "stm32f4xx_hal.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USBD_MAX_NUM_INTERFACES     2U
#define USBD_MAX_NUM_CONFIGURATION  1U
#define USBD_MAX_STR_DESC_SIZ       0x100U
#define USBD_SELF_POWERED           1U
#define USBD_DEBUG_LEVEL            0U

/* AUDIO Class Config */
#define USBD_AUDIO_FREQ             48000U

/* Memory management macros */
#define USBD_malloc                 malloc
#define USBD_free                   free
#define USBD_memset                 memset
#define USBD_memcpy                 memcpy
#define USBD_Delay                  LL_mDelay

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CONF_H */
