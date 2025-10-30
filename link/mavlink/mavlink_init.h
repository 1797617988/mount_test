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
// 通用消息发送接口
void send_mavlink_message(int fd, const void* dest_addr, socklen_t addr_len, const mavlink_message_t* msg);

// 协议无关的消息发送函数
void send_command_ack(int fd, const void* dest_addr, socklen_t addr_len, uint16_t command, uint8_t result);
void send_camera_information(int fd, const void* dest_addr, socklen_t addr_len);
void send_camera_capture_status(int fd, const void* dest_addr, socklen_t addr_len);
void send_video_stream_status(int fd, const void* dest_addr, socklen_t addr_len);
void send_video_stream_information(int fd, const void* dest_addr, socklen_t addr_len);
void send_camera_settings(int fd, const void* dest_addr, socklen_t addr_len);
void send_heartbeat(int fd, const void* dest_addr, socklen_t addr_len);
void send_autopilot_heartbeat(int fd, const void* dest_addr, socklen_t addr_len);
void handle_command_long(const mavlink_message_t* msg);
void mavlink_stop(void);


#ifdef __cplusplus
}
#endif
#endif