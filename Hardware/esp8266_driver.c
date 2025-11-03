#include "esp8266_driver.h"


/* Global Ver.*/
/* ********************************************** */
UART_HandleTypeDef esp8266_huart;

SemaphoreHandle_t xMutexEsp;

DMA_HandleTypeDef  hdma_rx;

DMA_HandleTypeDef  hdma_tx;

uint8_t rx_flag = NO_DATA;

uint8_t esp8266_RecvBuffer[RECV_DATA_BUFFER] = { 0 }; 

uint8_t esp8266_TxBuffer[Tx_DATA_BUFFER] = { 0 };

uint8_t recv_temp[RECV_DATA_BUFFER] = { 0 };

volatile uint16_t recvData_len;
/* ********************************************** */


/* ********************************************** */
TaskHandle_t xCurrentSendTaskHandle = NULL;



/* ********************************************** */



/* ********************************************** */
static void UART4_Init( void );
static void Wifi_Connect( void );
static uint32_t usart_timeout_Calculate( uint16_t data_len );
/* ********************************************** */



void vtask8266_Init( void *parameter )
{
  UART4_Init();

  xMutexEsp = xSemaphoreCreateRecursiveMutex();
  if ( xMutexEsp == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Mutex Get Failed in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_SOURCE_GET_FAILED, RTOS_VER);
    #endif // __DEBUG_LEVEL_2__
  }

  // TEST.
  for( ; ; )
  {
    printf("vtask8266 RUNNING!\n");

    esp8266_SendAT("AT+CWMODE=3");

    vTaskDelay(pdMS_TO_TICKS(1000));

    if ( rx_flag == RECV_DATA )
    {
      printf("Recv Data Len: %d\n", recvData_len);

      rx_flag = NO_DATA;
    }
  }

}


/**
 * @brief 在数据块中搜索子串.
 */
static void*  memmem( 
                      const uint8_t *haystack, uint16_t stack_len, 
                      const void* need_str,    uint16_t need_str_len                      
                    )
{
  


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



EspMode_t esp8266_ConnectModeChange( EspMode_t Mode )
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

    return ESP_ERROR;
  }

  BaseType_t err = xSemaphoreTakeRecursive(xMutexEsp, 500);
  if ( err != pdPASS )
    return ESP_ERROR;

  char atCommand[32];

  int result = snprintf(atCommand, sizeof(atCommand), "AT+CWMODE=%d", Mode);
  if ( result < 0 )
  {
    xSemaphoreGiveRecursive(xMutexEsp);

    return ESP_ERROR;
  }

  // 输出被截断，缓冲区太小
  if ( result >= sizeof(atCommand) )
  {
    xSemaphoreGiveRecursive(xMutexEsp);

    return ESP_ERROR;
  }

  esp8266_SendAT("%s", atCommand);

  xSemaphoreGiveRecursive(xMutexEsp);

  return Mode;
}



bool esp8266_WaitResponse( const char* expected, uint32_t timeout_ms )
{
  if ( expected == NULL || strlen(expected) == 0 )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong param of esp8266_WaitResponse in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_WRONG_PARAM, RTOS_VER);
    #endif // __DEBUG_LEVEL_2__

    return false;
  }

  const uint8_t *rx_data;

  uint16_t data_len = 0;

  TickType_t start_time = xTaskGetTickCount();

  while( (xTaskGetTickCount - start_time) < timeout_ms )
  {
    if ( rx_flag == RECV_DATA )
    {
      BaseType_t err = xSemaphoreTakeRecursive(xMutexEsp, 500);
      if ( err != pdPASS ) 
        return false;

      rx_data = esp8266_RecvBuffer;

      /*data_len = ....*/

      
    }
  }
}



static void UART4_Init( void )
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

    return;
  }

  
  hdma_rx.Instance = DMA1_Stream2;
  hdma_rx.Init.Channel = DMA_CHANNEL_4;
  hdma_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  hdma_rx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_rx.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_rx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_rx.Init.Mode = DMA_NORMAL;
  hdma_rx.Init.PeriphBurst = DMA_PBURST_SINGLE;
  hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_rx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_rx.Init.Priority = DMA_PRIORITY_MEDIUM;

  if ( HAL_DMA_Init(&hdma_rx) != HAL_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("DMA_Rx Init Failed in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_INIT_FAILED, RTOS_VER);
    #endif //__DEBUG_LEVEL_2__

    return;
  }

  hdma_tx.Instance = DMA1_Stream4;
  hdma_tx.Init.Channel = DMA_CHANNEL_4;
  hdma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  hdma_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_tx.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_tx.Init.Mode = DMA_NORMAL;
  hdma_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
  hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_tx.Init.Priority = DMA_PRIORITY_MEDIUM;

  if ( HAL_DMA_Init(&hdma_tx) != HAL_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("DMA_Tx Init Failed in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_INIT_FAILED, RTOS_VER);
    #endif //__DEBUG_LEVEL_2__

    return;    
  }

  __HAL_LINKDMA(&esp8266_huart, hdmarx, hdma_rx);

  __HAL_LINKDMA(&esp8266_huart, hdmatx, hdma_tx);

  if ( HAL_UARTEx_ReceiveToIdle_DMA(&esp8266_huart, recv_temp, RECV_DATA_BUFFER) != HAL_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Failed to start ReceiveToIdle DMA!\n");
    #endif

    return;
  }

  __HAL_UART_ENABLE_IT(&esp8266_huart, UART_IT_IDLE); // 显示使能IDLE中断.
  __HAL_UART_ENABLE_IT(&esp8266_huart, UART_IT_TC);  // 发送完成中断.

  __HAL_UART_CLEAR_FLAG(&esp8266_huart, UART_FLAG_TC);

  HAL_NVIC_SetPriority(UART4_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(UART4_IRQn);

  HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

  printf("ESP8266 USART Init OK\n");
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
