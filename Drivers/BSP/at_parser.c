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




/**
 * @brief 从原始 AT 响应缓冲区中提取 `"+key:"` 后首个连续十进制整数（无符号）
 *
 * 安全解析二进制响应帧（如 `LastReceivedFrame.RecvData`），不依赖 `\0` 终止符，
 * 严格基于显式长度 `buf_len` 进行边界检查，避免越界访问或 HardFault。
 *
 * @param[in]  buf       响应数据起始地址（`uint8_t*`，如 DMA 接收缓冲区）
 * @param[in]  buf_len   响应数据有效字节数（必须 > 0，由上层保证）
 * @param[in]  key       键名（不含 `"+"` 和 `":"`，如 `"CIPMUX"`、`"HTTPCLIENT"`）
 * @param[out] out_val   解析结果（成功时写入 `uint32_t`，不校验溢出）
 *
 * @retval true  找到 `"+key:"`，跳过前导空白后成功提取至少一位 `'0'-'9'`
 * @retval false 参数非法、未匹配模式、无数字、或 `buf_len` 不足
 *
 * @note
 *   - ✅ 仅识别连续 ASCII 数字（`0123456789`），遇 `,` `.` `-` `x` 等立即停止；
 *   - ✅ 自动跳过 `' '`, `'\t'`, `'\r'`, `'\n'` 等前导空白；
 *   - ⚠️ 不验证数值范围（如端口号 > 65535），上层需按协议语义校验；
 *   - ⚠️ 调用者须确保 `buf` 在解析期间不被 DMA 覆盖（推荐在 `xMutexEsp` 保护下使用）。
 */
bool at_get_num( const uint8_t *buf, uint16_t buf_len, const char *key, uint32_t *out_val )
{
  if ( !buf || !key || !out_val || buf_len <= 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of at_get_num in at_parser.c \n");
    #endif 

    return false;
  } 

  // 构造查找模式 "+KEY:"（AT 响应严格以 "+" 开头）.
  char pattern[32];
  int len = snprintf(pattern, sizeof(pattern), "+%s:", key);
  if ( len <= 0 || len >= (int)sizeof(pattern) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Construct keyVal failed in at_get_num.\n");
    #endif 

    return false;
  }

  const uint8_t *pFound = memmem(buf, buf_len, (const void *)pattern, len);
  if ( !pFound )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("memmem() cant get KeyVal in at_get_num.\n");
    #endif 

    return false;
  } 

  pFound += len;

  // 跳过前导空白.
  while( *pFound == ' ' || *pFound == '\t' || *pFound == '\r' || *pFound == '\n' )
  {
    pFound++;
  }

  if ( *pFound == '\0' )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("not get KeyVal in at_get_num.\n");
    #endif 

    return false;
  }

  // 获取连续数字.
  uint32_t value = 0;
  const uint8_t *qF = pFound;

  while( *qF >= '0' && *qF <= '9' )
  {
    uint32_t digit = *qF - '0';

    value = value * 10 + digit;

    qF++;
  }

  // 没有数字.
  if ( qF == pFound )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("No Num found in at_get_num.\n");
    #endif

    return false;
  }

  *out_val = value;
  return true;
}

