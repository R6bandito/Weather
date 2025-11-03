#ifndef __MAIN_H__
#define __MAIN_H__

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_usart_debug.h"
#include "hal_timebase.h"
#include "task_Exam.h"
#include "esp8266_driver.h"



// ========== 强制定义 ATOMIC_*  ==========
#ifndef __ATOMIC_MACROS_DEFINED__
#define __ATOMIC_MACROS_DEFINED__

// 防止重复定义
#ifndef ATOMIC_SET_BIT
  #define ATOMIC_SET_BIT(REG, BIT)                           \
    do {                                                     \
      uint32_t val;                                          \
      do {                                                   \
        val = __LDREXW((__IO uint32_t*)&(REG));              \
        val |= (BIT);                                        \
      } while (__STREXW(val, (__IO uint32_t*)&(REG)));       \
    } while (0)
#endif

#ifndef ATOMIC_CLEAR_BIT
  #define ATOMIC_CLEAR_BIT(REG, BIT)                         \
    do {                                                     \
      uint32_t val;                                          \
      do {                                                   \
        val = __LDREXW((__IO uint32_t*)&(REG));              \
        val &= ~(BIT);                                       \
      } while (__STREXW(val, (__IO uint32_t*)&(REG)));       \
    } while (0)
#endif

#ifndef ATOMIC_MODIFY_REG
  #define ATOMIC_MODIFY_REG(REG, CLEARMSK, SETMASK)          \
    do {                                                     \
      uint32_t val;                                          \
      do {                                                   \
        val = __LDREXW((__IO uint32_t*)&(REG));              \
        val = (val & (~(CLEARMSK))) | (SETMASK);             \
      } while (__STREXW(val, (__IO uint32_t*)&(REG)));       \
    } while (0)
#endif

#endif // __ATOMIC_MACROS_DEFINED__
// ============================================================



#endif // __MAIN_H__
