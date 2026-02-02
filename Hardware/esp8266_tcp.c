#include "esp8266_tcp.h"

extern ESP8266_HandleTypeDef hesp8266;

static esp_tcp_handle_t htcp8266;

/* ************************* */
esp_tcp_err_t esp8266_tcp_Init( void );
const esp_tcp_handle_t* esp8266_tcp_getState( void );

/* ************************* */


/**
 * @brief 获取当前 TCP 连接状态的只读句柄
 *
 * 返回指向内部静态 TCP 状态结构体的 const 指针，用于安全读取连接状态、
 * 目标主机、端口等运行时信息。该结构体由 esp8266_tcp_Init() 初始化，
 * 并在 esp8266_tcp_connect()/disconnect() 中自动更新。
 *
 * @note 调用者不得修改返回结构体内容；所有状态变更应通过公共 API 触发。
 * @return const esp_tcp_handle_t* 指向当前 TCP 状态的只读句柄（永不为 NULL）
 */
const esp_tcp_handle_t* esp8266_tcp_getState()
{
  return &htcp8266;
}



esp_tcp_err_t esp8266_tcp_Init( void )
{
  void *pResponse = NULL;
  bool res_a, res_b, reb_c;
  uint32_t out_val = 0xFF;
  char temp_outChar[30];

  // 判断网络是否连接上.
  if ( esp8266_SendAT("AT+CWJAP?") )
  {
    if ( esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) )
    {
      char temp_outChar[33];

      reb_c = at_extractString_between_quotes(&hesp8266, "+CWJAP:", temp_outChar, sizeof(temp_outChar), pdTRUE);

      if ( !reb_c )
      {
        esp8266_DropLastFrame();

        goto TCP_Init_ExtractNum_Err;
      }

      if ( strcmp(hesp8266.WifiSSID, temp_outChar) )
      {
        // 网络未连接上 或 连接状态与状态机管理状态不匹配.
        #if defined(__DEBUG_LEVEL_1__)
          printf("WIFI Connect Error.(esp8266_tcp_Init).\n");
        #endif 

        LOG_WRITE(LOG_ERROR, "NULL", "WIFI Connect Error.(esp8266_tcp_Init).");

        return ESP_TCP_ERR_CONNECT_FAIL;
      } 
    }
    else 
    {
      esp8266_DropLastFrame();

      goto TCP_Init_Wait_Err;
    }
  }
  else 
  {
    goto TCP_Init_ATSend_Err;
  }


  // 判断并设置为单连接模式.
  if ( !esp8266_SendAT("AT+CIPMUX?") )
  {
TCP_Init_ATSend_Err:
    #if defined(__DEBUG_LEVEL_1__)
      printf("SendAT Error in esp8266_tcp_Init.\n");
    #endif // __DEBUG_LEVEL_1__

    LOG_WRITE(LOG_WARNING, "NULL", "esp8266_tcp_Init SendAT Failed.");

    return ESP_TCP_ERR_TIMEOUT;  // AT命令发送失败(超时).
  } 

  pResponse = esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT);

  if ( pResponse != NULL )
  {
    res_b = at_extractNum(&hesp8266, "CIPMUX", &out_val, pdTRUE);

    if ( res_b && out_val == 0 )
    {
      // 已经为单连接模式.
    }

    // 断开多连接模式(设置为单连接模式).
    if ( res_b && out_val == 1 )
    {
      if ( !esp8266_SendAT("AT+CIPMUX=0") )  goto TCP_Init_ATSend_Err;

      if ( !esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) ) 
      {
        esp8266_DropLastFrame();
        goto TCP_Init_Wait_Err;
      }
    }

    if ( !res_b || ( out_val != 1 && out_val != 0 ) )
    {
TCP_Init_ExtractNum_Err:
      #if defined(__DEBUG_LEVEL_1__)
        printf("at_extractNum called error in esp8266_tcp_Init\n");
      #endif 

      LOG_WRITE(LOG_WARNING, "NULL", "at_extractNum failed in esp8266_tcp_Init");

      return ESP_TCP_ERR_NO_RESPONSE;
    } 
  }
  else 
  {
TCP_Init_Wait_Err:
    #if defined(__DEBUG_LEVEL_1__)
      printf("WaitResponse Timeout in esp8266_tcp_Init.\n");
    #endif

    LOG_WRITE(LOG_WARNING, "NULL", "WaitResponse Timeout in esp8266_tcp_Init.");

    return ESP_TCP_ERR_NO_RESPONSE;
  } 


  // 判断并设置为普通传输模式（不使用WIFI透传模式）.
  if ( esp8266_SendAT("AT+CIPMODE?") )
  {
    if ( esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) )
    {
      res_a = at_extractNum(&hesp8266, "CIPMODE", &out_val, pdTRUE);

      if ( res_a && out_val == 0 )
      {
        // 已经处于普通传输模式.
      }

      if ( res_a && out_val == 1 )
      {
        if ( !esp8266_SendAT("AT+CIPMODE=0") ) goto TCP_Init_ATSend_Err;

        if ( !esp8266_WaitResponse("OK", ESP_TCP_CMD_TIMEOUT) ) 
        {
          esp8266_DropLastFrame();
          goto TCP_Init_Wait_Err; 
        }
      }

      if ( !res_a || ( out_val != 1 && out_val != 0 ) )
      {
        goto TCP_Init_ExtractNum_Err;
      }
    }
    else 
    {
      goto TCP_Init_Wait_Err;
    }
  }
  else 
  {
    goto TCP_Init_ATSend_Err;
  }

  htcp8266.is_Connected = false;
  htcp8266.conn_ID = 0xFF;
  htcp8266.remain = 0xFF;
  htcp8266.Port = 0;
  memset(htcp8266.Host, 0, sizeof(htcp8266.Host));
  memset(htcp8266.remote_IP, 0, sizeof(htcp8266.remote_IP));

  #if defined(__DEBUG_LEVEL_1__)
    printf("TCP Init OK: CIPMUX=0, CIPMODE=0, CIPSTO=180, IP=%s\n", hesp8266.Wifi_Ipv4);
  #endif

  return ESP_TCP_OK;
}



