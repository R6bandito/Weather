#include "at_parser.h"


/* ************************************************** */
extern void*  memmem
( 
  const uint8_t *haystack, uint16_t stack_len, 
  const void* need_str,    uint16_t need_str_len                      
);
/* ************************************************** */


/**
 * @brief 从 buffer 中查找 key="value" 形式的值，并复制到 out 缓冲区
 *        ( 支持 key 后跟冒号或等号，value 在双引号内 ).
 */
bool at_get_string_between_quotes
(
  const uint8_t* buf, uint16_t len,
  const char* key,
  char* out_val, uint8_t out_len
)
{
  if ( buf == NULL || len <= 0 || key == NULL || out_len <= 0 || out_val == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of at_get_string_between_quotes in at_parser.c \n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_WRONG_PARAM, RTOS_VER);
    #endif // _DEBUG_LEVEL_2__

    return false;
  }

  uint16_t key_len = strlen(key);

  const uint8_t *start = (uint8_t *)memmem(buf, len, key, key_len);

  if ( start == NULL )
  {
    // 没有找到该关键词.
    return false;
  }

  // 跳过关键词.
  start += key_len;

  // 跳过可能存在的冒号，等于号 等的干扰.
  while ( (start < buf + len) && *start != '"')
  {
    start++;
  }
  

  // 没有双引号与之匹配，返回.
  if ( *start != '"' ) return false;

  start++;
  // 查找末尾双引号.
  const uint8_t *end = strchr(start, '"');

  // 未找到与之配对的双引号，返回.
  if ( end == NULL )
  { 
    #if defined(__DEBUG_LEVEL_1__)
      printf("Cant Get Relevant Value.(No Match ' \" ' )\n");
    #endif // __DEBUG_LEVEL_1__

    return false;
  }

  uint16_t val_len = end - start;
  if ( val_len >= out_len )  
  {
    // 提取到的数据长度大于所需输出长度，截断.
    val_len = out_len - 1;
  }
  memcpy(out_val, start, val_len);
  out_val[val_len] = '\0';

  return true;
}

