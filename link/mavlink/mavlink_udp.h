#ifndef __MAVLINK_UDP_H__
#define __MAVLINK_UDP_H__

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <mavlink_v2/common/mavlink.h>

#ifdef __cplusplus
extern "C" {
#endif

// UDP通信初始化
int mavlink_udp_init(void);

// UDP主循环
int mavlink_udp_main(void);

// UDP清理
void mavlink_udp_deinit(void);

#ifdef __cplusplus
}
#endif

#endif