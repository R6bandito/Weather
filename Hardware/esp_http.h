#ifndef  __ESP_HTTP_H

#include <stdio.h>
#include <string.h>
#include "esp8266_tcp.h"

#define HTTP_HOST_MAX_LEN           ( 64U )
#define HTTP_PATH__MEX_LEN          ( 256U )
#define HTTP_EXTRA_HEAD_MAX_LEN     ( 128U )
#define HTTP_REQ_BUF_MAX_LEN        ( 512U )

#define HTTP_METHOD_GET             ( 0U )
#define HTTP_METHOD_POST            ( 1U )
#define HTTP_VERSION_1_1            ( 0U )



typedef enum 
{
  ESP_HTTP_OK                 = 0,
  ESP_HTTP_ERR_SET_VAL        = -1,
  ESP_HTTP_ERR_INVALID_ARGS   = -2,
  ESP_HTTP_ERR_BUF_OVRFLW     = -3,
  ESP_HTTP_ERR_OFFLINE        = -4,
  ESP_HTTP_ERR_BUILD_REQ      = -5,
  ESP_HTTP_ERR_SEND_WAIT_FAIL = -6,
  ESP_HTTP_ERR_EXTRACT        = -7
} esp_http_err_t;


typedef struct 
{

  char host[HTTP_HOST_MAX_LEN];
  char path[HTTP_PATH__MEX_LEN];
  char extra_headers[HTTP_EXTRA_HEAD_MAX_LEN];
  uint8_t method;
  uint8_t http_version;
  uint16_t total_len;

} esp_http_t;


esp_http_err_t http_Init( esp_http_t *__phttp, uint8_t method );

esp_http_err_t http_SetHost( esp_http_t *__phttp, const char *host );

esp_http_err_t http_SetPath( esp_http_t *__phttp, const char *path );

esp_http_err_t http_AddHeader( esp_http_t *__phttp, const char *header );

esp_http_err_t http_RequestBuild( esp_http_t *__phttp, char *out_buf, uint16_t out_buf_size );

esp_http_err_t http_Get( esp_http_t *__phttp, char *out_json_body, uint16_t out_json_body_buf_size );

bool http_extract_json_body( const char* http_response, char *out_json, uint16_t out_json_size );

#endif // __ESP_HTTP_H


