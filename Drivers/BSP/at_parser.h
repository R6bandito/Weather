#ifndef __AT_PARSER_H
#define __AT_PARSER_H

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdbool.h>
#include "bsp_usart_debug.h"


/**
 * @brief 从 buffer 中查找 key="value" 形式的值，并复制到 out 缓冲区
 *        ( 支持 key 后跟冒号或等号，value 在双引号内 ).
 */
bool at_get_string_between_quotes(const uint8_t* buf, uint16_t len,
  const char* key,
  char* out_val, uint8_t out_len);


#endif // __AT_PARSER_H
