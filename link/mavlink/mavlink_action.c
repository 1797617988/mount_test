#include "mavlink_action.h"
#include "ss_log.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* ========================== 1. 全局变量定义 ============================ */

// 全局状态变量（在mavlink_init.c中定义，这里使用外部引用）
extern int current_camera_mode;           // 当前相机模式
extern bool simulate_autopilot;           // 模拟飞控控制变量
extern bool camera_button_pressed;        // 相机按键状态变量

// 简单的路径定义
#define CAPTURE_PATH 	"/tmp/media/capture"
#define RECORD_PATH		"/tmp/media/record"

// 分辨率配置表 - 统一使用30fps
static const resolution_config_t resolution_configs[RESOLUTION_MAX] = {
    {1920, 1080, 30.0f, "1080p"}, // RESOLUTION_1080P - 主视频流
    {1280, 720, 30.0f, "720p"},   // RESOLUTION_720P  - 备用视频流
};

// 当前分辨率状态
static video_resolution_t current_resolution = RESOLUTION_1080P;

// 录像状态跟踪
static bool is_recording = false;
static pid_t recording_pid = 0;

// 线程池支持（外部引用）
extern mavlink_threadpool_t* g_mavlink_threadpool;

/* ========================== 2. 命令处理函数声明 ============================ */

// 具体的命令处理函数声明
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
void mavlink_action_camera_track_rectangle(void* context, const mavlink_message_t* msg);
void mavlink_action_camera_stop_tracking(void* context, const mavlink_message_t* msg);
void mavlink_action_video_start_streaming(void* context, const mavlink_message_t* msg);
void mavlink_action_video_stop_streaming(void* context, const mavlink_message_t* msg);
void mavlink_action_request_camera_information(void* context, const mavlink_message_t* msg);
void mavlink_action_reset_camera_settings(void* context, const mavlink_message_t* msg);
void mavlink_action_set_camera_zoom(void* context, const mavlink_message_t* msg);
void mavlink_action_set_camera_focus(void* context, const mavlink_message_t* msg);

// 视频流切换函数声明
void switch_video_stream(int stream_mode);

// 注意：相机状态消息（CAMERA_INFORMATION, CAMERA_SETTINGS等）是相机发送给QGC的
// 不是QGC发送给相机的，所以不应该在这里声明处理函数

/* ========================== 3. 全局命令映射表 ============================ */

/* 全局命令映射表 - 统一使用函数指针映射表机制，类似线程池模式 */
MAVLINK_ACTION_S g_mavlink_action_tab[] = {
    // 心跳消息
    {MAVLINK_MSG_ID_HEARTBEAT, 0, mavlink_action_heartbeat, 10, "心跳消息"},
    
    // 拍照相关命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_IMAGE_START_CAPTURE, mavlink_action_image_start_capture, 5, "开始拍照"},
    
    // 视频相关命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_VIDEO_START_CAPTURE, mavlink_action_video_start_capture, 5, "开始录像"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_VIDEO_STOP_CAPTURE, mavlink_action_video_stop_capture, 5, "停止录像"},
    
    // 视频流相关命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_VIDEO_START_STREAMING, mavlink_action_video_start_streaming, 3, "开始视频流"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_VIDEO_STOP_STREAMING, mavlink_action_video_stop_streaming, 3, "停止视频流"},
    
    // 信息请求命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_REQUEST_CAMERA_INFORMATION, mavlink_action_request_camera_information, 1, "请求相机信息"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION, mavlink_action_request_video_stream_info, 1, "请求视频流信息"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_REQUEST_VIDEO_STREAM_STATUS, mavlink_action_request_video_stream_status, 1, "请求视频流状态"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_REQUEST_CAMERA_SETTINGS, mavlink_action_request_camera_settings, 1, "请求相机设置"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS, mavlink_action_request_camera_capture_status, 1, "请求相机捕获状态"},
    
    // 相机控制命令
    // 相机控制命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_IMAGE_START_CAPTURE, mavlink_action_image_start_capture, 4, "开始拍照"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_VIDEO_START_CAPTURE, mavlink_action_video_start_capture, 4, "开始录像"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_VIDEO_STOP_CAPTURE, mavlink_action_video_stop_capture, 4, "停止录像"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_RESET_CAMERA_SETTINGS, mavlink_action_reset_camera_settings, 4, "重置相机设置"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_FOCUS, mavlink_action_set_camera_focus, 4, "设置相机对焦"},
    
    // 跟踪命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_CAMERA_TRACK_POINT, mavlink_action_camera_track_point, 6, "点跟踪"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_CAMERA_TRACK_RECTANGLE, mavlink_action_camera_track_rectangle, 6, "矩形跟踪"},
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_CAMERA_STOP_TRACKING, mavlink_action_camera_stop_tracking, 6, "停止跟踪"},
    
    // 其他命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_MESSAGE_INTERVAL, mavlink_action_set_message_interval, 2, "设置消息间隔"},
    
    // QGC界面标准命令
    // 云台控制命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_DO_MOUNT_CONTROL, mavlink_action_gimbal_control, 5, "云台控制"},
    
    // 变焦控制命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_ZOOM, mavlink_action_zoom_control, 5, "相机变焦"},
    
    // 视频源选择命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_SOURCE, mavlink_action_camera_source, 5, "相机源选择"},
    
    // 相机模式命令
    {MAVLINK_MSG_ID_COMMAND_LONG, MAV_CMD_SET_CAMERA_MODE, mavlink_action_set_camera_mode, 5, "相机模式"},
    
    // 注意：相机状态消息（CAMERA_INFORMATION, CAMERA_SETTINGS等）是相机发送给QGC的
    // 不是QGC发送给相机的，所以不应该在这里处理
};

const uint32_t g_mavlink_action_count = sizeof(g_mavlink_action_tab) / sizeof(g_mavlink_action_tab[0]);

/* ========================== 4. 线程池支持函数 ============================ */

/* 初始化线程池 */
int mavlink_threadpool_init_simple(void) {
    if (g_mavlink_threadpool) {
        ss_log_w("Thread pool already initialized");
        return 0;
    }
    
    // 创建线程池，使用4个工作线程
    g_mavlink_threadpool = mavlink_threadpool_create(4);
    if (!g_mavlink_threadpool) {
        ss_log_e("Failed to initialize thread pool");
        return -1;
    }
    
    ss_log_i("MAVLink thread pool initialized successfully");
    return 0;
}

/* 销毁线程池 */
void mavlink_threadpool_cleanup(void) {
    if (g_mavlink_threadpool) {
        mavlink_threadpool_destroy(g_mavlink_threadpool);
        g_mavlink_threadpool = NULL;
        ss_log_i("MAVLink thread pool destroyed");
    }
}

/* ========================== 5. 异步命令处理 ============================ */

/* 异步命令处理上下文 */
typedef struct {
    mavlink_unified_context_t* context;
    mavlink_message_t* msg;
    mavlink_action_func handler;
    const char* description;
} async_command_context_t;

/* 异步命令处理函数 */
static void async_command_handler(void* arg) {
    async_command_context_t* ctx = (async_command_context_t*)arg;
    
    if (ctx && ctx->handler) {
        ss_log_i("🔄 Processing command asynchronously: %s", ctx->description);
        ctx->handler(ctx->context, ctx->msg);
    }
    
    // 释放上下文内存
    if (ctx) {
        if (ctx->msg) free(ctx->msg);
        if (ctx->context) free(ctx->context);
        free(ctx);
    }
}

/* ========================== 6. 命令分发函数 ============================ */

/* 查找命令处理函数 - 统一使用映射表机制 */
mavlink_action_func find_command_handler(uint32_t msg_id, uint16_t command_id) {
    for (uint32_t i = 0; i < g_mavlink_action_count; i++) {
        if (g_mavlink_action_tab[i].msg_id == msg_id) {
            // 对于COMMAND_LONG消息，需要匹配具体的命令ID
            if (msg_id == MAVLINK_MSG_ID_COMMAND_LONG) {
                if (g_mavlink_action_tab[i].command_id == command_id) {
                    return g_mavlink_action_tab[i].cmd_fun;
                }
            } else {
                // 对于其他消息类型，直接匹配消息ID
                return g_mavlink_action_tab[i].cmd_fun;
            }
        }
    }
    return NULL;
}



/* ========================== 5. 工具函数 ============================ */



/* ========================== 6. 相机捕获功能实现 ============================ */

/* 路径管理函数 */
char *mavlink_get_capture_path(void) {
    return CAPTURE_PATH;
}

char *mavlink_get_record_path(void) {
    return RECORD_PATH;
}

/* 获取当前分辨率配置 */
const resolution_config_t* get_current_resolution(void) {
    return &resolution_configs[current_resolution];
}

/* 设置视频分辨率 */
int set_video_resolution(video_resolution_t resolution) {
    if (resolution >= RESOLUTION_MAX) {
        ss_log_e("Invalid resolution index: %d", resolution);
        return -1;
    }
    
    // 如果正在录像，需要先停止录像
    if (is_recording) {
        ss_log_w("Cannot change resolution while recording. Stop recording first.");
        return -1;
    }
    
    const resolution_config_t* new_res = &resolution_configs[resolution];
    ss_log_i("Setting video resolution to %s (%dx%d @ %.1ffps)", 
             new_res->name, new_res->width, new_res->height, new_res->framerate);
    
    current_resolution = resolution;
    
    // 这里可以添加实际的分辨率切换逻辑
    // 例如：调用摄像头API设置分辨率
    
    return 0;
}

/* 拍照功能实现 */
int take_photo(void) {
    char filename[256];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    // 获取当前视频流分辨率配置
    const resolution_config_t* current_res = get_current_resolution();
    
    // 生成文件名：photo_分辨率_YYYYMMDD_HHMMSS.jpg
    snprintf(filename, sizeof(filename), 
             "%s/photo_%s_%04d%02d%02d_%02d%02d%02d.jpg", 
             mavlink_get_capture_path(),
             current_res->name,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    
    // 首先确保目录存在（只在第一次运行时创建）
    static bool dir_created = false;
    if (!dir_created) {
        // 使用mkdir创建目录，避免system调用
        char *path = mavlink_get_capture_path();
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
        int ret = system(cmd);
        if (ret == 0) {
            ss_log_i("Directory created successfully: %s", path);
            dir_created = true;
        } else {
            ss_log_e("Failed to create directory: %s", path);
        }
    }
    
    // 使用更快速的文件创建方式（避免system调用）
    FILE *fp = fopen(filename, "w");
    if (fp != NULL) {
        fclose(fp);
        ss_log_i("Photo captured successfully: %s", filename);
        printf("=== PHOTO SAVED TO: %s ===\n", filename);
        printf("=== PHOTO RESOLUTION: %dx%d @ %.1ffps ===\n", 
               current_res->width, current_res->height, current_res->framerate);
        return 0;
    } else {
        ss_log_e("Failed to capture photo: %s", filename);
        printf("=== PHOTO CAPTURE FAILED ===\n");
        return -1;
    }
}

/* 开始录像功能实现 */
int start_video_recording(void) {
    if (is_recording) {
        ss_log_w("Video recording is already in progress");
        return -1;
    }
    
    char filename[256];
    char command[512];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    // 获取当前视频流分辨率配置
    const resolution_config_t* current_res = get_current_resolution();
    
    // 生成文件名：video_分辨率_YYYYMMDD_HHMMSS.mp4
    snprintf(filename, sizeof(filename), 
             "%s/video_%s_%04d%02d%02d_%02d%02d%02d.mp4", 
             mavlink_get_record_path(),
             current_res->name,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    
    // 首先确保目录存在
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", mavlink_get_record_path());
    system(mkdir_cmd);
    
    // 使用系统命令模拟开始录像（这里使用后台进程模拟）
    // 在实际应用中，您需要替换为实际的摄像头录像命令
    // 这里添加分辨率信息到录像命令中
    snprintf(command, sizeof(command), 
             "while true; do echo \"Recording at %dx%d @ %.1ffps...\" >> \"%s\"; sleep 1; done & echo $!",
             current_res->width, current_res->height, current_res->framerate, filename);
    
    ss_log_i("Starting video recording at %dx%d @ %.1ffps: %s", 
             current_res->width, current_res->height, current_res->framerate, filename);
    printf("=== EXECUTING VIDEO START COMMAND: %s ===\n", command);
    
    FILE *fp = popen(command, "r");
    if (fp != NULL) {
        if (fscanf(fp, "%d", &recording_pid) == 1) {
            is_recording = true;
            ss_log_i("Video recording started successfully (PID: %d): %s", 
                     recording_pid, filename);
            printf("=== VIDEO RECORDING STARTED (PID: %d) ===\n", recording_pid);
            printf("=== VIDEO FILE: %s ===\n", filename);
            printf("=== RESOLUTION: %dx%d @ %.1ffps ===\n", current_res->width, current_res->height, current_res->framerate);
            pclose(fp);
            return 0;
        }
        pclose(fp);
    }
    
    ss_log_e("Failed to start video recording: %s", filename);
    printf("=== VIDEO RECORDING FAILED ===\n");
    return -1;
}

/* 停止录像功能实现 */
int stop_video_recording(void) {
    if (!is_recording) {
        ss_log_w("No video recording in progress");
        return -1;
    }
    
    ss_log_i("Stopping video recording (PID: %d)", recording_pid);
    printf("=== STOPPING VIDEO RECORDING (PID: %d) ===\n", recording_pid);
    
    // 停止录像进程
    if (recording_pid > 0) {
        char command[64];
        snprintf(command, sizeof(command), "kill %d", recording_pid);
        printf("=== EXECUTING STOP COMMAND: %s ===\n", command);
        int ret = system(command);
        
        if (ret == 0) {
            ss_log_i("Video recording stopped successfully");
            printf("=== VIDEO RECORDING STOPPED SUCCESSFULLY ===\n");
            is_recording = false;
            recording_pid = 0;
            return 0;
        } else {
            ss_log_e("Failed to stop video recording process");
            printf("=== VIDEO STOP FAILED ===\n");
            return -1;
        }
    }
    
    is_recording = false;
    recording_pid = 0;
    ss_log_i("Video recording stopped");
    printf("=== VIDEO RECORDING STOPPED ===\n");
    return 0;
}

/* 检查录像状态 */
bool is_video_recording(void) {
    return is_recording;
}

/* ========================== 7. 具体的命令处理函数实现 ============================ */

/* 心跳消息处理 */
void mavlink_action_heartbeat(void* context, const mavlink_message_t* msg) {
    // 心跳处理逻辑可以在这里实现
    ss_log_i("Heartbeat message received");
}

/* 拍照命令处理 */
void mavlink_action_image_start_capture(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_IMAGE_START_CAPTURE) {
        ss_log_i("📸 Executing PHOTO CAPTURE command");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("📸 Photo button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        // 立即发送确认，让QGC按钮快速恢复
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 异步执行拍照操作
        int photo_result = take_photo();
        ss_log_i("📸 Photo capture completed, result: %d", photo_result);
    }
}

/* 开始录像命令处理 */
void mavlink_action_video_start_capture(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_VIDEO_START_CAPTURE) {
        ss_log_i("🎥 Executing VIDEO START command");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("🎥 Video start button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        int video_start_result = start_video_recording();
        uint8_t ack_result = (video_start_result == 0) ? MAV_RESULT_ACCEPTED : MAV_RESULT_FAILED;
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, ack_result);
        ss_log_i("🎥 Video start completed, result: %d", video_start_result);
    }
}

/* 停止录像命令处理 */
void mavlink_action_video_stop_capture(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_VIDEO_STOP_CAPTURE) {
        ss_log_i("⏹️ Executing VIDEO STOP command");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("⏹️ Video stop button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        int video_stop_result = stop_video_recording();
        uint8_t ack_result = (video_stop_result == 0) ? MAV_RESULT_ACCEPTED : MAV_RESULT_FAILED;
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, ack_result);
        ss_log_i("⏹️ Video stop completed, result: %d", video_stop_result);
    }
}

/* 视频流信息请求处理 */
void mavlink_action_request_video_stream_info(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_REQUEST_VIDEO_STREAM_INFORMATION) {
        ss_log_i("🎥 Video stream information requested");
        
        // 调用发送视频流信息的函数
        send_video_stream_information(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len);
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 视频流状态请求处理 */
void mavlink_action_request_video_stream_status(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_REQUEST_VIDEO_STREAM_STATUS) {
        ss_log_i("🎥 Video stream status requested");
        
        // 调用发送视频流状态的函数
        send_video_stream_status(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len);
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 相机设置请求处理 */
void mavlink_action_request_camera_settings(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_REQUEST_CAMERA_SETTINGS) {
        ss_log_i("⚙️ Camera settings requested");
        
        // 调用发送相机设置的函数
        send_camera_settings(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len);
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 相机捕获状态请求处理 */
void mavlink_action_request_camera_capture_status(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_REQUEST_CAMERA_CAPTURE_STATUS) {
        ss_log_i("📷 Camera capture status requested");
        
        // 调用发送相机捕获状态的函数
        send_camera_capture_status(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len);
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 设置相机模式处理 */
void mavlink_action_set_camera_mode(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_SET_CAMERA_MODE) {
        ss_log_i("📷 Setting camera mode");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("📷 Camera mode button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        // 根据MAVLink协议，param2是相机模式值
        // CAMERA_MODE_IMAGE=0 (拍照模式), CAMERA_MODE_VIDEO=1 (视频模式)
        int camera_mode = (int)cmd.param2;
        
        ss_log_i("📷 Received mode change request: param1=%d, param2=%d (mode)", 
                 (int)cmd.param1, camera_mode);
        
        // 验证模式值
        if (camera_mode != CAMERA_MODE_IMAGE && camera_mode != CAMERA_MODE_VIDEO) {
            ss_log_e("Invalid camera mode: %d (expected 0=IMAGE or 1=VIDEO)", camera_mode);
            send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_FAILED);
            return;
        }
        
        // 更新当前模式
        current_camera_mode = camera_mode;
        
        // 记录模式切换
        const char* mode_names[] = {"拍照模式", "视频模式"};
        ss_log_i("✅ Camera mode set to %s (mode=%d)", mode_names[camera_mode], camera_mode);
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 发送更新后的设置
        send_camera_settings(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len);
    }
}

/* 设置消息间隔处理 */
void mavlink_action_set_message_interval(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_SET_MESSAGE_INTERVAL) {
        ss_log_i("📊 Setting message interval");
        
        uint32_t message_id = (uint32_t)cmd.param1;
        int32_t interval_ms = (int32_t)cmd.param2;
        ss_log_i("📊 Message ID: %u, Interval: %d ms", message_id, interval_ms);
        
        // 立即发送命令确认，让QGC按钮快速恢复
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 根据MAVLink协议，设置消息间隔后需要发送对应的消息
        // 这里可以添加特定消息的发送逻辑
        ss_log_i("✅ Message interval set successfully");
    }
}

/* 相机跟踪点处理 */
void mavlink_action_camera_track_point(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_CAMERA_TRACK_POINT) {
        ss_log_i("🎯 Starting point tracking");
        
        float point_x = cmd.param1;
        float point_y = cmd.param2;
        float radius = cmd.param3;
        ss_log_i("🎯 Tracking point: (%.2f, %.2f), radius: %.2f", point_x, point_y, radius);
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 停止相机跟踪处理 */
void mavlink_action_camera_stop_tracking(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_CAMERA_STOP_TRACKING) {
        ss_log_i("⏹️ Stopping camera tracking");
        
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 相机矩形跟踪处理 */
void mavlink_action_camera_track_rectangle(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_CAMERA_TRACK_RECTANGLE) {
        ss_log_i("🎯 Starting rectangle tracking");
        
        float top_left_x = cmd.param1;
        float top_left_y = cmd.param2;
        float bottom_right_x = cmd.param3;
        float bottom_right_y = cmd.param4;
        
        ss_log_i("🎯 Tracking rectangle: (%.2f, %.2f) to (%.2f, %.2f)", 
                 top_left_x, top_left_y, bottom_right_x, bottom_right_y);
        
        // 立即发送确认，让QGC按钮快速恢复
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 发送相机跟踪状态消息，让QGC知道跟踪已开始
        // 这里需要实现send_camera_tracking_status函数
        // send_camera_tracking_status(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, 
        //                            CAMERA_TRACKING_STATUS_FLAGS_ACTIVE, CAMERA_TRACKING_MODE_RECTANGLE);
        
        // 这里可以添加实际的矩形跟踪逻辑
        // 例如：start_rectangle_tracking(top_left_x, top_left_y, bottom_right_x, bottom_right_y);
    }
}

/* 开始视频流处理 */
void mavlink_action_video_start_streaming(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_VIDEO_START_STREAMING) {
        ss_log_i("📹 Starting video streaming");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("📹 Video streaming button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        uint8_t stream_id = (uint8_t)cmd.param1;
        ss_log_i("📹 Stream ID: %d", stream_id);
        
        // 立即发送确认，让QGC按钮快速恢复
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 这里可以添加实际的视频流启动逻辑
        // 例如：start_video_stream(stream_id);
    }
}

/* 停止视频流处理 */
void mavlink_action_video_stop_streaming(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_VIDEO_STOP_STREAMING) {
        ss_log_i("⏹️ Stopping video streaming");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("⏹️ Video stop streaming button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        uint8_t stream_id = (uint8_t)cmd.param1;
        ss_log_i("⏹️ Stream ID: %d", stream_id);
        
        // 立即发送确认，让QGC按钮快速恢复
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 这里可以添加实际的视频流停止逻辑
        // 例如：stop_video_stream(stream_id);
    }
}

/* 请求相机信息处理 */
void mavlink_action_request_camera_information(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_REQUEST_CAMERA_INFORMATION) {
        ss_log_i("📷 Requesting camera information");
        
        // 立即发送确认，让QGC按钮快速恢复
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 这里可以添加实际的相机信息发送逻辑
        send_camera_information(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len);
    }
}

/* 重置相机设置处理 */
void mavlink_action_reset_camera_settings(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_RESET_CAMERA_SETTINGS) {
        ss_log_i("🔄 Resetting camera settings to factory defaults");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("🔄 Reset camera settings button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        // 重置相机设置到出厂默认值
        current_camera_mode = 0; // 重置为正常模式
        
        // 立即发送确认，让QGC按钮快速恢复
        // TODO: 需要实现send_command_ack函数
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 发送更新后的相机设置
        // TODO: 需要实现send_camera_settings函数
        // send_camera_settings(socket_fd, dest_addr, dest_len);
    }
}

// /* 设置相机变焦处理 */
// void mavlink_action_set_camera_zoom(void* context, const mavlink_message_t* msg) {
//     mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
//     mavlink_command_long_t cmd;
//     mavlink_msg_command_long_decode(msg, &cmd);
    
//     if (cmd.command == MAV_CMD_SET_CAMERA_ZOOM) {
//         ss_log_i("🔍 Setting camera zoom");
        
//         // 设置相机按键状态并停止模拟飞控
//         camera_button_pressed = true;
//         simulate_autopilot = false;
//         ss_log_i("🔍 Camera zoom button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
//         uint8_t zoom_type = (uint8_t)cmd.param1;
//         float zoom_value = cmd.param2;
        
//         ss_log_i("🔍 Zoom type: %d, value: %.2f", zoom_type, zoom_value);
        
//         // 立即发送确认，让QGC按钮快速恢复
//         // TODO: 需要实现send_command_ack函数
//         send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
//         // 这里可以添加实际的变焦设置逻辑
//         // 例如：set_camera_zoom(zoom_type, zoom_value);
//     }
// }


/* 设置相机对焦处理 */
void mavlink_action_set_camera_focus(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_SET_CAMERA_FOCUS) {
        ss_log_i("🎯 Setting camera focus");
        
        // 设置相机按键状态并停止模拟飞控
        camera_button_pressed = true;
        simulate_autopilot = false;
        ss_log_i("🎯 Camera focus button pressed, setting camera_button_pressed=true, stopping autopilot simulation");
        
        uint8_t focus_type = (uint8_t)cmd.param1;
        float focus_value = cmd.param2;
        
        ss_log_i("🎯 Focus type: %d, value: %.2f", focus_type, focus_value);
        
        // 立即发送确认，让QGC按钮快速恢复
        // 使用通用消息发送接口
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
        
        // 这里可以添加实际的对焦设置逻辑
        // 例如：set_camera_focus(focus_type, focus_value);
    }
}

/* ========================== 9. 标准MAVLink命令处理函数 ============================ */

/* 云台控制处理 - MAV_CMD_DO_MOUNT_CONTROL (205) */
void mavlink_action_gimbal_control(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_DO_MOUNT_CONTROL) {
        float pitch = cmd.param1;  // 俯仰角度
        float roll = cmd.param2;   // 横滚角度
        float yaw = cmd.param3;    // 偏航角度
        
        // 根据参数判断云台控制方向
        if (pitch > 0) {
            ss_log_i("⬆️ 云台上 - 俯仰角度: %.2f度", pitch);
        } else if (pitch < 0) {
            ss_log_i("⬇️ 云台下 - 俯仰角度: %.2f度", pitch);
        }
        
        if (roll > 0) {
            ss_log_i("➡️ 云台右 - 横滚角度: %.2f度", roll);
        } else if (roll < 0) {
            ss_log_i("⬅️ 云台左 - 横滚角度: %.2f度", roll);
        }
        
        if (yaw > 0) {
            ss_log_i("↗️ 云台偏右 - 偏航角度: %.2f度", yaw);
        } else if (yaw < 0) {
            ss_log_i("↖️ 云台偏左 - 偏航角度: %.2f度", yaw);
        }
        
        // 检查是否为回中操作（所有角度接近0）
        if (fabs(pitch) < 0.1 && fabs(roll) < 0.1 && fabs(yaw) < 0.1) {
            ss_log_i("⏺️ 云台回中");
        }
        
        // 发送命令确认
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 变焦控制处理 - MAV_CMD_SET_CAMERA_ZOOM (531) */
void mavlink_action_zoom_control(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_SET_CAMERA_ZOOM) {
        uint8_t zoom_type = (uint8_t)cmd.param1;  // 变焦类型
        float zoom_value = cmd.param2;            // 变焦值
        
        ss_log_i("🔍 变焦控制 - 类型: %d, 值: %.2f", zoom_type, zoom_value);
        
        // 根据变焦值判断是放大还是缩小
        if (zoom_value > 0) {
            ss_log_i("🔍 变焦+ - 放大变焦");
        } else if (zoom_value < 0) {
            ss_log_i("🔍 变焦- - 缩小变焦");
        }
        
        // 发送命令确认
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}

/* 相机源选择处理 - MAV_CMD_SET_CAMERA_SOURCE (534) */
void mavlink_action_camera_source(void* context, const mavlink_message_t* msg) {
    mavlink_unified_context_t* ctx = (mavlink_unified_context_t*)context;
    
    mavlink_command_long_t cmd;
    mavlink_msg_command_long_decode(msg, &cmd);
    
    if (cmd.command == MAV_CMD_SET_CAMERA_SOURCE) {
        uint8_t primary_source = (uint8_t)cmd.param2;   // 主视频源
        uint8_t secondary_source = (uint8_t)cmd.param3; // 次视频源
        
        ss_log_i("🎬 视频源选择 - 主源: %d, 次源: %d", primary_source, secondary_source);
        
        // 根据视频源选择显示不同的模式并执行实际的流切换
        switch (primary_source) {
            case 1:
                ss_log_i("🎬 视频模式1 - 正常模式");
                // 切换到正常模式流: rtsp://192.168.144.253:8554/video0
                switch_video_stream(1);
                break;
            case 2:
                ss_log_i("🎬 视频模式2 - 变焦模式");
                // 切换到变焦模式流: rtsp://192.168.144.253:8554/video1
                switch_video_stream(2);
                break;
            case 3:
                ss_log_i("🎬 视频模式3 - 红外模式");
                // 切换到红外模式流: rtsp://192.168.144.253:8554/video2
                switch_video_stream(3);
                break;
            case 4:
                ss_log_i("🎬 视频模式4 - 变焦+红外模式");
                // 切换到变焦+红外模式流: rtsp://192.168.144.253:8554/video3
                switch_video_stream(4);
                break;
            default:
                ss_log_i("🎬 未知视频模式: %d", primary_source);
                break;
        }
        
        // 发送命令确认
        send_command_ack(ctx->transport.network.socket_fd, ctx->transport.network.addr, ctx->transport.network.addr_len, cmd.command, MAV_RESULT_ACCEPTED);
    }
}



/* ========================== 10. 视频流切换功能实现 ============================ */

/**
 * @brief 切换视频流模式
 * 
 * 根据stream_mode参数切换到对应的视频流：
 * 1 - 正常模式 (rtsp://192.168.144.253:8554/video0)
 * 2 - 变焦模式 (rtsp://192.168.144.253:8554/video1) 
 * 3 - 红外模式 (rtsp://192.168.144.253:8554/video2)
 * 4 - 变焦+红外模式 (rtsp://192.168.144.253:8554/video3)
 * 
 * @param stream_mode 视频流模式 (1-4)
 */
void switch_video_stream(int stream_mode) {
    char command[512];
    const char* stream_names[] = {"未知", "正常模式", "变焦模式", "红外模式", "变焦+红外模式"};
    const char* stream_uris[] = {
        "", 
        "rtsp://192.168.144.253:8554/video0", 
        "rtsp://192.168.144.253:8554/video1", 
        "rtsp://192.168.144.253:8554/video2",
        "rtsp://192.168.144.253:8554/video3"
    };
    
    // 验证模式参数
    if (stream_mode < 1 || stream_mode > 4) {
        ss_log_e("❌ 无效的视频流模式: %d (有效范围: 1-4)", stream_mode);
        return;
    }
    
    ss_log_i("🎬 开始切换到视频流模式: %d (%s)", stream_mode, stream_names[stream_mode]);
    
    // 这里应该实现实际的视频流切换逻辑
    // 例如：停止当前流，启动新流，更新播放器配置等
    
    // 模拟实际的流切换命令（需要根据你的实际播放器/流媒体客户端调整）
    snprintf(command, sizeof(command), 
             "echo '切换到视频流模式 %d: %s (URI: %s)' && \
             # 这里添加实际的流切换命令，例如：\
             # pkill -f 'ffplay.*rtsp' && \
             # ffplay -i %s &", 
             stream_mode, stream_names[stream_mode], stream_uris[stream_mode], stream_uris[stream_mode]);
    
    printf("=== 执行视频流切换命令 ===\n");
    printf("=== 模式: %d (%s) ===\n", stream_mode, stream_names[stream_mode]);
    printf("=== RTSP URI: %s ===\n", stream_uris[stream_mode]);
    printf("=== 命令: %s ===\n", command);
    
    // 执行流切换命令
    int ret = system(command);
    if (ret == 0) {
        ss_log_i("✅ 视频流切换成功: 模式 %d (%s)", stream_mode, stream_names[stream_mode]);
        printf("=== 视频流切换成功 ===\n");
    } else {
        ss_log_e("❌ 视频流切换失败: 模式 %d (%s)", stream_mode, stream_names[stream_mode]);
        printf("=== 视频流切换失败 ===\n");
    }
}

/* ========================== 8. 相机消息处理函数实现 ============================ */

// 注意：相机状态消息（CAMERA_INFORMATION, CAMERA_SETTINGS等）是相机发送给QGC的
// 不是QGC发送给相机的，所以不应该在这里实现处理函数
// 这些消息应该由相机主动发送给QGC，而不是被处理


