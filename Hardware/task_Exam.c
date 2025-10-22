#include "task_Exam.h"


void vtask1( void * parameters )
{

  for( ; ; )
  {
    printf("vTask1 is RUNNING!\n");

    vTaskDelay(250);
  }
}


void vtask2( void * parameters )
{

  for( ; ; )
  {
    printf("vTask2 is RUNNING!\n");

    vTaskDelay(250);
  }
}


void create_Task1( void )
{
  BaseType_t err;

  err = xTaskCreate((TaskFunction_t)vtask1, "Task1", TASK1_DEPTH, NULL, TASK1_PRIO, NULL);

  if ( err != pdPASS )
  {
    printf("Create Task1 Failed\n");
  }
}


void create_Task2( void )
{
  BaseType_t err;

  err = xTaskCreate((TaskFunction_t)vtask2, "Task2", TASK2_DEPTH, NULL, TASK2_PRIO, NULL);

  if ( err != pdPASS )
  {
    printf("Create Task2 Failed\n");
  }
}

