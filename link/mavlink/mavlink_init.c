// MAVLink通信总调度器
// 负责初始化和协调UDP、串口通信模块

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include "ss_log.h"
#include <mavlink_v2/common/mavlink.h>
#include "mavlink_action.h"
#include "mavlink_threadpool.h"
#include "mavlink_uart.h"
#include "mavlink_udp.h"
#include "mavlink_init.h"
// 使用MAVLink协议为独立相机推荐的组件ID范围 (100-106)
#define CAMERA_COMPONENT_ID MAV_COMP_ID_CAMERA // 100

// 这样QGC能正确识别相机，同时避免与飞控冲突
const uint8_t systemid = 1; // 相机系统ID
const uint8_t autopilot_systemid = 1; // 模拟飞控系统ID
const uint8_t camera_component_id = MAV_COMP_ID_CAMERA; // 相机组件ID

// 全局状态变量
int current_camera_mode = 0;              // 当前相机模式
bool simulate_autopilot = false;          // 模拟飞控控制变量
bool camera_button_pressed = false;       // 相机按键状态变量

// UDP模块函数声明
int mavlink_udp_init(void);
int mavlink_udp_main(void);
void mavlink_udp_deinit(void);




// 消息发送函数声明
void send_autopilot_heartbeat(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_heartbeat(int socket_fd, const struct sockaddr_in* src_addr, socklen_t src_addr_len);
void send_command_ack(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len, uint16_t command, uint8_t result);
void send_camera_information(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_camera_capture_status(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_video_stream_status(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_video_stream_information(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_camera_settings(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);

/* ========================== 1. 全局变量定义 ============================ */

// 通信线程
static pthread_t g_udp_thread = 0;
static pthread_t g_uart_thread = 0;
static bool g_udp_running = false;
static bool g_uart_running = false;

/* ========================== 2. 线程函数 ============================ */

/* UDP通信线程 */
static void* udp_thread_func(void* arg) {
    ss_log_i("UDP communication thread started\n");
    
    // 初始化UDP通信
    if (mavlink_udp_init() != 0) {
        ss_log_e("Failed to initialize UDP communication\n");
        return NULL;
    }
    
    // 运行UDP主循环
    mavlink_udp_main();
    
    // 清理UDP通信
    mavlink_udp_deinit();
    
    ss_log_i("UDP communication thread stopped\n");
    return NULL;
}

/* 串口通信线程 */
static void* uart_thread_func(void* arg) {
    ss_log_i("UART communication thread started\n");
    
    // 初始化串口通信
    if (mavlink_uart_init() != 0) {
        ss_log_e("Failed to initialize UART communication\n");
        return NULL;
    }
    
    // 运行串口主循环
    mavlink_uart_main();
    
    // 清理串口通信
    mavlink_uart_deinit();
    
    ss_log_i("UART communication thread stopped\n");
    return NULL;
}


/**
 * @brief 停止MAVLink通信
 * 
 * 这个函数用于优雅地停止MAVLink通信
 */
void mavlink_stop(void)
{
    ss_log_i("Stopping MAVLink communication...\n");
    
    // 设置停止标志
    g_udp_running = false;
    g_uart_running = false;
    
    // 等待线程结束
    if (g_udp_thread) {
        pthread_join(g_udp_thread, NULL);
        g_udp_thread = 0;
    }
    
    if (g_uart_thread) {
        pthread_join(g_uart_thread, NULL);
        g_uart_thread = 0;
    }
    
    ss_log_i("MAVLink communication stopped\n");
}

/* ========================== 4. 消息发送函数实现 ============================ */

/**
 * @brief 发送模拟飞控心跳包
 * 当检测不到真实飞控时，发送模拟飞控心跳让QGC认为有飞控存在
 */
void send_autopilot_heartbeat(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len)
{
    mavlink_message_t message;
    
    // 创建飞控心跳包（系统ID=1，组件ID=1）
    mavlink_msg_heartbeat_pack_chan(
        autopilot_systemid,            // 系统ID = 1 (飞控)
        MAV_COMP_ID_AUTOPILOT1,        // 组件ID = 1 (主飞控)
        MAVLINK_COMM_0,                // 通信通道
        &message,                       // 消息对象
        MAV_TYPE_QUADROTOR,            // 飞行器类型 - 四旋翼
        MAV_AUTOPILOT_PX4,             // 自动驾驶仪类型 - PX4
        MAV_MODE_FLAG_CUSTOM_MODE_ENABLED | MAV_MODE_FLAG_STABILIZE_ENABLED | MAV_MODE_FLAG_GUIDED_ENABLED | MAV_MODE_FLAG_AUTO_ENABLED,  // 基础模式
        0,                              // 自定义模式
        MAV_STATE_STANDBY               // 系统状态 - 待机
    );
    
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(buffer, &message);
    
    int ret = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)dest_addr, dest_len);
    if (ret != len) {
        ss_log_e("Failed to send autopilot heartbeat: %s", strerror(errno));
    } else {
        ss_log_d("Sent simulated autopilot heartbeat (sysid=1, compid=1)");
    }
}

/**
 * @brief 发送相机心跳包
 */
void send_heartbeat(int socket_fd, const struct sockaddr_in* src_addr, socklen_t src_addr_len)
{
    mavlink_message_t message;

    const uint8_t base_mode = 0;
    const uint8_t custom_mode = 0;
    
    // 使用pack函数而不是pack_chan
    mavlink_msg_heartbeat_pack(
        systemid,                      // 系统ID = 1 (相机)
        camera_component_id,           // 组件ID = 100 (相机)
        &message,                       // 消息对象
        MAV_TYPE_CAMERA,               // 飞行器类型
        MAV_AUTOPILOT_INVALID,         // 自动驾驶仪类型
        base_mode,                     // 基础模式
        custom_mode,                   // 自定义模式
        MAV_STATE_STANDBY              // 系统状态
    );

    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(buffer, &message);

    int ret = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)src_addr, src_addr_len);
    if (ret != len) {
        ss_log_e("Failed to send heartbeat: %s", strerror(errno));
    } else {
        ss_log_d("Sent heartbeat (sysid=%d, compid=%d)", systemid, camera_component_id);
    }
}

/**
 * @brief 发送命令确认
 */
void send_command_ack(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len, 
                     uint16_t command, uint8_t result) {
    mavlink_message_t msg;
    mavlink_command_ack_t ack;
    
    ack.command = command;
    ack.result = result;
    ack.progress = 0;
    ack.result_param2 = 0;
    ack.target_system = 0;  // 应答给发送者
    ack.target_component = 0; // 应答给发送者组件
    
    // MAVLink 2.0的encode函数可以处理协议兼容性
    mavlink_msg_command_ack_encode(systemid, camera_component_id, &msg, &ack);
    
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(buffer, &msg);
    
    int send_result = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)dest_addr, dest_len);
    if (send_result != len) {
        ss_log_e("❌ Failed to send command ACK: %s", strerror(errno));
    } else {
        ss_log_i("✅ Sent ACK for command %d with result %d", command, result);
    }
}

/**
 * @brief 发送相机信息消息
 * 这是关键消息，QGC需要这个信息才能识别设备为相机
 */
void send_camera_information(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len)
{
    mavlink_message_t message;
    mavlink_camera_information_t cam_info;
    
    // 填充相机信息
    memset(&cam_info, 0, sizeof(cam_info));
    
    // 设置相机基本信息
    cam_info.time_boot_ms = 0;
    cam_info.firmware_version = 1;

    // 启用参数支持，让QGC显示参数设置界面
    // 添加红外和变焦模式支持 - 使用标准MAVLink协议定义
    cam_info.flags = CAMERA_CAP_FLAGS_CAPTURE_IMAGE | CAMERA_CAP_FLAGS_CAPTURE_VIDEO | CAMERA_CAP_FLAGS_HAS_VIDEO_STREAM | CAMERA_CAP_FLAGS_HAS_MODES | CAMERA_CAP_FLAGS_CAN_CAPTURE_IMAGE_IN_VIDEO_MODE | CAMERA_CAP_FLAGS_CAN_CAPTURE_VIDEO_IN_IMAGE_MODE | CAMERA_CAP_FLAGS_HAS_IMAGE_SURVEY_MODE | CAMERA_CAP_FLAGS_HAS_BASIC_ZOOM | CAMERA_CAP_FLAGS_HAS_BASIC_FOCUS | CAMERA_CAP_FLAGS_HAS_TRACKING_POINT | CAMERA_CAP_FLAGS_HAS_TRACKING_RECTANGLE | CAMERA_CAP_FLAGS_HAS_TRACKING_GEO_STATUS | CAMERA_CAP_FLAGS_HAS_THERMAL_RANGE | CAMERA_CAP_FLAGS_HAS_MTI;
    
    // 正确填充字符串数组 - 使用更具体的相机信息
    strncpy((char*)cam_info.vendor_name, "Sensing", sizeof(cam_info.vendor_name) - 1);
    cam_info.vendor_name[sizeof(cam_info.vendor_name) - 1] = '\0';
    
    strncpy((char*)cam_info.model_name, "Camera (IR+Zoom)", sizeof(cam_info.model_name) - 1);
    cam_info.model_name[sizeof(cam_info.model_name) - 1] = '\0';
    
    // 设置相机URI和UUID，帮助QGC正确识别
    // 添加相机定义文件URI，告诉QGC我们支持红外和变焦模式
    strncpy((char*)cam_info.cam_definition_uri, "mavlink://camera_definition.xml", sizeof(cam_info.cam_definition_uri) - 1);
    cam_info.cam_definition_uri[sizeof(cam_info.cam_definition_uri) - 1] = '\0';
    cam_info.cam_definition_version = 1;  // 相机定义版本

    
    // 设置分辨率信息（可选，设为0表示未知）
    cam_info.resolution_h = 1920;  // 未知水平分辨率
    cam_info.resolution_v = 1080;  // 未知垂直分辨率

    // 传感器尺寸（1/2.3英寸传感器）
    cam_info.sensor_size_h = 6.17f;  // 水平尺寸：6.17mm
    cam_info.sensor_size_v = 4.55f;  // 垂直尺寸：4.55mm
    
    // 焦距（典型消费级无人机值）
    cam_info.focal_length = 4.5f;    // 焦距：4.5mm

    // 镜头信息
    cam_info.lens_id = 1;  // 单镜头系统
           
    // 编码相机信息消息 - MAVLink 2.0的encode函数可以处理协议兼容性
    mavlink_msg_camera_information_encode(systemid, camera_component_id, &message, &cam_info);
    
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(buffer, &message);

    int ret = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)dest_addr, dest_len);
    if (ret != len) {
        ss_log_e("Failed to send camera information: %s\n", strerror(errno));
    } else {
        ss_log_i("Sent camera information to QGC");
    }
}

/**
 * @brief 发送相机捕获状态消息
 * 这个消息告诉QGC相机的当前状态，包括是否正在录像等
 */
void send_camera_capture_status(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len)
{
    mavlink_message_t message;
    mavlink_camera_capture_status_t capture_status;
    
    // 填充相机捕获状态信息
    memset(&capture_status, 0, sizeof(capture_status));
    
    capture_status.time_boot_ms = 0;  // 系统启动时间（毫秒）
    capture_status.image_status = 0;  // 图像捕获状态：0=空闲，1=捕获中，2=间隔捕获
    capture_status.video_status = is_video_recording() ? 1 : 0;  // 视频状态：0=空闲，1=录制中
    capture_status.image_interval = 0.0f;  // 图像捕获间隔
    capture_status.recording_time_ms = 0;  // 录制时间（毫秒）
    capture_status.available_capacity = 1000.0f;  // 可用存储容量（MB）
    capture_status.camera_device_id = 0;  // 相机设备ID
    
    // 编码相机捕获状态消息 - MAVLink 2.0的encode函数可以处理协议兼容性
    mavlink_msg_camera_capture_status_encode(systemid, camera_component_id, &message, &capture_status);
    
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(buffer, &message);
    
    int ret = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)dest_addr, dest_len);
    if (ret != len) {
        ss_log_e("Failed to send camera capture status: %s", strerror(errno));
    } else {
        ss_log_d("Sent camera capture status (video_status: %d)", capture_status.video_status);
    }
}

/**
 * @brief 发送视频流状态消息
 * 告诉QGC当前视频流的状态
 */
void send_video_stream_status(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len)
{
    mavlink_message_t message;
    mavlink_video_stream_status_t stream_status;
    
    memset(&stream_status, 0, sizeof(stream_status));
    
    // 获取当前分辨率配置
    const resolution_config_t* current_res = get_current_resolution();
    
    // 当前视频流状态
    stream_status.stream_id = 0;
    stream_status.flags = VIDEO_STREAM_STATUS_FLAGS_RUNNING;
    stream_status.framerate = current_res->framerate;  // 当前帧率
    
    // 当前分辨率
    stream_status.resolution_h = current_res->width;
    stream_status.resolution_v = current_res->height;

    // 比特率等信息（根据分辨率动态调整）
    if (current_res->width == 1920 && current_res->height == 1080) {
        stream_status.bitrate = 8000;  // 8Mbps for 1080p
    } else if (current_res->width == 1280 && current_res->height == 720) {
        stream_status.bitrate = 4000;  // 4Mbps for 720p
    } else {
        stream_status.bitrate = 2000;  // 2Mbps for 480p
    }
    
    stream_status.rotation = 0;   // 旋转角度
    stream_status.hfov = 80;      // 水平视野角度
    
    mavlink_msg_video_stream_status_encode(systemid, camera_component_id, &message, &stream_status);
    
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    const int len = mavlink_msg_to_send_buffer(buffer, &message);
    
    int ret = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)dest_addr, dest_len);
    if (ret != len) {
        ss_log_e("Failed to send video stream status: %s", strerror(errno));
    } else {
        ss_log_d("Sent video stream status: %dx%d @ %.1ffps (bitrate: %dkbps)", 
                stream_status.resolution_h, stream_status.resolution_v, 
                stream_status.framerate, stream_status.bitrate);
    }
}

/**
 * @brief 发送视频流信息消息
 * 告诉QGC相机支持哪些分辨率
 */
void send_video_stream_information(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len)
{
    // 支持的分辨率配置 - 提供三种相机模式：正常、变焦、红外
    static const resolution_config_t supported_resolutions[] = {
        {1920, 1080, 30.0f, "1080p Normal"}, // 正常相机模式
        {1920, 1080, 30.0f, "1080p Zoom"},   // 变焦模式
        {1920, 1080, 30.0f, "1080p IR"}      // 红外模式
    };
    
    const int num_resolutions = sizeof(supported_resolutions) / sizeof(supported_resolutions[0]);
    
    // 发送每个分辨率的信息
    for (int i = 0; i < num_resolutions; i++) {
        mavlink_message_t message;
        mavlink_video_stream_information_t stream_info;
        
        memset(&stream_info, 0, sizeof(stream_info));
        
        // 设置视频流基本信息
        stream_info.stream_id = i;  // 使用索引作为stream_id
        stream_info.count = num_resolutions;  // 支持的分辨率总数
        stream_info.flags = VIDEO_STREAM_STATUS_FLAGS_RUNNING;  

        // 设置分辨率信息
        stream_info.resolution_h = supported_resolutions[i].width;
        stream_info.resolution_v = supported_resolutions[i].height;
        stream_info.framerate = supported_resolutions[i].framerate;

        // 设置流状态和类型
        stream_info.type = VIDEO_STREAM_TYPE_RTSP;  // 使用RTSP流类型

        stream_info.camera_device_id = 0;  //   相机设备ID
        
        // 流名称
        char stream_name[32];
        snprintf(stream_name, sizeof(stream_name), "%s", supported_resolutions[i].name);
        strncpy((char*)stream_info.name, stream_name, sizeof(stream_info.name));
        
        // 设置URI（RTSP流地址）- 根据模式设置不同的URI
        char uri[160];
        if (i == 0) {
            // 正常相机模式
            snprintf(uri, sizeof(uri), "rtsp://192.168.144.253:8554/video0");
        } else if (i == 1) {
            // 变焦模式
            snprintf(uri, sizeof(uri), "rtsp://192.168.144.253:8554/video1");
        } else {
            // 红外模式
            snprintf(uri, sizeof(uri), "rtsp://192.168.144.253:8554/video2");
        }
        strncpy((char*)stream_info.uri, uri, sizeof(stream_info.uri));
        
        // 编码并发送
        mavlink_msg_video_stream_information_encode(systemid, camera_component_id, &message, &stream_info);
        
        uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
        const int len = mavlink_msg_to_send_buffer(buffer, &message);
        
        int ret = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)dest_addr, dest_len);
        if (ret != len) {
            ss_log_e("Failed to send video stream information for %s: %s\n", 
                    supported_resolutions[i].name, strerror(errno));
        } else {
            ss_log_i("Sent video stream information: %s (stream_id=%d, URI=%s)\n",
                    supported_resolutions[i].name, stream_info.stream_id, uri);
        }
        
        // 短暂延迟，避免消息发送过快
        usleep(10000); // 10ms
    }
    
    ss_log_i("✅ Sent all %d video stream information messages to QGC\n", num_resolutions);
}

/**
 * @brief 发送相机设置信息
 * 告诉QGC相机支持哪些设置选项（红外、变焦等）
 */
void send_camera_settings(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len)
{
    mavlink_message_t message;
    mavlink_camera_settings_t camera_settings;
    
    memset(&camera_settings, 0, sizeof(camera_settings));
    
    // 设置时间戳
    camera_settings.time_boot_ms = 0;
    
    // 设置当前相机模式
    // 根据MAVLink协议，mode_id应该是实际的相机模式值
    // CAMERA_MODE_IMAGE=0 (拍照模式), CAMERA_MODE_VIDEO=1 (视频模式)
    if (current_camera_mode == 0) {
        camera_settings.mode_id = CAMERA_MODE_IMAGE;  // 拍照模式
    } else if (current_camera_mode == 1) {
        camera_settings.mode_id = CAMERA_MODE_VIDEO;  // 视频模式
    } else {
        camera_settings.mode_id = CAMERA_MODE_IMAGE;  // 默认拍照模式
    }
    
    camera_settings.zoomLevel = 0.0f;  // 不使用变焦
    camera_settings.focusLevel = 0.0f; // 不使用对焦
    
    // 发送当前相机模式设置
    mavlink_msg_camera_settings_encode(systemid, camera_component_id, &message, &camera_settings);
    
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    int len = mavlink_msg_to_send_buffer(buffer, &message);
    
    int ret = sendto(socket_fd, buffer, len, 0, (const struct sockaddr*)dest_addr, dest_len);
    if (ret != len) {
        ss_log_e("Failed to send camera mode settings: %s", strerror(errno));
    } else {
        ss_log_i("Sent camera mode settings (current mode: %d, mode_id: %d)", current_camera_mode, camera_settings.mode_id);
    }
}




/* ========================== 3. 总调度器函数 ============================ */

/**
 * @brief MAVLink通信总调度器主函数
 * 
 * 这个函数是MAVLink通信的入口点，负责：
 * 1. 初始化线程池
 * 2. 启动UDP通信线程
 * 3. 启动串口通信线程
 * 4. 等待通信线程结束
 * 
 * @return int 0表示成功，负数表示失败
 */
int mavlink_main(void)
{
    ss_log_i("MAVLink communication scheduler starting...\n");
    
    // 初始化线程池
    if (mavlink_threadpool_init_simple() != 0) {
        ss_log_e("Failed to initialize thread pool, continuing with synchronous processing\n");
    }
    
    // 初始化状态变量
    current_camera_mode = 0; // 默认正常模式
    simulate_autopilot = true;  // 默认启动模拟飞控
    camera_button_pressed = false;
    
    //启动UDP通信线程
    g_udp_running = true;
    if (pthread_create(&g_udp_thread, NULL, udp_thread_func, NULL) != 0) {
        ss_log_e("Failed to create UDP communication thread\n");
        g_udp_running = false;
        return -1;
    }
    
    // 启动串口通信线程
    g_uart_running = true;
    if (pthread_create(&g_uart_thread, NULL, uart_thread_func, NULL) != 0) {
        ss_log_e("Failed to create UART communication thread\n");
        g_uart_running = false;
        
        // 停止UDP线程
        g_udp_running = false;
        if (g_udp_thread) {
            pthread_join(g_udp_thread, NULL);
        }
        
        return -1;
    }
    
    ss_log_i("✅ MAVLink communication scheduler started successfully\n");
    ss_log_i("✅ UDP communication: port 14550\n");
    ss_log_i("✅ UART communication: /dev/ttyAMA5, 115200 baud\n");
    
    // 等待通信线程结束
    while (g_udp_running || g_uart_running) {
        sleep(1);
   //#if 0      
        // 检查线程状态
        if (g_udp_running) {
            int ret = pthread_join(g_udp_thread, NULL);
            if (ret == 0) {
                g_udp_running = false;
                ss_log_i("UDP communication thread finished\n");
            }
        }
        
        if (g_uart_running) {
            int ret = pthread_join(g_uart_thread, NULL);
            if (ret == 0) {
                g_uart_running = false;
                ss_log_i("UART communication thread finished\n");
            }
        }
    //#endif
    }
    
    // 清理线程池
    mavlink_threadpool_cleanup();
    
    ss_log_i("MAVLink communication scheduler stopped\n");
    return 0;
}



