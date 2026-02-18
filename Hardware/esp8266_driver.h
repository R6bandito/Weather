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
#include "at_parser.h"
#include "main.h"
#include <assert.h>
#include "queue.h"
#include "flash_log.h"


/* ********************************************** */
#define WIFI_SSID           "esptest"
#define WIFI_PASSWORD       "123456789"
/* ********************************************** */

/* ********************************************** */
#define ESP_UART_BAUDRATE    115200
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
#define RECV_DATA_BUFFER       1368
#define Tx_DATA_BUFFER         128
#define RECV_DATA              (0xFF)
#define NO_DATA                (0xFE)
#define DATA_QUEUE_LENGTH      4

#define WIFI_IPV4_LENGTH       40
#define WIFI_PASSWORD_LENGTH   65
#define WIFI_SSID_LENGTH       33

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

// 初始化状态枚举.
typedef enum {
  INIT_STATE_CHECK_AT,
  INIT_STATE_SET_MODE,
  INIT_STATE_CONNECT_WIFI,
  INIT_STATE_GET_IP,
  INIT_STATE_COMPLETE,
  INIT_STATE_ERROR
} EspInitState_t;


typedef enum {
  LastRecvFrame_Valid,  // 接收到的最新数据帧仍未被解析.
  LastRecvFrame_Used    // 接收到的最新数据帧已被解析.
} FrameStatus_t;

 
typedef struct
{
  uint8_t RecvData[RECV_DATA_BUFFER];

  size_t Data_Len;

} EspRecvMsg_t;


/**
 * @brief Esp8266模块结构体.
 *  
 *  char WifiSSID： WIFI名称.
 *  char WifiPassword：WIFI密码.
 *  char Wifi_Ipv4：Ipv4地址.
 * 
 *  EspWifiMode_t CurrentMode：ESP8266目前处于的网络连接模式.
 *            其参数(Param)可为： STATION             客户端模式.
 *                              SOFTAP              软路由模式.
 *                              STATION_SOFTAP      混合模式(结合上面两种模式的综合应用).
 *            当处于ESP_WIFI_ERROR时，表征此时未显式声明工作模式.
 * 
 *  EspWifiMode_t TargetMode：ESP8266目标工作模式.参数同上，正常工作时应有如下关系.
 *                      TargetMode == CurrentMode
 *            若出现其它情况则说明在最近一次切换工作模式时出现错误，未切换成功.
 * 
 *  EspStatus_t Status：ESP8266的当前工作状态.参数见 EspStatus_t.
 * 
 *  QueueHandle_t  xRecvQueue：ESP8266 AT响应回送数据队列.该队列用于接收ESP8266返回的数据.
 * 
 *  EspRecvMsg_t LastReceivedFrame：队列中最新一帧的数据.
 * 
 *  FrameStatus_t LastFrameValid：读取到的最新一帧数据是否已被解析.
 * 
 *  uint8_t RetryCount：初始化重置计数.
 * 
 *  uint8_t MaxRetry：初始化最多重试次数.
 */
typedef struct 
{
  char WifiSSID[WIFI_SSID_LENGTH];
  char WifiPassword[WIFI_PASSWORD_LENGTH];
  char Wifi_Ipv4[WIFI_IPV4_LENGTH];

  EspWifiMode_t CurrentMode;
  EspWifiMode_t TargetMode;
  EspStatus_t Status;  
  QueueHandle_t  xRecvQueue;
  EspRecvMsg_t LastReceivedFrame;
  FrameStatus_t LastFrameValid;

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

  void *esp8266_WaitResponse( const char* expected, uint32_t timeout_ms ); // 等待响应.

  void esp8266_DropLastFrame(void); // 丢弃当前数据帧.


  bool at_extractString_between_quotes
  ( 
    ESP8266_HandleTypeDef *hpesp8266, 
    const char *key,
    char* out_val,
    uint8_t out_len,
    BaseType_t mode
  );


  bool at_extractNum( ESP8266_HandleTypeDef *hpesp8266, const char *key, uint32_t *out_val, BaseType_t mode );

  bool at_extractField( ESP8266_HandleTypeDef *hpesp8266, at_field_type_t type, uint8_t index, const uint8_t **pReturn, uint16_t *pLen, BaseType_t mode );

  void*  memmem( 
    const uint8_t *haystack, uint16_t stack_len, 
    const void* need_str,    uint16_t need_str_len                      
  );

#ifdef __cplusplus
  }
#endif // __cplusplus


#endif // __ESP8266_DRIVER_H

