#include "main.h"


int main( void )
{
	HAL_Init();

	Debug_Led_Init();

	Debug_USART_Init();

	while(1)
	{
		printf("Heelo.\n");

		HAL_Delay(1000);
	}
	
}
