#include "hal_timebase.h"

TIM_HandleTypeDef htim4;

HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority);

/**
 * @brief  定时器TIM4初始化.用于给HAL的 uwTick提供时基.
 * @param  void
 * @note   该方法用于FreeRTOS启动之前,在初始化阶段配置TIM4为HAL提供时基.
 *         
 */
static void hal_timeBase_Init( void )
{
  TIM_Clk_Enable(TIM4);

  htim4.Instance = TIM4;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  htim4.Init.ClockDivision  = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 1000 - 1;
  htim4.Init.Prescaler = ( SystemCoreClock / 1000000 ) - 1;
  htim4.Init.RepetitionCounter = 0;

  if ( HAL_TIM_Base_Init(&htim4) != HAL_OK )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("TIM6 Initial Failed in hal_timebase.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)

    #endif // __DEBUG_LEVEL_2__

    return;
  }

  HAL_NVIC_SetPriority(TIM4_IRQn, 4, 0);

  HAL_NVIC_EnableIRQ(TIM4_IRQn);

  HAL_TIM_Base_Start_IT(&htim4);
}


void TIM4_IRQHandler( void )
{
  HAL_TIM_IRQHandler(&htim4);
}

void HAL_TIM_PeriodElapsedCallback( TIM_HandleTypeDef *htim )
{
  if ( htim -> Instance == TIM4 )
  {
    HAL_IncTick();
  }
}


HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    hal_timeBase_Init();

    return HAL_OK;
}

