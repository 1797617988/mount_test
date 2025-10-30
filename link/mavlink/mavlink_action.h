#ifndef __MAVLINK_ACTION_H__
#define __MAVLINK_ACTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <mavlink_v2/common/mavlink.h>
#include "ss_log.h"
#include "mavlink_threadpool.h"
#include "mavlink_init.h"
/* ========================== 1. 基础定义（与原工程一致） ============================ */

/* 定义函数指针类型 - 与原工程完全一致 */
typedef void (*mavlink_action_func)(void* context, const mavlink_message_t* msg);

/* 通信协议类型 */
typedef enum {
    MAVLINK_PROTOCOL_UDP = 0,
    MAVLINK_PROTOCOL_TCP = 1,
    MAVLINK_PROTOCOL_UART = 2
} mavlink_protocol_t;

/* 统一通信上下文结构体 */
typedef struct {
    mavlink_protocol_t protocol;    // 通信协议类型
    union {
        struct {
            int socket_fd;              // UDP/TCP socket文件描述符
            struct sockaddr_in* addr;   // 目标地址
            socklen_t addr_len;         // 地址长度
        } network;
        struct {
            int uart_fd;                // 串口文件描述符
            const char* device_path;     // 设备路径
            uint32_t baud_rate;         // 波特率
        } uart;
    } transport;
    uint8_t system_id;                // 系统ID
    uint8_t component_id;             // 组件ID
} mavlink_unified_context_t;

/* ========================== 2. 命令映射表结构（与原工程结构一致） ============================ */

/* 命令映射表结构体 - 与原工程完全一致 */
typedef struct {
    uint32_t msg_id;           // MAVLink消息ID
    uint16_t command_id;       // 具体命令ID（对于COMMAND_LONG消息）
    mavlink_action_func cmd_fun; // 命令处理函数
    uint32_t priority;         // 命令优先级
    const char* description;   // 命令描述
} MAVLINK_ACTION_S;

/* ========================== 3. 全局变量声明 ============================ */

/* 全局命令映射表 */
extern MAVLINK_ACTION_S g_mavlink_action_tab[];
extern const uint32_t g_mavlink_action_count;

/* 全局状态变量 */
extern int current_camera_mode;        // 当前相机模式
extern bool simulate_autopilot;       // 模拟飞控控制变量
extern bool camera_button_pressed;    // 相机按键状态变量

/* 线程池支持 */
extern mavlink_threadpool_t* g_mavlink_threadpool;

/* ========================== 4. 命令处理函数声明 ============================ */

/* 具体的命令处理函数声明 */
void mavlink_action_heartbeat(void* context, const mavlink_message_t* msg);
void mavlink_action_image_start_capture(void* context, const mavlink_message_t* msg);
void mavlink_action_video_start_capture(void* context, const mavlink_message_t* msg);
void mavlink_action_video_stop_capture(void* context, const mavlink_message_t* msg);
void mavlink_action_request_video_stream_info(void* context, const mavlink_message_t* msg);
void mavlink_action_request_video_stream_status(void* context, const mavlink_message_t* msg);
void mavlink_action_request_camera_settings(void* context, const mavlink_message_t* msg);
void mavlink_action_request_camera_capture_status(void* context, const mavlink_message_t* msg);
void mavlink_action_set_camera_mode(void* context, const mavlink_message_t* msg);
void mavlink_action_set_message_interval(void* context, const mavlink_message_t* msg);
void mavlink_action_camera_track_point(void* context, const mavlink_message_t* msg);
void mavlink_action_camera_stop_tracking(void* context, const mavlink_message_t* msg);
void mavlink_action_camera_track_rectangle(void* context, const mavlink_message_t* msg);
void mavlink_action_video_start_streaming(void* context, const mavlink_message_t* msg);
void mavlink_action_video_stop_streaming(void* context, const mavlink_message_t* msg);
void mavlink_action_request_camera_info(void* context, const mavlink_message_t* msg);
void mavlink_action_camera_reset_settings(void* context, const mavlink_message_t* msg);
void mavlink_action_set_camera_zoom(void* context, const mavlink_message_t* msg);
void mavlink_action_set_camera_focus(void* context, const mavlink_message_t* msg);

/* 协议特定初始化函数 */
int mavlink_udp_init(void);
int mavlink_uart_init(void);

/* 协议特定清理函数 */
void mavlink_udp_deinit(void);

/* 标准MAVLink命令处理函数 */
void mavlink_action_gimbal_control(void* context, const mavlink_message_t* msg);
void mavlink_action_zoom_control(void* context, const mavlink_message_t* msg);
void mavlink_action_camera_source(void* context, const mavlink_message_t* msg);
void mavlink_action_camera_mode(void* context, const mavlink_message_t* msg);




/* 线程池支持函数 */
int mavlink_threadpool_init_simple(void);
void mavlink_threadpool_cleanup(void);

/* 命令查找函数 */
mavlink_action_func find_command_handler(uint32_t msg_id, uint16_t command_id);

/* ========================== 5. 相机捕获功能函数声明 ============================ */

/* 拍照和录像功能 */
int take_photo(void);
int start_video_recording(void);
int stop_video_recording(void);
bool is_video_recording(void);

/* 分辨率管理 */
typedef enum {
    RESOLUTION_1080P = 0,
    RESOLUTION_720P = 1,
    RESOLUTION_MAX
} video_resolution_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    float framerate;
    const char* name;
} resolution_config_t;

const resolution_config_t* get_current_resolution(void);
int set_video_resolution(video_resolution_t resolution);

/* 路径管理 */
char *mavlink_get_capture_path(void);
char *mavlink_get_record_path(void);

#ifdef __cplusplus
}
#endif

#endif