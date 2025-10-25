#ifndef __ESP8266_DRIVER_H
#define __ESP8266_DRIVER_H

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_usart_debug.h"
#include "Perp_clkEn.h"
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

/* ********************************************** */
#define ESP_USART_BAUDRATE    9600
#define ESP_USART_MODE        USART_MODE_TX_RX
#define ESP_USART_STOPBITS    USART_STOPBITS_1
#define ESP_USART_PARITY      USART_PARITY_NONE
#define ESP_USART_WORDLENGTH  USART_WORDLENGTH_8B
/* ********************************************** */


/* ********************************************** */
#define ESP_USART_RX          GPIO_PIN_3
#define ESP_USART_TX          GPIO_PIN_2
#define ESP_USART_PORT        GPIOA
#define ESP_USART_RST         GPIO_PIN_1
/* ********************************************** */


/* ********************************************** */
#define INIT_TASK_DEPTH        128
#define INIT_TASK_PRIO         8
/* ********************************************** */


/* ********************************************** */
#define COMMAND_BUFFER         128
#define RECV_DATA_BUFFER       1024
#define RECV_DATA              (0xFF)
#define NO_DATA                (0xFE)

typedef enum {
  STATION = 1,
  SOFTAP,
  STATION_SOFTAP,

  ESP_ERROR
} EspMode_t;

/* ********************************************** */


#ifdef __cplusplus
  extern "C" {
#endif // __cplusplus

  ErrorStatus vEspInit_TaskCreate( void );

  void vtask8266_Init( void *parameter );

  EspMode_t esp8266_ConnectModeChange( EspMode_t Mode );

  bool esp8266_SendAT( const char* format, ... );

  bool esp8266_WaitResponse( const char* expected, uint32_t timeout_ms );


#ifdef __cplusplus
  }
#endif // __cplusplus


#endif // __ESP8266_DRIVER_H

