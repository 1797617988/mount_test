#include "ss_main.h"

#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>

#include "ss_log.h"
// #include "ss_pq.h"
// #include "mpp_common.h"
// #include "mpp_pipeline.h"
// #include "rtsp.h"
#include "storage.h"
// #include "queue_table.h"

// #include "capture.h"
// #include "record.h"
// #include "sample.h"
// #include "cmd.h"
// #include "ss_vgs.h"
// #include "pip.h"
// #include "net_ex_link.h"
// #include "podControl.h"
// #include "zoom.h"
// #include "config_ini.h"
// #include "net_ex_link_action.h"
// #include "system.h"

// #ifdef ZIYAN_LINK_ENABLED
// #include "ziyan_main_test.h"
// #elif defined(DJI_LINK_ENABLED)
// #include "dji_main_test.h"
// #else

// #endif
#include "mavlink_action.h"
#include "mavlink_init.h"

// C函数声明
extern "C" {
    int mavlink_threadpool_init_simple(void);
    void mavlink_threadpool_cleanup(void);
}

uint32_t g_main_signal_flag = 0;

// 线程函数声明
static void* udp_communication_thread(void* arg);
static void* uart_communication_thread(void* arg);

static void ss_sys_signal(void (*func)(int))
{
    struct sigaction sa = {0}; 

    sa.sa_handler = func;  
    sa.sa_flags   = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void ss_main_handle_sig(int32_t signo)
{
    if(signo == SIGINT || signo == SIGTERM){ 
        g_main_signal_flag = 1; 
    }
}

static void ss_main_func_pause(void)
{
    if(g_main_signal_flag == 0){ 
        getchar(); 
    }
}

/* 主MAVLink通信线程函数 */
static void* mavlink_main_thread(void* arg) {
    ss_log_i("Starting MAVLink main communication thread");
    
    // 直接调用mavlink_main函数，它会处理UDP和串口通信
    mavlink_main();
    
    return NULL;
}

int32_t main(int32_t argc, char *argv[])
{
    ss_log_p("sensing four-light-pod with MAVLink architecture\n");

    int32_t ret = 0;
    pthread_t mavlink_thread;

    uint16_t input = 0;

    g_main_signal_flag = 0;   
    ss_sys_signal(&ss_main_handle_sig);

    ss_log_i("Starting MAVLink communication system");
    
    // 创建MAVLink主线程
    if (pthread_create(&mavlink_thread, NULL, mavlink_main_thread, NULL) != 0) {
        ss_log_e("Failed to create MAVLink communication thread");
        return -1;
    }
    
    ss_log_i("MAVLink communication thread started successfully");
    
    //StorageStartDetectThread();

    ss_log_i("Main loop started, MAVLink communication is running");

    while(g_main_signal_flag == 0){
        sleep(1); 
    }
    
    // 清理资源
    ss_log_i("Shutting down communications...");
    
    // 等待线程结束
    pthread_join(mavlink_thread, NULL);
    
    ss_log_i("All communications stopped");
    return ret;
}
