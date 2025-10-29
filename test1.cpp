#include <iostream>
#include <string.h>
#include <unistd.h>

typedef void (*action)(void*);

typedef struct {
    uint32_t protocol_id;  // 协议ID
    uint32_t cmd_id;       // 命令ID
    uint32_t link_id;      // 连接ID
    uint32_t data_len;     // 数据长度
    uint8_t* pdata;        // 数据指针
} NET_EX_FRAM_INFO_T;

/* 设置云台模式命令的数据结构 */
typedef struct{
    uint8_t mode;  // 模式值
} NET_GIMBAL_MODE_INFO_S;


/* 相应数据结构 */
typedef struct{
    uint8_t status;  // 状态码
} NET_ACK_S;


/* 设置云台模式处理函数 */
void net_ex_set_gimbal_mode(void* p){

    NET_EX_FRAM_INFO_T* frame = (NET_EX_FRAM_INFO_T*)p;

    NET_GIMBAL_MODE_INFO_S* data = (NET_GIMBAL_MODE_INFO_S*)frame->pdata;
    
    NET_ACK_S ack_data ={.status = 1};

    free(p);
}




typedef struct {
    uint32_t protocol_id;  // 协议ID
    uint32_t cmd_id;       // 命令ID
    action cmd_fun;      // 连接ID
    uint32_t data_len;     // 数据长度
    uint32_t ack_len;        // 数据指针
} TCP_LINK_ACTION_S;

TCP_LINK_ACTION_S g_action_tab[] ={

};

action find_command_handler(uint32_t cmd_id) {
    int table_size = sizeof(g_action_tab) / sizeof(g_action_tab[0]);

    for(int i = 0; i< table_size; i++){
        if(g_action_tab[i].cmd_id == cmd_id){
            return g_action_tab[i].cmd_fun;
        }
    }
    return NULL;

}

typedef struct{
    NET_EX_FRAM_INFO_T* frame;
    int extra_param;
}  THREADPARAM;


