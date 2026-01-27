#ifndef __FLASH_LOG_H
#define __FLASH_LOG_H

#include "stm32f4xx_hal.h"
#include "bsp_usart_debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>


#define LOG_ON                    ( 0x5AA55AA5UL )
#define LOG_OFF                   ( 0xA5A55A5AUL )

#define LOG_FLASH_START_ADDR      ( 0x08060000 ) 
#define LOG_FLASH_SIZE            ( 0x00010000 ) // 64kb
#define LOG_FLASH_END             ( LOG_FLASH_START_ADDR + LOG_FLASH_SIZE )
#define LOG_SECTOR                ( FLASH_SECTOR_7 )
#define OFFSET_ERASEFLAG          ( 4 )
#define ERASE_FLAG_MAGIC_WORD     ( 0x5A5A5A5AUL )
#define LOG_VALID_MAGIC_FLAG      ( 0xA5A5A5A5UL )
#define LOG_PANIC_MAGIC           ( 0xCAFEBABEUL )
#define LOG_PANIC_ADDR            ( BKPSRAM_BASE )
#define __LOG__FLASH__ALIGNMENT   ( 4 )

#define LOG_IS_FULL_PERCENT       ( 0x5F ) // 95
#define LOG_MUTEX_BLOCK_TIME      ( 50UL )
#define MAX_LOG_NUM               ( 900UL )
#define FLASH_LOG_RETRY_COUNT     ( 3UL )
#define DEFAULT_READNUM           ( 4UL )





/**
 * @brief   日志级别枚举类型
 * @details 定义了系统中可用的日志严重程度等级，用于标识每条日志的重要性和类型。
 *          级别从低到高排列，数值越大表示问题越严重。
 *          可用于运行时过滤日志输出（如只保存 WARNING 及以上级别的日志）。
 *
 * @note    从 DEBUG 开始编号为 1，便于在某些场景下将 0 视为 "无日志" 或 "未初始化" 状态。
 */
typedef enum 
{
  LOG_DEBUG = 1,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR
} LogLevel_t;



/**
 * @brief   日志条目数据结构
 * @details 定义一条日志记录的基本组成字段，包含时间、级别、来源任务和消息内容。
 *          所有通过 flash_log_write() 写入的日志都应使用此格式，确保一致性与可解析性。
 *
 * @note    该结构体总大小为 sizeof(uint32_t) + sizeof(LogLevel_t) + 16 + 46 + 4 = 72 字节，
 *          
 */
typedef struct 
{
  uint32_t timeStamp;
  LogLevel_t level;
  uint8_t  pad[3]; // 手动补齐字节。将其对齐到4字节  
  char taskName[16];
  char message[44];
  uint32_t valid_flag;
} LogType_t;
// 编译期检查. 如果结构体未对齐，则编译失败并输出名字.
#define STATIC_ASSERT(expr, msg) typedef char msg[(expr) ? 1 : -1]
STATIC_ASSERT((sizeof(LogType_t) % 4) == 0, log_struct_not_4byte_aligned);




/**
 * @brief   向 Flash 日志系统写入一条格式化日志（推荐使用的日志接口宏）
 *
 * @details 该宏用于在代码中便捷地记录带时间戳、任务名和格式化消息的日志条目，
 *          并将其持久化存储至 Flash。它是 _log_write_impl() 函数的安全封装。
 *
 *          使用示例：
 *          ~~~c
 *          LOG_WRITE(LOG_INFO, "MAIN", "System started, version: %s", "v1.0");
 *          LOG_WRITE(LOG_WARNING, "SENSOR", "Temperature high: %d°C", temp);
 *          ~~~
 *
 * @param   level       日志级别，取值为 LogLevel_t 枚举类型：
 *                      - LOG_DEBUG     调试信息（最低级别）
 *                      - LOG_INFO      普通运行信息
 *                      - LOG_WARNING   警告状态（不影响功能）
 *                      - LOG_ERROR     错误事件（功能异常）
 *
 * @param   taskName    字符串字面量或字符指针，表示当前日志来源的任务/模块名称。
 *                      建议长度 ≤15 字符，自动截断并补 '\0'。
 *
 * @param   fmt         格式化字符串，支持标准 printf 风格占位符（如 %d, %s, %f 等）
 *
 * @param   ...         可选变参列表，对应于 fmt 中的占位符
 *
 * @note
 *   - 此宏是线程安全的，可在任意任务上下文中调用（但不支持中断 ISR）
 *   - 若日志系统未启用（Log_Disable()），则宏会静默丢弃日志
 *   - 内部使用 vsnprintf，确保不会发生缓冲区溢出
 *   - 支持 Keil/IAR/GCC 编译器（通过 ##__VA_ARGS__ 兼容空变参）
 *
 * @warning
 *   - 不可在中断服务程序（ISR）中调用，因为底层涉及 va_list 和互斥锁操作
 *   - 过度频繁调用可能影响系统实时性，请合理控制日志密度
 *   - fmt 参数不能为空或非法指针，否则行为未定义
 *
 * @see     _log_write_impl(), Log_Flash_Write(), LogLevel_t, LogType_t
 */
#define LOG_WRITE(level, taskName, fmt, ...) \
    _log_write_impl((level), (taskName), (fmt), ##__VA_ARGS__)



/**
 * @brief   Flash 日志系统运行状态信息结构体
 *
 *          该结构体用于对外暴露日志模块的当前使用状态，
 *          可供上层任务（如命令行接口、GUI 监控、自检程序）查询使用。
 */
typedef struct
{
  uint16_t logNum;            // 当前已存储的有效日志条目总数.
  uint32_t used_bytes;        // 已使用的 Flash 存储空间（字节）.
  uint32_t free_bytes;        // 剩余可用的存储空间（字节）.
  uint16_t remain_logNum;     // 还剩下多少多少条日志可以写入.
  uint8_t utilization_rate;   // 使用率百分比.
  bool is_full;               // 日志区域是否已满.
} LogStatus_t;





/*  ******************************************   */
void _log_write_impl(LogLevel_t level, const char* taskName, const char* fmt, ...);

bool Log_Flash_Write( const LogType_t *log_event );

bool Log_GetAtIndex( uint16_t index, LogType_t *Log_WhetherSucceededToBeAcquired );

bool Log_GetLatestN( uint16_t n, uint32_t *flash_addr );

bool Log_IsValid( const LogType_t *__log );

bool Log_SelfTest( void );

const LogStatus_t* Log_GetStatus( void );

void Log_UpdateStatus( void );

void Log_Flash_ClearLogMes( void );

void Log_Flash_Init( void );

void Log_Enable( void );

void Log_Disable( void );

void Log_PanicWrite( const char *taskName, const char *reason );

uint16_t Log_ReadLatest( uint16_t ReadNum, LogType_t *buffer, uint16_t buffer_capacity );

uint16_t Log_GetCount( void );
/*  ******************************************   */

#endif // __FLASH_LOG_H
