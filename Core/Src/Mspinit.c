#include "Mspinit.h"

DMA_HandleTypeDef  hdma_rx;

DMA_HandleTypeDef  hdma_tx;

extern UART_HandleTypeDef esp8266_huart;


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
     
    }
  
    __HAL_LINKDMA(&esp8266_huart, hdmarx, hdma_rx);
  
    __HAL_LINKDMA(&esp8266_huart, hdmatx, hdma_tx);
  }
}


