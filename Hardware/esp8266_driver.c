#include "esp8266_driver.h"

USART_HandleTypeDef esp8266_husart;

SemaphoreHandle_t xMutexEsp;

uint8_t rx_flag = NO_DATA;

uint8_t esp8266_RecvBuffer[RECV_DATA_BUFFER] = { 0 };



/* ********************************************** */
static void USART2_Init( void );
static void Wifi_Connect( void );
/* ********************************************** */



void vtask8266_Init( void *parameter )
{
  USART2_Init();

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

    esp8266_ConnectModeChange(STATION_SOFTAP);

    vTaskDelay(pdMS_TO_TICKS(1000));

    esp8266_SendAT("AT");

    vTaskDelay(pdMS_TO_TICKS(1000));
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

  char buffer[COMMAND_BUFFER];

  va_list args;

  va_start( args, format );

  int len = vsnprintf( buffer, sizeof(buffer), format, args );

  // (int)(sizeof(buffer) - 3) 预留出 \r\n\0的位置.
  if ( len < 0 || len >= (int)(sizeof(buffer) - 3))
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("AT Command Too Long or Format Error!\n");
    #endif

    xSemaphoreGiveRecursive(xMutexEsp);

    return false;
  }

  if ( len > 0 )
  {
    buffer[len] = '\r';
    buffer[len + 1] = '\n';
    buffer[len + 2] = '\0'; 
    len += 2;   // 发送长度增加 2

    HAL_StatusTypeDef status = 
                HAL_USART_Transmit(&esp8266_husart, (uint8_t *)buffer, len, 500);

    if ( status != HAL_OK )
    {
      #if defined(__DEBUG_LEVEL_1__)
        printf("AT Send Error!\n");
      #endif // __DEBUG_LEVEL_1__

      xSemaphoreGiveRecursive(xMutexEsp);

      return false;
    }
    
    xSemaphoreGiveRecursive(xMutexEsp);

    return true;
  }

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



static void USART2_Init( void )
{
  USART_Clk_Enable(USART2);

  esp8266_husart.Instance = USART2;
  esp8266_husart.Init.BaudRate = ESP_USART_BAUDRATE;
  esp8266_husart.Init.Mode = ESP_USART_MODE;
  esp8266_husart.Init.Parity = ESP_USART_PARITY;
  esp8266_husart.Init.StopBits = ESP_USART_STOPBITS;
  esp8266_husart.Init.WordLength = ESP_USART_WORDLENGTH;

  if ( HAL_USART_Init(&esp8266_husart) != HAL_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("USART Init Failed in esp8266_driver.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_INIT_FAILED, RTOS_VER);
    #endif //__DEBUG_LEVEL_2__
  }

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
