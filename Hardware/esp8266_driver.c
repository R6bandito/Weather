#include "esp8266_driver.h"

USART_HandleTypeDef esp8266_husart;



/* ********************************************** */
static void USART2_Init( void );
/* ********************************************** */



void vtask8266_Init( void *parameter )
{
  USART2_Init();

  // TEST.
  for( ; ; )
  {
    printf("vtask8266 RUNNING!\n");

    vTaskDelay(pdMS_TO_TICKS(1000));
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
      Debug_LED_Dis(DEBUG_INIT_FAILED, COMN_VER);
    #endif //__DEBUG_LEVEL_2__
  }

  HAL_USART_Transmit(&esp8266_husart, "ESP8266 USART Init OK!\n", sizeof("ESP8266 USART Init OK!\n") - 1, 400);
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
