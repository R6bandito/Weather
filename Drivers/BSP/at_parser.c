#include "at_parser.h"


/* ************************************************** */
extern void*  memmem
( 
  const uint8_t *haystack, uint16_t stack_len, 
  const void* need_str,    uint16_t need_str_len                      
);

static const uint8_t *find_nth( const uint8_t *buf, uint16_t buf_len, uint8_t find_index, char ch, bool pair );

static const uint8_t *skip_ws( const uint8_t *p );
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


/**
 * @brief 在指定缓冲区中查找第 N 个指定字符的出现位置（支持引号配对逻辑）
 *
 * 此函数专为 AT 命令响应解析设计，能安全处理含二进制数据的响应帧（如 +IPD）。
 * 
 * @param buf       指向待搜索的原始数据缓冲区（uint8_t*），允许含 '\0'
 * @param buf_len   缓冲区总长度（字节），必须 > 0
 * @param find_index 查找索引（从 1 开始计数）：
 *                    - 若 ch != '"' 或 pair == false：查找第 find_index 个 ch
 *                    - 若 ch == '"' 且 pair == true：查找第 find_index 对引号的**起始引号**（即第 (2*find_index-1) 个 '"'）
 * @param ch        要查找的目标字符（如 '"', ',', ':'）
 * @param pair      是否启用引号配对模式（仅当 ch == '"' 时有意义）
 *
 * @return          成功：指向第 find_index 个 ch 的指针（即 &buf[i]）
 *                  失败：NULL（参数非法 / 未找到 / 索引越界）
 *
 * @note            - 该函数不依赖 '\0' 终止符，完全由 buf_len 控制范围，适用于任意二进制 AT 响应
 *                  - 当 pair==true 且 ch!='"' 时，pair 参数被忽略（无副作用）
 *                  - 错误场景会通过 __DEBUG_LEVEL_1__ 输出诊断信息，不影响返回值
 *
 * @example         // 从 "+CIPSTATUS:0,\"TCP\",\"192.168.1.100\",..." 中提取第 2 个引号内 IP：
 *                  // 先调 find_nth(buf, len, 2, '"', true) → 得到第 2 对引号的起始 '"'
 *                  // 再向后找下一个 '"' → 即可截取 IP 字符串
 */
static const uint8_t *find_nth( const uint8_t *buf, uint16_t buf_len, uint8_t find_index, char ch, bool pair )
{
  if ( !buf || buf_len == 0 || find_index == 0 || find_index > buf_len )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of find_nth.\n");
    #endif 

    return NULL;
  }

  if ( ch == '"' && pair == true )
  {
    find_index = 2 * find_index - 1;
  }

  uint16_t i = 0;
  while( find_index && i <= buf_len - 1 )
  {
    if ( buf[i] == ch )
    {
      find_index --;

      if ( find_index == 0 )
      {
        return &buf[i];
      }
    } 
    
    i++;
  }

  if ( i >= buf_len || find_index != 0 )
  {
    // 没找到对应的数据.
    #if defined(__DEBUG_LEVEL_1__)
      printf("find_nth() called failed. Cannot find %u-th occurrence of '%c'.\n", find_index + 1, ch);
    #endif 

    return NULL;
  }

  return NULL;
}



/**
 * @brief 跳过字符串起始处的空白字符（空格、制表符、回车、换行）
 * 
 * 该函数从输入指针的**下一个位置**开始扫描（即跳过 *p 本身），向后查找第一个非空白字符。
 * 注意：此行为与常见 skip_ws 实现不同（通常包含 *p），此处设计用于配合 AT 响应中
 * 冒号/逗号后的“紧邻空白”场景（如 "+CIFSR:  \"192.168.1.100\"" 中冒号后可能有空格）。
 * 
 * @param p 指向待处理字符串的首地址（允许为 NULL）
 * @return const uint8_t* 指向第一个非空白字符的指针；若 p 为 NULL 或全为空白，则返回 NULL
 * @note 不检查缓冲区边界！调用者需确保 p 指向有效内存，且后续访问不越界。
 */
static const uint8_t *skip_ws( const uint8_t *p )
{
  if ( !p )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of skip_whiteBlank.\n");
    #endif

    return NULL;
  }

  p++;

  while( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' )
  {
    p++;
  }

  return p;
}



bool at_get_field( 
  const uint8_t *buffer, uint16_t buf_len, 
   at_field_type_t type, uint8_t index,
    const uint8_t **out_start, uint16_t *out_len 
  )
  {
    if ( !buffer || buf_len == 0 || !out_start || !out_len )
    {
      #if defined(__DEBUG_LEVEL_1__)
        printf("Wrong Param of at_get_field.\n");
      #endif 

      return false;
    }

    uint16_t out_count = 0;

    if ( type == AT_FIELD_IN_QUOTES )
    {
      const uint8_t *pReturn = find_nth(buffer, buf_len, index, '"', true);

      // 跳过可能潜在的空白符.
      pReturn = skip_ws(pReturn);

      if (!pReturn || pReturn >= buffer + buf_len) return false;

      *out_start = pReturn;

      const uint8_t *pTemp = pReturn;

      while( *pTemp != '"' )
      {
        out_count++;

        pTemp++;

        if ( pTemp >= buf_len + buffer )
        {
          #if defined(__DEBUG_LEVEL_1__)
            printf("at_get_field___AT_FIELD_IN_QUOTES extract wrong.\n");
          #endif 

          return false;
        }
      }

      *out_len = out_count;

      return true;
    }

    if ( type == AT_FIELD_AFTER_COLON )
    {
      const uint8_t *pReturn = find_nth(buffer, buf_len, index, ':', false);

      pReturn = skip_ws(pReturn);

      if (!pReturn || pReturn >= buffer + buf_len) return false;

      const uint8_t *pTemp = pReturn;

      while( *pTemp != ',' && *pTemp != ';' && *pTemp != ':' && *pTemp != '\0' && *pTemp != '\r' && *pTemp != '\n' )
      {
        out_count++;

        pTemp++;

        if ( pTemp >= buffer + buf_len )
        {
          #if defined(__DEBUG_LEVEL_1__)
            printf("at_get_field___AT_FIELD_AFTER_COLON extract wrong.\n");
          #endif 

          return false;
        }
      }

      *out_start = pReturn;
      *out_len = out_count;

      return true;
    }

    if ( type == AT_FIELD_BETWEEN_COMMA )
    {
      const uint8_t *pReturn = find_nth(buffer, buf_len, index, ',', false);
      pReturn = skip_ws(pReturn);
      if (!pReturn || pReturn >= buffer + buf_len) return false;
      const uint8_t *pTemp = pReturn;

      while( *pTemp != ',' )
      {
        out_count++;

        pTemp++;

        if ( pTemp >= buffer + buf_len )
        {
          #if defined(__DEBUG_LEVEL_1__)
            printf("at_get_field___AT_FIELD_BETWEEN_COMMA extract wrong.\n");
          #endif         
          
          return false;
        }
      }

      *out_start = pReturn;
      *out_len = out_count;

      return true;
    }

    #if defined(__DEBUG_LEVEL_1__)
      printf("Unexpected Error happened in at_get_field.\n");
    #endif 

    return false;
  }

