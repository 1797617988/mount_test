#ifndef __MAVLINK_INIT_H__
#define __MAVLINK_INIT_H__
#include <stdint.h>
#include <mavlink_v2/common/mavlink.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

// 相机组件ID
#define CAMERA_COMPONENT_ID MAV_COMP_ID_CAMERA

int mavlink_main(void);
void send_command_ack(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len, uint16_t command, uint8_t result);
void send_camera_information(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void handle_command_long(const mavlink_message_t* msg);


#ifdef __cplusplus
}
#endif
#endif