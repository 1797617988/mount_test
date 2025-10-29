#include "mavlink_action.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <time.h>
#include "mavlink_uart.h"
#include "ss_log.h"
/* ========================== 1. 全局变量定义 ============================ */

// 串口通信相关变量
static bool g_uart_thread_running = false;
static pthread_t g_uart_receive_thread = 0;
static int g_uart_fd = -1;

// QGC连接状态
static bool g_uart_qgc_connected = false;
static time_t g_uart_last_qgc_message = 0;

// 飞控检测状态
static bool g_uart_real_autopilot_detected = false;
static time_t g_uart_last_real_autopilot_hb = 0;

// 协议版本标志
static bool g_uart_use_mavlink_v1 = false;

// 全局状态变量
extern int current_camera_mode;           // 当前相机模式
extern bool simulate_autopilot;           // 模拟飞控控制变量
extern bool camera_button_pressed;        // 相机按键状态变量

// 消息发送函数声明
void send_autopilot_heartbeat(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
extern void send_heartbeat(int socket_fd, const struct sockaddr_in* src_addr, socklen_t src_addr_len);
void send_command_ack(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len, uint16_t command, uint8_t result);
void send_camera_information(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_camera_capture_status(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_video_stream_status(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_video_stream_information(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);
void send_camera_settings(int socket_fd, const struct sockaddr_in* dest_addr, socklen_t dest_len);

/* ========================== 2. 串口初始化函数 ============================ */

/* 串口初始化函数 */
int mavlink_uart_init(void) {
    // 打开串口设备
    g_uart_fd = open("/dev/ttyAMA5", O_RDWR | O_NOCTTY | O_NDELAY);
    if (g_uart_fd < 0) {
        ss_log_e("Failed to open UART device /dev/ttyAMA5: %s", strerror(errno));
        return -1;
    }
    
    // 配置串口参数
    struct termios options;
    tcgetattr(g_uart_fd, &options);
    
    // 设置波特率
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    
    // 设置数据位：8位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    
    // 设置无奇偶校验
    options.c_cflag &= ~PARENB;
    options.c_iflag &= ~INPCK;
    
    // 设置停止位：1位
    options.c_cflag &= ~CSTOPB;
    
    // 启用接收
    options.c_cflag |= (CLOCAL | CREAD);
    
    // 禁用硬件流控制
    options.c_cflag &= ~CRTSCTS;
    
    // 设置原始输入模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    
    // 设置原始输出模式
    options.c_oflag &= ~OPOST;
    
    // 设置超时
    options.c_cc[VMIN] = 0;     // 最小字符数
    options.c_cc[VTIME] = 10;   // 超时时间（0.1秒）
    
    // 应用配置
    if (tcsetattr(g_uart_fd, TCSANOW, &options) != 0) {
        ss_log_e("Failed to set UART attributes: %s", strerror(errno));
        close(g_uart_fd);
        g_uart_fd = -1;
        return -1;
    }
    
    // 清空缓冲区
    tcflush(g_uart_fd, TCIOFLUSH);
    
    ss_log_i("UART communication initialized successfully: /dev/ttyAMA5, 115200 baud");
    return 0;
}

/* 串口清理函数 */
void mavlink_uart_deinit(void) {
    if (g_uart_fd >= 0) {
        close(g_uart_fd);
        g_uart_fd = -1;
        ss_log_i("UART communication deinitialized");
    }
}

/* ========================== 3. 串口数据接收函数 ============================ */

/* 串口数据接收线程函数 */
static void* uart_receive_thread(void* arg) {
    ss_log_i("UART receive thread started");
    
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];
    mavlink_status_t status;
    mavlink_message_t msg;
    
    while (g_uart_thread_running) {
        // 读取串口数据
        ssize_t n = read(g_uart_fd, buffer, sizeof(buffer));
        // HexPrintf(buffer, n);
        
        if (n > 0) {
            // 更新QGC连接状态
            HexPrintf(buffer, n);
            #if 0
            g_uart_qgc_connected = true;
            g_uart_last_qgc_message = time(NULL);
            
            ss_log_i("Received UART data, Length:%ld", n);
            
            // 解析MAVLink消息
            for (ssize_t i = 0; i < n; i++) {
                if (mavlink_parse_char(MAVLINK_COMM_0, buffer[i], &msg, &status)) {
                    ss_log_i("Received MAVLink message via UART, ID: %d", msg.msgid);
                    
                    // 检测QGC连接 - 当收到来自QGC的消息时设置连接状态
                    if (!g_uart_qgc_connected) {
                        g_uart_qgc_connected = true;
                        ss_log_i("✅ QGC connected via UART! Starting camera identification process");
                        
                        // 立即发送相机信息包让QGC识别相机
                        // 注意：串口通信没有目标地址，这里使用NULL
                        send_camera_information(g_uart_fd, NULL, 0);
                        ss_log_i("📷 Sent camera information to QGC via UART");
                    }
                    
                    // 检测是否为真实飞控心跳（系统ID=1，组件ID=1）
                    if (msg.msgid == MAVLINK_MSG_ID_HEARTBEAT && msg.sysid == 1 && msg.compid == 1) {
                        g_uart_real_autopilot_detected = true;
                        g_uart_last_real_autopilot_hb = time(NULL);
                        ss_log_d("Real autopilot heartbeat detected via UART");
                    }
                    
                    // 检测协议版本
                    if (msg.magic == 0xFE) {  // MAVLink 1.0
                        g_uart_use_mavlink_v1 = true;
                    } else if (msg.magic == 0xFD) {  // MAVLink 2.0
                        g_uart_use_mavlink_v1 = false;
                    }
                    
                    // 直接处理命令
                    if (msg.msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
                        // 处理命令长消息
                        mavlink_command_long_t cmd;
                        mavlink_msg_command_long_decode(&msg, &cmd);
                        ss_log_i("Received COMMAND_LONG via UART: command=%d", cmd.command);
                        
                        // 这里可以调用具体的命令处理函数
                        // 例如：handle_command_long(&msg);
                    }
                }
            }
        } else if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ss_log_e("UART read error: %s", strerror(errno));
                break;
            }
        }
        #endif
        }
        usleep(10000); // 10ms延迟，避免CPU占用过高

    }
    
    ss_log_i("UART receive thread stopped");
    return NULL;
}

/* 启动串口接收线程 */
static int start_uart_receive_thread(void) {
    if (g_uart_thread_running) {
        ss_log_w("UART receive thread already running");
        return 0;
    }
    
    g_uart_thread_running = true;
    
    if (pthread_create(&g_uart_receive_thread, NULL, uart_receive_thread, NULL) != 0) {
        ss_log_e("Failed to create UART receive thread");
        g_uart_thread_running = false;
        return -1;
    }
    
    ss_log_i("UART receive thread started successfully");
    return 0;
}

/* 停止串口接收线程 */
static int stop_uart_receive_thread(void) {
    if (!g_uart_thread_running) {
        return 0;
    }
    
    g_uart_thread_running = false;
    
    if (g_uart_receive_thread) {
        pthread_join(g_uart_receive_thread, NULL);
        g_uart_receive_thread = 0;
    }
    
    ss_log_i("UART receive thread stopped");
    return 0;
}

/* ========================== 4. 串口主循环函数 ============================ */

/* 串口主循环函数 */
int mavlink_uart_main(void) {
    ss_log_i("Entering UART main loop, waiting for MAVLink messages...");
    
    // 启动接收线程
    if (start_uart_receive_thread() != 0) {
        ss_log_e("Failed to start UART receive thread");
        return -1;
    }
  
    // 初始化状态变量
    camera_button_pressed = false;
    simulate_autopilot = true;  // 默认启动模拟飞控
    
    time_t last_broadcast = 0;
    
    while (g_uart_thread_running) {
        #if 0
        time_t current_time = time(NULL);
        
        // 定期发送状态信息
        static time_t last_stream_info_sent = 0;
        if (current_time - last_stream_info_sent >= 5) {  // 每5秒发送一次
            // 注意：串口通信没有目标地址，这里使用NULL
            send_video_stream_status(g_uart_fd, NULL, 0);
            last_stream_info_sent = current_time;
            
            static int stream_info_count = 0;
            if (stream_info_count++ % 6 == 0) {  // 每30秒发送一次完整信息
                send_video_stream_information(g_uart_fd, NULL, 0);
            }
        }
        
        // 定期发送心跳包
        if (current_time - last_broadcast >= 3) {  // 每3秒发送一次
            static time_t last_heartbeat = 0;
            if (current_time - last_heartbeat >= 3) {
                // 检查QGC是否断开连接（10秒内没有收到消息）
                if (g_uart_qgc_connected && (current_time - g_uart_last_qgc_message) >= 10) {
                    g_uart_qgc_connected = false;
                    camera_button_pressed = false;  // QGC断开，重置按键状态
                    ss_log_i("QGC disconnected via UART, resetting camera button state");
                }
                
                // 模拟飞控控制逻辑
                if (g_uart_real_autopilot_detected) {
                    // 有真实飞控，不需要模拟飞控，但相机消息必须继续发送
                    if (simulate_autopilot) {
                        ss_log_i("Real autopilot detected via UART, stopping simulation but continuing camera messages");
                        simulate_autopilot = false;
                    }
                } else if (camera_button_pressed) {
                    // 相机按键被按下，停止模拟
                    if (simulate_autopilot) {
                        ss_log_i("Camera button pressed via UART, stopping simulation");
                        simulate_autopilot = false;
                    }
                } else if (!g_uart_qgc_connected) {
                    // QGC断开连接，重新启动模拟
                    if (!simulate_autopilot) {
                        ss_log_i("QGC disconnected via UART, restarting simulation");
                        simulate_autopilot = true;
                    }
                }
                
                // 如果需要模拟飞控，发送飞控心跳
                if (simulate_autopilot) {
                    // 注意：串口通信没有目标地址，这里使用NULL
                    send_autopilot_heartbeat(g_uart_fd, NULL, 0);
                }
                
                // 无论是否有真实飞控，相机消息必须持续发送
                // 发送相机心跳包
                send_heartbeat(g_uart_fd, NULL, 0);
                last_heartbeat = current_time;
                
                // 关键修复：定期发送相机信息，让QGC识别设备为相机
                static time_t last_camera_info_sent = 0;
                if (current_time - last_camera_info_sent >= 10) {  // 每10秒发送一次相机信息
                    send_camera_information(g_uart_fd, NULL, 0);
                    last_camera_info_sent = current_time;
                }
                
                // 发送相机捕获状态
                send_camera_capture_status(g_uart_fd, NULL, 0);
                
                // 发送相机设置
                send_camera_settings(g_uart_fd, NULL, 0);
            }
            last_broadcast = current_time;
        }
        
        // 检查真实飞控是否断开连接（30秒内没有收到心跳）
        if (g_uart_real_autopilot_detected && (current_time - g_uart_last_real_autopilot_hb) >= 30) {
            g_uart_real_autopilot_detected = false;
            ss_log_i("Real autopilot connection lost via UART");
        }
        #endif
        usleep(100000); // 100ms延迟，避免CPU占用过高
    }
    
    // 停止接收线程
    stop_uart_receive_thread();
    
    ss_log_i("UART main loop exited");
    return 0;
}

/* ========================== 5. 串口消息发送适配函数 ============================ */

/* 串口专用的消息发送函数 - 适配串口通信 */
int uart_send_message(const uint8_t* buffer, int len) {
    if (g_uart_fd < 0) {
        ss_log_e("UART not initialized, cannot send message");
        return -1;
    }
    
    int ret = write(g_uart_fd, buffer, len);
    if (ret != len) {
        ss_log_e("Failed to send message via UART: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}