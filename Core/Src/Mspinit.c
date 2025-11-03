#include "Mspinit.h"


void HAL_USART_MspInit( USART_HandleTypeDef *husart )
{
  /* 调式串口底层GPIO初始化. */
  if ( husart -> Instance == DEBUG_USART )
  {
    GPIO_Clk_Enable(DEBUG_USART_PORT);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.Alternate = GPIO_AF7_USART3;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Pin = DEBUG_USART_TX_PIN;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DEBUG_USART_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
    GPIO_InitStructure.Pin = DEBUG_USART_RX_PIN;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DEBUG_USART_PORT, &GPIO_InitStructure);
  }

}


void HAL_UART_MspInit( UART_HandleTypeDef *huart )
{
  /* ESP8266 通信串口GPIO初始化 .*/
  if ( huart -> Instance == UART4 )
  {
    GPIO_Clk_Enable(ESP_UART_PORT);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.Alternate = GPIO_AF8_UART4;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Pin = ESP_UART_TX;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ESP_UART_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.Alternate = GPIO_AF8_UART4;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Pin = ESP_UART_RX;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ESP_UART_PORT, &GPIO_InitStructure);
  }
}


