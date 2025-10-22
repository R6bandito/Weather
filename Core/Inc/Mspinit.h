#ifndef __USART_MSP_H
#define __USART_MSP_H

#include "stm32f4xx_hal.h"
#include "bsp_usart_debug.h"
#include "Perp_clkEn.h"

void HAL_USART_MspInit(USART_HandleTypeDef *husart);


#endif // __USART_MSP_H
