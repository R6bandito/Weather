#include "main.h"


int main( void )
{
	HAL_Init();

	Debug_Led_Init();

	Debug_USART_Init();

	if ( !UART4_Init() )
	{
		#if defined(__DEBUG_LEVEL_1__)
			printf("UART Init Failed!\n");
		#endif // __DEBUG_LEVEL_1__

		#if defined(__DEBUG_LEVEL_2__)
			Debug_LED_Dis(DEBUG_INIT_FAILED, RTOS_VER);
		#endif // __DEBUG_LEVEL_2__

    // 短暂延时确保信息全部输出完毕.
    HAL_Delay(100);

    // 复位.
    NVIC_SystemReset();
	}

	vEspInit_TaskCreate();

	vTaskStartScheduler();

	while(1)
	{
		printf("Heelo.\n");

		HAL_Delay(1000);
	}
	
}
