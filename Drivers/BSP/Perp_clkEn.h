#ifndef __PERP_CLK_EN_H
#define __PERP_CLK_EN_H

#include "stm32f4xx_hal.h"
#include "bsp_usart_debug.h"

/* #define GPIOPORTS_HAVE_MORE */

/* #define TIM_HAVE_MORE */

/* #define USART_HAVE_MORE */


#ifdef __cplusplus
  extern "C" {
#endif // __cplusplus

void GPIO_Clk_Enable( GPIO_TypeDef * __gpio );

void TIM_Clk_Enable( TIM_TypeDef *__hptim );

void USART_Clk_Enable( USART_TypeDef *__usart );

#ifdef __cplusplus
  }
#endif // __cplusplus


#endif // __PERP_CLK_EN_H
