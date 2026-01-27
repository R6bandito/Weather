#include "esp8266_driver.h"

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

              printf("Ipv4: %s", hesp8266.Wifi_Ipv4);

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
    const LogStatus_t* log_status = Log_GetStatus();

    Log_Flash_ClearLogMes();

    for( ; ; );
  }
  
  /*    TEST     */
}


/**
 * @brief 在数据块中搜索子串.
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
   * @brief  ESP8266 AT命令发送
   * @param  const char* format, ... : 格式化字符串.
   *          
   * @note  
   *         
 */
bool esp8266_SendAT( const char* format, ... )
{
  BaseType_t err = xSemaphoreTakeRecursive(xMutexEsp, 500);
  if ( err != pdPASS )
  {

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


void esp8266_DropLastFrame(void)
{
    if (xSemaphoreTakeRecursive(xMutexEsp, portMAX_DELAY) == pdPASS)
    {
        hesp8266.LastFrameValid = LastRecvFrame_Used;

        //清空数据
        memset(&hesp8266.LastReceivedFrame, 0, sizeof(EspRecvMsg_t));

        xSemaphoreGiveRecursive(xMutexEsp);
    }
}


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
 * @brief 从最新一帧数据中查找 key="value" 形式的值，并复制到 out_val 缓冲区
 *      
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

    return true;
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
