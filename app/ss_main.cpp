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
uint32_t g_main_signal_flag = 0;

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

int32_t main(int32_t argc, char *argv[])
{
    ss_log_p("sensing four-light-pod\n");

    int32_t ret = 0;

    uint16_t input = 0;

    g_main_signal_flag = 0;   
    ss_sys_signal(&ss_main_handle_sig);

    StorageStartDetectThread();


    while(g_main_signal_flag == 0){

        sleep(1); 
    }
    return ret;
}
