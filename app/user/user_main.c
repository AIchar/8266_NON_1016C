#include "ets_sys.h"
#include "os_type.h"
#include "c_types.h"
#include "osapi.h"
#include "mem.h"
#include "gpio.h"
#include "user_interface.h"

#include "driver/uart.h"
#include "driver/wifi.h"
#include "driver/1016C.h"

#define MAIN_DEBUG_ON

#if defined(MAIN_DEBUG_ON)
#define INFO( format, ... ) os_printf( format, ## __VA_ARGS__ )
#else
#define INFO( format, ... )
#endif



uint32 ICACHE_FLASH_ATTR user_rf_cal_sector_set(void) {
	enum flash_size_map size_map = system_get_flash_size_map();
	uint32 rf_cal_sec = 0;

	switch (size_map) {
	case FLASH_SIZE_4M_MAP_256_256:
		rf_cal_sec = 128 - 5;
		break;

	case FLASH_SIZE_8M_MAP_512_512:
		rf_cal_sec = 256 - 5;
		break;

	case FLASH_SIZE_16M_MAP_512_512:
	case FLASH_SIZE_16M_MAP_1024_1024:
		rf_cal_sec = 512 - 5;
		break;

	case FLASH_SIZE_32M_MAP_512_512:
	case FLASH_SIZE_32M_MAP_1024_1024:
		rf_cal_sec = 1024 - 5;
		break;

	case FLASH_SIZE_64M_MAP_1024_1024:
		rf_cal_sec = 2048 - 5;
		break;
	case FLASH_SIZE_128M_MAP_1024_1024:
		rf_cal_sec = 4096 - 5;
		break;
	default:
		rf_cal_sec = 0;
		break;
	}

	return rf_cal_sec;
}



/**
 * 	按键短按回调
 */
LOCAL void ICACHE_FLASH_ATTR key1ShortPress(void) {
	fp_mode_change();
}


/**
 * 	按键初始化
 */
LOCAL void ICACHE_FLASH_ATTR keyInit(void) {

	set_key_num(1);	//设置按键数量
	key_add(D3, NULL, key1ShortPress);	//按键回调

}


void user_init(void) {

	uart_init(115200, 115200);
	os_delay_us(60000);

	wifi_set_opmode(NULL_MODE); 

	keyInit();
	
	fprint1016_init();
}

