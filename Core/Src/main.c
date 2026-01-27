#include "main.h"


int main( void )
{
	HAL_Init();

	Log_Enable();

	// 开启对BKPSRAM的访问.
	{
		__HAL_RCC_PWR_CLK_ENABLE();

		__HAL_RCC_BKPSRAM_CLK_ENABLE();

		HAL_PWR_EnableBkUpAccess();

		HAL_PWR_EnableBkUpReg();
	}

#if defined(__DEBUG_LEVEL_2__)
	Debug_Led_Init();
#endif // __DEBUG_LEVEL_2__

#if defined(__DEBUG_LEVEL_1__)
	Debug_USART_Init();
#endif // __DEBUG_LEVEL_1__

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
		// 正常运行流程下程序不会执行到此处.
		printf("Error! Task Running Error!\n");
	}
	
}
