#include "Perp_clkEn.h"



/**
 * @brief  使能指定GPIO端口的时钟
 * @param  __gpio: 指向GPIO端口的指针，例如 GPIOA, GPIOB, ...
 * @return 无
 * @note   
 */
void GPIO_Clk_Enable( GPIO_TypeDef * __gpio )
{
  if ( __gpio == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      /* Send Error Info. */
      printf("Wrong param In GPIO_clk_Enable.\n");

    #endif //__DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      /* Led Status. */

    #endif // __DEBUG_LEVEL_2__

    return;
  }

  if ( __gpio == GPIOA )
  {
    RCC -> AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    __DSB();  return;
  }

  if ( __gpio == GPIOB )
  {
    RCC -> AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

    __DSB();  return;
  }

  if ( __gpio == GPIOC )
  {
    RCC -> AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

    __DSB();  return;
  }

  if ( __gpio == GPIOD )
  {
    RCC -> AHB1ENR |= RCC_AHB1ENR_GPIODEN;

    __DSB();  return;
  }

  #if defined(GPIOPORTS_HAVE_MORE)
  /* 
    如有更多的GPIO口支持，在启用该宏后自行添加。 
    If have more GPIO ports, adding these new ports to following content after 
    define this flag:  GPIOPORTS_HAVE_MORE 
  */
    if ( __gpio == GPIOE )
    {

    }

  #endif // GPIOPORTS_HAVE_MORE
}


/**
 * @brief  使能指定定时器的外设时钟。
 * @param  __hptim: 指向TIM_TypeDef类型的指针，指定要使能时钟的定时器实例。
 *                可以是TIM1, TIM2, TIM3, ..., TIM14等。
 * @note   此函数直接操作RCC寄存器，不使用HAL库。
 *         
 */
void TIM_Clk_Enable( TIM_TypeDef *__hptim )
{
  if ( __hptim == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      /* Send Error Info. */
      printf("Wrong param of TIM_Clk_Enable in the Perp_clkEn.c\n");
    #endif //__DEBUG_LEVEL_1__
      Debug_LED_Dis(DEBUG_WRONG_PARAM, COMN_VER);
    #if defined(__DEBUG_LEVEL_2__)
      /* Led Status. */

    #endif // __DEBUG_LEVEL_2__
    
    return;
  }

  if ( __hptim == TIM1 )
  {
    RCC -> APB2ENR |= RCC_APB2ENR_TIM1EN;

    __DSB();  return;
  }

  if ( __hptim == TIM2 )
  {
    RCC -> APB1ENR |= RCC_APB1ENR_TIM2EN;

    __DSB();  return;
  }

  if ( __hptim == TIM3 )
  {
    RCC -> APB1ENR |= RCC_APB1ENR_TIM3EN;

    __DSB();  return;
  }

  if ( __hptim == TIM4 )
  {
    RCC -> APB1ENR |= RCC_APB1ENR_TIM4EN;

    __DSB();  return;
  }

  if ( __hptim == TIM6 )
  {
    RCC -> APB1ENR |= RCC_APB1ENR_TIM6EN;

    __DSB();  return;
  }

  if ( __hptim == TIM8 )
  {
    RCC -> APB2ENR |= RCC_APB2ENR_TIM8EN;

    __DSB();  return;
  }

  #if defined(TIM_HAVE_MORE)
  /* 
    如有更多的TIM需要支持，在启用该宏后自行添加。 
    If have more TIM that need to be suppport, adding these new TIM to following content after 
    define this flag:  TIM_HAVE_MORE 
  */
    if ( __hptim == TIM_? )
    {

    }

  #endif // TIM_HAVE_MORE
}




/**
 * @brief  使能指定USART的外设时钟。
 * @param  __usart: 指向USART_TypeDef类型的指针，指定要使能的USART实例。
 *                可以是USART1, USART2, USART3等。
 * @note  
 *         
 */
void USART_Clk_Enable( USART_TypeDef *__usart )
{
  if ( __usart == NULL )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong param of USART_Clk_Enable in the Perp_clkEn.c\n");
    #endif // __DEBUG_LEVEL_1__

    #if defined(__DEBUG_LEVEL_2__)
      Debug_LED_Dis(DEBUG_WRONG_PARAM, COMN_VER);
    #endif // __DEBUG_LEVEL_2__

    return;
  }

  if ( __usart == USART1 )
  {
    RCC -> APB2ENR |= RCC_APB2ENR_USART1EN;

    __DSB();  return;
  }

  if ( __usart == USART2 )
  {
    RCC -> APB1ENR |= RCC_APB1ENR_USART2EN;

    __DSB();  return;
  }

  if ( __usart == USART3 )
  {
    RCC ->APB1ENR |= RCC_APB1ENR_USART3EN;

    __DSB();  return;
  }

  #if defined(USART_HAVE_MORE)
  /* 
    如有更多的USART需要支持，在启用该宏后自行添加。 
    If have more USART that need to be suppport, adding these new USART to following content after 
    define this flag:   USART_HAVE_MORE
  */
    if ( __usart == USART4 )
    {

    }
  #endif // USART_HAVE_MORE
}

