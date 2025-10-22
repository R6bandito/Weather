#ifndef __TASK_EXA_H
#define __TASK_EXA_H

#include "stm32f4xx_hal.h"
#include "bsp_usart_debug.h"
#include "FreeRTOS.h"
#include "task.h"

#define TASK1_DEPTH   128
#define TASK2_DEPTH   128

#define TASK1_PRIO    2
#define TASK2_PRIO    2


void create_Task1( void );

void create_Task2( void );


#endif // __TASK_EXA_H
