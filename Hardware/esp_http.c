#include "esp_http.h"
#include "esp8266_driver.h"

/* ******************************** */
esp_http_err_t http_Init( esp_http_t *__phttp, uint8_t method );

esp_http_err_t http_SetHost( esp_http_t *__phttp, const char *host );

esp_http_err_t http_SetPath( esp_http_t *__phttp, const char *path );

esp_http_err_t http_AddHeader( esp_http_t *__phttp, const char *header );

esp_http_err_t http_RequestBuild( esp_http_t *__phttp, char *out_buf, uint16_t out_buf_size );

esp_http_err_t http_Get( esp_http_t *__phttp, char *out_json_body, uint16_t out_json_body_buf_size );

bool http_extract_json_body( const char* http_response, char *out_json, uint16_t out_json_size );

esp_http_err_t http_json_getCity( const char *json_body, char *out_city, uint16_t out_city_buf_len );

esp_http_err_t http_json_getString( const char *json_main, const char **key_paths, uint8_t path_cnt, char *out_buf, uint16_t out_size );

esp_http_err_t http_json_getNum( const char *json_main, const char **key_paths, uint8_t path_cnt, double *out_num );

static void safety_strncpy( char *dest, const char *source, size_t length );

static bool json_util_extractQuotes( const char *source, char *out_str, uint16_t buf_len );
/* ******************************** */


/* ******************************** */
static char JsonBody[1024];

extern ESP8266_HandleTypeDef hesp8266;

/* ******************************** */


// 跳过空白、逗号、冒号、花括号（安全跳过 JSON 结构符）
static const char* skip_ws_and_struct(const char *p) {
  while ( *p && (isspace((unsigned char)*p ) || *p == ',' || *p == ':' || *p == '{' || *p == '[')) {
      p++;
  }
  return p;
}




static bool json_util_extractQuotes( const char *source, char *out_str, uint16_t buf_len )
{
  if ( !source || !out_str || buf_len == 0 )  return false;

  const char *p = source;

  while( *p && *p != '"' )  p++;  // 找到第一个".

  if ( *p != '"' )  return false;

  ++p;

  const char *q = p;
  uint16_t count = 0;
 
  while( *p )
  {
    if ( *p == '"' && ( (p == source + 1) || *(p-1) != '\\') )
    {
      // 找到第二个非转义结束的".
      break;
    }

    p++;
    count++;
  }

  if ( count >= buf_len - 1 ) count = buf_len - 2;

  memcpy(out_str, q, count);
  out_str[count] = '\0';
  return true;
}


/**
 * @brief 安全字符串拷贝函数（零内存越界风险）
 *
 * 将 source 字符串最多拷贝 (length - 1) 个字节到 dest，并强制以 '\0' 终止。
 * 该函数完全规避了标准库 strncpy() 的三大缺陷：
 *   1. 不保证 dest 以 '\0' 结尾（当 source 长度 >= length 时）；
 *   2. 会用 '\0' 填充剩余空间（低效且无意义）；
 *   3. 无法处理 NULL 指针（导致未定义行为）。
 *
 * @param dest      [out] 目标缓冲区指针（必须非 NULL，且长度 ≥ length）
 * @param source    [in]  源字符串指针（允许为 NULL，此时 dest 被置为 ""）
 * @param length    [in]  dest 缓冲区总字节数（含 '\0' 终止符）
 *
 * @return          无返回值（void）
 *
 * @note
 *   - 若 dest == NULL 或 source == NULL 或 length == 0，则立即返回（无操作）；
 *   - 若 source 长度 ≥ (length - 1)，则只拷贝前 (length - 2) 字节 + 末尾 '\0'；
 *   - 本函数不调用任何动态内存分配函数，纯栈操作，适用于中断/RTOS 环境；
 *
 * @example
 *   char buf[10];
 *   safety_strncpy(buf, "HelloWorld", sizeof(buf)); // → "HelloWorl\0"
 *   safety_strncpy(buf, NULL, sizeof(buf));          // → "\0"
 */
static void safety_strncpy( char *dest, const char *source, size_t length )
{
  if ( !dest || !source || length == 0 ) return;

  size_t len = strlen(source);
  size_t copy_len = (len < length - 1) ? len : length - 1;
  memcpy(dest, source, copy_len);
  dest[copy_len] = '\0';
}




/**
 * @brief 初始化 HTTP 请求结构体，重置所有字段为安全默认值
 * 
 * 此函数将 __phttp 所有成员清零（包括 host/path/headers 缓冲区），并设置：
 *   - method：根据入参 method 非零值选择 HTTP_METHOD_POST 或 HTTP_METHOD_GET
 *   - http_version：固定为 HTTP_VERSION_1_1（当前唯一支持版本）
 *   - total_len：初始化为 0（表示尚未构建请求）
 * 
 * ⚠️ 注意：该函数不执行任何网络操作，仅做内存初始化。
 * 
 * @param __phttp 指向待初始化的 esp_http_t 结构体指针（必须非 NULL）
 * @param method  方法标识符：
 *                - 0 → 设置为 HTTP_METHOD_GET（默认）
 *                - 非 0 → 设置为 HTTP_METHOD_POST（预留扩展，当前 GET 专用）
 * @return ESP_HTTP_OK           成功初始化（__phttp 已就绪）
 * @return ESP_HTTP_ERR_INVALID_ARGS  __phttp 为 NULL
 * 
 * @note 调用后必须依次调用 http_SetHost() 和 http_SetPath() 才能构建有效请求。
 * @see http_SetHost(), http_SetPath(), http_RequestBuild()
 */
esp_http_err_t http_Init( esp_http_t *__phttp, uint8_t method )
{
  if ( !__phttp )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of http_Init.\n");
    #endif 

    LOG_WRITE(LOG_WARNING, "HTTP", "Wrong Param of http_Init.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  memset(__phttp, 0, sizeof(esp_http_t));

  if ( method )
  {
    __phttp->method = HTTP_METHOD_POST;
  }
  else 
  {
    __phttp->method = HTTP_METHOD_GET;
  }

  __phttp->http_version = HTTP_VERSION_1_1;

  return ESP_HTTP_OK;
}




/**
 * @brief 设置 HTTP 请求的目标主机（Host header 及 TCP 连接地址）
 * 
 * 此函数将 host 字符串安全拷贝至 __phttp->host 缓冲区，并执行基础校验：
 *   - 检查指针非空；
 *   - 使用 safety_strncpy 截断防溢出（最大 HTTP_HOST_MAX_LEN-1 字节 + '\0'）；
 *   - 确保拷贝后 host 非空字符串（拒绝纯空白或空串）。
 * 
 * ⚠️ 注意：
 *   - host 不含端口（如填 "api.seniverse.com"，而非 "api.seniverse.com:80"）；
 *   - 该值将用于：
 *         • 构建 "Host: xxx" HTTP header；
 *         • 调用 esp8266_tcp_Connect() 时作为域名参数；
 *   - 若 host 含非法字符（如空格、控制符），safety_strncpy 仍会拷贝，但后续
 *     TCP 连接或 DNS 解析可能失败（此层不校验字符合法性，由底层驱动处理）。
 * 
 * @param __phttp 指向已初始化的 esp_http_t 结构体（非 NULL）
 * @param host    目标主机名字符串（非 NULL，且以 '\0' 结尾，推荐 ASCII 域名）
 * 
 * @return ESP_HTTP_OK           成功设置（host 已写入且非空）
 * @return ESP_HTTP_ERR_INVALID_ARGS  __phttp 或 host 为 NULL
 * @return ESP_HTTP_ERR_SET_VAL      host 拷贝后为空字符串（如传入 ""、"   "、"\t\n"）
 * 
 * @note 调用前请确保已调用 http_Init() 初始化结构体。
 * @see http_Init(), http_RequestBuild(), esp8266_tcp_Connect()
 */
esp_http_err_t http_SetHost( esp_http_t *__phttp, const char *host )
{
  if ( !__phttp || !host )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of http_SetHost.\n");
    #endif 

    LOG_WRITE(LOG_WARNING, "HTTP", "Wrong Param of http_SetHost.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  safety_strncpy(__phttp->host, host, sizeof(__phttp->host));

  if ( strlen(__phttp->host) == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Http set host error. Empty host.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "HTTP", "Http set host error. Empty host.");
    return ESP_HTTP_ERR_SET_VAL;
  } 

  return ESP_HTTP_OK;
}




/**
 * @brief 设置 HTTP 请求路径（URL 的 path + query 部分）
 * 
 * 此函数将 `path` 字符串安全拷贝至 `__phttp->path` 缓冲区，并执行以下检查：
 *   - 输入指针非 NULL；
 *   - `path` 必须以 '/' 开头（如 "/v3/weather/now.json"）；
 *   - 拷贝后自动截断并补 '\0'，防止缓冲区溢出；
 *   - 拷贝结果非空（拒绝仅含空白或全 '\0' 的输入）。
 * 
 * @param __phttp 指向已初始化的 esp_http_t 结构体（必须非 NULL）
 * @param path    请求路径字符串，格式为 "/xxx"（必须以 '/' 开头），支持 query 参数  
 *                （例如："/v3/weather/now.json?key=abc&location=shanghai"）
 * @return        ESP_HTTP_OK          成功设置  
 *                ESP_HTTP_ERR_INVALID_ARGS  输入指针为空 或 path 不以 '/' 开头  
 *                ESP_HTTP_ERR_SET_VAL       拷贝后路径为空（无效字符串）  
 *                （注：不返回 ESP_HTTP_ERR_BUF_OVRFLW —— 截断由 safety_strncpy 内部处理）
 *
 * @note 该函数是纯内存操作，不触发网络通信；线程安全（无全局状态依赖）。
 */
esp_http_err_t http_SetPath( esp_http_t *__phttp, const char *path )
{
  if ( !__phttp || !path )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Warong Param of http_SetPath.\n");
    #endif         

    LOG_WRITE(LOG_WARNING, "HTTP", "Warong Param of http_SetPath.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  if ( path[0] != '/' )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("set_path: must start with '/'\n");
    #endif          

    LOG_WRITE(LOG_WARNING, "HTTP", "set_path: must start with '/'.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  safety_strncpy(__phttp->path, path, sizeof(__phttp->path));

  if ( strlen(__phttp->path) == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Http set path error. Empty path.\n");
    #endif              

    LOG_WRITE(LOG_ERROR, "HTTP", "Http set path error. Empty path.");
    return ESP_HTTP_ERR_SET_VAL;
  } 

  return ESP_HTTP_OK;
}




/**
 * @brief 向 HTTP 请求中追加一条自定义请求头（Header），支持多次调用
 *
 * 此函数将 header 字符串（如 "User-Agent: WeatherClock/1.0"）安全添加到
 * __phttp->extra_headers 缓冲区末尾，并自动添加 "\r\n" 分隔符。
 * 若为首次添加，则不前置换行；若已有内容，则先追加 "\r\n" 再写入新 header。
 *
 * ✅ 特性：
 *   - 自动长度校验，防止缓冲区溢出（HTTP_EXTRA_HEAD_MAX_LEN）
 *   - 空指针与空字符串防护
 *   - 严格遵循 HTTP/1.1 头部格式（不添加额外空格或换行）
 *   - 零动态内存分配，全栈操作，线程安全（前提：调用者确保 __phttp 不被并发修改）
 *
 * ⚠️ 注意：
 *   - header 参数 **不应包含结尾的 "\r\n"**（函数会自动添加）
 *   - header 中若含双引号、逗号等特殊字符，需由调用者保证其符合 HTTP 字段值语法
 *   - 调用后需再次调用 http_RequestBuild() 才能使新 header 生效
 *
 * @param __phttp 指向已初始化的 esp_http_t 结构体（非 NULL）
 * @param header  待添加的头部字符串，格式为 "Key: Value"（例如："Accept: application/json"）
 *
 * @return ESP_HTTP_OK           成功添加
 * @return ESP_HTTP_ERR_INVALID_ARGS  __phttp 或 header 为 NULL
 * @return ESP_HTTP_ERR_BUF_OVRFLW   缓冲区不足，无法容纳 header + "\r\n"
 */
esp_http_err_t http_AddHeader( esp_http_t *__phttp, const char *header )
{
  if ( !__phttp || !header )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of http_AddHeader.\n");
    #endif       

    LOG_WRITE(LOG_WARNING, "HTTP", "Wrong Param of http_AddHeader.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  }

  size_t current_len = strlen(__phttp->extra_headers);
  size_t header_len = strlen(header);
  size_t need_len = current_len + header_len + 2; // 留出 /r/n.

  if ( need_len >= sizeof(__phttp->extra_headers) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("add_header: overflow (need %u)\n", (unsigned)need_len);
    #endif          

    LOG_WRITE(LOG_ERROR, "HTTP", "add_header: overflow (need %u)\n", (unsigned)need_len);
    return ESP_HTTP_ERR_BUF_OVRFLW;
  } 

  // 自动换行.
  if ( current_len > 0 )
  {
    strcat(__phttp->extra_headers, "\r\n");
  }

  strcat(__phttp->extra_headers, header);

  return ESP_HTTP_OK;
}




/**
 * @brief 构建标准 HTTP/1.1 GET 请求报文（不含请求体），用于后续 esp8266_tcp_Send()
 *
 * 此函数将当前 esp_http_t 配置（host/path/headers）组装为符合 RFC 7230 的
 * 完整 HTTP 请求行 + 头部字段，格式如下：
 *   GET /path HTTP/1.1\r\n
 *   Host: example.com\r\n
 *   Connection: close\r\n
 *   [Custom Headers...]\r\n
 *   \r\n
 *
 * ✅ 关键保障：
 *   - 严格校验输入参数（NULL / 零长度 / TCP 连接状态）
 *   - 预计算最小所需缓冲区大小，防止 snprintf 截断或溢出
 *   - 自动添加必要头部（Host, Connection: close），无需手动设置
 *   - 支持空 extra_headers（不插入多余 "\r\n"）
 *   - 输出字符串以 '\0' 结尾，且 __phttp->total_len 精确记录有效字节数（含 '\0' 前）
 *
 * ⚠️ 重要约束：
 *   - 仅支持 GET 方法（POST 返回 ESP_HTTP_ERR_INVALID_ARGS，预留扩展）
 *   - 不生成 Content-Length 或 Transfer-Encoding（GET 无 body）
 *   - Host 字段值直接取自 __phttp->host（不自动补端口，若需端口请写为 "api.seniverse.com:80"）
 *   - out_buf 必须是可写内存，且 out_buf_size ≥ min_need + 1（snprintf 安全要求）
 *
 * 📌 调用前提：
 *   - 已调用 http_Init() + http_SetHost() + http_SetPath()（至少 host/path 非空）
 *   - TCP 已处于 ESP_TCP_STATE_CONNECTED 状态（否则返回 ESP_HTTP_ERR_OFFLINE）
 *   - 若添加了自定义 header，需确保其格式合法（如 "Key: Value"，不含结尾 \r\n）
 *
 * @param __phttp     指向已配置的 esp_http_t 实例（非 NULL）
 * @param out_buf     输出缓冲区地址（用于存放构建好的 HTTP 请求字符串）
 * @param out_buf_size out_buf 总字节数（必须 ≥ 128，建议 ≥ 512）
 *
 * @return ESP_HTTP_OK              构建成功，out_buf 中已存入完整请求报文
 * @return ESP_HTTP_ERR_INVALID_ARGS 参数非法（NULL / host/path 为空 / method 非 GET）
 * @return ESP_HTTP_ERR_OFFLINE      TCP 未连接（需先调用 esp8266_tcp_Connect()）
 * @return ESP_HTTP_ERR_BUF_OVRFLW   out_buf_size 不足以容纳请求（min_need ≥ out_buf_size）
 * @return ESP_HTTP_ERR_BUILD_REQ    snprintf 内部失败（极罕见，通常因 out_buf 无效或内存损坏）
 */
esp_http_err_t http_RequestBuild( esp_http_t *__phttp, char *out_buf, uint16_t out_buf_size )
{
  if ( !__phttp || !out_buf || out_buf_size == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of http_RequestBuild.\n");
    #endif           

    LOG_WRITE(LOG_WARNING, "HTTP", "Wrong Param of http_RequestBuild.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  }

  if ( esp8266_tcp_getState()->is_Connected == false || esp8266_tcp_getState()->state != ESP_TCP_STATE_CONNECTED )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("TCP Connect not detected. http_RequestBuild() called failed.\n");
    #endif      

    LOG_WRITE(LOG_WARNING, "HTTP", "No TCP Con. RequestBuild() called fail.");
    return ESP_HTTP_ERR_OFFLINE;
  }

  if ( strlen(__phttp->host) == 0 || strlen(__phttp->path) == 0 )
  {
    LOG_WRITE(LOG_WARNING, "HTTP", "build: host or path not set.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  }

  if ( __phttp->method == HTTP_METHOD_POST )
  {
    return ESP_HTTP_ERR_INVALID_ARGS;
  }

  // "GET /path HTTP/1.1\r\nHost: ...\r\nConnection: close\r\n[headers]\r\n\r\n"
  size_t min_need = 
          4U +                            // "GET "
          strlen(__phttp->path) +         // "/path"
          11U +                           // " HTTP/1.1\r\n"
          6U +                            // "Host: "
          strlen(__phttp->host) + 2U +    // ".../r/n"
          19U +                           // "Connection: close\r\n"
          strlen(__phttp->extra_headers) +  // "[headers]"
          2U +                              // "[\r\n]"
          1U;                               // "'\0'"

  if ( min_need >= out_buf_size )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("build: buf too small (need %u, have %u)", (unsigned)min_need, (unsigned)out_buf_size);
    #endif 

    LOG_WRITE(LOG_ERROR, "HTTP", "requestbuf too small(need %u,have %u)",(unsigned)min_need, (unsigned)out_buf_size);
    return ESP_HTTP_ERR_BUF_OVRFLW;
  } 

  int len = snprintf(out_buf, out_buf_size, 
                      "GET %s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "Connection: close\r\n"
                      "%s%s\r\n", __phttp->path, __phttp->host, (__phttp->extra_headers[0] != '\0') ? __phttp->extra_headers : "",
                                                                  (__phttp->extra_headers[0] != '\0') ? "\r\n" : "" );

  if ( len < 0 || len >= (int)out_buf_size )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("snprintf failed in http_RequestBuild");
    #endif     

    LOG_WRITE(LOG_ERROR, "HTTP", "snprintf failed in http_RequestBuild");
    return ESP_HTTP_ERR_BUILD_REQ;
  } 

  printf("%s", out_buf);

  __phttp->total_len = (uint16_t)len;

  return ESP_HTTP_OK;
}





/**
 * @brief 执行一次完整的 HTTP GET 请求流程（连接 → 构建 → 发送 → 接收 → JSON 提取）
 *
 * 本函数封装了从零开始发起 HTTP GET 请求所需的全部步骤，专为嵌入式资源受限场景（ESP8266 + STM32F4）优化：
 *   - ✅ 自动检测 TCP 连接状态：若未连接，则调用 esp8266_tcp_Connect() 尝试连接目标 host:80；
 *   - ✅ 安全构建请求：调用 http_RequestBuild() 生成标准 HTTP/1.1 GET 报文（含 Host、Connection: close）；
 *   - ✅ 同步发送与接收：调用 esp8266_tcp_Send() 发送请求，并复用 LastReceivedFrame 获取响应；
 *   - ✅ 智能提取 JSON body：自动跳过 HTTP headers（定位首个 "\r\n\r\n"），仅返回纯净 JSON 字符串；
 *   - ✅ 资源友好：全程使用栈/静态缓冲区，**零 malloc/free，零动态内存分配**；
 *   - ✅ 错误收敛：所有底层错误（AT 超时、解析失败、TCP 断连）统一映射为 esp_http_err_t，便于上层统一处理。
 *
 * @pre
 *   - 必须已在 vtask8266_Init() 或类似初始化任务中完成：
 *       • esp8266_driver_init()（UART/DMA/定时器/互斥量）；
 *       • esp8266_tcp_Init()（TCP 子系统初始化）；
 *       • __phttp 已通过 http_Init() + http_SetHost() + http_SetPath() 正确配置；
 *   - out_json_body 缓冲区必须 ≥ 512 字节（典型天气 API 响应长度 ≤ 400B）；
 *   - 当前固件支持 "+IPD," 响应格式（所有标准 ESP8266 AT 固件均支持）。
 *
 * @param[in]  __phttp               已初始化并配置好的 esp_http_t 结构体指针（非 NULL）
 * @param[out] out_json_body         输出缓冲区，用于存放提取出的 JSON body 字符串（以 '\0' 结尾）
 * @param[in]  out_json_body_buf_size out_json_body 总字节数（含 '\0'，建议 ≥ 512）
 *
 * @return esp_http_err_t            执行结果码：
 *         - @ref ESP_HTTP_OK                : 成功；out_json_body 中已写入有效 JSON 字符串；
 *         - @ref ESP_HTTP_ERR_INVALID_ARGS  : 输入参数非法（NULL / size=0）；
 *         - @ref ESP_HTTP_ERR_OFFLINE       : TCP 连接失败或无响应（esp8266_tcp_Connect/Send 失败）；
 *         - @ref ESP_HTTP_ERR_BUILD_REQ     : http_RequestBuild() 构建失败（host/path 未设置等）；
 *         - @ref ESP_HTTP_ERR_EXTRACT       : 无法从响应中定位 "\r\n\r\n" 或 JSON body 为空；
 *         - @ref ESP_HTTP_ERR_SEND_WAIT_FAIL: esp8266_tcp_Send() 返回非 ESP_TCP_OK。
 *
 * @note
 *   - 本函数是**可重入的（reentrant）**：内部不依赖全局静态变量（除已加锁的 hesp8266.LastReceivedFrame）；
 *   - 不会保持长连接：每次调用均在发送后隐式断开（由 esp8266_tcp_Send() 内部触发 +IPD 等待逻辑决定）；
 *   - 若需连续请求，请在上层控制重试逻辑（推荐指数退避：1s → 2s → 4s）；
 *   - 日志输出遵循 LOG_WRITE() 规范，调试信息在 __DEBUG_LEVEL_1__ 启用时打印至 USART。
 *
 * @example
 *   esp_http_t req;
 *   http_Init(&req, HTTP_METHOD_GET);
 *   http_SetHost(&req, "api.seniverse.com");
 *   http_SetPath(&req, "/v3/weather/now.json?key=xxx&location=beijing");
 *
 *   char json_buf[512];
 *   if (http_Get(&req, json_buf, sizeof(json_buf)) == ESP_HTTP_OK) {
 *       printf("✅ Got JSON: %.*s\n", (int)strlen(json_buf), json_buf);
 *       // → 后续调用 json_get_int(json_buf, "\"temperature\":", &temp);
 *   }
 */
esp_http_err_t http_Get( esp_http_t *__phttp, char *out_json_body, uint16_t out_json_body_buf_size )
{
  if ( !__phttp || !out_json_body || out_json_body_buf_size == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of http_Get.\n");
    #endif

    LOG_WRITE(LOG_WARNING, "HTTP", "Wrong Param of http_Get.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  // 检查TCP连接. 若无连接则尝试连接.
  if ( esp8266_tcp_getState()->is_Connected == false || esp8266_tcp_getState()->state != ESP_TCP_STATE_CONNECTED )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("TCP Connect not detected. http_Get() called failed.\n");
    #endif          

    LOG_WRITE(LOG_WARNING, "HTTP", "TCP Con not detected.http_Get() call fail.");

    esp_tcp_err_t err = esp8266_tcp_Connect(__phttp->host, 80, TCP );
    if ( err != ESP_TCP_OK )
    {
      LOG_WRITE(LOG_ERROR, "HTTP", "auto-connect failed: %d", (int)err);
      return ESP_HTTP_ERR_OFFLINE;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  } 

  if ( strlen(__phttp->host) == 0 || strlen(__phttp->path) == 0 )
  {
    // 传入的__phttp不合法.（未正确初始化）.
    #if defined(__DEBUG_LEVEL_1__)
      printf("esp_http_t *__phttp not valid.\n");
    #endif        

    LOG_WRITE(LOG_WARNING, "HTTP", "esp_http_t *__phttp not valid.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  static char req_buf[HTTP_REQ_BUF_MAX_LEN]; memset(req_buf, 0, sizeof(req_buf));
  esp_http_err_t req_err = http_RequestBuild(__phttp, req_buf, sizeof(req_buf));
  if ( req_err != ESP_HTTP_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Request build error in http_Get().\n");
    #endif            

    LOG_WRITE(LOG_ERROR, "HTTP", "Request build error in http_Get().");
    return ESP_HTTP_ERR_BUILD_REQ;
  }


  // esp8266_tcp_Send（）里已经包含了esp8266_WaitResponse("+IPD")，上层无需继续等待！否则将收不到数据.
  // 注意！调用tcp_send之后并不会调用esp8266_DropLastFrame()，请在解析完数据后于上层手动调用esp8266_DropLastFrame()释放状态！
  esp_tcp_err_t send_err = esp8266_tcp_Send((const uint8_t *)req_buf, __phttp->total_len);
  if ( send_err != ESP_TCP_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("TcpSend error in http_Get().\n");
    #endif              

    LOG_WRITE(LOG_ERROR, "HTTP", "TcpSend error in http_Get().");
    return ESP_HTTP_ERR_SEND_WAIT_FAIL;
  }

  const uint8_t *pRet = (const uint8_t *)memmem(hesp8266.LastReceivedFrame.RecvData, hesp8266.LastReceivedFrame.Data_Len, "+IPD,", strlen("+IPD,"));

  const uint8_t *recv_data = pRet;

  if ( !http_extract_json_body(recv_data, out_json_body, out_json_body_buf_size) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Failed to extract JSON body.\n");
    #endif        

    esp8266_DropLastFrame();
    LOG_WRITE(LOG_ERROR, "HTTP", "Failed to extract JSON body.");
    return ESP_HTTP_ERR_EXTRACT;
  }

  esp8266_DropLastFrame();

  return ESP_HTTP_OK;
}




/**
 * @brief 从 ESP8266 AT 模块返回的完整 HTTP 响应中，安全提取 JSON body 字符串
 * 
 * 该函数专为 AT 指令模式（+IPD）设计，自动跳过：
 *   - "+IPD,<len>:" 前缀（AT 固件封装格式）
 *   - HTTP 响应头（Headers），以首个 "\r\n\r\n" 为分隔标志
 * 
 * 提取结果为纯 JSON 字符串（不含任何 HTTP 头部、状态行或控制字符），
 * 并保证输出缓冲区以 '\0' 结尾，长度严格受控，**零内存越界风险**。
 * 
 * @param http_response  指向完整响应数据的 const char*（如 "+IPD,237:HTTP/1.1 200 OK\r\nContent-Type:...\r\n\r\n{...}"）
 * @param out_json       输出缓冲区指针（必须非 NULL，且空间足够容纳 JSON + '\0'）
 * @param out_json_size  out_json 缓冲区总字节数（含 '\0' 占位，最小建议 256）
 * 
 * @return true  成功提取有效 JSON body（out_json 已写入且以 '\0' 结尾）
 * @return false 失败：参数非法 / 未找到 "\r\n\r\n" / JSON body 为空 / 缓冲区不足
 * 
 * @note
 *   - 本函数不依赖 heap，不调用 malloc/free，完全线程安全；
 *   - 对中文 UTF-8 字符（如 "北京"、"晴天"）完全透明，不做任何编码转换；
 *   - 若响应中存在多个 "\r\n\r\n"，仅取第一个（符合 HTTP 规范）；
 *   - 建议 out_json_size ≥ 512，以兼容天气 API 典型响应（通常 200~400 字节）。
 */
bool http_extract_json_body( const char* http_response, char *out_json, uint16_t out_json_size )
{
  if ( !http_response || !out_json || out_json_size == 0 )  return false;

  const char *body_start = http_response;

  // 跳过 "+IPD,x:" 前缀.
  if ( strncmp(body_start, "+IPD,", 5) == 0 )
  {
    const char *p = body_start + 5;
    while( *p && isdigit((unsigned char)*p) )   p++;
    body_start = p;
    if ( *p == ':' )  body_start = p + 1;
  }

  // 查找 \r\n\r\n(Header结束标志).
  const char *found = strstr(body_start, "\r\n\r\n");
  if ( !found ) return false;

  const char *json_start = found + 4;
  size_t json_len = strlen(json_start);

  if ( json_len >= out_json_size - 1 )
  {
    json_len = out_json_size - 2;
  }

  memcpy(out_json, json_start, json_len);
  out_json[json_len] = '\0';

  return true;
}




// esp_http_err_t http_json_getCity( const char *json_body, char *out_city, uint16_t out_city_buf_len )
// {
//   if ( !json_body || !out_city || out_city_buf_len == 0 )
//   {
//     #if defined(__DEBUG_LEVEL_1__)
//       printf("Wrong param of http_json_getCity.\n");
//     #endif 

//     LOG_WRITE(LOG_WARNING, "HTTP", "Wrong param of http_json_getCity.");
//     return ESP_HTTP_ERR_INVALID_ARGS;
//   } 

//   const char *p_temp = json_body;
//   const char *key[] = { "\"location\":", "\"city\":", "\"Location\":", "\"City\":", "\"name\":", "\"Name\":", "\"place\":", "\"Place\":"};

//   for ( uint8_t i = 0; i < sizeof(key)/sizeof(key[0]); i++ )
//   {
//     p_temp = json_body;

//     p_temp = strstr(json_body, key[i]);
//     if ( p_temp )
//     {
//       if ( strcmp("\"location\":", key[i]) == 0 || strcmp("\"Location\":", key[i]) == 0 )
//       {
//         p_temp += strlen(key[i]);

//         // {"results":[{"location":{"name":"北京"}}]} 防止此类情形下提取错误位置.再遍历一遍.
//         for ( uint8_t j = 0; j < sizeof(key)/sizeof(key[0]); j++ )
//         {
//           p_temp = strstr(p_temp, key[j]);

//           if ( p_temp )
//           {
//             // 找到真正位置.
//             p_temp += strlen(key[j]);
//             goto AHEAD;
//           }
//         }
//         // 二次遍历未找到相关匹配key. 说明 location/Location 后即为位置字符串.往下继续运行即可.
//         goto AHEAD;
//       }

//       p_temp += strlen(key[i]);
// AHEAD:
//       p_temp = skip_ws_and_struct(p_temp);

//       if ( !p_temp )  continue;

//       if ( json_util_extractQuotes(p_temp, out_city, out_city_buf_len) )
//       {
//         // 成功提取出位置字符串.
//         uint16_t len = strlen(out_city);

//         if ( len == 0 ) break;

//         // 去掉可能的右空格.
//         while( len > 0 && isspace((unsigned char)out_city[len - 1]) )
//         {
//           out_city[--len] = '\0';
//         }

//         uint16_t left_skip = 0;

//         // 去掉可能的左空格.
//         while( len > 0 && isspace((unsigned char)out_city[left_skip]) )
//         {
//           left_skip++;
//         }

//         // 内存剪切.
//         if ( left_skip > 0 )
//         {
//           memmove(out_city, out_city + left_skip, len - left_skip + 1 );
//         }

//         return ESP_HTTP_OK;
//       }
//       else 
//       {
//         #if defined(__DEBUG_LEVEL_1__)
//           printf("json_util_extractQuotes called fail in http_json_getCity.\n");
//         #endif 

//         break;
//       }
//     }
//   }

//   // 整个缓冲区已经遍历完毕. 仍未找到相关key.
//   #if defined(__DEBUG_LEVEL_1__)
//     printf("Something error happened in http_json_getCity.\n");
//   #endif 

//   LOG_WRITE(LOG_ERROR, "HTTP", "Something err happen in http_json_getCity.");
//   return ESP_HTTP_ERR_UNKNOWN;
// }




esp_http_err_t http_json_getString( const char *json_main, const char **key_paths, uint8_t path_cnt, char *out_buf, uint16_t out_size )
{
  if ( !json_main || !key_paths || path_cnt == 0 || !out_buf || out_size == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong param of http_json_getString.\n");
    #endif     

    LOG_WRITE(LOG_WARNING, "HTTP", "Wrong param of http_json_getString.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  const char *p = json_main;
  uint16_t count = 0;

  for ( uint8_t j = 0; j < path_cnt; j++ )
  {
    if ( key_paths[j] == NULL )   continue;

    p = strstr(json_main, key_paths[j]);
    const char *q = p; 
    count = 0;

    if ( p && p > json_main )
    {
      // 判断是否是字段名.若是字段名则进行提取后面紧跟的一个"的字段.
      if ( *(p - 1) == '"' && *( p + strlen(key_paths[j]) ) == '"' && *( p + strlen(key_paths[j]) + 1 ) == ':' )
      {
        p += strlen(key_paths[j]) + 1;

        p = skip_ws_and_struct(p);

        if ( *p != '"' )  continue;    // 查找到的字段值并非string类型. 

        p++;    // 跳过第一个".
        q = p;
        while( *p != '"' )
        {
          count++;
          p++;
          if ( count > out_size - 2 )   break;  // 缓冲区不够. 退回.
        }

        memcpy(out_buf, q, count);
        out_buf[count] = '\0';

        // 去掉字段值中可能带有的右空格.
        while( count > 0 && isspace((unsigned char)out_buf[count - 1]) )
        {
          out_buf[--count] = '\0';
        }

        // 去掉字段值中可能带有的左空格.
        uint16_t left_skip = 0;
        while( count > 0 && isspace((unsigned char)out_buf[left_skip]) )
        {
          left_skip++;
        }

        if ( left_skip > 0 )
        {
          memmove(out_buf, out_buf + left_skip, count - left_skip + 1 );
        }

        return ESP_HTTP_OK;
      }
      else 
      {
        // 不是字段名. 继续匹配其它key.
        continue;
      }
    }
    else continue;
  }

  // 整个缓冲区已经遍历完毕. 仍未找到相关key.
  #if defined(__DEBUG_LEVEL_1__)
    printf("Key not found in http_json_getString.\n");
  #endif   

  LOG_WRITE(LOG_ERROR, "HTTP", "Key not found in http_json_getString.");
  return ESP_HTTP_ERR_UNKNOWN;
}




esp_http_err_t http_json_getNum( const char *json_main, const char **key_paths, uint8_t path_cnt, double *out_num )
{
  if ( !json_main || !key_paths || path_cnt == 0 || !out_num )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong param of http_json_getNum.\n");
    #endif       

    LOG_WRITE(LOG_WARNING, "HTTP", "Wrong param of http_json_getNum.");
    return ESP_HTTP_ERR_INVALID_ARGS;
  } 

  const char *p = json_main;
  const char *q = p;
  uint16_t count = 0;
  char temp[64];

  for ( uint8_t j = 0; j < path_cnt; j++ )
  {
    if ( key_paths[j] == NULL )  continue;

    count = 0;

    p = strstr(json_main, key_paths[j]);

    if ( !p )  continue;

    if ( p && p > json_main )
    {
      // 判断是否为字段名.
      if ( *(p - 1) == '"' && *( p + strlen(key_paths[j]) - 1 ) == '"' && *(p + strlen(key_paths[j])) == ':' )
      {
        p += strlen(key_paths[j]);

        p = skip_ws_and_struct(p);

        // 先将数据存入到字符串中，再从字符串中转化为浮点数.
        q = p;
        while( *p != ',' && *p != '}' && *p != ']' && *p != '\0' )
        {
          if ( isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == ' ' || *p == 'E' || (*p == '+' && p == q) || (*p == '-' && p == q) )
          {
            temp[count++] = *p++;
          }
          else  break;

          if ( count > sizeof(temp) - 2 ); break;
        }
        temp[count] = '\0';
        if ( count == 0 ) continue;

        // 去掉字段值中可能带有的右空格.
        while( count > 0 && isspace((unsigned char)temp[count - 1]) )
        {
          temp[--count] = '\0';
        }

        // 去掉字段值中可能带有的左空格.
        uint16_t left_skip = 0;
        while( count > 0 && isspace((unsigned char)temp[left_skip]) )
        {
          left_skip++;
        }

        if ( left_skip > 0 )
        {
          memmove(temp, temp + left_skip, count - left_skip + 1 );
        }        

        char *endptr;
        double val = strtod(temp, &endptr);
        if ( *endptr != '\0' || endptr == temp )  continue; // 转换失败.

        *out_num = val;

        return ESP_HTTP_OK;
      }
    }
  }

  // 整个缓冲区已经遍历完毕. 仍未找到相关key.
  #if defined(__DEBUG_LEVEL_1__)
    printf("Key not found in http_json_getNum.\n");
  #endif     

  LOG_WRITE(LOG_ERROR, "HTTP", "Key not found in http_json_getNum.");
  return ESP_HTTP_ERR_UNKNOWN;
}
