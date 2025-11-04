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
#define WIFI_SSID           "Your_Wifi_Name"
#define WIFI_PASSWORD       "123456789"
/* ********************************************** */

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
#define INIT_TASK_DEPTH        512
#define INIT_TASK_PRIO         8
#define MAX_RETRY_COUNT        3
/* ********************************************** */


/* ********************************************** */
#define COMMAND_BUFFER         128
#define RECV_DATA_BUFFER       512
#define Tx_DATA_BUFFER         128
#define RECV_DATA              (0xFF)
#define NO_DATA                (0xFE)

// WIFI模式枚举.
typedef enum {
  STATION = 1,
  SOFTAP,
  STATION_SOFTAP,
  ESP_WIFI_ERROR
} EspWifiMode_t; 


// 连接状态枚举
typedef enum {
  ESP_STATUS_DISCONNECTED,
  ESP_STATUS_CONNECTED_TO_AP,   // 已连接到路由器
  ESP_STATUS_GOT_IP,            // 已获取IP
  ESP_STATUS_CONNECTING,        // 正在连接
  ESP_STATUS_CONNECT_FAILED,    // 连接失败
  ESP_STATUS_UNKNOWN
} EspStatus_t;


typedef enum {
  INIT_STATE_RESET,
  INIT_STATE_CHECK_AT,
  INIT_STATE_SET_MODE,
  INIT_STATE_CONNECT_WIFI,
  INIT_STATE_GET_IP,
  INIT_STATE_COMPLETE,
  INIT_STATE_ERROR
} EspInitState_t;


typedef struct 
{
  char WifiSSID[33];
  char WifiPassword[65];

  EspWifiMode_t CurrentMode;
  EspWifiMode_t TargetMode;
  EspStatus_t Status;  

  uint8_t RetryCount;      
  uint8_t MaxRetry;        

} ESP8266_HandleTypeDef;

/* ********************************************** */


#ifdef __cplusplus
  extern "C" {
#endif // __cplusplus

  bool UART4_Init( void );

  ErrorStatus vEspInit_TaskCreate( void );

  void vtask8266_Init( void *parameter );

  EspWifiMode_t esp8266_ConnectModeChange( EspWifiMode_t Mode );

  bool esp8266_SendAT( const char* format, ... );

  void *esp8266_WaitResponse( const char* expected, uint32_t timeout_ms );


#ifdef __cplusplus
  }
#endif // __cplusplus


#endif // __ESP8266_DRIVER_H

