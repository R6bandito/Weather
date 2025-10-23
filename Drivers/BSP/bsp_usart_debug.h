#ifndef __USART_DEBUG_H
#define __USART_DEBUG_H


/*  *********************************************    */
  // 调式等级. 可同时开启__DEBUG_LEVEL_1__,__DEBUG_LEVEL_2__
  // 一但定义 __REALEASE__ 将会关闭所有调试功能. (无论是否定义 __DEBUG_LEVEL_1__,__DEBUG_LEVEL_2__)
              #define __DEBUG_LEVEL_1__
              #define __DEBUG_LEVEL_2__
              /* #define __REALEASE__ */
/*  *********************************************    */

#if !defined( __DEBUG_LEVEL_1__) && !defined(__DEBUG_LEVEL_2__) && !defined(__REALEASE__) 
#warning "No debug level defined! And Not in REALEASE Status."
#endif 

#if !defined(__REALEASE__)

#include "stm32f4xx_hal.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "Perp_clkEn.h"
#include "Mspinit.h"

/*  *********************************************    */
#define DEBUG_BAUDRATE             115200
#define DEBUG_USART                USART3
#define DEBUG_USART_PORT           GPIOB
#define DEBUG_USART_TX_PIN         GPIO_PIN_10
#define DEBUG_USART_RX_PIN         GPIO_PIN_11
/*  *********************************************    */

/*  *********************************************    */
#define DEBUG_LED_PORT             GPIOA
#define DEBUG_LED_PIN              GPIO_PIN_6
#define DEBUG_LED_ACTIVE_LEVEL     GPIO_PIN_RESET // The Pin_Level that will active the LED

#define DEBUG_LED_STATE_SHORT      100
#define DEBUG_LED_STATE_LONG       650
#define DEBUG_LED_STATE_CYCLE      1000
/*  *********************************************    */


/*  *********************************************    */
/*  调试LED状态展示 */
typedef enum {

  DEBUG_INIT_FAILED,

  DEBUG_SOURCE_GET_FAILED,

  DEBUG_HARD_FAULT,

  DEBUG_WRONG_PARAM

} DebugLedState_t;



typedef enum {

  RTOS_VER,

  COMN_VER

} DebugLEDEnvMode_t;
/*  *********************************************    */



  #if defined(__DEBUG_LEVEL_1__)

    ErrorStatus Debug_USART_Init( void );

  #endif // __DEBUG_LEVEL_1__


  #if defined(__DEBUG_LEVEL_2__)

    void Debug_Led_Init( void );

    void Debug_LED_Dis( DebugLedState_t State, DebugLEDEnvMode_t Mode );

  #endif // __DEBUG_LEVEL_2__


#endif // __USART_DEBUG_H

#endif // __REALEASE__


