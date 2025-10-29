#ifndef __MAVLINK_UART_H__
#define __MAVLINK_UART_H__

#include <stdint.h>
#include <mavlink_v2/common/mavlink.h>

#ifdef __cplusplus
extern "C" {
#endif

// 串口通信初始化
int mavlink_uart_init(void);

// 串口主循环
int mavlink_uart_main(void);

// 串口清理
void mavlink_uart_deinit(void);

#ifdef __cplusplus
}
#endif

#endif