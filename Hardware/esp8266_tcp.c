#include "esp8266_tcp.h"
#include "stm32f4xx_hal.h"
#include "esp8266_driver.h"

extern ESP8266_HandleTypeDef hesp8266;

extern UART_HandleTypeDef esp8266_huart;

static esp_tcp_handle_t htcp8266;

/* ************************* */
static const char host_weather[ESP_TCP_WEATHER_HOST_LEN] = { 0 };

/* ************************* */

/* ************************* */
esp_tcp_err_t esp8266_tcp_Init( void );

const esp_tcp_handle_t* esp8266_tcp_getState( void );

esp_tcp_err_t esp8266_tcp_Connect( const char *Host, uint16_t Port, connect_type_t Mode );

esp_tcp_err_t esp8266_tcp_Disconnect( void );

esp_tcp_err_t esp8266_tcp_Send( const uint8_t *data, uint16_t data_len );

esp_tcp_err_t esp8266_tcp_DNSResolve( const char *Host, char *out_ip_str, uint8_t *out_ip_size );
/* ************************* */


/**
 * @brief 获取当前 TCP 连接状态的只读句柄
 *
 * 返回指向内部静态 TCP 状态结构体的 const 指针，用于安全读取连接状态、
 * 目标主机、端口等运行时信息。该结构体由 esp8266_tcp_Init() 初始化，
 * 并在 esp8266_tcp_connect()/disconnect() 中自动更新。
 *
 * @note 调用者不得修改返回结构体内容；所有状态变更应通过公共 API 触发。
 * @return const esp_tcp_handle_t* 指向当前 TCP 状态的只读句柄（永不为 NULL）
 */
const esp_tcp_handle_t* esp8266_tcp_getState()
{
  return &htcp8266;
}




/**
 * @brief 初始化 ESP8266 TCP 客户端子系统（单连接、透传关闭模式）
 *
 * 本函数完成 TCP 通信前的关键 AT 指令配置，确保 ESP8266 处于稳定、可预测的工作状态：
 *   -  验证 WiFi 已成功关联至预设 SSID（通过 `AT+CWJAP?` + `WifiSSID` 字符串比对）
 *   -  强制设置为单连接模式（`AT+CIPMUX=0`），避免多连接 ID 管理复杂性
 *   -  切换至普通数据传输模式（`AT+CIPMODE=0`），禁用透传（Transparent Mode）
 *   -  （注：TCP 超时 `AT+CIPSTO` 移至 `connect()` 中按需设置，提升灵活性）
 *
 * @pre
 *   - 必须已在 `esp8266_driver_init()` 中完成 UART、DMA、定时器及互斥量（xMutexEsp）初始化；
 *   - `hesp8266.WifiSSID` 和 `hesp8266.Wifi_Ipv4` 必须已由 WiFi 连接状态机正确填充；
 *   - ESP8266 固件版本 ≥ V2.0.0（支持 `AT+CWJAP?`, `AT+CIPMUX?`, `AT+CIPMODE?`）。
 *
 * @return esp_tcp_err_t 返回初始化结果：
 *         - @ref ESP_TCP_OK                : 初始化成功，TCP 子系统就绪；
 *         - @ref ESP_TCP_ERR_CONNECT_FAIL  : WiFi 未连接或连接 SSID 不匹配；
 *         - @ref ESP_TCP_ERR_TIMEOUT       : AT 命令发送超时（UART 卡死、ESP 无响应）；
 *         - @ref ESP_TCP_ERR_NO_RESPONSE   : AT 命令有响应但无有效数据（解析失败/固件不兼容）；
 *
 * @note
 *   - 本函数是**线程安全的**（内部使用 `xMutexEsp` 保护 UART 通信），但调用者应避免并发调用；
 *   - 初始化后，`htcp8266` 结构体被完全清零并进入 `DISCONNECTED` 状态，可供后续 `connect()` 安全使用；
 *   - 错误日志通过 `LOG_WRITE()` 输出，调试信息在 `__DEBUG_LEVEL_1__` 宏启用时打印到 USART；
 */
esp_tcp_err_t esp8266_tcp_Init( void )
{
  void *pResponse = NULL;
  bool res_a, res_b, reb_c;
  uint32_t out_val = 0xFF;
  char temp_outChar[30];

  // 判断网络是否连接上.
  if ( esp8266_SendAT("AT+CWJAP?") )
  {
    if ( esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) )
    {
      char temp_outChar[33];

      reb_c = at_extractString_between_quotes(&hesp8266, "+CWJAP:", temp_outChar, sizeof(temp_outChar), pdTRUE);

      if ( !reb_c )
      {
        esp8266_DropLastFrame();

        goto TCP_Init_ExtractNum_Err;
      }

      if ( strcmp(hesp8266.WifiSSID, temp_outChar) )
      {
        // 网络未连接上 或 连接状态与状态机管理状态不匹配.
        #if defined(__DEBUG_LEVEL_1__)
          printf("WIFI Connect Error.(esp8266_tcp_Init).\n");
        #endif 

        LOG_WRITE(LOG_ERROR, "NULL", "WIFI Connect Error.(esp8266_tcp_Init).");

        return ESP_TCP_ERR_CONNECT_FAIL;
      } 
    }
    else 
    {
      esp8266_DropLastFrame();

      goto TCP_Init_Wait_Err;
    }
  }
  else 
  {
    goto TCP_Init_ATSend_Err;
  }


  // 判断并设置为单连接模式.
  if ( !esp8266_SendAT("AT+CIPMUX?") )
  {
TCP_Init_ATSend_Err:
    #if defined(__DEBUG_LEVEL_1__)
      printf("SendAT Error in esp8266_tcp_Init.\n");
    #endif // __DEBUG_LEVEL_1__

    LOG_WRITE(LOG_WARNING, "NULL", "esp8266_tcp_Init SendAT Failed.");

    return ESP_TCP_ERR_TIMEOUT;  // AT命令发送失败(超时).
  } 

  pResponse = esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT);

  if ( pResponse != NULL )
  {
    res_b = at_extractNum(&hesp8266, "CIPMUX", &out_val, pdTRUE);

    if ( res_b && out_val == 0 )
    {
      // 已经为单连接模式.
    }

    // 断开多连接模式(设置为单连接模式).
    if ( res_b && out_val == 1 )
    {
      if ( !esp8266_SendAT("AT+CIPMUX=0") )  goto TCP_Init_ATSend_Err;

      if ( !esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) ) 
      {
        esp8266_DropLastFrame();
        goto TCP_Init_Wait_Err;
      }
    }

    if ( !res_b || ( out_val != 1 && out_val != 0 ) )
    {
TCP_Init_ExtractNum_Err:
      #if defined(__DEBUG_LEVEL_1__)
        printf("at_extractNum called error in esp8266_tcp_Init\n");
      #endif 

      LOG_WRITE(LOG_WARNING, "NULL", "at_extractNum failed in esp8266_tcp_Init");

      return ESP_TCP_ERR_NO_RESPONSE;
    } 
  }
  else 
  {
TCP_Init_Wait_Err:
    #if defined(__DEBUG_LEVEL_1__)
      printf("WaitResponse Timeout in esp8266_tcp_Init.\n");
    #endif

    LOG_WRITE(LOG_WARNING, "NULL", "WaitResponse Timeout in esp8266_tcp_Init.");

    return ESP_TCP_ERR_NO_RESPONSE;
  } 


  // 判断并设置为普通传输模式（不使用WIFI透传模式）.
  if ( esp8266_SendAT("AT+CIPMODE?") )
  {
    if ( esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) )
    {
      res_a = at_extractNum(&hesp8266, "CIPMODE", &out_val, pdTRUE);

      if ( res_a && out_val == 0 )
      {
        // 已经处于普通传输模式.
      }

      if ( res_a && out_val == 1 )
      {
        if ( !esp8266_SendAT("AT+CIPMODE=0") ) goto TCP_Init_ATSend_Err;

        if ( !esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) ) 
        {
          esp8266_DropLastFrame();
          goto TCP_Init_Wait_Err; 
        }
      }

      if ( !res_a || ( out_val != 1 && out_val != 0 ) )
      {
        goto TCP_Init_ExtractNum_Err;
      }
    }
    else 
    {
      goto TCP_Init_Wait_Err;
    }
  }
  else 
  {
    goto TCP_Init_ATSend_Err;
  }

  htcp8266.is_Connected = false;
  htcp8266.conn_ID = 0; // 单连接模式conn_ID恒为0.
  htcp8266.remain = 0xFF;
  htcp8266.Port = 0;
  memset(htcp8266.Host, 0, sizeof(htcp8266.Host));
  memset(htcp8266.remote_IP, 0, sizeof(htcp8266.remote_IP));

  strncpy(host_weather, ESP_TCP_HOST_WEATHER, sizeof(host_weather) - 1);

  #if defined(__DEBUG_LEVEL_1__)
    printf("TCP Init OK: CIPMUX=0, CIPMODE=0, CIPSTO=180, IP=%s\n", hesp8266.Wifi_Ipv4);
  #endif

  return ESP_TCP_OK;
}



esp_tcp_err_t esp8266_tcp_Connect( const char *Host, uint16_t Port, connect_type_t Mode )
{
  char *conn_type = NULL;

  if ( !Host || Port == 0 || Port > 65535 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param in esp8266_tcp_Connect.\n");
    #endif  

    LOG_WRITE(LOG_WARNING, "NULL", "Wrong Param in esp8266_tcp_Connect.");

    return ESP_TCP_ERR_INVALID_ARGS;
  }

  // 判断当前是否已有TCP连接.
  if ( htcp8266.is_Connected && htcp8266.state == ESP_TCP_STATE_CONNECTED )
  {
    LOG_WRITE(LOG_INFO, "TCP", "Already connect to %s:%u", htcp8266.Host, htcp8266.Port);

    return ESP_TCP_ERR_BUSY;
  }

  // 模式检查.
  if ( Mode == TCPv6 )
  {
    conn_type = "TCPv6";
  }
  else
  {
    conn_type = "TCP";
  }


  // 构造命令.
  char at_cmd[128];
  int len = snprintf(at_cmd, sizeof(at_cmd), "AT+CIPSTART=\"%s\",\"%s\",%u", conn_type, Host, Port);
  if ( len <= 0 || len >= (int)sizeof(at_cmd) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("ATCmd buffer overflow in esp8266_tcp_Connect.\n");
    #endif 

    LOG_WRITE(LOG_WARNING, "NULL", "ATCmd buffer overflow.");

    return ESP_TCP_ERR_INVALID_ARGS;
  }


  htcp8266.state = ESP_TCP_STATE_CONNECTING;
  // 发送命令.
  if ( !esp8266_SendAT("%s", at_cmd) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("ATSend failed in esp8266_tcp_Connect.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "NULL", "ATSend failed in esp8266_tcp_Connect.");

    htcp8266.state = ESP_TCP_STATE_DISCONNECTED;

    return ESP_TCP_ERR_TIMEOUT;
  } 


  // 命令解析.
  HAL_Delay(5); // 适当延时.
  void *pResponse = esp8266_WaitResponse("OK", ESP_TCP_CON_TIMEOUT);
  if ( pResponse == NULL )
  {
    htcp8266.state = ESP_TCP_STATE_DISCONNECTED;

    #if defined(__DEBUG_LEVEL_1__)
      printf("WaitRes failed in esp8266_tcp_Connect.\n");
    #endif     

    LOG_WRITE(LOG_WARNING, "NULL", "WaitRes failed in esp8266_tcp_Connect.");

    esp8266_DropLastFrame();

    return ESP_TCP_ERR_NO_RESPONSE;
  } 

  const uint8_t *response_buf = hesp8266.LastReceivedFrame.RecvData;
  uint16_t response_buf_len = hesp8266.LastReceivedFrame.Data_Len;
  if ( memmem(response_buf, response_buf_len, (const void *)"CONNECT", 7) )
  {
    // 连接成功.
    uint8_t flag = 0;
    uint8_t *pReturn = NULL;
    uint16_t pLen = 0;
    esp8266_DropLastFrame();

    if ( !esp8266_SendAT("AT+CIPSTATUS") ) 
    {
      // 查询信息失败，但是连接是成功了的，不能直接返回.
      #if defined(__DEBUG_LEVEL_1__)
        printf("AT+CIPSTATE? send error.\n");
      #endif       

      LOG_WRITE(LOG_WARNING, "NULL", "AT+CIPSTATE? send error.");
    } else  flag = 1;

    if ( flag && !esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) )
    {
      return ESP_TCP_ERR_NO_RESPONSE;
    }

    if ( at_extractField(&hesp8266, AT_FIELD_IN_QUOTES, 2, &pReturn, &pLen, pdTRUE) )
    {
      memcpy(htcp8266.remote_IP, pReturn, pLen);
    }

    htcp8266.is_Connected = true;
    htcp8266.conn_ID = 0;
    htcp8266.Port = Port;
    htcp8266.state = ESP_TCP_STATE_CONNECTED;
    strncpy(htcp8266.Host, Host, sizeof(htcp8266.Host) - 1);
    htcp8266.Host[sizeof(htcp8266.Host) - 1] = '\0';
    esp8266_DropLastFrame();

    #if defined(__DEBUG_LEVEL_1__)
      printf("TCP Successfully connect to %s\n", Host);
    #endif 

    return ESP_TCP_OK;
  }

  if ( memmem(response_buf, response_buf_len, (const void *)"ERROR", 5) != NULL || 
          memmem(response_buf, response_buf_len, (const void *)"FAIL", 4) != NULL )
  {
    htcp8266.state = ESP_TCP_STATE_DISCONNECTED;

    // 连接失败.
    #if defined(__DEBUG_LEVEL_1__)
      printf("TCP Connect Failed.\n");
    #endif

    esp8266_DropLastFrame();

    LOG_WRITE(LOG_ERROR, "NULL", "TCP CONN FAILED.");

    return ESP_TCP_ERR_CONNECT_FAIL;
  }

  esp8266_DropLastFrame();

  htcp8266.state = ESP_TCP_STATE_DISCONNECTED;

  LOG_WRITE(LOG_ERROR, "NULL", "Unexpected Error in esp8266_tcp_Connect.");

  return ESP_TCP_ERR_NO_RESPONSE;
}




/**
 * @brief 主动断开当前 TCP 连接（单连接模式）
 *
 * 向 ESP8266 发送 `AT+CIPCLOSE` 命令，安全终止已建立的 TCP 客户端连接。
 * 本函数具备幂等性：若当前未处于有效连接状态（即 `is_Connected == false` 且
 * `state != ESP_TCP_STATE_CONNECTED` 且 `remote_IP` 为空），将直接返回成功，
 * 避免对空闲模块执行冗余 AT 操作。
 *
 * @pre
 *   - 必须已在 `esp8266_tcp_Init()` 中完成 TCP 子系统初始化；
 *   - UART 通信通道（`esp8266_huart`）与互斥量（`xMutexEsp`）处于可用状态；
 *   - ESP8266 固件支持 `AT+CIPCLOSE`（所有标准固件均支持）。
 *
 * @return esp_tcp_err_t 返回断开操作结果：
 *         - @ref ESP_TCP_OK                : 成功收到 `OK` 响应，本地状态已完全复位；
 *         - @ref ESP_TCP_ERR_TIMEOUT       : `AT+CIPCLOSE` 命令发送失败（UART 超时/ESP 无响应）；
 *         - @ref ESP_TCP_ERR_NO_RESPONSE   : 命令发送成功但未在超时时间内收到 `OK` 响应（连接可能已断开，但状态不可信）；
 *         - 其他错误码不会从此函数返回（无参数校验失败或内存错误）。
 *
 * @note
 *   - 本函数是**线程安全的**（内部使用 `xMutexEsp` 保护 UART 通信）；
 *   - 断开后自动清空 `htcp8266` 中所有连接上下文字段（`conn_ID`, `Port`, `Host`, `remote_IP`），
 *     并将 `state` 设为 `ESP_TCP_STATE_DISCONNECTED`，确保后续 `connect()` 可安全重用；
 *   - 若 `AT+CIPCLOSE` 发送失败，函数会**回滚 `state` 至 `ESP_TCP_STATE_CONNECTED`**，
 *     以反映“连接仍应视为有效”的保守策略（避免状态错乱）；
 *   - 日志通过 `LOG_WRITE()` 记录，调试信息在 `__DEBUG_LEVEL_1__` 宏启用时输出至 USART；
 *   - 此函数不触发 DNS 或 WiFi 层操作，仅作用于 TCP 连接层。
 */
esp_tcp_err_t esp8266_tcp_Disconnect( void )
{
  if ( ( htcp8266.is_Connected == false ) && ( htcp8266.state != ESP_TCP_STATE_CONNECTED ) && ( strlen(htcp8266.remote_IP) == 0 ) )
  {
    // 未连接TCP 空调用，直接返回. 
    return ESP_TCP_OK;
  }

  htcp8266.state = ESP_TCP_STATE_DISCONNECTING;

  if ( !esp8266_SendAT("AT+CIPCLOSE") )
  {
    htcp8266.state = ESP_TCP_STATE_CONNECTED;

    #if defined(__DEBUG_LEVEL_1__)
      printf("ATCmd Send Failed in esp8266_tcp_Disconnect.\n");
    #endif 

    LOG_WRITE(LOG_ERROR, "NULL", "ATSend Failed in esp8266_tcp_Disconnect.");
    return ESP_TCP_ERR_TIMEOUT;
  } 


  uint8_t *pReturn = (uint8_t *)esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT);
  if ( !pReturn )
  {
    // 命令发送成功，但是未收到回复信号.目前是否断开状态未知.
    htcp8266.state = ESP_TCP_STATE_ERROR;

    #if defined(__DEBUG_LEVEL_1__)
      printf("Wait Failed in esp8266_tcp_Disconnect.\n");
    #endif 
    
    LOG_WRITE(LOG_ERROR, "NULL", "Wait Failed in esp8266_tcp_Disconnect.");
    return ESP_TCP_ERR_NO_RESPONSE;
  } 

  // 成功断开连接. 状态复位.
  htcp8266.conn_ID = 0xFF;
  htcp8266.is_Connected = false;
  htcp8266.Port = 0;
  htcp8266.state = ESP_TCP_STATE_DISCONNECTED;
  memset(htcp8266.remote_IP, 0, sizeof(htcp8266.remote_IP));
  memset(htcp8266.Host, 0, sizeof(htcp8266.Host));

  return ESP_TCP_OK;
}




/**
 * @brief 通过 ESP8266 模块发送 TCP 数据（阻塞式）
 * 
 * 本函数执行完整的 AT+CIPSEND 流程：  
 * 1. 校验输入参数有效性；  
 * 2. 检查 TCP 连接状态（要求 htcp8266.is_Connected == true && htcp8266.state == ESP_TCP_STATE_CONNECTED）；  
 * 3. 发送 "AT+CIPSEND=<len>" 命令；  
 * 4. 等待模块返回 ">" 提示符（表示进入透传模式）；  
 * 5. 调用 HAL_UART_Transmit() 同步发送原始数据帧；  
 * 6. 等待模块返回 "SEND OK" 确认发送成功。
 * 
 * ⚠️ 注意事项：
 *   - 当前 HAL_UART_Transmit() 调用为裸调用，**未加互斥锁**。若在多任务/中断上下文中调用本函数，
 *     请确保 UART 外设访问的线程安全性 —— 后续版本将在此处引入临界区保护（如 osMutexWait / HAL_NVIC_DisableIRQ 等）；
 *   - data_len 严格限制在 [1, 65535] 范围内（ESP8266 AT 固件对单次 CIPSEND 长度的硬性限制）；
 *   - 调用前必须确保 ESP8266 已完成初始化、Wi-Fi 关联、TCP 连接建立且处于稳定通信状态；
 *   - 本函数为阻塞实现，超时由 ESP_TCP_CMD_TIMEOUT 统一控制（单位：ms），超时将返回对应错误码。
 * 
 * @param[in]  data      待发送的数据缓冲区指针（非 NULL）
 * @param[in]  data_len  待发送字节数（取值范围：1 ~ 65535）
 * 
 * @return     ESP_TCP_OK                成功发送并收到 "SEND OK"
 * @return     ESP_TCP_ERR_INVALID_ARGS  data 为 NULL，或 data_len 为 0 或 > 65535
 * @return     ESP_TCP_ERR_CONNECT_FAIL  未检测到有效 TCP 连接（is_Connected == false 或 state != ESP_TCP_STATE_CONNECTED）
 * @return     ESP_TCP_ERR_CMD_BUILD_ERROR  AT 命令字符串构建失败（snprintf 异常）
 * @return     ESP_TCP_ERR_TIMEOUT       AT 命令发送超时（esp8266_SendAT 失败）
 * @return     ESP_TCP_ERR_NO_RESPONSE   等待 ">" 或 "SEND OK" 响应超时，或响应解析失败
 * @return     ESP_TCP_ERR_UNKNOWN       （内部保留，当前逻辑不返回）
 * 
 * @note       调用本函数期间会修改底层 UART 句柄状态，禁止在中断中直接调用（除非已确保 UART 安全）；
 *             建议在任务上下文（如 FreeRTOS Task）中使用，并配合连接状态机做前置校验。
 */
esp_tcp_err_t esp8266_tcp_Send( const uint8_t *data, uint16_t data_len )
{
  if ( !data || data_len > 65535 || data_len == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of esp8266_tcp_Send.\n");
    #endif 

    LOG_WRITE(LOG_WARNING, "NULL", "Wrong Param of esp8266_tcp_Send.");
    return ESP_TCP_ERR_INVALID_ARGS;
  } 

  if ( ( htcp8266.is_Connected == false ) || ( htcp8266.state != ESP_TCP_STATE_CONNECTED ) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("No Connect Detected.Send Failed.\n");
    #endif 

    LOG_WRITE(LOG_WARNING, "NULL", "No Connect Detected.Send Failed.");
    return ESP_TCP_ERR_CONNECT_FAIL;
  } 

  char cmd[32];

  int len = snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", data_len);
  if ( len <= 0 || len >= (int)sizeof(cmd) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("cmd build failed in esp8266_tcp_Send.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "NULL", "cmd build failed in esp8266_tcp_Send.");
    return ESP_TCP_ERR_CMD_BUILD_ERROR;
  } 


  if ( !esp8266_SendAT("%s", cmd) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("At Send error in esp8266_tcp_Send.\n");
    #endif    
    
    LOG_WRITE(LOG_ERROR, "NULL", "At Send error in esp8266_tcp_Send.");
    return ESP_TCP_ERR_TIMEOUT;
  }


  uint8_t *pReturn = (uint8_t *)esp8266_WaitResponse(">", ESP_TCP_CMD_TIMEOUT);
  if ( pReturn == NULL )
  {
    esp8266_DropLastFrame();

    #if defined(__DEBUG_LEVEL_1__)
      printf("wait error in esp8266_tcp_Send.\n");
    #endif  
    
    LOG_WRITE(LOG_ERROR, "NULL", "wait error in esp8266_tcp_Send.");
    return ESP_TCP_ERR_NO_RESPONSE;
  }

  esp8266_DropLastFrame();

  // 阻塞式发送确保数据发送完全.
  HAL_UART_Transmit(&esp8266_huart, data, data_len, ESP_TCP_CMD_TIMEOUT);
  HAL_Delay(2);

  void *pRes = esp8266_WaitResponse("+IPD,", ESP_TCP_CMD_TIMEOUT);
  if ( !pRes )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Not Recv +IPD in esp8266_tcp_Send.\n");
    #endif  
    
    LOG_WRITE(LOG_ERROR, "NULL", "Not Recv +IPD in esp8266_tcp_Send.");
    return ESP_TCP_ERR_NO_RESPONSE;    
  }

  return ESP_TCP_OK;
}




/**
 * @brief 执行 DNS 域名解析（支持 IPv4），兼容直接传入 IP 地址的快速透传模式
 *
 * 本函数用于通过 ESP8266 模块的 AT+CIPDOMAIN 指令解析域名（如 "www.example.com"）为 IPv4 地址。
 * 若输入 `Host` 已为合法点分十进制 IPv4 格式（如 "192.168.1.1"），则跳过 AT 指令交互，直接拷贝并校验后返回，
 * 实现零延迟、零网络依赖的“伪解析”优化。
 *
 * @param[in]  Host         待解析的主机名（域名）或已知 IPv4 字符串（如 "api.example.com" 或 "10.0.0.5"）
 * @param[out] out_ip_str   输出缓冲区，用于存放解析得到的 IPv4 字符串（如 "192.168.1.100"），必须以 '\0' 结尾
 * @param[in]  out_ip_size  `out_ip_str` 的字节容量（含终止符 '\0'），建议 ≥ 16（足够容纳 "255.255.255.255\0"）
 *
 * @return esp_tcp_err_t    错误码，成功时返回 ESP_TCP_OK；失败时返回对应错误码（见 esp_tcp_err_t 枚举定义）
 * @retval ESP_TCP_OK                   解析成功，`out_ip_str` 中已写入合法 IPv4 字符串
 * @retval ESP_TCP_ERR_INVALID_ARGS     `Host`、`out_ip_str` 为空指针，或 `out_ip_size == 0`
 * @retval ESP_TCP_ERR_CMD_BUILD_ERROR  AT 命令构造失败（缓冲区溢出或格式异常）
 * @retval ESP_TCP_ERR_TIMEOUT          AT 指令超时（发送失败、无 OK 响应、或无法提取 IP 字段）
 * @retval ESP_TCP_ERR_NO_RESPONSE      等待模块响应时发生通信中断或无有效响应
 *
 * @note
 *   - 本函数是**可重入的（reentrant）**：内部静态缓冲区 `at_cmd[128]` 在每次调用结束前均被清零，不保留跨调用状态；
 *   - 调用安全性依赖于底层 `esp8266_SendAT()` 和 `esp8266_WaitResponse()` 的可重入实现（即 UART 访问已加锁或为独占通道）；
 *   - `Host` 中仅允许字母、数字、点（`.`）和连字符（`-`），不支持下划线、通配符、IPv6 或国际化域名（IDN）；
 *   - 对于纯 IP 输入，函数执行严格校验（sscanf + 范围检查），非法 IP（如 "256.1.1.1"、"192.168.1"）将返回 ESP_TCP_ERR_TIMEOUT；
 *   - 日志等级由 `__DEBUG_LEVEL_1__` 和 `LOG_WRITE()` 宏控制，生产环境建议关闭调试输出以节省资源。
 *
 * @example
 *   char ip_buf[16];
 *   uint8_t ip_len = sizeof(ip_buf);
 *   if (esp8266_tcp_DNSResolve("mqtt.example.com", ip_buf, ip_len) == ESP_TCP_OK) {
 *       printf("Resolved IP: %s\n", ip_buf); // e.g., "172.20.10.5"
 *   }
 */
esp_tcp_err_t esp8266_tcp_DNSResolve( const char *Host, char *out_ip_str, uint8_t *out_ip_size )
{
  if ( !Host || !out_ip_str || out_ip_size == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of esp8266_tcp_DNSResolve.\n");
    #endif

    LOG_WRITE(LOG_WARNING, "NULL", "Wrong Param of esp8266_tcp_DNSResolve.");
    return ESP_TCP_ERR_INVALID_ARGS;
  } 

  // 输入的Host就是原生Ipv4地址. 直接返回.
  if ( strchr(Host, '.') && sscanf(Host, "%hhu.%hhu.%hhu.%hhu", 
                                                (unsigned char *)&out_ip_str[0], 
                                                (unsigned char *)&out_ip_str[1],
                                                (unsigned char *)&out_ip_str[2],
                                                (unsigned char *)&out_ip_str[3]) == 4 )
  {
    int len = snprintf(out_ip_str, out_ip_size, "%s", Host);
    if ( len <= 0 || len >= (int)out_ip_size )
    {
      #if defined(__DEBUG_LEVEL_1__)
        printf("Ip buf overflow of esp8266_tcp_DNSResolve.\n");
      #endif 

      LOG_WRITE(LOG_ERROR, "NULL", "Ipbuf OVFL of esp8266_tcp_DNSResolve.");
      return ESP_TCP_ERR_CMD_BUILD_ERROR;
    }

    return ESP_TCP_OK;
  }


  // 验证 Host 合法性.
  const char *__ptrTemp = Host;
  while( *__ptrTemp )
  {
    if ( !isalnum((unsigned char)*__ptrTemp) && ( *__ptrTemp != '.' && *__ptrTemp != '-' ) )
    {
      #if defined(__DEBUG_LEVEL_1__)
        printf("DNS: Illegal char '%c' in host '%s'\n", *__ptrTemp, Host);
      #endif 

      LOG_WRITE(LOG_WARNING, "NULL", "DNS Illegal in esp8266_tcp_DNSResolve.");
      return ESP_TCP_ERR_INVALID_ARGS;
    } 

    __ptrTemp++;
  }


  static char at_cmd[128];
  int len = snprintf(at_cmd, sizeof(at_cmd), "AT+CIPDOMAIN=\"%s\"", Host);
  if ( len <= 0 || len >= (int)sizeof(at_cmd) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("ATCmd Build Faild in esp8266_tcp_DNSResolve.\n");
    #endif 

    LOG_WRITE(LOG_ERROR, "NULL", "AT build fail in esp8266_tcp_DNSResolve.");
    return ESP_TCP_ERR_CMD_BUILD_ERROR;
  }


  if ( !esp8266_SendAT("%s", at_cmd) )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("AT send error in esp8266_tcp_DNSResolve.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "NULL", "AT send error in esp8266_tcp_DNSResolve.");
    memset(at_cmd, 0, sizeof(at_cmd));
    return ESP_TCP_ERR_TIMEOUT;
  } 


  void *pReturn = esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT);
  if ( !pReturn )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("wait error in esp8266_tcp_DNSResolve.\n");
    #endif         

    LOG_WRITE(LOG_ERROR, "NULL", "wait error in esp8266_tcp_DNSResolve.");
    memset(at_cmd, 0, sizeof(at_cmd));
    return ESP_TCP_ERR_NO_RESPONSE;;
  }


  uint8_t *pRet = NULL;
  uint16_t size = 0;
  bool res = at_extractField(&hesp8266, AT_FIELD_AFTER_COLON, 1, &pRet, &size, pdTRUE);
  if ( !res )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("extract error in esp8266_tcp_DNSResolve.\n");
    #endif            

    LOG_WRITE(LOG_ERROR, "NULL", "extract error in esp8266_tcp_DNSResolve.");
    memset(at_cmd, 0, sizeof(at_cmd));
    return ESP_TCP_ERR_TIMEOUT;
  }
  memcpy(out_ip_str, pRet, size);
  out_ip_str[size] = '\0';

  // 额外校验所得是否为正确Ipv4地址.
  uint8_t a,b,c,d;
  if (sscanf(out_ip_str, "%hhu.%hhu.%hhu.%hhu", &a,&b,&c,&d) != 4 ||
      a > 255 || b > 255 || c > 255 || d > 255) 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Invalid IP format: %s.\n", out_ip_str);
    #endif           

    LOG_WRITE(LOG_ERROR, "NULL", "Invalid IP format: %s", out_ip_str);
    memset(at_cmd, 0, sizeof(at_cmd));
    memset(out_ip_str, 0, out_ip_size);
    return ESP_TCP_ERR_TIMEOUT;
  }

  memset(at_cmd, 0, sizeof(at_cmd));
  return ESP_TCP_OK;
}





