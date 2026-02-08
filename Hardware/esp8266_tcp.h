#ifndef __ESP8266_TCP_H
#define __ESP8266_TCP_H


#include "flash_log.h"
#include "bsp_usart_debug.h"
#include <ctype.h>



#define ESP_TCP_CMD_TIMEOUT  ( 500UL  )
#define ESP_TCP_CON_TIMEOUT  ( 50000UL )
// #define TCP_TIMEOUT_S        ( 50     )


typedef enum
{
  ESP_TCP_OK                   = 0,
  ESP_TCP_ERR_TIMEOUT          = -1,
  ESP_TCP_ERR_BUSY             = -2,
  ESP_TCP_ERR_CONNECT_FAIL     = -3, 
  ESP_TCP_ERR_NO_RESPONSE      = -4,
  ESP_TCP_ERR_INVALID_ARGS     = -5,
  ESP_TCP_ERR_CMD_BUILD_ERROR  = -6
} esp_tcp_err_t;


typedef enum
{
  ESP_TCP_STATE_DISCONNECTED = 0,
  ESP_TCP_STATE_CONNECTING,
  ESP_TCP_STATE_CONNECTED,
  ESP_TCP_STATE_DISCONNECTING,
  ESP_TCP_STATE_ERROR
} esp_tcp_state_t;



typedef enum 
{
  TCPv6 = 0,
  TCP
} connect_type_t;



typedef struct 
{
  
  bool is_Connected;
  uint8_t conn_ID;
  uint16_t Port;
  char Host[64];
  char remote_IP[18];
  esp_tcp_state_t state;
  uint8_t remain;  // 预留.

} esp_tcp_handle_t;



/* *********************************************** */
esp_tcp_err_t esp8266_tcp_Init( void );

const esp_tcp_handle_t* esp8266_tcp_getState( void );

esp_tcp_err_t esp8266_tcp_Connect( const char *Host, uint16_t Port, connect_type_t Mode );

esp_tcp_err_t esp8266_tcp_Disconnect( void );

esp_tcp_err_t esp8266_tcp_Send( const uint8_t *data, uint16_t data_len );

esp_tcp_err_t esp8266_tcp_DNSResolve( const char *Host, char *out_ip_str, uint8_t *out_ip_size );

/* *********************************************** */



#endif // __ESP8266_TCP_H
