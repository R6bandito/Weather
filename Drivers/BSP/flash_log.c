#include "flash_log.h"

LogType_t log_manager;

// 全局维护一个地址指针.
static uint32_t write_address = ( LOG_FLASH_START_ADDR + OFFSET_ERASEFLAG );

// 擦除标志(魔数验证).
static uint32_t erase_flag_Addr = LOG_FLASH_START_ADDR;

// 全局日志系统开关变量(默认打开日志).
uint32_t g_log_enable = LOG_ON;

/*  **********************************   */
static void Log_Flash_Erase( void );
bool Log_Flash_Write( const LogType_t *log_event );
void Log_Flash_Init( void );
void Log_Flash_ClearLogMes( void );
void Log_Enable( void );
void Log_Disable( void );
/*  **********************************   */


void Log_Enable( void )
{
  if ( g_log_enable != LOG_ON )
  {
    g_log_enable = LOG_ON;
  }
  else 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Log System Has Already Ran.\n");
    #endif // __DEBUG_LEVEL_1__
  }
}


void Log_Disable( void )
{
  if ( g_log_enable != LOG_OFF )
  {
    g_log_enable = LOG_OFF;
  }
  else 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Log System Has Already turned off.\n");
    #endif //__DEBUG_LEVEL_1__
  }
}



/**
 * @brief 清除日志区域：擦除整个扇区
 *        
 * @note 注：该方法内部使用，不开放接口. 具体调用请参考Log_Flash_ClearLogMes.
 */
static void Log_Flash_Erase( void )
{
  HAL_StatusTypeDef status;
  uint32_t SectorError;
  FLASH_EraseInitTypeDef flash_erase = { 0 };

  if ( HAL_FLASH_Unlock() != HAL_OK )
  {
    // flash解锁失败.
    #if defined(__DEBUG_LEVEL_1__)
      printf("flash unlock failed! \n");
    #endif // __DEBUG_LEVEL_1__

    return;
  }

  flash_erase.TypeErase = TYPEERASE_SECTORS;
  flash_erase.Banks = FLASH_BANK_1;
  flash_erase.NbSectors = 1;
  flash_erase.Sector = LOG_SECTOR;
  flash_erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;  

  __HAL_FLASH_DATA_CACHE_DISABLE();//FLASH操作期间，必须禁止数据缓存
  status = HAL_FLASHEx_Erase(&flash_erase, &SectorError);
  __HAL_FLASH_DATA_CACHE_ENABLE();//开启数据缓存

  // 向前4字节写入标志。表示日志扇区已被擦除，可使用.
  HAL_FLASH_Program(TYPEPROGRAM_WORD, erase_flag_Addr, ERASE_FLAG_MAGIC_WORD);
 

  if ( status != HAL_OK )
  {
    // 擦除失败，立即返回并向调试串口打印数据.
    #if defined(__DEBUG_LEVEL_1__)
      printf("Erase Sector Failed!\n");
    #endif // __DEBUG_LEVEL_1__

    goto exit;
  }

exit:
  HAL_FLASH_Lock();

  return;
}



/**
 * @brief  清除所有已存储的日志数据：擦除日志所在的 Flash 扇区，并重置写入指针
 *
 *         该函数会：
 *         1. 检查魔数标志，确认日志区域是否已被初始化；
 *         2. 若已初始化，则执行 Flash 扇区擦除操作；
 *         3. 擦除后重新设置起始写地址，并调用初始化函数恢复状态。
 *
 *         调用此函数后，原有所有日志将被永久清除，系统进入“空日志”状态。
 *
 * @note   此 API 用于外部主动触发日志清空（如用户命令、出厂复位等）。
 *         内部实际通过调用静态函数 Log_Flash_Erase() 完成擦除操作。
 *         即使日志系统未启用或未初始化，调用本函数也不会造成硬件异常。
 *
 * @warning
 *         - 该操作不可逆！一旦调用，所有历史日志将丢失；
 *         - 执行期间会短暂解锁 Flash，需避免与其他 Flash 操作并发；
 *         - 建议在无日志写入任务运行时调用（如暂停 logger task）。
 *
 * @return void 无返回值
 */
void Log_Flash_ClearLogMes( void )
{
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGAERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGPERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGSERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_WRPERR);

  uint32_t erase_flagVal = *(volatile uint32_t *)erase_flag_Addr;

  if ( erase_flagVal != ERASE_FLAG_MAGIC_WORD )
  {
    // 魔数验证失败，表示这块区域并未被日志系统使用. 直接返回.
    // 即：当日志系统被关闭时，调用该API将无任何效果.
    return;
  }

  taskENTER_CRITICAL();
  // 执行擦除.
  Log_Flash_Erase();
  taskEXIT_CRITICAL();

  // 重置地址指针状态.
  write_address = LOG_FLASH_START_ADDR + OFFSET_ERASEFLAG;

  Log_Flash_Init();
}




/**
 * @brief  将一条日志数据写入指定的 Flash 区域
 *
 * @param  log_event   指向待写入日志结构体的常量指针
 * @return bool        写入成功返回 true；失败（空间不足、解锁失败、编程失败）返回 false
 *
 * @note
 * - 当前以 WORD（32-bit）为单位进行编程，要求：
 *   1. LogType_t 结构体大小必须是 4 字节对齐（即 sizeof(LogType_t) % 4 == 0）
 *   2. 目标 Flash 地址也必须是 32-bit 对齐（由 write_address 保证）
 * - 写入过程中若任意一次 HAL_FLASH_Program 失败，将立即终止并返回 false
 * - 只有完全成功后才会更新 write_address 指针，确保原子性
 * - 使用前后需确保 Flash 区域已正确擦除
 */
bool Log_Flash_Write( const LogType_t *log_event )
{
  if ( g_log_enable == LOG_OFF ) 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Write the Flash when the Log System close is not allowed\n");
    #endif  

    return false;
  }

  uint32_t base_addr = write_address;

  // 检验写入地址是否已经对齐.
  if ( ( base_addr % __LOG__FLASH__ALIGNMENT ) != 0 )
  {
    // 地址未对齐，先进行对齐.
    uint32_t aligned = ( write_address + __LOG__FLASH__ALIGNMENT - 1 ) & ~(__LOG__FLASH__ALIGNMENT - 1); 

    base_addr = aligned;
  }

  HAL_StatusTypeDef status = HAL_BUSY;

  // 必须先进行一次擦除操作，将Sector7全部擦除干净才能进行写入操作.
  uint32_t flagVal = *(volatile uint32_t *)erase_flag_Addr;
  if ( flagVal != ERASE_FLAG_MAGIC_WORD )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Log flash not initialized or not erased!\n");
    #endif 

    return false;
  }

  // 日志区写满，不写入直接返回.
  if ( ( base_addr + sizeof(LogType_t) ) > LOG_FLASH_END )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Log has No Enough Space to Write.\n");
    #endif 

    return false;
  }

  status = HAL_FLASH_Unlock();
  if ( status != HAL_OK )
  {
    // flash解锁失败.
    #if defined(__DEBUG_LEVEL_1__)
      printf("flash unlock failed!\n");
    #endif 

    return false;
  }
  status = HAL_BUSY;

  // 以 Word 为单位进行写入.
  const uint32_t *data = (const uint32_t *)log_event;

  uint16_t word_count = ( sizeof(LogType_t) - 4 ) / 4; // 最后的valid_flag四字节单独拿出来写.

  taskENTER_CRITICAL();
  // 写日志主体内容.
  for(uint16_t j = 0; j < word_count; j++)
  {
    status = HAL_FLASH_Program(TYPEPROGRAM_WORD, ( base_addr + j * 4), data[j]); 

    if ( status != HAL_OK )
    {
      break;
    }
  }
  // 在该此日志数据后加上验证标志.
  if ( HAL_FLASH_Program(TYPEPROGRAM_WORD, base_addr + sizeof(LogType_t) - 4, LOG_VALID_MAGIC_FLAG) != HAL_OK )
  {
    taskEXIT_CRITICAL();

    HAL_FLASH_Lock();

    return false;
  }
  taskEXIT_CRITICAL();

  if ( status == HAL_OK )
  {
    // 写入完毕后手动偏移指针到下一个写入地址.
    write_address += sizeof(LogType_t);
  }
  else 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("flash write failed!\n");
    #endif 

    HAL_FLASH_Lock();

    return false;
  }

  HAL_FLASH_Lock();

  return true;
}




/**
 * @brief  初始化 Flash 日志系统
 *
 *         该函数负责初始化用于存储日志的 Flash 区域。主要执行以下操作：
 *         1. 检查魔数标志位，若未初始化则调用擦除函数清空日志区；
 *         2. 确保写入地址（write_address）满足 Flash 写入对齐要求；
 *         3. 扫描已写入的日志条目，定位下一个有效写入位置。
 *
 *         调用此函数后，write_address 将指向第一个空闲且对齐的日志存储位置，
 *         后续日志可从此地址开始写入。
 *
 * @warning
 *         - 必须确保 LOG_FLASH_START 到 LOG_FLASH_END 地址范围正确且属于用户可用 Flash 区；
 *         - 多次调用是安全的，可用于重启或恢复场景；
 *         - 不处理 Flash 写保护或硬件错误，需上层保证。
 */
void Log_Flash_Init( void )
{
  // 检验该区块flash是否已被初始化过（通过检查魔数）.
  uint32_t magic_flagVal = *(volatile uint32_t *)erase_flag_Addr;

  if ( magic_flagVal != ERASE_FLAG_MAGIC_WORD )
  {
    // flash没被初始化过，执行擦除.
    Log_Flash_Erase();
  }

Retry:
  // 检验写入地址是否已经对齐.
  if ( ( write_address % __LOG__FLASH__ALIGNMENT ) != 0 )
  {
    // 地址未对齐，先进行对齐.
    uint32_t aligned = ( write_address + __LOG__FLASH__ALIGNMENT - 1 ) & ~(__LOG__FLASH__ALIGNMENT - 1); 

    write_address = aligned;

    goto Retry;
  }

  uint32_t addr = write_address;

  // 移动指针到待写入位置.
  while( addr < LOG_FLASH_END )
  {
    const LogType_t *log = (const LogType_t*)addr;

    if ( log -> valid_flag != LOG_VALID_MAGIC_FLAG )
    {
      // 验证字段无效.表示此处未写入.
      break;
    }

    addr += sizeof(LogType_t);
  }

  write_address = addr;
 
  return;
}

