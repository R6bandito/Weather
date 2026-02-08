#ifndef __AT_PARSER_H
#define __AT_PARSER_H

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdbool.h>
#include "bsp_usart_debug.h"

typedef enum 
{
  AT_FIELD_IN_QUOTES,
  AT_FIELD_BETWEEN_COMMA,
  AT_FIELD_AFTER_COLON
} at_field_type_t;


/**
 * @brief 从 buffer 中查找 key="value" 形式的值，并复制到 out 缓冲区
 *        ( 支持 key 后跟冒号或等号，value 在双引号内 ).
 */
bool at_get_string_between_quotes( const uint8_t* buf, uint16_t len,
  const char *key,
  char* out_val, uint8_t out_len );


bool at_get_num( const uint8_t *buf, uint16_t buf_len, const char *key, uint32_t *out_val );


bool at_get_field( 
                  const uint8_t *buffer, uint16_t buf_len, 
                   at_field_type_t type, uint8_t index,
                    const uint8_t **out_start, uint16_t *out_len 
                  );


#endif // __AT_PARSER_H
