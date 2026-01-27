#include "rtos_hook.h"


void vApplicationTickHook( void )
{
  static uint16_t tickCount = 0;

  // 2s 更新日志.
  if ( ++tickCount >= 2000 )
  {
    tickCount = 0;

    Log_UpdateStatus();
  }
}



void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
  // 关闭任务调度器及关中断.
  taskDISABLE_INTERRUPTS();

  __disable_irq();

  static char *temp_ptr = pcTaskGetName;

  // 串口打印调试信息.
  #if defined(__DEBUG_LEVEL_1__)
    static char buffer[64] = { 0 };

    debug_puts_direct("=== STACK OVERFLOW ===\n");

    if ( !pcTaskName )
    {
      temp_ptr = "Unknown";
    }

    Log_PanicWrite( temp_ptr, "vApplicationHook Trigged! Stack OverFlow!\n" );

    snprintf(buffer, sizeof(buffer), "TaskName: %s\n", temp_ptr);

    debug_puts_direct(buffer);

    memset(buffer, 0, sizeof(buffer));

    snprintf(buffer, sizeof(buffer), "Handle: 0x%p\n", xTask);

    debug_puts_direct(buffer);

    if ( xTask != NULL )
    {
      UBaseType_t prio = uxTaskPriorityGet(xTask);

      memset(buffer, 0, sizeof(buffer));

      snprintf(buffer, sizeof(buffer), "Priority: %u\n", prio);

      debug_puts_direct(buffer);

      memset(buffer, 0, sizeof(buffer));
    }
  #endif 

  #if defined(__DEBUG_LEVEL_2__)
    Debug_LED_Dis( DEBUG_STACK_OVERFLOW, COMN_VER );
  #endif

  // 栈溢出，接下来的运行情况不可预测. 因此与此处设死循环阻止后续运行.
  for( ; ; )
  {
    __nop();
  }
}

