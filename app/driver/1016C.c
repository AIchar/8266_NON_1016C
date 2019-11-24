/*
 * 1016C driver
 * Author: Alchar
 */
#include "ets_sys.h"
#include "os_type.h"
#include "c_types.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"

#include "driver/uart.h"
#include "driver/1016C.h"

// #define FPRINT_DEBUG_ON 0

#if defined(FPRINT_DEBUG_ON)
#define INFO( format, ... ) os_printf( format, ## __VA_ARGS__ )
#else
#define INFO( format, ... )
#endif

os_timer_t OS_Timer_Wakeup;	//检测手指
os_timer_t OS_Timer_LedClose;	//关闭LED

fingerprint_cb callback;

uint8 fprint_mode = VERIFY_MODE;	//工作模式
u8 max_fp_num = 32;

/**
 * 校验数据和
 * @return SUCCESS:1|FAIL:0
 */
uint8 check_sum(u8* data, uint16 data_len){
	u8 i = 0;
	int sum = 0;
	for(i=0;i<data_len-2;i++){
		sum = sum + data[i];
	}
	if(data[data_len-2] == (sum & 0xFF)){
		sum = sum >> 8;
		if(data[data_len-1] == (sum & 0xFF))
			return 1;
		return 0;
	}
	return 0;	
}


/**
 * 计算数据和
 */
void calculate_sum(u8* data, uint16 data_len){
	u8 i = 0;
	int sum = 0;
	for(i=0;i<data_len-2;i++){
		sum = sum + data[i];
	}
	data[data_len-2] = sum & 0xFF;
	sum = sum >> 8;
	data[data_len-1] = sum & 0xFF;
}


/**
 * 发送命令
 */
void ICACHE_FLASH_ATTR send_cmd(u8 mCMD, u8 data_len, u8 *data){
	//55 AA 00 00 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 1f 01
	u8 mdata[26]={0};
	u8 i;
	mdata[0] = 0x55;
	mdata[1] = 0xAA;
	mdata[4] = mCMD;
	if (data_len > 0)
	{
		mdata[6] = data_len;
		for ( i = 0; i < data_len; i++)
		{
			mdata[i+8]=data[i];
		}
	}
	INFO("[INFO]Send CMD:%02X\r\n",mCMD);
	calculate_sum(mdata,sizeof(mdata));
	uart0_tx_buffer(mdata, sizeof(mdata));
}


/**
 * 解析响应
 */
char ICACHE_FLASH_ATTR dataProces(u8 *src, DataPacket* rcData){
	//AA 55 00 00 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 1f 01
	u8 i;
	if(*src!=0xAA || *(src+1)!=0x55){
		INFO("[ERROR]Packet head error\r\n");
		return -1;
	}
	if (check_sum(src,CMD_PACKET_LEN)!=1)
	{
		INFO("[ERROR]CKS ERROR\r\n");
		return -1;
	}
	rcData->CMD = src[4];
	rcData->dataLen = src[6];
	for (i = 0; i < rcData->dataLen; i++)
	{
		rcData->data[i]=src[i+8];
	}
	INFO("[INFO]Recive CMD:%02X\r\n",src[4]);
	return 1;
}


/**
 * 删除指定ID指纹模板
 */
void ICACHE_FLASH_ATTR fp_delete(u8 initialID,u8 endID){
	u8 sendData[14]={0};
	sendData[0]=initialID;
	sendData[2]=endID;
	send_cmd(CMD_DEL_CHAR,4,sendData);
}

/**
 * 串口接收回调
 */
void uart_rx_cb(uint8* pData_buf, uint16 data_len) {
	DataPacket DataPacket;
	u8 sendData[14]={0};

	if (dataProces(pData_buf,&DataPacket))
	{
		//验证模式
		if (fprint_mode == VERIFY_MODE)
		{
			switch (DataPacket.CMD)
			{
			case CMD_GET_IMAGE:	//获取图像
				if (DataPacket.dataLen==0x02 && DataPacket.data[0]==ERR_SUCCESS)
				{
					send_cmd(CMD_GENERATE,0x02,sendData);
				}else
				{
					INFO("[ERROR]Get Image error\r\n");
					led_set(LED_COLOR_YELLOW,LED_ON);
					os_timer_arm(&OS_Timer_LedClose,1000,0);
				}
				break;
			case CMD_GENERATE:	//生成特征
				if (DataPacket.dataLen==0x02 && DataPacket.data[0]==ERR_SUCCESS)
				{
					sendData[2]=0X01;
					sendData[4]=max_fp_num;
					send_cmd(CMD_SEARCH,0x06,sendData);
				}else
				{
					INFO("[ERROR]Generate error\r\n");
				}
				break;			
			case CMD_SEARCH:	//1:N验证
				if (DataPacket.dataLen==0x05 && DataPacket.data[0]==ERR_SUCCESS)
				{
					//通过验证
					INFO("[INFO]Finger verify pass | finger ID:%d\r\n",DataPacket.data[2]);
					led_set(LED_COLOR_GREEN,LED_ON);
					callback(VERIFY_SUCCESS,DataPacket.data[2]);
				}else
				{
					//验证失败
					led_set(LED_COLOR_RED,LED_ON);
					INFO("[INFO]Finger verify fail\r\n");
					callback(VERIFY_FAIL,0);
				}
				os_timer_arm(&OS_Timer_LedClose,1000,0);
				break;
			case CMD_DEL_CHAR:	//删除指纹
				if (DataPacket.dataLen==0x02)
				{
					if (DataPacket.data[0]==ERR_SUCCESS)
					{
						led_set(LED_COLOR_GREEN,LED_BLINK_F);
						callback(DELETE_SUCCESS,0);
					}else
					{
						led_set(LED_COLOR_RED,LED_BLINK_F);
						callback(DELETE_FAIL,0);
					}
					os_timer_arm(&OS_Timer_LedClose,1500,0);
				}				
				break;	
			case CMD_SLED_CTRL:
				break;			
			default:
				INFO("[ERROR]Unknow error\r\n");
				break;
			}
		}
		//注册模式
		else if(fprint_mode == REGISTER_MODE)
		{
			static u8 fprintID = 0;
			static u8 step = 0;
			switch (DataPacket.CMD)
			{
			case CMD_GET_EMPTY_ID:	//获取可用ID
				if (DataPacket.dataLen==4 && DataPacket.data[0]==ERR_SUCCESS)
				{
					led_set(LED_COLOR_CYAN,LED_ON);
					fprintID=DataPacket.data[2];
					INFO("[INFO]Fingerprint ID: %d\r\n",fprintID);
				}else
				{
					INFO("[ERROR]Fingerprint storage full\r\n");
					fprintID = 0;
					fprint_mode = VERIFY_MODE;
				}				
				break;
			case CMD_GET_IMAGE:	//获取图像
				if (DataPacket.dataLen==0x02 && DataPacket.data[0]==ERR_SUCCESS)
				{
					sendData[0]=step;
					send_cmd(CMD_GENERATE,0x02,sendData);
				}else
				{
					INFO("[ERROR]Get Image error\r\n");
				}
				break;
			case CMD_GENERATE:	//生成特征
				if (DataPacket.dataLen==0x02 && DataPacket.data[0]==ERR_SUCCESS)
				{
					step+=1;
					if(step == 3){
						step=0;
						sendData[2]=3;
						send_cmd(CMD_MERGE,3,sendData);
					}else
					{
						led_set(LED_COLOR_CYAN,LED_ON);
					}
					
				}else
				{
					INFO("[ERROR]Generate error\r\n");
				}
				break;				
			case CMD_MERGE:	//合成模板
				if (DataPacket.dataLen==0x02 && DataPacket.data[0]==ERR_SUCCESS){
					INFO("[INFO]Merge success...\r\n");
					sendData[0]=fprintID;
					send_cmd(CMD_STORE_CHAR,4,sendData);
				}else
				{
					INFO("[ERROR]Merge fail\r\n");
					fprintID = 0;
					fp_verify();
				}
				break;
			case CMD_STORE_CHAR:
				if (DataPacket.dataLen==0x02 && DataPacket.data[0]==ERR_SUCCESS){
					INFO("[INFO]Fingerprint register success | Finger ID:%d\r\n",fprintID);
					callback(REGISTER_SUCCESS,fprintID);
					fprintID=0;
					led_set(LED_COLOR_GREEN,LED_ON);
					os_timer_arm(&OS_Timer_LedClose,1500,0);
					fp_verify();
				}
				break;
			default:
				break;
			}
		}		
	}
}


/**
 * 检测手指回调
 */
void ICACHE_FLASH_ATTR wakeupHandle() {
	bool Key_press = 0;		//按键状态
    static bool Key_State = 1;	//历史状态

    Key_press = gpio_read(wakeup);
	
	//手指触碰
	if (Key_State && Key_press)
	{
		Key_State = 0;	//更新历史状态
		INFO("[INFO]Finger press\r\n");
		switch (fprint_mode)
		{
		case VERIFY_MODE:	//验证模式
		case REGISTER_MODE:	//注册模式
			led_set(LED_COLOR_BLUE,LED_ON);
			os_delay_us(50000);
			send_cmd(CMD_GET_IMAGE, 0, NULL);
			break;
		}
		
	}
	//手指松开
	else if (Key_State==0 && Key_press==0)
	{
		Key_State=1;
	}
}


/**
 * 初始化
 */
void fprint1016_init(fingerprint_cb func_cb){
    
    //设置串口接收回调
	set_uart_cb(uart_rx_cb);
    UART_SetPrintPort(1);

	if (func_cb!=NULL)
		{
			callback =  func_cb;
		}
	gpio_disout_init(wakeup);

	os_timer_disarm(&OS_Timer_LedClose);
    os_timer_setfn(&OS_Timer_LedClose, (os_timer_func_t *)led_close, NULL);

    os_timer_disarm(&OS_Timer_Wakeup);
    os_timer_setfn(&OS_Timer_Wakeup, (os_timer_func_t *)wakeupHandle, NULL);
    os_timer_arm(&OS_Timer_Wakeup, 40, 1);

}


/**
 * 关闭LED
 */
void ICACHE_FLASH_ATTR led_close(){
	u8 sendData[14]={0};	
	sendData[1]=0x87;
	send_cmd(CMD_SLED_CTRL,2,sendData);
}


/**
 * 延时关闭led
 */
void ICACHE_FLASH_ATTR close_timer(u32 time){
	os_timer_disarm(&OS_Timer_LedClose);
	os_timer_arm(&OS_Timer_LedClose,time,0);
}



/**
 * 设置LED
 */
void ICACHE_FLASH_ATTR led_set(led_color color, led_mode status){
	u8 sendData[14]={0};
	sendData[0]=status;	
	sendData[1]=color;
	send_cmd(CMD_SLED_CTRL,2,sendData);
}


/**
 * 注册模式
 */
void ICACHE_FLASH_ATTR fp_register(){
	fprint_mode = REGISTER_MODE;
	u8 sendData[14]={0};
	sendData[0]=1;
	sendData[2]=max_fp_num;
	send_cmd(CMD_GET_EMPTY_ID, 4, sendData);
	INFO("[INFO]Register mode\r\n");
}


/**
 * 验证模式
 */
void ICACHE_FLASH_ATTR fp_verify(){
	fprint_mode = VERIFY_MODE;
	INFO("[INFO]Verify mode\r\n");
}


/**
 * 空闲模式
 */
void ICACHE_FLASH_ATTR fp_free(){
	fprint_mode = FREE_MODE;
	INFO("[INFO]Free mode\r\n");
}


/**
 * 修改模式
 */
void ICACHE_FLASH_ATTR fp_mode_change(){
	switch (fprint_mode)
	{
	case REGISTER_MODE:
		fp_free();
		break;
	case VERIFY_MODE:
		fp_register();
		break;
	case FREE_MODE:
		fp_verify();
		break;
	default:
		break;
	}
}