#include "flash_log.h"


// 全局维护一个LogStatus_t 日志状态信息记录变量（对外不可见）.
static LogStatus_t log_status;

// 全局维护一个地址指针（对外不可见）.
static uint32_t write_address = ( LOG_FLASH_START_ADDR + OFFSET_ERASEFLAG );

// 擦除标志(魔数验证)（对外不可见）.
static uint32_t erase_flag_Addr = LOG_FLASH_START_ADDR;

// 全局日志系统开关变量(默认关闭日志).
uint32_t g_log_enable = LOG_OFF;

// 全局维护一个互斥锁.
static SemaphoreHandle_t xMutexLog = NULL;


/*  **********************************   */
static void Log_Flash_Erase( void );
bool Log_Flash_Write( const LogType_t *log_event );
bool Log_GetAtIndex( uint16_t index, LogType_t *Log_WhetherSucceededToBeAcquired );
bool Log_GetLatestN( uint16_t n, uint32_t *flash_addr );
bool Log_IsValid( const LogType_t *__log );
bool Log_SelfTest( void );
void Log_Flash_Init( void );
void Log_Flash_ClearLogMes( void );
void Log_Enable( void );
void Log_Disable( void );
void Log_UpdateStatus( void );
void Log_PanicWrite( const char *taskName, const char *reason );
const LogStatus_t* Log_GetStatus( void );
uint16_t Log_ReadLatest( uint16_t ReadNum, LogType_t *buffer, uint16_t buffer_capacity );
uint16_t Log_GetCount( void );
/*  **********************************   */



/**
 * @brief 启用 Flash 日志系统，完成初始化并进入可写入状态
 *
 * @details
 *   该函数用于启动日志子系统，执行以下关键操作：
 *   - 设置全局启用标志 `g_log_enable = LOG_ON`
 *   - 调用 `Log_Flash_Init()` 初始化 Flash 区域（含魔数校验、地址对齐、空闲位置定位）
 *   - 调用`Log_UpdateStatus()` 扫描 Flash 统计日志数量
 *
 * @note
 *   - 本函数是幂等的：重复调用不会导致重复初始化或异常。
 *   - 若互斥锁创建失败（`xSemaphoreCreateMutex()` 返回 NULL），日志系统将自动禁用（`g_log_enable = LOG_OFF`），
 *     并通过调试串口输出错误信息（需定义 `__DEBUG_LEVEL_1__`）。
 *   - 此函数可在任意上下文（任务/中断）中安全调用，但建议在系统初始化阶段（如 `main()` 或 `app_init()`）首次启用。
 *
 * @warning
 *   - 不应在中断服务程序（ISR）中调用该函数（因内部可能涉及动态内存分配或阻塞型 HAL 操作）。
 *   - 若 `Log_Flash_Init()` 失败（如 Flash 擦除异常），日志系统将处于未就绪状态，后续写入将被拒绝。
 *
 * @see Log_Flash_Init(), Log_Disable(), g_log_enable
 */
void Log_Enable( void )
{
  if ( g_log_enable != LOG_ON )
  {
    // 打开全局标志.
    g_log_enable = LOG_ON;

    // 初始化日志系统. 保证所有相关变量处于明确的正常运行状态.
    Log_Flash_Init();

    // 更新日志状态.
    Log_UpdateStatus();
  }
  else 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Log System Has Already Ran.\n");
    #endif // __DEBUG_LEVEL_1__
  }
}




/**
 * @brief 禁用 Flash 日志系统，停止所有日志写入行为
 *
 * @details
 *   该函数仅设置全局禁用标志 `g_log_enable = LOG_OFF`，不执行任何 Flash 擦除、内存释放或资源清理操作。
 *   已存在的日志数据保持完整，`Log_GetStatus()`、`Log_ReadLatest()` 等读取接口仍可正常使用。
 *   下次调用 `Log_Enable()` 将重新激活系统（无需再次擦除扇区）。
 *
 * @note
 *   - 本函数是轻量级、无副作用的纯状态切换操作，执行时间极短（常数时间）。
 *   - 是线程安全的：标志位为原子写入（32-bit 对齐变量，在 Cortex-M4 上保证原子性）。
 *   - 可在任务、中断或系统关机流程中安全调用。
 *
 * @warning
 *   - 禁用后，所有 `Log_Flash_Write()` 和 `LOG_WRITE(...)` 宏调用将立即返回 `false` 并丢弃日志，
 *     不会触发任何硬件操作或错误报告（除非启用了 `__DEBUG_LEVEL_1__`）。
 *   - 若需彻底清除日志数据，请显式调用 `Log_Flash_ClearLogMes()`。
 *
 * @see Log_Enable(), g_log_enable, Log_Flash_ClearLogMes()
 */
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
 * @brief   更新 Flash 日志系统的当前运行状态信息
 *
 * @details 该函数通过遍历指定 Flash 区域内的所有日志条目，
 *          检查每条记录的 valid_flag 是否为 LOG_VALID_MAGIC_FLAG，
 *          来动态统计当前已存储的有效日志数量及相关状态。
 *
 *          状态信息包括：
 *          - 已存日志条数 (logNum)
 *          - 已用/剩余存储空间 (used_bytes, free_bytes)
 *          - 存储使用率百分比 (utilization_rate)
 *          - 剩余可写入条数 (remain_logNum)
 *          - 是否已满标志位 (is_full)
 *
 * @note    本函数不依赖 write_address 指针，而是基于实际数据有效性进行实时扫描，
 *          以便即使在写入过程中发生掉电或异常，仍能准确反映真实日志数量，。
 *
 * @warning 该函数仅在日志系统启用时（g_log_enable == LOG_ON）才执行更新；
 *          若日志系统被关闭，则直接返回，不进行任何操作。
 *
 */
void Log_UpdateStatus( void )
{
  if ( g_log_enable != LOG_ON )
  {
    // 更新日志信息状态必须保持日志系统开启.
    return;
  }

  LogType_t *__ptrLog = (LogType_t*)( LOG_FLASH_START_ADDR + OFFSET_ERASEFLAG ); // 指向第一个日志条目.

  uint16_t numOfLog = 0;

  while( (uint32_t)__ptrLog < LOG_FLASH_END )
  {
    if ( __ptrLog -> valid_flag == LOG_VALID_MAGIC_FLAG )
    {
      numOfLog++;

      __ptrLog = (LogType_t*)((uint8_t*)__ptrLog + sizeof(LogType_t));  continue;
    }

    // 已计数到最后一个有效条目.直接退出循环.
    break;
  }

  BaseType_t err = xSemaphoreTake(xMutexLog, LOG_MUTEX_BLOCK_TIME);
  if ( err != pdPASS )
  {
    // 锁获取失败. 放弃此次更新.
    return;
  }

  log_status.logNum = numOfLog;

  // 计算使用字节与未使用字节.
  {
    log_status.used_bytes = numOfLog * sizeof(LogType_t); 
    log_status.free_bytes = (log_status.used_bytes >= LOG_FLASH_SIZE) ?  0 : LOG_FLASH_SIZE - log_status.used_bytes;
  }

  // 计算使用率.
  {
    log_status.utilization_rate = (uint8_t)(
      ((double)log_status.used_bytes * 100.0 / (double)LOG_FLASH_SIZE) + 0.5);
  }

  // 计算剩余可写入日志条数.
  {
    log_status.remain_logNum = MAX_LOG_NUM - log_status.logNum;
  }

  if ( log_status.utilization_rate > LOG_IS_FULL_PERCENT )
  {
    log_status.is_full = true;
  }
  else 
  {
    log_status.is_full = false;
  }

  xSemaphoreGive(xMutexLog);
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

  // 擦除完毕后更新状态.
  if ( xSemaphoreTake(xMutexLog, LOG_MUTEX_BLOCK_TIME) == pdPASS )
  {
    log_status.logNum = 0;
    log_status.remain_logNum = MAX_LOG_NUM - log_status.logNum;
    log_status.used_bytes = 0;
    log_status.free_bytes = LOG_FLASH_SIZE - log_status.used_bytes;
    log_status.utilization_rate = 0;
    log_status.is_full = false;

    xSemaphoreGive(xMutexLog);
  }

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
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGAERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGPERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_PGSERR);
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_WRPERR);

  if ( g_log_enable == LOG_OFF ) 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Write the Flash when the Log System close is not allowed\n");
    #endif  

    return false;
  }

  BaseType_t err = xSemaphoreTake(xMutexLog, LOG_MUTEX_BLOCK_TIME);
  if ( err != pdPASS )
  {
    // 未获取到锁，放弃此次写入.
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

    xSemaphoreGive(xMutexLog);

    return false;
  }

  // 日志区写满，不写入直接返回.
  if ( ( base_addr + sizeof(LogType_t) ) > LOG_FLASH_END )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Log has No Enough Space to Write.\n");
    #endif 

    xSemaphoreGive(xMutexLog);

    return false;
  }

  status = HAL_FLASH_Unlock();
  if ( status != HAL_OK )
  {
    // flash解锁失败.
    #if defined(__DEBUG_LEVEL_1__)
      printf("flash unlock failed!\n");
    #endif 

    xSemaphoreGive(xMutexLog);

    return false;
  }
  //status = HAL_BUSY;

  // 以 Word 为单位进行写入.
  const uint32_t *data = (const uint32_t *)log_event;

  uint16_t word_count = ( sizeof(LogType_t) - 4 ) / 4; // 最后的valid_flag四字节单独拿出来写.

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
    HAL_FLASH_Lock();

    xSemaphoreGive(xMutexLog);

    return false;
  }

  if ( status == HAL_OK )
  {
    // 写入完毕后手动偏移指针到下一个写入地址.
    write_address += sizeof(LogType_t);

    // 同一个锁内更新状态.
    log_status.logNum++;
    log_status.used_bytes += sizeof(LogType_t);
    log_status.free_bytes = LOG_FLASH_SIZE - log_status.used_bytes;
    log_status.remain_logNum = MAX_LOG_NUM - log_status.logNum;
    log_status.utilization_rate = (uint8_t)((double)(log_status.used_bytes) * 100.0 / (double)LOG_FLASH_SIZE + 0.5);

    if ( log_status.utilization_rate > LOG_IS_FULL_PERCENT )
    {
      log_status.is_full = true;
    }
  }
  else 
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("flash write failed!\n");
    #endif 

    HAL_FLASH_Lock();

    xSemaphoreGive(xMutexLog);

    return false;
  }

  HAL_FLASH_Lock();

  xSemaphoreGive(xMutexLog);

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
  if ( xMutexLog == NULL )
  {
    xMutexLog = xSemaphoreCreateMutex();
  }

  if ( xMutexLog == NULL )
  {
    // 锁获取失败.日志系统关闭，待手动重启.
    #if defined(__DEBUG_LEVEL_1__)
      printf("Get xMutexLog Failed! Please restart the LogSystem manually.");
    #endif // __DEBUG_LEVEL_1__

    g_log_enable = LOG_OFF;

    return;
  }

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



/**
 * @brief   获取日志系统的当前运行状态
 *
 *          返回指向内部唯一日志状态结构体的常量指针。该结构体由日志系统
 *          内部维护，只读访问，用户不得修改其内容。
 *
 * @warning 返回的指针指向内部静态变量，请在持有期间避免长时间占用；
 *          若需长期保存状态快照，请自行复制内容。
 *
 * @note    此函数是线程安全的（前提是调用上下文不破坏互斥锁机制），
 *          但在多任务环境中建议尽快完成读取操作。
 *
 * @return  const LogStatus_t* 指向日志状态结构体的有效指针
 */
const LogStatus_t* Log_GetStatus( void )
{
  return &log_status;
}



/**
 * @brief 获取第 index 条有效日志（0-indexed）
 * @param index  要读取的日志序号（如 0=第一条，9=第十条）
 * @param Log_WhetherSucceededToBeAcquired    输出参数：存放日志内容
 * @return bool 成功找到返回 true，越界或无效返回 false
 */
bool Log_GetAtIndex( uint16_t index, LogType_t *Log_WhetherSucceededToBeAcquired )
{
  if ( index > MAX_LOG_NUM )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Index Negetive in Log_GetAtIndex.\n");
    #endif // __DEBUG_LEVEL_1__

    return false;
  }

  if ( !Log_WhetherSucceededToBeAcquired )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param in Log_GetAtIndex of Log_WhetherSuccessedToBeAcquired.\n");
    #endif // __DEBUG_LEVEL_1__

    return false;
  }

  uint32_t addr = LOG_FLASH_START_ADDR + OFFSET_ERASEFLAG;
  uint16_t current_index = 0;

  while( addr < LOG_FLASH_END )
  {
    const LogType_t *__pLog = (const LogType_t*)addr;

    if ( __pLog -> valid_flag != LOG_VALID_MAGIC_FLAG )
    {
      // 无效条目. 直接返回.
      break;
    }

    if ( current_index == index )
    {
      // 找到对应条目. 拷贝出数据后返回.
      *Log_WhetherSucceededToBeAcquired = *__pLog;
      return true;
    }

    current_index++;

    addr += sizeof(LogType_t);
  }

  return false;
}



/**
 * @brief 获取最近第 n 条日志在 Flash 中的物理地址（无需拷贝日志内容）
 * 
 * @details 该函数通过 log_status 快速定位目标日志的 Flash 地址，避免了全量扫描：
 *          - n = 0 表示最新写入的一条日志（时间最晚）
 *          - n = 1 表示倒数第二条日志
 *          - 以此类推...
 * 
 * @note    优势：
 *          时间复杂度 O(1)，比扫描方式快 10-100 倍
 *          直接返回 Flash 物理地址，避免数据拷贝开销
 *          适用于 LVGL 等需要快速定位日志的场景
 * 
 * @param   n           目标日志序号（0=最新一条，必须小于 log_status.logNum）(例如 写入n=2 则是最后最新的第三条数据)
 * @param   flash_addr  输出参数：目标日志在 Flash 中的起始地址指针
 * @return  bool        成功找到返回 true，参数错误/越界/锁超时返回 false
 * 
 * @warning 
 *          - 返回的地址是 Flash 物理地址，直接读取即可获取日志内容
 *          - 必须确保调用期间 log_status 已正确维护（通过 Log_Flash_Write 等接口）
 *          - 该地址仅在本次调用有效，后续写入可能使旧地址失效
 */
bool Log_GetLatestN( uint16_t n, uint32_t *flash_addr )
{
  if ( !flash_addr )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of Log_GetLatestN(uint32_t *).\n");
    #endif 

    return false;
  }

  uint8_t count = 0;

Retry_Log_GetLatestN:
  // 获取锁. 以防write_address被修改.
  BaseType_t err = xSemaphoreTake( xMutexLog, LOG_MUTEX_BLOCK_TIME );
  if ( err != pdPASS )
  {
    // 锁获取失败，开始重试.
    while( count < FLASH_LOG_RETRY_COUNT )
    {
      count++;

      vTaskDelay(pdMS_TO_TICKS(5));

      // 重试仍然获取失败，放弃此次调用并给出调试信息.
      if ( count >= FLASH_LOG_RETRY_COUNT )
      {
        #if defined(__DEBUG_LEVEL_1__)
          printf("Cant take xMutexLog at Log_GetLatestN.\n");
        #endif           

        return false;
      }

      goto Retry_Log_GetLatestN;
    }
  }

  if ( log_status.logNum == 0 || n >= log_status.logNum )
  {
    xSemaphoreGive(xMutexLog);

    return false;
  }

  // 直接计算目标地址.
  uint16_t target_index = log_status.logNum - 1 - n;

  uint32_t addr = ( LOG_FLASH_START_ADDR + OFFSET_ERASEFLAG + target_index * sizeof(LogType_t) );


  if ( addr < ( LOG_FLASH_START_ADDR + OFFSET_ERASEFLAG ) || addr + sizeof(LogType_t) > ( LOG_FLASH_END ) )
  {
    // 计算出的地址不在正常范围.
    xSemaphoreGive(xMutexLog);

    return false;
  }

  const LogType_t *__pLog = (const LogType_t *)addr;

  // 验证该日志是否有效.
  if ( __pLog -> valid_flag != LOG_VALID_MAGIC_FLAG )
  {
    xSemaphoreGive(xMutexLog);

    return false;
  }

  *flash_addr = addr;

  xSemaphoreGive(xMutexLog);

  return true;
}



/**
 * @brief  从 Flash 日志区读取最新的若干条日志记录（最新日志在前）
 *
 *         该函数按时间倒序（最新日志优先）从 Flash 中读取指定数量的日志条目，
 *         并拷贝到用户提供的缓冲区中。适用于 UI 显示（如 LVGL 列表）等需要
 *         快速获取最近日志的场景。
 *
 * @param  ReadNum           请求读取的日志条数
 *                           - 若为 0 或超过当前存储总数，则自动使用 DEFAULT_READNUM
 * @param  buffer            输出缓冲区，用于存放读取到的日志数据
 *                           - 必须指向有效内存空间，且容量不小于 ReadNum
 * @param  buffer_capacity   buffer 缓冲区的最大容量（以 LogType_t 数目计）
 *
 * @return uint16_t          实际成功读取并验证有效的日志条数
 *                           - 返回值 ≤ min(ReadNum, log_status.logNum, buffer_capacity)
 *                           - 若发生地址错误或魔术字校验失败，会跳过无效条目并返回实际有效数
 *
 * @note
 *   - 函数内部调用 Log_GetLatestN() 定位起始日志位置，依赖全局 log_status 状态
 *   - 读取后会对每条日志的 valid_flag 进行二次校验（防御性编程）
 *   - 校验失败的日志会被丢弃（对应 buffer 项清空 valid_flag），不影响其他条目
 *   - 推荐 buffer_capacity >= ReadNum，避免截断
 *     本函数为性能优化设计，未内置互斥锁。如需线程安全，请外部加锁调用
 *   - 当返回的实际有效数小于请求读取的日志条数时，使用每条日志必须进行有效数校验.
 *
 * @warning
 *   - buffer 不可为 NULL，否则立即返回 0
 *   - 本函数未加互斥锁，若与其他写日志任务并发运行，可能存在短暂状态不一致风险；
 *     建议在低并发或临界区中调用
 *   - 若 log_status 未及时更新（如写入后未刷新），可能导致读取条数计算错误
 *
 * @see    Log_GetLatestN(), Log_UpdateStatus(), DEFAULT_READNUM
 */
uint16_t Log_ReadLatest( uint16_t ReadNum, LogType_t *buffer, uint16_t buffer_capacity )
{
  if ( !buffer )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of Log_ReadLatest(LogType_t*).\n");
    #endif

    return 0;
  }

  // ReadNum为0或大于日志条数,则采用默认读取数目.
  if ( ( ReadNum == 0 ) || ( ReadNum > log_status.logNum ) )
  {
    ReadNum = DEFAULT_READNUM;
  }

  if ( ReadNum > buffer_capacity )
  {
    ReadNum = buffer_capacity;
  }

  uint32_t __ptrLogAddr_Temp;

  if ( !Log_GetLatestN( ReadNum - 1, &__ptrLogAddr_Temp ) )
  {
    // 未获取到对应的日志条目. 返回.
    return 0;
  }

  for( int16_t j = ( ReadNum - 1); j >= 0; j-- )
  {
    buffer[j] = *(LogType_t *)__ptrLogAddr_Temp;

    __ptrLogAddr_Temp += sizeof(LogType_t);
  }

  uint8_t count = 0;

  uint8_t error = 0;

  uint8_t failed_count = 0;

  do 
  {
    if ( buffer[count].valid_flag == LOG_VALID_MAGIC_FLAG ) {   }
    else 
    {
      buffer[count].valid_flag = 0x00;

      failed_count++;

      error = 1;
    }

    count++;
  } while( count < ReadNum );

  if ( error )
  {
    #if defined(__DEBUG_LEVEL_1__)
     printf("In Log_ReadLatest(),some logs got wrong MAGIC_FLAG. Has been droped!\n");
    #endif 

    // 成功读取的日志数.
    return count - failed_count;
  }

  #if defined(__DEBUG_LEVEL_1__)
    printf("Buffer Verify OK of Log_ReadLatest!\n");
  #endif 

  return ReadNum;
}



/**
 * @brief  检查指定日志条目是否有效
 * 
 *         通过验证日志结构体中的 valid_flag 是否等于预设魔数，
 *         判断该日志是否为已成功写入的有效记录。
 * 
 * @param  __log  指向待检查的日志条目的指针
 * @return bool   有效返回 true，无效或指针为空返回 false
 */
bool Log_IsValid( const LogType_t *__log )
{
  if ( !__log )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Wrong Param of Log_IsValid.\n");
    #endif    

    return false;
  } 

  if ( __log -> valid_flag == LOG_VALID_MAGIC_FLAG )
  {
    return true;
  }
  else 
  {
    return false;
  }
}



/**
 * @brief   获取当前已存储的有效日志条目总数
 *
 *          该函数返回 Flash 日志区中已被成功写入且标记为有效的日志数量。
 *          数值来源于内部维护的 log_status 结构体，通过互斥锁保护访问，
 *          确保多任务环境下的数据一致性。
 *
 * @return  uint16_t 当前有效日志条数
 *                  - 若日志系统未启用或无日志，返回 0
 *                  - 最大值受 MAX_LOG_NUM 和 Flash 容量限制
 *
 * @note    此函数是线程安全的，内部使用 xMutexLog 保证原子读取。
 *          推荐用于 UI 显示日志总数、分页计算等场景。
 *
 * @warning 
 *          - 返回值为调用时刻的快照，后续其他任务写入/清除日志会改变实际数量；
 *          - 不要依赖此数值进行长时间逻辑判断，建议“获取 → 使用”紧邻操作。
 *
 * @see     Log_GetStatus(), Log_ReadLatest()
 */
uint16_t Log_GetCount( void )
{
  uint16_t cnt = 0;

  if ( xSemaphoreTake(xMutexLog, LOG_MUTEX_BLOCK_TIME) == pdPASS )
  {
    cnt = log_status.logNum;

    xSemaphoreGive(xMutexLog);
  }

  return cnt;
}



/**
 * @brief   写入一条格式化日志到 Flash 存储系统（内部实现函数）
 *
 * @details 该函数是 LOG_WRITE 宏的底层实现，负责构造完整的 LogType_t 日志结构体，
 *          并将其安全写入预分配的 Flash 区域。支持变长参数格式化消息（printf 风格）。
 *
 *          主要执行流程：
 *          1. 初始化日志结构体（自动清零）
 *          2. 填充时间戳、日志级别、任务名称
 *          3. 使用 vsnprintf 安全格式化用户消息（防止缓冲区溢出）
 *          4. 调用 Log_Flash_Write() 持久化存储
 *
 * @param   level       日志严重级别（LOG_DEBUG / LOG_INFO / LOG_WARNING / LOG_ERROR）
 * @param   taskName    发生日志的任务名（建议 ≤15 字符，自动截断并补 '\0'）
 * @param   fmt         格式化字符串（如 "Temperature: %d, State: %s"）
 * @param   ...         可变参数列表（对应 fmt 中的占位符）
 *
 * @note
 *   - 此函数不可直接调用，请使用宏 LOG_WRITE(level, taskName, fmt, ...) 代替
 *   - 线程安全：内部由 Log_Flash_Write() 使用互斥锁保护，可在多任务中并发调用
 *   - 异常处理：若日志系统未启用或写入失败，会静默丢弃日志（除非启用了 __DEBUG_LEVEL_1__ 输出警告）
 *   - 性能提示：频繁调用可能阻塞其他任务，请合理控制日志密度
 *
 * @warning
 *   - 不可在中断服务程序（ISR）中调用（因使用 va_list 和动态内存操作）
 *   - fmt 格式字符串必须有效，否则可能导致 undefined behavior
 *   - message 字段最大长度为 44 字节（含结尾 '\0'），超长部分将被截断
 *
 * @see     LOG_WRITE(), Log_Flash_Write(), LogType_t
 */
void _log_write_impl(LogLevel_t level, const char* taskName, const char* fmt, ...)
{
    LogType_t log;
    memset(&log, 0, sizeof(log));
    log.level = level;
    log.timeStamp = xTaskGetTickCount();
    strncpy(log.taskName, taskName, sizeof(log.taskName) - 1);
    log.taskName[sizeof(log.taskName) - 1] = '\0';
    va_list args;
    va_start(args, fmt);
    vsnprintf(log.message, sizeof(log.message), fmt, args);
    va_end(args);
    Log_Flash_Write(&log);
}



/**
 * @brief   执行日志系统的自检测试，验证写入与读取功能是否正常工作
 *
 * @details 该函数用于检测 Flash 日志系统的基本功能完整性，执行以下步骤：
 *          1. 检查日志系统是否已启用（g_log_enable == LOG_ON）
 *          2. 使用 LOG_WRITE 宏写入一条标记为 "TEST" 的日志条目
 *          3. 短暂延时以确保写入完成（尤其在高负载系统中）
 *          4. 调用 Log_GetAtIndex 读取最新一条日志（索引为 logNum - 1）
 *          5. 验证读出的日志任务名为 "TEST" 且有效标志位正确
 *
 * @note
 *   - 本测试依赖外部 API：Log_GetCount()、Log_GetAtIndex() 和 LOG_WRITE()
 *   - 需要 FreeRTOS 的 vTaskDelay 支持以提供上下文切换机会
 *   - 建议在系统初始化后或故障诊断时调用此函数
 *   - 测试成功仅表示基本读写链路通畅，不验证 Flash 寿命或断电恢复能力
 *
 * @warning
 *   - 不可在中断上下文中调用（因使用了 vTaskDelay 和可变参数宏）
 *   - 若与其他任务并发大量写日志，可能导致测试误判（建议在低负载时运行）
 *
 * @return bool 自检结果：全部验证通过返回 true；任一环节失败返回 false
 */
bool Log_SelfTest( void )
{
  if ( g_log_enable == LOG_OFF )
  {
    #if defined(__DEBUG_LEVEL_1__)
      printf("Log System SelfTest Failed.(LOG_FLAG Off)\n");
    #endif 

    return false;
  }

  LOG_WRITE(LOG_INFO, "TEST", "Log Self Test.\n");

  vTaskDelay(pdMS_TO_TICKS(5));

  LogType_t test_log;

  if ( Log_GetAtIndex(Log_GetCount() - 1, &test_log) )
  {
    if ( !strcmp(test_log.taskName, "TEST") && test_log.valid_flag == LOG_VALID_MAGIC_FLAG )
    {
      return true;
    }
    else 
    {
      return false;
    }
  }
  else 
  {
    return false;
  }
}




/**
 * @brief   写入一条紧急日志到备份内存（Panic Log），用于记录致命系统错误
 *
 * @details 该函数专为不可恢复的严重故障场景设计（如栈溢出、HardFault 等），
 *          将关键诊断信息写入 Backup SRAM 区域（地址: LOG_PANIC_ADDR）。  
 *          即使系统随后复位，只要 VBAT 供电正常，该日志仍可被保留，供下次启动时读取分析。
 *
 *          记录内容包括：
 *          - 错误原因字符串（reason）
 *          - 发生任务名称（taskName，若为空则标记为 "Unknown"）
 *          - 时间戳（HAL_GetTick）
 *          - 日志级别（固定为 LOG_ERROR）
 *          - 魔数标志（LOG_PANIC_MAGIC），用于有效性校验
 *
 * @note
 *   - 此函数假设调用环境已关闭调度器与中断（如 vApplicationStackOverflowHook 中），
 *     因此内部不再进行任何临界区保护操作。
 *   - 不依赖动态内存、标准库格式化函数（如 snprintf）或复杂 API，确保在损坏环境中仍能执行。
 *   - 若 reason 为 NULL，则函数直接返回，不进行任何写入操作。
 *   - taskName 可为 NULL，此时将记录为 "Unknown"。
 *
 * @warning
 *   - 必须确保 BKPSRAM 已正确初始化并启用时钟（__HAL_RCC_BKPSRAM_CLK_ENABLE()）；
 *     否则写入无效。
 *   - 若设备无 VBAT 供电，掉电后本日志将丢失。
 *   - 不可在普通运行路径中滥用此接口（仅限 fatal error 场景）。
 *
 * @param   taskName    出错的任务名（建议 ASCII 字符串，最长 15 字符 + '\0'）
 * @param   reason      错误原因描述字符串（必须非空，否则函数返回）
 *
 * @see     vApplicationStackOverflowHook, HardFault_Handler, LOG_PANIC_ADDR, LOG_PANIC_MAGIC
 */
void Log_PanicWrite( const char *taskName, const char *reason )
{
  uint8_t count = 0;

  uint8_t Param_isValid = 1;

  if ( !taskName )
  {
    Param_isValid = 0;
  }

  // 必须要写明原因. 否则直接返回.
  if ( !reason )
  {
    return;
  }

  LogType_t *__panic_log = (LogType_t *)LOG_PANIC_ADDR;
  memset((LogType_t *)LOG_PANIC_ADDR, 0, sizeof(LogType_t));
  __panic_log->valid_flag = LOG_PANIC_MAGIC;
  __panic_log->level = LOG_ERROR;
  __panic_log->timeStamp = HAL_GetTick();

  if ( Param_isValid )
  {
    uint8_t j = 0;

    for( j; j < 16 && taskName[j]; j++ )
    {
      __panic_log->taskName[j] = taskName[j];
    }
    __panic_log->taskName[j] = '\0';
  }
  else 
  {
    strcpy(__panic_log->taskName, "Unknown");
  }

  uint8_t len = 0;

  while (len < sizeof(__panic_log->message) - 1 && reason[len]) {
      __panic_log->message[len] = reason[len];
      len++;
  }
  __panic_log->message[len] = '\0';
}
