#include "bsp_usart_debug.h"


USART_HandleTypeDef husart3;

SemaphoreHandle_t vMutex_Debug;

#if !defined(__REALEASE__)



#if defined(__DEBUG_LEVEL_2__)
  static void Debug_LED_State_INIT_Failed( DebugLEDEnvMode_t Mode );

  static void Debug_LED_State_HARD_FAULT_Failed( DebugLEDEnvMode_t Mode );

  static void Debug_LED_State_SOURCE_GET_Failed( DebugLEDEnvMode_t Mode );

  void Debug_LED_Dis( DebugLedState_t State, DebugLEDEnvMode_t Mode );

  static void Debug_LED_State_WRONG_PARAM_Failed( DebugLEDEnvMode_t Mode );
#endif // __DEBUG_LEVEL_2__



#if 1
  #if (__ARMCC_VERSION >= 6010050)            /* 使用AC6编译器时 */
  __asm(".global __use_no_semihosting\n\t");  /* 声明不使用半主机模式 */
  __asm(".global __ARM_use_no_argv \n\t");    /* AC6下需要声明main函数为无参数格式，否则部分例程可能出现半主机模式 */

  #else
  /* 使用AC5编译器时, 要在这里定义__FILE 和 不使用半主机模式 */
  #pragma import(__use_no_semihosting)
#endif

  struct __FILE
  {
      int handle;
          
  };
  FILE __stdout;

  int _ttywrch(int ch)
  {
      ch = ch;
      return ch;
  }

  void _sys_exit(int x)
  {
      x = x;
  }

  char *_sys_command_string(char *cmd, int len)
  {
      return NULL;
  }

  /* 重定向 printf. 线程安全 */
  int fputc(int ch, FILE *f) {
      (void)f;

      xSemaphoreTake(vMutex_Debug, portMAX_DELAY);

      if ( HAL_USART_Transmit(&husart3, (uint8_t*)&ch, 1, 500) != HAL_OK )
      {
        /* Error Handler. */

        xSemaphoreGive(vMutex_Debug);
      }

      xSemaphoreGive(vMutex_Debug);

      return ch;
    }
#endif

#if defined(__DEBUG_LEVEL_1__)
  ErrorStatus Debug_USART_Init( void )
  {
    __HAL_RCC_USART3_CLK_ENABLE();

    husart3.Instance = DEBUG_USART;
    husart3.Init.BaudRate = DEBUG_BAUDRATE;
    husart3.Init.Mode = USART_MODE_TX_RX;
    husart3.Init.Parity = USART_PARITY_NONE;
    husart3.Init.StopBits = USART_STOPBITS_1;
    husart3.Init.WordLength = USART_WORDLENGTH_8B;

    if ( HAL_USART_Init(&husart3) != HAL_OK )
    {
      /* Error Handler. */
      #if defined(__DEBUG_LEVEL_2__)
        Debug_LED_Dis(DEBUG_INIT_FAILED, COMN_VER);
      #endif // 

      return ERROR;
    }

    HAL_USART_Transmit(&husart3, "Debug_USART Init OK!\n", sizeof("Debug_USART Init OK!\n") - 1, 200);

    vMutex_Debug = xSemaphoreCreateMutex();

    if ( vMutex_Debug == NULL )
    {
      printf("Get Mutex Failed in bsp_usart_debug.c!\n");

      #if defined(__DEBUG_LEVEL_2__)
        Debug_LED_Dis(DEBUG_SOURCE_GET_FAILED, COMN_VER);
      #endif // __DEBUG_LEVEL_2__

      return ERROR;
    }

    return SUCCESS;
  }
#endif // __DEBUG_LEVEL_1__


#if defined(__DEBUG_LEVEL_2__)

  void Debug_Led_Init( void )
  {
    GPIO_Clk_Enable(DEBUG_LED_PORT);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStructure.Pin = DEBUG_LED_PIN;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DEBUG_LED_PORT, &GPIO_InitStructure);

    GPIO_PinState offLevel = 
      ( DEBUG_LED_ACTIVE_LEVEL == GPIO_PIN_SET )? GPIO_PIN_RESET : GPIO_PIN_SET;
    
    HAL_GPIO_WritePin(DEBUG_LED_PORT, DEBUG_LED_PIN, offLevel);
  }


  static inline void Debug_LED_On( void )
  {
    uint32_t offset;

    if ( DEBUG_LED_ACTIVE_LEVEL == GPIO_PIN_SET )
    {
      offset = DEBUG_LED_PIN;
    }
    else if ( DEBUG_LED_ACTIVE_LEVEL == GPIO_PIN_RESET )
    {
      offset = DEBUG_LED_PIN << 16;
    }

    GPIOA -> BSRR |= offset;
  }




  static inline void Debug_LED_Off( void )
  {
    uint32_t offset;

    if ( DEBUG_LED_ACTIVE_LEVEL == GPIO_PIN_SET )
    {
      offset = DEBUG_LED_PIN << 16;
    }
    else if ( DEBUG_LED_ACTIVE_LEVEL == GPIO_PIN_RESET )
    {
      offset = DEBUG_LED_PIN;
    }

    GPIOA -> BSRR |= offset;
  }



  /**
   * @brief  LED 调式状态指示:  初始化失败 (三短一长).
   * @param  Mode:  指示调用位置的工作环境. 
   *        可以为以下参数: 
   *              COMN_VER  裸机环境(RTOS未启动).
   *              RTOS_VER  实时操作系统环境下.
   *                
   * @note  NULL.
   *         
 */
  static void Debug_LED_State_INIT_Failed( DebugLEDEnvMode_t Mode )
  {
    if ( Mode == COMN_VER )
    {
      for( ; ; )
      {
        for(uint8_t j = 0; j < 3; j++)
        {
          Debug_LED_On();
  
          HAL_Delay(DEBUG_LED_STATE_SHORT);
    
          Debug_LED_Off();
    
          HAL_Delay(DEBUG_LED_STATE_SHORT);
        }
  
        Debug_LED_On();
  
        HAL_Delay(DEBUG_LED_STATE_LONG);
  
        Debug_LED_Off();
  
        HAL_Delay(DEBUG_LED_STATE_CYCLE);
      }
    }
    else 
    {
      for( ; ; )
      {
        for(uint8_t j = 0; j < 3; j++)
        {
          Debug_LED_On();
  
          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));
    
          Debug_LED_Off();
    
          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));
        }
  
        Debug_LED_On();
  
        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_LONG));
  
        Debug_LED_Off();
  
        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_CYCLE));
      }
    }
  }



  /**
   * @brief  LED 调式状态指示:  资源获取失败(互斥信号量请求失败,内存分配失败等) (四短).
   * @param  Mode:  指示调用位置的工作环境. 
   *        可以为以下参数: 
   *              COMN_VER  裸机环境(RTOS未启动).
   *              RTOS_VER  实时操作系统环境下.
   *                
   * @note  NULL.
   *         
 */
  static void Debug_LED_State_SOURCE_GET_Failed( DebugLEDEnvMode_t Mode )
  { 
    if ( Mode == COMN_VER )
    {
      for( ; ; )
      {
        for(uint8_t j = 0; j < 4; j++)
        {
          Debug_LED_On();
  
          HAL_Delay(DEBUG_LED_STATE_SHORT);
  
          Debug_LED_Off();
  
          HAL_Delay(DEBUG_LED_STATE_SHORT);
        }
  
        HAL_Delay(DEBUG_LED_STATE_CYCLE);
      }
    }

    if ( Mode == RTOS_VER )
    {
      for( ; ; )
      {
        for(uint8_t j = 0; j < 4; j++)
        {
          Debug_LED_On();
  
          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));
  
          Debug_LED_Off();
  
          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));
        }
  
        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_CYCLE));
      }
    }
  }


  /**
   * @brief  LED 调式状态指示:  硬件故障 (一长 三短).
   * @param  Mode:  指示调用位置的工作环境. 
   *        可以为以下参数: 
   *              COMN_VER  裸机环境(RTOS未启动).
   *              RTOS_VER  实时操作系统环境下.
   *                
   * @note  NULL.
   *         
 */
  static void Debug_LED_State_HARD_FAULT_Failed( DebugLEDEnvMode_t Mode )
  {
    if ( Mode == COMN_VER )
    {
      for( ; ; )
      {
        Debug_LED_On();

        HAL_Delay(DEBUG_LED_STATE_LONG);

        Debug_LED_Off();

        HAL_Delay(DEBUG_LED_STATE_LONG);

        for(uint8_t j = 0; j < 3; j++)
        {
          Debug_LED_On();

          HAL_Delay(DEBUG_LED_STATE_SHORT);

          Debug_LED_Off();

          HAL_Delay(DEBUG_LED_STATE_SHORT);
        }

        HAL_Delay(DEBUG_LED_STATE_CYCLE);
      }
    }

    if (Mode == RTOS_VER )
    {
      for( ; ; )
      {
        Debug_LED_On();

        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_LONG));

        Debug_LED_Off();

        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_LONG));

        for(uint8_t j = 0; j < 3; j++)
        {
          Debug_LED_On();

          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));

          Debug_LED_Off();

          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));
        }

        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_CYCLE));
      }
    }
  }



  /**
   * @brief  LED 调式状态指示:  函数传参不合法 (两长 两短).
   * @param  Mode:  指示调用位置的工作环境. 
   *        可以为以下参数: 
   *              COMN_VER  裸机环境(RTOS未启动).
   *              RTOS_VER  实时操作系统环境下.
   *                
   * @note  NULL.
   *         
 */
  static void Debug_LED_State_WRONG_PARAM_Failed( DebugLEDEnvMode_t Mode )
  {
    if ( Mode == COMN_VER )
    {
      for( ; ; )
      {
        for(uint8_t j = 0; j < 2; j++)
        {
          Debug_LED_On();

          HAL_Delay(DEBUG_LED_STATE_LONG);
  
          Debug_LED_Off();

          HAL_Delay(DEBUG_LED_STATE_LONG);
        }

        for(uint8_t j = 0; j < 2; j++)
        {
          Debug_LED_On();

          HAL_Delay(DEBUG_LED_STATE_SHORT);

          Debug_LED_Off();

          HAL_Delay(DEBUG_LED_STATE_SHORT);
        }

        HAL_Delay(DEBUG_LED_STATE_CYCLE);
      }
    }

    if (Mode == RTOS_VER )
    {
      for( ; ; )
      {
        for(uint8_t j = 0; j < 2; j++)
        {
          Debug_LED_On();

          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_LONG));
  
          Debug_LED_Off();

          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_LONG));
        }

        for(uint8_t j = 0; j < 2; j++)
        {
          Debug_LED_On();

          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));

          Debug_LED_Off();

          vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_SHORT));
        }

        vTaskDelay(pdMS_TO_TICKS(DEBUG_LED_STATE_CYCLE));
      }
    }
  }



  void Debug_LED_Dis( DebugLedState_t State, DebugLEDEnvMode_t Mode )
  {
    switch ( State )
    {
    case DEBUG_INIT_FAILED: 
      Debug_LED_State_INIT_Failed( Mode );
      break;

    case DEBUG_SOURCE_GET_FAILED:
      Debug_LED_State_SOURCE_GET_Failed( Mode );
      break;
    
    case DEBUG_HARD_FAULT:
      Debug_LED_State_HARD_FAULT_Failed( Mode );
      break;

    case DEBUG_WRONG_PARAM:
      Debug_LED_State_WRONG_PARAM_Failed( Mode );
      break;
    
    default:
      break;
    }
  }


#endif // __DEBUG_LEVEL_2__

#endif // __REALEASE__

