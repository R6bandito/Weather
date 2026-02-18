#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "stm32f4xx_hal.h"

// ====================  JSON 配置   ====================
extern const char* const city_path[];
extern const uint8_t city_path_cnt;

extern const char* const province_path[];
extern const uint8_t province_path_cnt;

extern const char* const country_path[];
extern const uint8_t country_path_cnt;


#endif // __CONFIG_H__
