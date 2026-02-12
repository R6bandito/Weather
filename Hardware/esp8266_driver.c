#include "esp8266_driver.h"
#include "esp8266_tcp.h"
#include "esp_http.h"


/* Global Ver.*/
/* ********************************************** */
ESP8266_HandleTypeDef hesp8266 = { 0 };

UART_HandleTypeDef esp8266_huart;

QueueHandle_t xRecvDataQueue;

SemaphoreHandle_t xMutexEsp;

extern DMA_HandleTypeDef  hdma_rx;

extern DMA_HandleTypeDef  hdma_tx;

uint8_t rx_flag = NO_DATA;

uint8_t esp8266_TxBuffer[Tx_DATA_BUFFER] = { 0 };

uint8_t recv_temp[RECV_DATA_BUFFER] = { 0 };

volatile uint16_t recvData_len;
/* ********************************************** */


/* ********************************************** */
TaskHandle_t xCurrentSendTaskHandle = NULL;



/* ********************************************** */



/* ********************************************** */
bool UART4_Init( void );
static uint32_t usart_timeout_Calculate( uint16_t data_len );
static void esp8266Handle_Initial( ESP8266_HandleTypeDef *hpesp8266 );
static BaseType_t esp8266_ClearRecvQueue_Manual( void );
static void FlushRecvQueue( void );
bool at_extractString_between_quotes
( 
  ESP8266_HandleTypeDef *hpesp8266, 
  const char* key,
  char* out_val,
  uint8_t out_len,
  BaseType_t mode
);
bool at_extractNum( ESP8266_HandleTypeDef *hpesp8266, const char *key, uint32_t *out_val, BaseType_t mode );
bool at_extractField( ESP8266_HandleTypeDef *hpesp8266, at_field_type_t type, uint8_t index, const uint8_t **pReturn, uint16_t *pLen, BaseType_t mode );
/* ********************************************** */



void vtask8266_Init( void *parameter )
{
  esp8266Handle_Initial( &hesp8266 );

  xMutexEsp = xSemaphoreCreateRecursiveMutex();
  if ( xMutexEsp == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("xMutexEsp Get Failed in esp8266_driver.c.\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_SOURCE_GET_FAILED, RTOS_VER);
    #endif // __DEBUG_LEVEL_2__

    for(; ;);
  }

  FlushRecvQueue();

  EspInitState_t currentState = INIT_STATE_CHECK_AT;

  #if defined(__DEBUG_LEVEL_1__)
    printf("Esp8266 Init Start.\n");
  #endif // __DEBUG_LEVEL_1__


  while( (currentState != INIT_STATE_ERROR) && (currentState != INIT_STATE_COMPLETE) )
  {
    switch(currentState)
    {
      case INIT_STATE_CHECK_AT:      
        {
          if ( esp8266_SendAT("AT") )
          {
            void *pReady = esp8266_WaitResponse("OK", 250);

            if ( pReady != NULL )
            {
              esp8266_DropLastFrame();
              printf("AT Check OK.\n");
              currentState = INIT_STATE_SET_MODE;
              hesp8266.RetryCount = 0;
            }
            else 
            {
              printf("AT Test Failed!\n");
              hesp8266.RetryCount++;
            }
          }
          else 
          {
            printf("Send AT Command Failed!\n");
            hesp8266.RetryCount++;
          }

          break;
        }
      
      case INIT_STATE_SET_MODE:
        {
          if ( esp8266_ConnectModeChange(hesp8266.TargetMode) == hesp8266.TargetMode )
          {
            void *pOK = esp8266_WaitResponse("OK", 250);

            if ( pOK != NULL )
            {
              esp8266_DropLastFrame();
              printf("Wifi Mode Set OK.\n");
              hesp8266.CurrentMode = hesp8266.TargetMode;
              currentState = INIT_STATE_CONNECT_WIFI;
              hesp8266.RetryCount = 0;
            }
            else 
            {
              printf("Wifi Mode Wait Response Failed!\n");
              hesp8266.RetryCount++;
            }
          }
          else 
          {
            printf("Set Wifi Mode Failed!\n");
            hesp8266.RetryCount++;
          }
          break;
        }

      case INIT_STATE_CONNECT_WIFI:
        {
          char *patCommand = (char *)pvPortMalloc(128);
          if ( patCommand != NULL )
          {
            int len = snprintf(patCommand, 128, "AT+CWJAP=\"%s\",\"%s\"", hesp8266.WifiSSID, hesp8266.WifiPassword);
            if (len < 0 || len >= 128) 
            {
              printf("AT Command Format Error or Too Long! len=%d\n", len);
              hesp8266.RetryCount++;
              goto Free;
            }
          }
          else 
          {
            printf("Memory Allocation Failed!\n");
            hesp8266.RetryCount++;
            break;
          }

  

          if ( esp8266_SendAT("%s", patCommand) )
          {
            void *pConnectOK = esp8266_WaitResponse("WIFI GOT IP", 15000); // WIFI连接时间较长.
            if ( pConnectOK != NULL )
            {
              esp8266_DropLastFrame();
              printf("Wifi Connected.\n");
              currentState = INIT_STATE_GET_IP;
              hesp8266.RetryCount = 0;
            }
            else 
            {
              /* 错误码检查. 涉及复杂缓冲区管理 待实现. */
              printf("Wifi Connect Timeout!\n");
              hesp8266.RetryCount++;
            }
          }
          else 
          {
            printf("Send AT+CWJAP command failed.\n");
            hesp8266.RetryCount++;
          }
Free:
          vPortFree(patCommand);
          break;
        }

      case INIT_STATE_GET_IP:
        {
          if ( esp8266_SendAT("AT+CIPSTA?") )
          {
            void *pResponse = esp8266_WaitResponse("+CIPSTA:", 5000);
            if ( pResponse != NULL )
            {
              // 成功获取到IP信息.
              printf("Got IP Address Information\n");

              at_extractString_between_quotes(&hesp8266, "+CIPSTA:ip", hesp8266.Wifi_Ipv4, WIFI_IPV4_LENGTH, pdTRUE);

              printf("Ipv4: %s\n", hesp8266.Wifi_Ipv4);

              currentState = INIT_STATE_COMPLETE;
              hesp8266.RetryCount = 0;
            }
            else 
            {
              printf("Get IP Address Failed!\n");
              hesp8266.RetryCount++;
            }
          }
          else 
          {
            printf("Send AT+CIPSTA? command failed.\n");
            hesp8266.RetryCount++;
          }
          break;
        }

      default: 
        {
          currentState = INIT_STATE_ERROR;
          break;
        }
    }

    if ( hesp8266.RetryCount >= MAX_RETRY_COUNT)
    {
      for(; ;); // 用于调试. 

      printf("Max retry attempts (%d) reached at state %d. Initialization Failed.\n", MAX_RETRY_COUNT, currentState);

      HAL_Delay(500);

      NVIC_SystemReset();
    }

    if ( (currentState != INIT_STATE_COMPLETE) && (currentState != INIT_STATE_ERROR) && hesp8266.RetryCount != 0 )
    {
      HAL_Delay(pdMS_TO_TICKS(1000));  // 延迟1s后尝试重新初始化对应状态.
    }

    if ( currentState == INIT_STATE_ERROR )
    {
      printf("ESP8266 Initialization Failed. Please check hardware and configuration.\n");

      for( ; ; ); // 错误循环 用于调试.
    }
  }

  if ( currentState == INIT_STATE_COMPLETE )
  {
    // 调试部分****************************
    const LogStatus_t* log_status = Log_GetStatus();

    esp8266_tcp_Init();

    esp8266_tcp_Connect("ip9.com.cn", 80, TCP);

    const esp_tcp_handle_t *pState = esp8266_tcp_getState();

    printf("\n=== ESP8266 TCP Connection State ===\n");
    printf("is_Connected : %s\n", pState->is_Connected ? "true" : "false");
    printf("conn_ID      : %u\n", (unsigned int)pState->conn_ID);
    printf("Port         : %u\n", (unsigned int)pState->Port);
    printf("Host         : \"%.*s\"\n", 
           (int)sizeof(pState->Host), 
           pState->Host[0] ? pState->Host : "(not set)");
    printf("remote_IP    : \"%.*s\"\n", 
           (int)sizeof(pState->remote_IP), 
           pState->remote_IP[0] ? pState->remote_IP : "(not connected)");
    printf("state        : %s (%d)\n", 
           (pState->state), (int)pState->state);
    printf("remain       : 0x%02X\n", pState->remain);
    printf("=====================================\n");

    esp_http_t req;
    http_Init(&req, HTTP_METHOD_GET);
    http_SetHost(&req, "ip9.com.cn");
    http_SetPath(&req, "/get");
    //http_AddHeader(&req, "User-Agent: STM32-ESP8266-Client/1.0");

    static char json_buf[256];
    http_Get(&req, json_buf, sizeof(json_buf));
    printf("%s\n", json_buf);


  }
  // 调试部分****************************
  /*    TEST     */
}




/**
 * @brief 在指定长度的内存块中查找子串（无 null-terminator 依赖的 `memmem` 实现）
 *
 * 本函数是标准 `memmem()` 的轻量级嵌入式替代实现，专为 ESP8266 AT 响应解析设计：
 * 它不依赖 `\0` 结尾，仅基于显式长度进行安全搜索，避免因未终止字符串导致的越界访问。
 * 典型用于在 `LastReceivedFrame.RecvData` 中查找 `"OK"`, `"+IPD:"`, `"\"value\"` 等固定模式。
 *
 * @note
 *   - 搜索范围严格限定在 `[haystack, haystack + stack_len)` 内，**绝不越界读取**；
 *   - 若 `need_str_len == 0`，视为“空模式”，直接返回 `haystack`（POSIX 兼容行为）；
 *   - 若 `stack_len < need_str_len`，循环条件 `j <= stack_len - need_str_len` 自动失效，立即返回 `NULL`；
 *   - 使用朴素暴力算法（O(n×m)），适用于短响应帧（典型 `< 512B`），无额外内存开销；
 *   - 所有调试输出受 `__DEBUG_LEVEL_1__/2__` 控制，不影响 Release 构建；
 *   - **非线程安全**：调用者需确保 `haystack` / `need_str` 在搜索期间不被其他任务/中断修改。
 *
 * @param[in] haystack     待搜索的内存块起始地址（可为任意 `uint8_t*` 数据，如 UART 接收缓冲区）
 * @param[in] stack_len    `haystack` 的有效字节数（必须为 `uint16_t`，适配 ESP8266 帧长度）
 * @param[in] need_str     要查找的子串起始地址（可为 `char*` 或 `const void*`）
 * @param[in] need_str_len `need_str` 的字节数（必须为 `uint16_t`，支持二进制数据匹配）
 *
 * @retval non-NULL  指向 `haystack` 中首次匹配位置的指针（即 `&haystack[j]`）
 * @retval NULL      未找到匹配，或输入参数非法（`haystack==NULL` 或 `need_str==NULL`）
 *
 * @warning
 *   - 此函数**不验证 `need_str` 是否在合法内存区域** —— 若 `need_str` 指向非法地址，`needle[i]` 访问将触发 HardFault；
 *   - `stack_len` 和 `need_str_len` 均为 `uint16_t`，最大支持 64KB 搜索，但实际 AT 帧极少超过 1KB；
 *   - 该实现**不兼容 POSIX `memmem()` 的 `size_t` 参数类型**，但语义完全一致（返回匹配首地址）；
 *   - 在中断上下文调用时，需确保 `haystack` 缓冲区已由上层加锁保护（如 `xMutexEsp`），否则存在竞态风险。
 *
 * @see at_get_string_between_quotes(), esp8266_WaitResponse(), memchr()
 */
void*  memmem( 
                      const uint8_t *haystack, uint16_t stack_len, 
                      const void* need_str,    uint16_t need_str_len                      
                    )
{
  if ( haystack == NULL || need_str == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of memmem in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_WRONG_PARAM, RTOS_VER);
    #endif // __DEBUG_LEVEL_2__

    return NULL;
  }

  // 空字符串，直接返回haystack.
  if ( need_str_len == 0 )
  {
    return (void *)haystack;
  }

  const uint8_t *needle = (const uint8_t *)need_str;

  for(uint16_t j = 0; j <= stack_len - need_str_len; j++)
  {
    uint16_t i;

    for(i = 0; i < need_str_len; i++)
    {
      if ( haystack[j + i] != needle[i] )
      {
        break;
      }
    }

    if ( i == need_str_len )
    {
      // 找到子字符串.返回匹配起始位置的地址.
      return (void *)&haystack[j];
    }
  }

  // 没找到. 返回空指针.
  return NULL;
}




/**
 * @brief 同步发送格式化 AT 命令至 ESP8266 模块（基于 DMA + 任务通知机制）
 *
 * 使用 HAL_UART_Transmit_DMA 异步发送 AT 命令，并通过 FreeRTOS 任务通知（Task Notification）
 * 等待发送完成或超时。该函数是 ESP8266 驱动层最核心的命令下发接口。
 *
 * @note
 *   - 命令字符串经 `vsnprintf()` 安全格式化到全局缓冲区 `esp8266_TxBuffer`，
 *     并自动追加 `\r\n` 结尾（符合 AT 协议规范），末尾显式置 `\0`；
 *   - 发送长度上限为 `Tx_DATA_BUFFER - 3`（预留 `\r`, `\n`, `\0` 空间），超长或格式化失败立即返回 `false`；
 *   - 使用递归互斥量 `xMutexEsp` 保护临界区（防止多任务并发发送），获取超时 500ms；
 *   - 成功启动 DMA 后，将当前任务句柄存入 `xCurrentSendTaskHandle`，供 UART TX 完成中断回调使用；
 *   - 调用 `ulTaskNotifyTake()` 等待发送完成通知，超时则主动调用 `HAL_UART_AbortTransmit()` 终止 DMA；
 *   - 所有错误路径（互斥量失败、格式化失败、HAL 错误、超时）均会：
 *       • 清理 `xCurrentSendTaskHandle`；
 *       • 释放互斥量；
 *       • 记录对应级别日志（`LOG_DEBUG` 或 `LOG_WARNING`）；
 *   - **非阻塞但同步语义**：函数返回 `true` 表示 DMA 已成功启动且收到完成通知（即字节已移入硬件 FIFO）；
 *     返回 `false` 表示发送未完成（失败/超时），上层需重试或报错。
 *
 * @param[in] format 格式化字符串（如 `"AT+CWMODE=%d"`, `"AT+CWJAP=\"%s\",\"%s\""`），支持标准 printf 语法
 * @param[in] ...    可变参数列表（与 format 中占位符一一对应）
 *
 * @retval true  命令已成功格式化、发送并完成 DMA 传输（硬件 FIFO 已加载完毕）
 * @retval false 发送失败：互斥量获取超时、缓冲区溢出、`vsnprintf` 错误、HAL_UART 失败、或 DMA 超时
 *
 * @warning
 *   - **严禁在中断服务程序（ISR）中调用** —— `xSemaphoreTakeRecursive()` 和 `ulTaskNotifyTake()` 均不可在 ISR 中使用；
 *   - `format` 字符串长度 + 所有参数展开后总长 **必须 < Tx_DATA_BUFFER - 3**，否则静默截断并返回 `false`；
 *   - 若 UART 外设处于异常状态（如 `gState != HAL_UART_STATE_READY`），`HAL_UART_Transmit_DMA()` 可能直接返回 `HAL_ERROR`；
 *   - 本函数不校验 ESP8266 是否在线或响应能力 —— 它只负责“发出去”，响应处理由 `esp8266_WaitResponse()` 独立承担。
 *
 * @see usart_timeout_Calculate(), esp8266_WaitResponse(), HAL_UART_TxCpltCallback()
 */
bool esp8266_SendAT( const char* format, ... )
{
  BaseType_t err = xSemaphoreTakeRecursive(xMutexEsp, 500);
  if ( err != pdPASS )
  {
    LOG_WRITE(LOG_DEBUG, "NULL", "SendAT() get Mutex failed\n");

    return false;
  }

  va_list args;

  va_start( args, format );

  memset(esp8266_TxBuffer, 0, Tx_DATA_BUFFER);

  int len = vsnprintf( esp8266_TxBuffer, Tx_DATA_BUFFER, format, args );

  va_end(args);

  // (int)(sizeof(buffer) - 3) 预留出 \r\n\0的位置.
  if ( len < 0 || len >= Tx_DATA_BUFFER - 3)
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("AT Command Too Long or Format Error!\n");
    #endif

    LOG_WRITE(LOG_WARNING, "NULL", "AT Command Too Long or Format Error!\n");

    goto exit; 
  }

  if ( len > 0 )
  {
    esp8266_TxBuffer[len] = '\r';
    esp8266_TxBuffer[len + 1] = '\n';
    esp8266_TxBuffer[len + 2] = '\0'; 
    len += 2;   // 发送长度增加 2

    xCurrentSendTaskHandle = xTaskGetCurrentTaskHandle();

    HAL_StatusTypeDef status = 
                HAL_UART_Transmit_DMA(&esp8266_huart, esp8266_TxBuffer, len);

    if ( status != HAL_OK )
    {
      #if defined(__DEBUG_LEVEL_1__)
        printf("AT Send Error!\n");
      #endif // __DEBUG_LEVEL_1__

      LOG_WRITE(LOG_WARNING, "NULL", "AT Send Error.\n");

      goto exit;
    }

    if ( ulTaskNotifyTake(pdTRUE, usart_timeout_Calculate(len)) > 0 )
    {
      // 成功接收到通知.
      xSemaphoreGiveRecursive(xMutexEsp);

      xCurrentSendTaskHandle = NULL;

      return true;
    }
    
    // 发送超时.
    #if defined(__DEBUG_LEVEL_1__)
      printf("AT Command Send Timeout.\n");
    #endif // __DEBUG_LEVEL_1__

    __disable_irq();
    HAL_UART_StateTypeDef currentState = esp8266_huart.gState;
    __enable_irq();

    // 取消可能还在进行的DMA传输.
    if ( currentState == HAL_UART_STATE_BUSY_TX )
    {
      HAL_UART_AbortTransmit(&esp8266_huart);
    }

    goto exit;
  }


exit:
  xCurrentSendTaskHandle = NULL;
  xSemaphoreGiveRecursive(xMutexEsp);
  return false;
}




/**
 * @brief 配置 ESP8266 的 Wi-Fi 工作模式（STATION / SOFTAP / STATION+SOFTAP）
 *
 * 向 ESP8266 发送 `AT+CWMODE=<mode>` 命令，请求切换其 Wi-Fi 运行模式。
 * 该函数仅负责**下发配置指令**，不等待或验证模块是否实际切换成功；
 * 实际模式确认需由调用者后续调用 `esp8266_WaitResponse("OK")` 并结合状态机处理。
 *
 * @note
 *   - 支持的合法模式仅限枚举值：`STATION` (1), `SOFTAP` (2), `STATION_SOFTAP` (3)；
 *     其他值（如 0、4、负数）均视为非法输入，立即返回 `ESP_WIFI_ERROR`；
 *   - 使用递归互斥量 `xMutexEsp` 保护 AT 命令发送临界区，防止多任务并发冲突；
 *   - 命令字符串在栈上构造（`atCommand[32]`），通过 `snprintf` 严格校验长度：
 *       • `result < 0` → 格式化失败（内部错误）；
 *       • `result >= sizeof(atCommand)` → 缓冲区溢出风险，拒绝执行；
 *   - 成功构造后，调用 `esp8266_SendAT()` 异步发送（不阻塞），该函数返回后命令仍在 DMA 传输中；
 *   - **重要**：本函数返回 `Mode` 仅表示“已成功下发该模式指令”，不代表 ESP8266 当前已处于该模式；
 *     模块真实状态必须通过 `WaitResponse("OK")` + `AT+CWMODE?` 查询或状态机同步更新。
 *
 * @param[in] Mode 目标 Wi-Fi 模式，取值必须为以下之一：
 *                 - `STATION`: 仅作为 STA 连接路由器（客户端模式）；
 *                 - `SOFTAP`: 仅作为 AP 提供热点（服务端模式）；
 *                 - `STATION_SOFTAP`: 同时启用 STA+AP（混合模式）。
 *
 * @retval STATION         输入合法且命令已成功下发（非实时生效）
 * @retval SOFTAP          同上
 * @retval STATION_SOFTAP  同上
 * @retval ESP_WIFI_ERROR  输入非法、互斥量获取超时、或命令缓冲区不足
 *
 * @warning
 *   - 此函数**不处理 AT 响应**（如 "OK" 或 "ERROR"），也不修改 `hpesp8266->CurrentMode` 字段；
 *     状态同步必须由上层状态机（如 `vtask8266_Init`）在收到 `OK` 后显式赋值；
 *   - 若在中断上下文调用，将导致 `xSemaphoreTakeRecursive()` 永久阻塞 —— **严禁在 ISR 中调用**；
 *   - `esp8266_SendAT()` 可能因 UART 忙/超时失败，但本函数不捕获该错误（返回值被忽略），
 *     调用者需自行检查 `WaitResponse()` 结果以判定命令是否真正送达。
 *
 * @see esp8266_SendAT(), esp8266_WaitResponse(), vtask8266_Init()
 */
EspWifiMode_t esp8266_ConnectModeChange( EspWifiMode_t Mode )
{
  if  ( 
        Mode != STATION && 
        Mode != STATION_SOFTAP &&
        Mode != SOFTAP 
      )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of esp8266_ConnectModeChange in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_WRONG_PARAM, RTOS_VER);
    #endif // __DEBUG_LEVEL_2__

    LOG_WRITE(LOG_DEBUG, "NULL", "esp8266_ConnectModeChange called failed!\n");

    return ESP_WIFI_ERROR;
  }

  BaseType_t err = xSemaphoreTakeRecursive(xMutexEsp, 500);
  if ( err != pdPASS )
    return ESP_WIFI_ERROR;

  char atCommand[32];

  int result = snprintf(atCommand, sizeof(atCommand), "AT+CWMODE=%d", Mode);
  if ( result < 0 )
  {
    xSemaphoreGiveRecursive(xMutexEsp);

    return ESP_WIFI_ERROR;
  }

  // 输出被截断，缓冲区太小
  if ( result >= sizeof(atCommand) )
  {
    xSemaphoreGiveRecursive(xMutexEsp);

    return ESP_WIFI_ERROR;
  }

  esp8266_SendAT("%s", atCommand);

  xSemaphoreGiveRecursive(xMutexEsp);

  return Mode;
}




/**
 * @brief 阻塞等待 ESP8266 模块返回包含指定子串的 AT 响应帧
 *
 * 本函数从 FreeRTOS 队列 `hesp8266.xRecvQueue` 中接收一帧完整响应数据（`EspRecvMsg_t`），
 * 并在该帧的 `RecvData[]` 缓冲区中使用 `memmem()` 安全搜索目标字符串 `expected`。
 * 成功匹配后，立即将 `hesp8266.LastFrameValid` 标记为 `LastRecvFrame_Valid`，防止上层
 * 多次调用时重复解析同一帧；若超时或未匹配，则返回 NULL。
 *
 * @note
 *   - 调用前必须确保 `hesp8266.LastFrameValid == LastRecvFrame_Used`，否则函数立即返回 NULL；
 *     此设计强制要求上层在解析完一帧后调用 `esp8266_DropLastFrame()` 主动释放帧所有权；
 *   - 搜索基于显式长度（`Data_Len`），不依赖 `\0` 终止符，完全兼容二进制响应（如 `+IPD,0,5:Hello`）；
 *   - 使用 `xSemaphoreTakeRecursive(xMutexEsp, 200)` 保护队列接收临界区，超时 200ms 防死锁；
 *   - 若 `xQueueReceive()` 超时（即 `xTicksToWait` 耗尽），函数终止并返回 NULL；
 *   - 所有调试日志受 `__DEBUG_LEVEL_1__` 控制，不影响 Release 构建体积与性能；
 *   - 本函数**不可在中断上下文（ISR）中调用**（`xSemaphoreTakeRecursive` / `xQueueReceive` 非 ISR-safe）。
 *
 * @param[in]  expected     待搜索的目标子串（如 `"OK"`, `"+IPD:"`, `"WIFI GOT IP"`），必须非 NULL 且非空
 * @param[in]  timeout_ms   总等待超时时间（毫秒），建议：AT 命令响应设 200~500ms；TCP 连接设 5000~15000ms
 *
 * @retval non-NULL    指向 `LastReceivedFrame.RecvData` 中首次匹配位置的指针（即 `&RecvData[i]`）
 * @retval NULL        ① 参数非法；② 上一帧未释放（`LastFrameValid == Valid`）；③ 队列接收超时；④ `memmem` 未找到
 *
 * @warning
 *   - 返回的指针**仅在当前帧生命周期内有效**：一旦调用 `esp8266_DropLastFrame()` 或下一帧覆盖缓冲区，该地址失效；
 *   - 若需长期持有匹配内容，请立即 `memcpy` 到自有缓冲区；
 *   - 不检查 `expected` 是否在合法内存区域 —— 若传入非法地址，`memmem()` 将触发 HardFault；
 *   - 本函数不修改 `LastReceivedFrame.Data_Len` 或 `RecvData[]` 内容，仅读取。
 *
 */
void *esp8266_WaitResponse( const char* expected, uint32_t timeout_ms )
{
  if ( expected == NULL || strlen(expected) == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong param of esp8266_WaitResponse in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_WRONG_PARAM, RTOS_VER);
    #endif // __DEBUG_LEVEL_2__

    return NULL;
  }

  if ( hesp8266.LastFrameValid == LastRecvFrame_Valid )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Last frame still valid!\n");
    #endif // __DEBUG_LEVEL_1__

    return NULL;  //  上一帧数据仍未被解析,拒绝接受新帧.
  }

  uint16_t hayNeed_len = strlen(expected);

  TickType_t xTicksToWait = pdMS_TO_TICKS(timeout_ms);

  while( xTicksToWait > 0 )
  {
    BaseType_t err = xSemaphoreTakeRecursive(xMutexEsp, 200);

    if ( err != pdPASS )
      return NULL;

    if ( xQueueReceive(hesp8266.xRecvQueue, &hesp8266.LastReceivedFrame, xTicksToWait) == pdTRUE )
    {

      void *pSearch = memmem(hesp8266.LastReceivedFrame.RecvData, hesp8266.LastReceivedFrame.Data_Len, expected, hayNeed_len);

      if ( pSearch != NULL )
      {
        // 成功找到相关子串.
        hesp8266.LastFrameValid = LastRecvFrame_Valid;

        xSemaphoreGiveRecursive(xMutexEsp);

        return pSearch;
      }
      else 
      {
        // 没找到，继续等待下一帧数据.
        continue;
      }
    }
    else 
    {
      // 队列接收超时.
      #if defined(__DEBUG_LEVEL_1__)
        printf("Queue Recv Timeout!\n");
      #endif 

      break;
    }
  }

  // 等待超时.
  #if defined(__DEBUG_LEVEL_1__)
    printf("WaitResponse Timeout!\n");
  #endif // __DEBUG_LEVEL_1__

  xSemaphoreGiveRecursive(xMutexEsp);

  return NULL;
}




/**
 * @brief 初始化 ESP8266 专用 UART4 外设（含 DMA 接收与中断协同机制）
 *
 * 配置 UART4 为 ESP8266 通信通道，采用 **HAL_UARTEx_ReceiveToIdle_DMA** 模式实现高效、低功耗的帧接收：
 * - 利用 UART IDLE 线空闲中断检测一帧数据结束（替代固定长度超时），天然适配变长 AT 响应；
 * - DMA 自动将接收到的数据流写入 `recv_temp[]` 缓冲区，无需 CPU 干预；
 * - 启用 `UART_IT_IDLE` 和 `UART_IT_TC` 中断，分别用于帧结束识别和发送完成通知；
 * - 配置 DMA1_Stream4 服务 UART4_RX，并设置合理中断优先级（DMA: 6 > UART: 5），避免接收丢失。
 *
 * @note
 *   - `HAL_UARTEx_ReceiveToIdle_DMA()` 要求：DMA 缓冲区必须足够容纳单帧最大长度（`RECV_DATA_BUFFER`），
 *     且在 IDLE 中断触发后，需由 `UART4_IRQHandler` 调用 `HAL_UARTEx_ReceiveToIdle_IT()` 重启下一次接收；
 *   - `__HAL_UART_CLEAR_FLAG(&huart, UART_FLAG_TC)` 在初始化末尾显式清除 TC 标志，防止后续首次发送时误触发 TC 中断；
 *   - 所有外设时钟（UART4、DMA1）均通过 `__HAL_RCC_*_CLK_ENABLE()` 显式开启，符合 STM32CubeMX 最佳实践；
 *   - 初始化成功后打印 `"ESP8266 USART Init OK"`（仅 DEBUG_LEVEL_1+），便于产线快速验证；
 *   - 失败时返回 `false`，上层（如 `vtask8266_Init()`）应执行错误恢复（如复位模块或重试）。
 *
 * @retval true  UART4 及关联 DMA/中断成功初始化，可立即用于 `esp8266_SendAT()` 和 AT 响应解析
 * @retval false 初始化失败：HAL_UART_Init() 或 HAL_UARTEx_ReceiveToIdle_DMA() 返回 `HAL_ERROR`/`HAL_BUSY`
 *
 * @warning
 *   - **必须确保 `recv_temp[]` 缓冲区生命周期全局有效且未被其他任务修改** —— DMA 直接访问该地址；
 *   - `UART4_IRQn` 和 `DMA1_Stream4_IRQn` 的中断服务函数（ISR）**必须已正确实现**：
 *       • `UART4_IRQHandler` 中需调用 `HAL_UART_IRQHandler()` → 触发 `HAL_UARTEx_RxEventCallback()`；
 *       • `DMA1_Stream4_IRQHandler` 中需调用 `HAL_DMA_IRQHandler()` → 完成传输处理；
 *   - 若 `RECV_DATA_BUFFER` 小于 ESP8266 单次响应长度（如大 JSON 或固件升级包），将导致 DMA 溢出 —— 此函数不校验缓冲区是否足够；
 *   - 本函数**不启动 UART 发送 DMA**，发送始终使用 `HAL_UART_Transmit_DMA()` 按需触发（见 `esp8266_SendAT()`）。
 *
 * @see esp8266_SendAT(), HAL_UARTEx_ReceiveToIdle_DMA(), UART4_IRQHandler(), DMA1_Stream4_IRQHandler()
 * @see HAL_UARTEx_RxEventCallback(), HAL_UART_TxCpltCallback()
 */
bool UART4_Init( void )
{
  __HAL_RCC_UART4_CLK_ENABLE();

  __HAL_RCC_DMA1_CLK_ENABLE();

  esp8266_huart.Instance = ESP_UART;
  esp8266_huart.Init.BaudRate = ESP_UART_BAUDRATE;
  esp8266_huart.Init.Mode = ESP_UART_MODE;
  esp8266_huart.Init.Parity = ESP_UART_PARITY;
  esp8266_huart.Init.StopBits = ESP_UART_STOPBITS;
  esp8266_huart.Init.WordLength = ESP_UART_WORDLENGTH;
  esp8266_huart.Init.HwFlowCtl = ESP_UART_HwFLOW;

  if ( HAL_UART_Init(&esp8266_huart) != HAL_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("UART Init Failed in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_INIT_FAILED, RTOS_VER);
    #endif //__DEBUG_LEVEL_2__

    return false;
  }

  if ( HAL_UARTEx_ReceiveToIdle_DMA(&esp8266_huart, recv_temp, RECV_DATA_BUFFER) != HAL_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Failed to start ReceiveToIdle DMA!\n");
    #endif

    return false;
  }

  __HAL_UART_ENABLE_IT(&esp8266_huart, UART_IT_IDLE); // 显示使能IDLE中断.
  __HAL_UART_ENABLE_IT(&esp8266_huart, UART_IT_TC);  // 发送完成中断.

  __HAL_UART_CLEAR_FLAG(&esp8266_huart, UART_FLAG_TC);

  HAL_NVIC_SetPriority(UART4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(UART4_IRQn);

  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

  printf("ESP8266 USART Init OK\n");

  return true;
}




ErrorStatus vEspInit_TaskCreate( void )
{

  BaseType_t err = xTaskCreate((TaskFunction_t)vtask8266_Init, 
                                 "vtask8266_Init",
                                    INIT_TASK_DEPTH,
                                      NULL,
                                        INIT_TASK_PRIO,
                                         NULL );
  
  if ( err != pdPASS )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("EspInitTask Create Failed! in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_SOURCE_GET_FAILED, COMN_VER);
    #endif // __DEBUG_LEVEL_2__

    return ERROR;
  }

  return SUCCESS;
}



static uint32_t usart_timeout_Calculate( uint16_t data_len )
{
  assert( data_len > 0 );

  uint32_t per_Frame = 10;  // 一帧数据大小(+停止位 起始位).
  uint32_t per_bit_us = 1000000 / ESP_UART_BAUDRATE;  // 1帧数据传输时间 us.
  uint32_t transfer_time = data_len * per_Frame * per_bit_us;

  // 增加50%的余量，并将其转换为ms.
  uint32_t timeout = ( transfer_time * 3 / 2 ) / 1000;

   // 最小和最大超时限制
   return (timeout < 100) ? 100 : (timeout > 5000) ? 5000 : timeout;
}




/**
 * @brief 释放当前持有的最后一帧 AT 响应数据，标记其为“已消费”，并清空缓冲区内容
 *
 * 该函数用于显式告知驱动层：“上一帧响应数据已完成解析，可安全覆盖”。
 * 它将 `hesp8266.LastFrameValid` 置为 `LastRecvFrame_Used`，并调用 `memset()` 彻底清零
 * `hesp8266.LastReceivedFrame` 结构体（含 `RecvData[]` 缓冲区与 `Data_Len`），防止残留数据
 * 干扰后续解析（如旧 `+IPD` 数据未清导致误匹配）。
 *
 * @note
 *   - 此函数**必须在成功解析一帧响应后手动调用**（例如：提取完 `"OK"`、解析完 `"+CIPSTART:"` 或 `"+IPD,"` 后）；
 *   - 若未调用本函数，`esp8266_WaitResponse()` 将拒绝接收新帧（返回 NULL），避免重复解析同一帧；
 *   - 使用递归互斥量 `xMutexEsp` 保护临界区，确保多任务/中断安全（DMA 接收与解析不冲突）；
 *   - 清零操作使用 `sizeof(EspRecvMsg_t)`，精确覆盖整个结构体（含 padding），杜绝未初始化字节。
 *
 * @warning
 *   - ❗ 调用前请确保 `LastReceivedFrame.RecvData` 中的数据**已全部提取完毕**（如 `out_val` 已 memcpy）；
 *     返回的指针（如 `esp8266_WaitResponse()` 的结果）在调用本函数后立即失效！
 *   - ❗ **严禁在 UART RX IDLE 中断或 DMA 回调中直接调用** —— `xSemaphoreTakeRecursive()` 不可在中断上下文使用；
 *     如需在中断中触发清理，请通过 `xTaskNotifyGive()` 唤醒解析任务后由任务调用。
 *   - ❗ 若 `xSemaphoreTakeRecursive()` 失败（极罕见），本函数静默返回，**不会清空数据** —— 上层需保证互斥量已正确创建且未损坏。
 *
 * @see esp8266_WaitResponse(), at_extractString_between_quotes(), at_extractNum()
 */
void esp8266_DropLastFrame(void)
{
    if ( hesp8266.LastFrameValid == LastRecvFrame_Used )
    {
      return;
    }

    if (xSemaphoreTakeRecursive(xMutexEsp, portMAX_DELAY) == pdPASS)
    {
        hesp8266.LastFrameValid = LastRecvFrame_Used;

        //清空数据
        memset(&hesp8266.LastReceivedFrame, 0, sizeof(EspRecvMsg_t));

        xSemaphoreGiveRecursive(xMutexEsp);
    }
}




/**
 * @brief 初始化 ESP8266 句柄结构体（HAL 层抽象对象）的默认状态与运行时资源
 *
 * 该函数执行以下关键操作：
 *   1. 重置所有状态字段至安全初始值（如 RetryCount=0, Status=DISCONNECTED）；
 *   2. 安全拷贝预定义的 WiFi 凭据（WIFI_SSID / WIFI_PASSWORD）到句柄缓冲区，
 *      使用 strncpy + 显式 '\0' 终止，防止缓冲区溢出与未终止字符串风险；
 *   3. 创建用于接收 AT 响应数据帧的 FreeRTOS 队列（xRecvQueue），大小为 DATA_QUEUE_LENGTH；
 *      若创建失败，记录调试日志并立即返回（不中断上层流程）；
 *   4. 不初始化硬件资源（UART/DMA/IRQ），此职责由 UART4_Init() 独立承担；
 *   5. 不创建互斥量（xMutexEsp）或任务句柄，这些由 vtask8266_Init() 在上下文安全后完成。
 *
 * @note
 *   - 此函数为纯软件初始化，**不触发任何硬件访问或阻塞操作**，可安全在任意上下文调用；
 *   - 所有字符串拷贝均遵循 C 安全实践：长度严格限制于目标缓冲区 size-1，并手动补 '\0'；
 *   - 队列创建失败时仅记录错误，不 panic —— 上层（vtask8266_Init）会检查并处理该失败；
 *   - 调用者必须确保传入的 hpesp8266 指针有效且已分配内存（非 NULL）。
 *
 * @param[in,out] hpesp8266 指向待初始化的 ESP8266_HandleTypeDef 结构体指针
 *                          （必须为已分配内存的有效地址，否则直接 return）
 *
 * @retval None
 *
 * @see UART4_Init(), vtask8266_Init(), xQueueCreate()
 */
static void esp8266Handle_Initial( ESP8266_HandleTypeDef *hpesp8266 )
{
  if ( hpesp8266 == NULL )
  {
    return;
  }

  hpesp8266->RetryCount = 0;
  hpesp8266->MaxRetry = 3;
  hpesp8266->Status = ESP_STATUS_DISCONNECTED;
  hpesp8266->CurrentMode = ESP_WIFI_ERROR;
  hpesp8266->TargetMode = STATION_SOFTAP;
  hpesp8266->LastReceivedFrame.Data_Len = 0;
  hpesp8266->LastFrameValid = LastRecvFrame_Used;

  memset(hpesp8266->Wifi_Ipv4, 0, sizeof(hpesp8266->Wifi_Ipv4));

  strncpy(hpesp8266->WifiSSID, WIFI_SSID, sizeof(hpesp8266->WifiSSID) - 1);
  hpesp8266->WifiSSID[sizeof(hpesp8266->WifiSSID) - 1] = '\0';

  strncpy(hpesp8266->WifiPassword, WIFI_PASSWORD, sizeof(hpesp8266->WifiPassword) - 1);
  hpesp8266->WifiPassword[sizeof(hpesp8266->WifiPassword) - 1] = '\0';

  hpesp8266->xRecvQueue = xQueueCreate(DATA_QUEUE_LENGTH, sizeof(EspRecvMsg_t));

  if ( hpesp8266->xRecvQueue == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Failed to create Rx Queue!\n");
    #endif

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_SOURCE_GET_FAILED, RTOS_VER);
    #endif 

    return;
  }
}




/**
 * @brief 从 ESP8266 最新接收的有效响应帧中提取指定键（key）对应的双引号内字符串值
 *
 * 该函数是 AT 响应解析的关键工具，用于安全提取形如 `+CIPSTA:ip:"192.168.4.1"` 或
 * `AT+CWJAP="MyWiFi","******"` 等响应中 key 后紧跟的 `"value"` 字符串。
 *
 * @note
 *   - 提取逻辑完全委托给底层函数 `at_get_string_between_quotes()`，本函数仅负责：
 *       • 参数合法性校验（指针非空、out_len > 0）；
 *       • 从 `hpesp8266->LastReceivedFrame` 中提供原始数据与长度；
 *       • 根据 `mode` 参数决定是否自动标记该帧为“已消费”（即置 `LastFrameValid = Used`）；
 *   - 若 `mode == pdTRUE`：成功提取后立即将 `LastFrameValid` 设为 `LastRecvFrame_Used`，
 *     防止后续调用重复解析同一帧（典型用于初始化阶段的一次性提取）；
 *   - 若 `mode == pdFALSE`：仅返回提取结果，不修改帧状态，适用于需多次解析同一帧的场景；
 *   - 所有调试输出（`printf`）受 `__DEBUG_LEVEL_1__` 宏控制，不影响 Release 构建；
 *   - `out_val` 缓冲区必须由调用者确保足够容纳目标字符串（含终止 `\0`），本函数不越界写入。
 *
 * @param[in]     hpesp8266 指向 ESP8266 句柄的指针，用于访问 `LastReceivedFrame`
 * @param[in]     key       要匹配的键名（如 `"+CIPSTA:ip"`、`"AT+CWJAP"`），区分大小写
 * @param[out]    out_val   输出缓冲区，用于存放提取出的字符串（不含双引号，已 `\0` 终止）
 * @param[in]     out_len   `out_val` 缓冲区总字节数（必须 ≥ 1，否则返回 false）
 * @param[in]     mode      解析模式：
 *                          - `pdTRUE`: 成功后自动标记 `LastFrameValid = LastRecvFrame_Used`
 *                          - `pdFALSE`: 不修改帧状态，保持 `LastFrameValid` 不变
 *
 * @retval true  成功提取字符串（`out_val` 已写入有效内容并 `\0` 终止）
 * @retval false 参数非法、`at_get_string_between_quotes()` 失败、或 `out_len` 不足
 *
 * @warning
 *   - 此函数**不检查 `LastFrameValid` 状态** —— 调用者须确保 `LastReceivedFrame` 当前有效且未被覆盖；
 *   - 若 `hpesp8266->LastReceivedFrame.Data_Len == 0` 或数据未以 `\0` 结尾，`at_get_string_between_quotes()`
 *     行为取决于其内部实现（建议其使用 `memmem` + 显式长度，而非 `strstr`）；
 *   - `out_len` 必须 ≥ 1，否则无法写入终止符 `\0`，导致未定义行为。
 *
 * @see at_get_string_between_quotes(), esp8266_WaitResponse(), esp8266_DropLastFrame()
 */
bool at_extractString_between_quotes
( 
  ESP8266_HandleTypeDef *hpesp8266, 
  const char* key,
  char* out_val,
  uint8_t out_len,
  BaseType_t mode
)
{
  // 参数检查
  if (hpesp8266 == NULL || key == NULL || out_val == NULL || out_len == 0) 
  {
    #if defined(__DEBUG_LEVEL_1__)
        printf("Invalid parameter in at_extractString_between_quotes\n");
    #endif
    return false;
  }

  if ( hpesp8266->LastFrameValid == LastRecvFrame_Used )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Not Valid Frame in at_extractString_between_quotes.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "NULL", "Not Valid Frame at_ext_betwn_quotes.");
    return false;
  }

  bool result = at_get_string_between_quotes(hpesp8266->LastReceivedFrame.RecvData, 
                                    hpesp8266->LastReceivedFrame.Data_Len, 
                                        key, out_val, out_len);
  
  if ( result == false )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Extracts Data Failed of at_extractString_between_quotes in esp8266_driver.c!\n");
    #endif // __DEBUG_LEVEL_1__

    return false;
  }

  if ( mode == pdTRUE )
  {
    hpesp8266->LastFrameValid = LastRecvFrame_Used;
  }

  return true;
}




/**
 * @brief 从 ESP8266 最新接收帧中提取指定键（key）后紧跟的无符号整数值
 *
 * 基于 `at_get_num()` 解析 `LastReceivedFrame.RecvData` 中形如 `"+KEY:123"` 的数字字段，
 * 专用于获取 AT 响应中的状态码、ID、端口、长度等整型参数。
 *
 * @note
 *   - 要求 `LastReceivedFrame` 当前有效（`LastFrameValid == LastRecvFrame_Valid`）；
 *   - 提取逻辑依赖 `at_get_num()`：自动匹配 `"+key:"` 模式，跳过前导空白，提取首个连续十进制数字；
 *   - 若 `mode == pdTRUE`，成功后自动标记该帧为“已消费”（`LastFrameValid = LastRecvFrame_Used`）；
 *   - 所有调试输出受 `__DEBUG_LEVEL_1__` 控制，不影响 Release 构建。
 *
 * @param[in]  hpesp8266 指向 ESP8266 句柄的指针（必须非 NULL）
 * @param[in]  key       待查找的键名（不含 `"+"` 和 `":"`，如 `"HTTPCLIENT"`, `"CIPSTATUS"`）
 * @param[out] out_val   输出参数：成功时写入解析得到的 `uint32_t` 值（不校验溢出）
 * @param[in]  mode      消费模式：`pdTRUE` → 提取后自动释放帧；`pdFALSE` → 保留帧状态供多次解析
 *
 * @retval true  成功找到 `"+key:"` 且其后存在有效十进制数字，并完成赋值
 * @retval false 参数非法、`at_get_num()` 失败、或 `out_val` 为 NULL
 *
 * @warning
 *   - ❗ 不支持负数、十六进制、逗号分隔、科学计数法或带单位字符串（如 `"123ms"`）；
 *   - ❗ 返回 `true` 不代表数值语义合法（如端口号 > 65535），上层需按协议校验范围；
 *   - 此函数**不加锁** —— 调用者须确保 `hpesp8266->LastReceivedFrame` 在解析期间未被 DMA 覆盖（通常已在 `esp8266_WaitResponse()` 后持有 `xMutexEsp`）。
 */
bool at_extractNum( ESP8266_HandleTypeDef *hpesp8266, const char *key, uint32_t *out_val, BaseType_t mode )
{
  if ( !hpesp8266 || !key || !out_val )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Invalid parameter in at_extractNum.\n");
    #endif    

    LOG_WRITE(LOG_DEBUG, "NULL", "Invalid parameter in at_extractNum.");
    return false; 
  }

  if ( hpesp8266->LastFrameValid == LastRecvFrame_Used )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Not Valid Frame in at_extractNum.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "NULL", "Not Valid Frame in at_extractNum.");
    return false;
  }

  bool result = at_get_num(hpesp8266->LastReceivedFrame.RecvData, hesp8266.LastReceivedFrame.Data_Len, key, out_val);

  if ( result == false )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Get num failed in at_extractNum.\n");
    #endif 

    LOG_WRITE(LOG_WARNING, "NULL", "Get num failed in at_extractNum.");
    return false;
  }

  if ( mode == pdTRUE )
  {
    hpesp8266->LastFrameValid = LastRecvFrame_Used;
  }

  return true;
}



bool at_extractField( ESP8266_HandleTypeDef *hpesp8266, at_field_type_t type, uint8_t index, const uint8_t **pReturn, uint16_t *pLen, BaseType_t mode )
{
  if ( !hpesp8266 || index == 0 || !pReturn || !pLen )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of at_extractField.\n");
    #endif 

    LOG_WRITE(LOG_WARNING, "NULL", "Wrong Param of at_extractField.");
    return false;
  } 

  if ( hpesp8266->LastFrameValid == LastRecvFrame_Used )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Not Valid Frame in at_extractField.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "NULL", "Not Valid Frame in at_extractField.");
    return false;
  }

  bool res = at_get_field(hpesp8266->LastReceivedFrame.RecvData, hpesp8266->LastReceivedFrame.Data_Len, type, index, pReturn, pLen);

  if ( res == false )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("at_get_field Wrong.\n");
    #endif     

    LOG_WRITE(LOG_ERROR, "NULL", "at_get_field Wrong.");
    return false;
  } 

  if ( mode == pdTRUE )
  {
    hpesp8266->LastFrameValid = LastRecvFrame_Used;
  }

  return true;
}




/**
 * @brief      手动清空 ESP8266 接收数据队列
 * @details    该函数用于在初始化或状态重置前，清除接收队列中残留的无效数据帧
 *             （如模块上电时输出的乱码、旧响应等）.通过递归互斥量保护操作,
 *             防止并发访问。若首次获取互斥量失败,会延迟重试一次.
 *             
 *             常用于：
 *              - 模块复位后清理脏数据.
 *              - 初始化开始前丢弃启动日志.
 *              - 避免历史数据干扰新流程.
 *
 * @return     pdTRUE 成功完成清空操作.
 * @return     pdFALSE 获取互斥量超时且重试仍失败.
 *
 * @note       调用此函数前应确保 UART DMA 接收已启动，否则无数据可清.
 * @warning    不要从中断上下文调用！本函数可能触发任务调度（vTaskDelay）.
 */
static BaseType_t esp8266_ClearRecvQueue_Manual( void )
{
  EspRecvMsg_t dummy;

  if ( xSemaphoreTakeRecursive(xMutexEsp, portMAX_DELAY) == pdPASS )
  {
Clear:    // 持续从队列中取出数据直到为空
    while( xQueueReceive(hesp8266.xRecvQueue, &dummy, 0) == pdTRUE )
    {
      UNUSED(dummy);
    }

    xSemaphoreGiveRecursive(xMutexEsp);
  }
  else 
  {
    vTaskDelay(pdMS_TO_TICKS(500));

    // 再次尝试获取锁.
    if ( xSemaphoreTakeRecursive(xMutexEsp, portMAX_DELAY) != pdPASS )
      goto Clear;

    #if defined(__DEBUG_LEVEL_1__)
      printf("Cant get Mutex inside the ClearRecvQueue_Manual in esp8266_driver.c.\n");
    #endif 

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_SOURCE_GET_FAILED, RTOS_VER);
    #endif

    return pdFALSE;
  }

  return pdTRUE;
}


static void FlushRecvQueue( void )
{
  uint8_t errorFlag = 0;

  uint8_t tmp;

  BaseType_t err = esp8266_ClearRecvQueue_Manual();
  if ( err == pdFALSE )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Error Clear RecvBuff! Start Retry.\n");
    #endif // __DEBUG_LEVEL_1__

    errorFlag = 1;
  }
  if ( errorFlag )
  {
    for(uint8_t j = 0; j < 3; j++)
    {
      err = esp8266_ClearRecvQueue_Manual();

      if ( err == pdTRUE )
      {
        break;
      }

      // 延时重试.
      vTaskDelay(pdMS_TO_TICKS(500));
      #if defined(__DEBUG_LEVEL_1__)
        printf("Retry Count: %d", j);
      #endif
    }

    // 再清硬件缓冲区（防残留）
    while (__HAL_UART_GET_FLAG(&esp8266_huart, UART_FLAG_RXNE))
    {
        tmp = esp8266_huart.Instance->DR & 0xFF;
    }
    HAL_Delay(10); // 稳定一下
    // 无论是否成功都必须接着往下执行.

  }
}
