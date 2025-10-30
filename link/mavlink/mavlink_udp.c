#include "mavlink_action.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include "mavlink_udp.h"
#include "ss_log.h"
#include "mavlink_init.h"

/* ========================== 1. 全局变量定义 ============================ */

// UDP通信相关变量
static bool g_udp_thread_running = false;
static pthread_t g_udp_receive_thread = 0;
static int g_udp_socket_fd = -1;
static struct sockaddr_in g_udp_src_addr;
static socklen_t g_udp_src_addr_len = sizeof(g_udp_src_addr);

// QGC连接状态
static bool g_udp_qgc_connected = false;
static time_t g_udp_last_qgc_message = 0;

// 飞控检测状态
static bool g_udp_real_autopilot_detected = false;
static time_t g_udp_last_real_autopilot_hb = 0;

// 协议版本标志
static bool g_udp_use_mavlink_v1 = false;

/* ========================== 2. UDP数据接收函数 ============================ */

/* UDP数据接收线程函数 */
static void* udp_receive_thread(void* arg) {
    ss_log_i("UDP receive thread started\n");
    
    char buffer[2048]; // enough for MTU 1500 bytes
    
    while (g_udp_thread_running) {
        // 读取UDP数据
        ssize_t n = recvfrom(g_udp_socket_fd, buffer, sizeof(buffer), 0, 
                            (struct sockaddr*)&g_udp_src_addr, &g_udp_src_addr_len);
        
        if (n > 0) {
            // 更新QGC连接状态
            g_udp_qgc_connected = true;
            g_udp_last_qgc_message = time(NULL);
            
            ss_log_i("Received UDP packet, Length:%ld\n", n);
            
            mavlink_message_t message;
            mavlink_status_t status;
            
            // 解析MAVLink消息
            for (ssize_t i = 0; i < n; ++i) {
                if (mavlink_parse_char(MAVLINK_COMM_0, buffer[i], &message, &status) == 1) {
                    
                    ss_log_i("Received MAVLink message %d (0x%02X) from %d/%d\n",
                        message.msgid, message.msgid, message.sysid, message.compid);

                    // 检测QGC连接 - 当收到来自QGC的消息时设置连接状态
                    if (!g_udp_qgc_connected) {
                        g_udp_qgc_connected = true;
                        ss_log_i("✅ QGC connected via UDP! Starting camera identification process\n");
                        ss_log_i("✅ QGC IP: %s, Port: %d\n", 
                                inet_ntoa(g_udp_src_addr.sin_addr), ntohs(g_udp_src_addr.sin_port));
                        
                        // 立即发送相机信息包让QGC识别相机
                        send_camera_information(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
                        ss_log_i("📷 Sent camera information to QGC via UDP\n");
                    }
                    
                    // 检测是否为真实飞控心跳（系统ID=1，组件ID=1）
                    if (message.msgid == MAVLINK_MSG_ID_HEARTBEAT && message.sysid == 1 && message.compid == 1) {
                        g_udp_real_autopilot_detected = true;
                        g_udp_last_real_autopilot_hb = time(NULL);
                        ss_log_d("Real autopilot heartbeat detected via UDP\n");
                    }
                    
                    // 检测协议版本
                    if (message.magic == 0xFE) {  // MAVLink 1.0
                        g_udp_use_mavlink_v1 = true;
                    } else if (message.magic == 0xFD) {  // MAVLink 2.0
                        g_udp_use_mavlink_v1 = false;
                    }
                    
                    // 使用统一命令处理架构
                    if (message.msgid == MAVLINK_MSG_ID_COMMAND_LONG) {
                        // 处理命令长消息
                        mavlink_command_long_t cmd;
                        mavlink_msg_command_long_decode(&message, &cmd);
                        ss_log_i("Received COMMAND_LONG: command=%d\n", cmd.command);
                        
                        // 查找并调用对应的命令处理函数
                        mavlink_action_func handler = find_command_handler(message.msgid, cmd.command);
                        if (handler != NULL) {
                            ss_log_i("Calling command handler for command %d via UDP\n", cmd.command);
                            
                            // 创建上下文并调用处理函数
                            mavlink_unified_context_t ctx = {
                                .transport = {
                                    .network = {
                                        .socket_fd = g_udp_socket_fd,
                                        .addr = &g_udp_src_addr,
                                        .addr_len = g_udp_src_addr_len
                                    }
                                }
                            };
                            
                            handler(&ctx, &message);
                        } else {
                            ss_log_w("No handler found for command %d via UDP\n", cmd.command);
                        }
                    } else {
                        // 处理其他类型的消息（如心跳消息等）
                        mavlink_action_func handler = find_command_handler(message.msgid, 0);
                        if (handler != NULL) {
                            ss_log_i("Calling handler for message ID %d via UDP\n", message.msgid);
                            
                            // 创建上下文并调用处理函数
                            mavlink_unified_context_t ctx = {
                                .transport = {
                                    .network = {
                                        .socket_fd = g_udp_socket_fd,
                                        .addr = &g_udp_src_addr,
                                        .addr_len = g_udp_src_addr_len
                                    }
                                }
                            };
                            
                            handler(&ctx, &message);
                        }
                    }
                }
            }
        } else if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ss_log_e("UDP recvfrom error: %s\n", strerror(errno));
                break;
            }
        }
        
        usleep(10000); // 10ms延迟，避免CPU占用过高
    }
    
    ss_log_i("UDP receive thread stopped\n");
    return NULL;
}

/* 启动UDP接收线程 */
static int start_udp_receive_thread(void) {
    if (g_udp_thread_running) {
        ss_log_w("UDP receive thread already running\n");
        return 0;
    }
    
    g_udp_thread_running = true;
    
    if (pthread_create(&g_udp_receive_thread, NULL, udp_receive_thread, NULL) != 0) {
        ss_log_e("Failed to create UDP receive thread\n");
        g_udp_thread_running = false;
        return -1;
    }
    
    ss_log_i("UDP receive thread started successfully\n");
    return 0;
}

/* 停止UDP接收线程 */
static int stop_udp_receive_thread(void) {
    if (!g_udp_thread_running) {
        return 0;
    }
    
    g_udp_thread_running = false;
    
    if (g_udp_receive_thread) {
        pthread_join(g_udp_receive_thread, NULL);
        g_udp_receive_thread = 0;
    }
    
    ss_log_i("UDP receive thread stopped\n");
    return 0;
}

/* ========================== 3. UDP主循环函数 ============================ */

/* UDP主循环函数 */
int mavlink_udp_main(void) {
    ss_log_i("Entering UDP main loop, waiting for MAVLink messages...\n");
    
    // 启动接收线程
    if (start_udp_receive_thread() != 0) {
        ss_log_e("Failed to start UDP receive thread\n");
        return -1;
    }
    
    // 初始化状态变量
    camera_button_pressed = false;
    simulate_autopilot = true;  // 默认启动模拟飞控
    
    time_t last_broadcast = 0;
    
    while (g_udp_thread_running) {
        time_t current_time = time(NULL);
        
        // 定期发送状态信息
        static time_t last_stream_info_sent = 0;
        if (current_time - last_stream_info_sent >= 5) {  // 每5秒发送一次
            send_video_stream_status(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
            last_stream_info_sent = current_time;
            
            static int stream_info_count = 0;
            if (stream_info_count++ % 6 == 0) {  // 每30秒发送一次完整信息
                send_video_stream_information(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
            }
        }
        
        // 定期发送心跳包
        if (current_time - last_broadcast >= 3) {  // 每3秒发送一次
            static time_t last_heartbeat = 0;
            if (current_time - last_heartbeat >= 3) {
                // 检查QGC是否断开连接（10秒内没有收到消息）
                if (g_udp_qgc_connected && (current_time - g_udp_last_qgc_message) >= 10) {
                    g_udp_qgc_connected = false;
                    camera_button_pressed = false;  // QGC断开，重置按键状态
                    ss_log_i("QGC disconnected via UDP, resetting camera button state\n");
                }
                
                // 模拟飞控控制逻辑
                if (g_udp_real_autopilot_detected) {
                    // 有真实飞控，不需要模拟飞控，但相机消息必须继续发送
                    if (simulate_autopilot) {
                        ss_log_i("Real autopilot detected via UDP, stopping simulation but continuing camera messages\n");
                        simulate_autopilot = false;
                    }
                } else if (camera_button_pressed) {
                    // 相机按键被按下，停止模拟
                    if (simulate_autopilot) {
                        ss_log_i("Camera button pressed via UDP, stopping simulation\n");
                        simulate_autopilot = false;
                    }
                } else if (!g_udp_qgc_connected) {
                    // QGC断开连接，重新启动模拟
                    if (!simulate_autopilot) {
                        ss_log_i("QGC disconnected via UDP, restarting simulation\n");
                        simulate_autopilot = true;
                    }
                }
                
                // 如果需要模拟飞控，发送飞控心跳
                if (simulate_autopilot) {
                    send_autopilot_heartbeat(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
                }
                
                // 无论是否有真实飞控，相机消息必须持续发送
                // 发送相机心跳包
                send_heartbeat(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
                last_heartbeat = current_time;
                
                // 关键修复：定期发送相机信息，让QGC识别设备为相机
                static time_t last_camera_info_sent = 0;
                if (current_time - last_camera_info_sent >= 10) {  // 每10秒发送一次相机信息
                    send_camera_information(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
                    last_camera_info_sent = current_time;
                }
                
                // 发送相机捕获状态
                send_camera_capture_status(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
                
                // 发送相机设置
                send_camera_settings(g_udp_socket_fd, &g_udp_src_addr, g_udp_src_addr_len);
            }
            last_broadcast = current_time;
        }
        
        // 检查真实飞控是否断开连接（30秒内没有收到心跳）
        if (g_udp_real_autopilot_detected && (current_time - g_udp_last_real_autopilot_hb) >= 30) {
            g_udp_real_autopilot_detected = false;
            ss_log_i("Real autopilot connection lost via UDP\n");
        }
        
        usleep(100000); // 100ms延迟，避免CPU占用过高
    }
    
    // 停止接收线程
    stop_udp_receive_thread();
    
    ss_log_i("UDP main loop exited\n");
    return 0;
}

/* ========================== 4. UDP初始化函数 ============================ */

/* UDP初始化函数 */
int mavlink_udp_init(void) {
    // Open UDP socket
    g_udp_socket_fd = socket(PF_INET, SOCK_DGRAM, 0);

    if (g_udp_socket_fd < 0) {
        ss_log_e("UDP socket error: %s\n", strerror(errno));
        return -1;
    }

    // Bind to port
    struct sockaddr_in addr = {};
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &(addr.sin_addr)); // listen on all network interfaces
    addr.sin_port = htons(14550); // default port on the ground

    if (bind(g_udp_socket_fd, (struct sockaddr*)(&addr), sizeof(addr)) != 0) {
        ss_log_e("UDP bind error: %s\n", strerror(errno));
        close(g_udp_socket_fd);
        g_udp_socket_fd = -1;
        return -2;
    }
    
    // 设置超时
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 100000;
    if (setsockopt(g_udp_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        ss_log_e("UDP setsockopt error: %s\n", strerror(errno));
        close(g_udp_socket_fd);
        g_udp_socket_fd = -1;
        return -3;
    }
    
    ss_log_i("UDP communication initialized successfully, bound to port 14550\n");
    return 0;
}

/* UDP清理函数 */
void mavlink_udp_deinit(void) {
    if (g_udp_socket_fd >= 0) {
        close(g_udp_socket_fd);
        g_udp_socket_fd = -1;
        ss_log_i("UDP communication deinitialized\n");
    }
}