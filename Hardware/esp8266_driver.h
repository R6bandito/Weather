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
#include "main.h"
#include <assert.h>

/* ********************************************** */
#define ESP_UART_BAUDRATE    9600
#define ESP_UART_MODE        UART_MODE_TX_RX
#define ESP_UART_STOPBITS    UART_STOPBITS_1
#define ESP_UART_PARITY      UART_PARITY_NONE
#define ESP_UART_WORDLENGTH  UART_WORDLENGTH_8B
#define ESP_UART_HwFLOW      UART_HWCONTROL_NONE
#define ESP_UART             UART4
/* ********************************************** */


/* ********************************************** */
#define ESP_UART_RX          GPIO_PIN_11
#define ESP_UART_TX          GPIO_PIN_10
#define ESP_UART_PORT        GPIOC
#define ESP_UART_RST         GPIO_PIN_1
/* ********************************************** */


/* ********************************************** */
#define INIT_TASK_DEPTH        128
#define INIT_TASK_PRIO         8
/* ********************************************** */


/* ********************************************** */
#define COMMAND_BUFFER         128
#define RECV_DATA_BUFFER       512
#define Tx_DATA_BUFFER         128
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

