/*
 * 函数指针机制详细演示 - 完整工程流程
 * 模拟完整的网络命令处理系统，包含线程池、命令映射表等
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

/* ========================== 1. 基础定义（与原工程一致） ============================ */

/* 定义函数指针类型 - 与原工程完全一致 */
typedef void (*action)(void*);

/* 模拟网络数据帧结构 - 成员数量和类型与原工程一致 */
typedef struct {
    uint32_t protocol_id;  // 协议ID
    uint32_t cmd_id;       // 命令ID
    uint32_t link_id;      // 连接ID
    uint32_t data_len;     // 数据长度
    uint8_t* pdata;        // 数据指针
} NET_EX_FRAM_INFO_T;

/* ========================== 2. 具体命令数据结构 ============================ */

/* 设置云台模式命令的数据结构 */
typedef struct {
    uint8_t mode;  // 模式值
} NET_GIMBAL_MODE_INFO_S;

/* 设置云台角度命令的数据结构 */
typedef struct {
    int16_t roll;   // 横滚角
    int16_t pitch;  // 俯仰角
    int16_t yaw;    // 偏航角
} NET_GIMBAL_ANGLE_INFO_S;

/* 获取云台状态命令的数据结构 */
typedef struct {
    uint8_t sensor_id;  // 传感器ID
} NET_GIMBAL_GET_AN_S;

/* 响应数据结构 */
typedef struct {
    uint8_t status;  // 状态码
} NET_ACK_S;

/* ========================== 3. 线程池实现（简化版） ============================ */

/* 任务结构体 */
typedef struct job {
    void (*function)(void*);  // 函数指针
    void* arg;                // 参数
    struct job* next;         // 下一个任务
} job_t;

/* 线程池结构体 */
typedef struct {
    pthread_t* threads;       // 线程数组
    int num_threads;         // 线程数量
    job_t* job_queue;        // 任务队列
    pthread_mutex_t lock;    // 互斥锁
    pthread_cond_t cond;     // 条件变量
    int shutdown;            // 关闭标志
} threadpool_t;

/* 全局线程池实例 */
threadpool_t* g_pool = NULL;

/* 线程工作函数 */
void* worker_thread(void* arg) {
    threadpool_t* pool = (threadpool_t*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->lock);
        
        // 等待任务
        while (pool->job_queue == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->cond, &pool->lock);
        }
        
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->lock);
            pthread_exit(NULL);
        }
        
        // 获取任务
        job_t* job = pool->job_queue;
        pool->job_queue = job->next;
        pthread_mutex_unlock(&pool->lock);
        
        printf("线程池: 开始执行任务，函数指针: %p\n", job->function);
        
        // 执行任务（调用函数指针）
        job->function(job->arg);
        
        free(job);
    }
    
    return NULL;
}

/* 初始化线程池 */
int thpool_init(int num_threads) {
    g_pool = (threadpool_t*)malloc(sizeof(threadpool_t));
    g_pool->num_threads = num_threads;
    g_pool->job_queue = NULL;
    g_pool->shutdown = 0;
    
    pthread_mutex_init(&g_pool->lock, NULL);
    pthread_cond_init(&g_pool->cond, NULL);
    
    // 创建线程
    g_pool->threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&g_pool->threads[i], NULL, worker_thread, g_pool);
    }
    
    return 0;
}

/* 添加任务到线程池 - 与原工程函数签名一致 */
int thpool_add_work(void* pool, void (*function)(void*), void* arg) {
    threadpool_t* thpool = (threadpool_t*)pool;
    
    job_t* new_job = (job_t*)malloc(sizeof(job_t));
    new_job->function = function;
    new_job->arg = arg;
    new_job->next = NULL;
    
    pthread_mutex_lock(&thpool->lock);
    
    // 添加到队列尾部
    if (thpool->job_queue == NULL) {
        thpool->job_queue = new_job;
    } else {
        job_t* current = thpool->job_queue;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_job;
    }
    
    pthread_cond_signal(&thpool->cond);
    pthread_mutex_unlock(&thpool->lock);
    
    printf("线程池: 任务已添加到队列，等待执行\n");
    return 0;
}

/* ========================== 4. 命令处理函数 ============================ */

/* 设置云台模式处理函数 */
void net_ex_set_gimbal_mode(void* p) {
    printf("\n=== 开始执行 net_ex_set_gimbal_mode ===\n");
    
    // 步骤1: 将void*转换回NET_EX_FRAM_INFO_T*
    NET_EX_FRAM_INFO_T* frame = (NET_EX_FRAM_INFO_T*)p;
    printf("步骤1: void* → NET_EX_FRAM_INFO_T* 转换完成\n");
    printf("   命令ID: 0x%X, 数据长度: %d\n", frame->cmd_id, frame->data_len);
    
    // 步骤2: 将pdata转换为具体的数据结构
    NET_GIMBAL_MODE_INFO_S* data = (NET_GIMBAL_MODE_INFO_S*)frame->pdata;
    printf("步骤2: frame->pdata → NET_GIMBAL_MODE_INFO_S* 转换完成\n");
    printf("   云台模式值: %d\n", data->mode);
    
    // 步骤3: 实际业务处理
    printf("步骤3: 执行实际业务逻辑 - 设置云台模式\n");
    
    // 步骤4: 发送响应
    NET_ACK_S ack_data = {.status = 1};
    printf("步骤4: 发送响应数据，状态码: %d\n", ack_data.status);
    
    // 步骤5: 释放内存
    free(p);
    printf("步骤5: 释放内存，函数执行完成\n");
}

/* 设置云台角度处理函数 */
void net_ex_set_gimbal_angle(void* p) {
    printf("\n=== 开始执行 net_ex_set_gimbal_angle ===\n");
    
    NET_EX_FRAM_INFO_T* frame = (NET_EX_FRAM_INFO_T*)p;
    printf("步骤1: void* → NET_EX_FRAM_INFO_T* 转换完成\n");
    
    NET_GIMBAL_ANGLE_INFO_S* data = (NET_GIMBAL_ANGLE_INFO_S*)frame->pdata;
    printf("步骤2: frame->pdata → NET_GIMBAL_ANGLE_INFO_S* 转换完成\n");
    printf("   角度值 - 横滚: %d, 俯仰: %d, 偏航: %d\n", data->roll, data->pitch, data->yaw);
    
    printf("步骤3: 执行实际业务逻辑 - 设置云台角度\n");
    
    NET_ACK_S ack_data = {.status = 1};
    printf("步骤4: 发送响应数据，状态码: %d\n", ack_data.status);
    
    free(p);
    printf("步骤5: 释放内存，函数执行完成\n");
}

/* 获取云台状态处理函数 */
void net_ex_get_gimbal_angle(void* p) {
    printf("\n=== 开始执行 net_ex_get_gimbal_angle ===\n");
    
    NET_EX_FRAM_INFO_T* frame = (NET_EX_FRAM_INFO_T*)p;
    printf("步骤1: void* → NET_EX_FRAM_INFO_T* 转换完成\n");
    
    NET_GIMBAL_GET_AN_S* data = (NET_GIMBAL_GET_AN_S*)frame->pdata;
    printf("步骤2: frame->pdata → NET_GIMBAL_GET_AN_S* 转换完成\n");
    printf("   传感器ID: %d\n", data->sensor_id);
    
    printf("步骤3: 执行实际业务逻辑 - 获取云台状态\n");
    
    NET_ACK_S ack_data = {.status = 1};
    printf("步骤4: 发送响应数据，状态码: %d\n", ack_data.status);
    
    free(p);
    printf("步骤5: 释放内存，函数执行完成\n");
}

/* ========================== 5. 命令映射表（与原工程结构一致） ============================ */

/* 命令ID定义 */
#define NET_EX_SET_GIMBAL_MODE    0x65
#define NET_EX_SET_GIMBAL_ANGLE   0x66
#define NET_EX_GET_GIMBAL_ANGLE   0x67

/* 命令映射表结构体 - 与原工程完全一致 */
typedef struct {
    uint32_t protocol_id;  // 协议ID
    uint32_t cmd_id;       // 命令ID
    action cmd_fun;        // 函数指针
    uint32_t date_len;     // 数据长度
    uint32_t ack_len;      // 响应长度
} TCP_LINK_ACTION_S;

/* 全局命令映射表 - 与原工程格式完全一致 */
TCP_LINK_ACTION_S g_action_tab[] = {
    {0, NET_EX_SET_GIMBAL_MODE,  net_ex_set_gimbal_mode,  sizeof(NET_GIMBAL_MODE_INFO_S),  sizeof(NET_ACK_S)},
    {0, NET_EX_SET_GIMBAL_ANGLE, net_ex_set_gimbal_angle, sizeof(NET_GIMBAL_ANGLE_INFO_S), sizeof(NET_ACK_S)},
    {0, NET_EX_GET_GIMBAL_ANGLE, net_ex_get_gimbal_angle, sizeof(NET_GIMBAL_GET_AN_S),    sizeof(NET_ACK_S)},
};

/* ========================== 6. 命令分发函数 ============================ */

/* 查找命令处理函数 */
action find_command_handler(uint32_t cmd_id) {
    int table_size = sizeof(g_action_tab) / sizeof(g_action_tab[0]);
    
    for (int i = 0; i < table_size; i++) {
        if (g_action_tab[i].cmd_id == cmd_id) {
            return g_action_tab[i].cmd_fun;
        }
    }
    return NULL;
}

/* 命令分发主函数 - 模拟网络接收处理 */
void process_network_command(NET_EX_FRAM_INFO_T* frame) {
    printf("\n〓〓〓 开始处理网络命令 〓〓〓\n");
    printf("步骤A: 接收到网络数据帧\n");
    printf("   协议ID: %u, 命令ID: 0x%X\n", frame->protocol_id, frame->cmd_id);
    printf("   连接ID: %u, 数据长度: %u\n", frame->link_id, frame->data_len);
    
    // 步骤B: 在命令表中查找对应的处理函数
    action handler = find_command_handler(frame->cmd_id);
    if (handler == NULL) {
        printf("错误: 未找到命令ID 0x%X 的处理函数\n", frame->cmd_id);
        return;
    }
    printf("步骤B: 在命令映射表中找到处理函数\n");
    printf("   函数指针地址: %p\n", handler);
    
    // 步骤C: 复制数据（实际工程中需要深拷贝）
    NET_EX_FRAM_INFO_T* new_frame = (NET_EX_FRAM_INFO_T*)malloc(sizeof(NET_EX_FRAM_INFO_T) + frame->data_len);
    memcpy(new_frame, frame, sizeof(NET_EX_FRAM_INFO_T));
    
    if (frame->data_len > 0) {
        new_frame->pdata = (uint8_t*)(new_frame + 1);
        memcpy(new_frame->pdata, frame->pdata, frame->data_len);
    }
    printf("步骤C: 数据复制完成，准备添加到线程池\n");
    
    // 步骤D: 添加到线程池
    printf("步骤D: 调用 thpool_add_work 添加到线程池\n");
    thpool_add_work(g_pool, handler, (void*)new_frame);
    
    printf("〓〓〓 命令分发完成 〓〓〓\n");
}

/* ========================== 7. 主函数 - 完整流程演示 ============================ */

int main() {
    printf("=== 函数指针机制完整工程流程演示 ===\n\n");
    
    /* 步骤1: 初始化线程池 */
    printf("步骤1: 初始化线程池（2个工作线程）\n");
    thpool_init(2);
    sleep(1);  // 等待线程启动
    
    /* 步骤2: 准备测试数据 */
    printf("\n步骤2: 准备测试数据\n");
    
    // 测试数据1: 设置云台模式
    NET_GIMBAL_MODE_INFO_S mode_data = {.mode = 1};
    NET_EX_FRAM_INFO_T mode_frame = {
        .protocol_id = 0,
        .cmd_id = NET_EX_SET_GIMBAL_MODE,
        .link_id = 1,
        .data_len = sizeof(NET_GIMBAL_MODE_INFO_S),
        .pdata = (uint8_t*)&mode_data
    };
    
    // 测试数据2: 设置云台角度
    NET_GIMBAL_ANGLE_INFO_S angle_data = {.roll = 100, .pitch = 200, .yaw = 300};
    NET_EX_FRAM_INFO_T angle_frame = {
        .protocol_id = 0,
        .cmd_id = NET_EX_SET_GIMBAL_ANGLE,
        .link_id = 1,
        .data_len = sizeof(NET_GIMBAL_ANGLE_INFO_S),
        .pdata = (uint8_t*)&angle_data
    };
    
    // 测试数据3: 获取云台状态
    NET_GIMBAL_GET_AN_S get_data = {.sensor_id = 2};
    NET_EX_FRAM_INFO_T get_frame = {
        .protocol_id = 0,
        .cmd_id = NET_EX_GET_GIMBAL_ANGLE,
        .link_id = 1,
        .data_len = sizeof(NET_GIMBAL_GET_AN_S),
        .pdata = (uint8_t*)&get_data
    };
    
    printf("步骤2完成: 准备了3个测试命令\n");
    
    /* 步骤3: 模拟网络接收和处理命令 */
    printf("\n步骤3: 开始模拟网络命令处理流程\n");
    
    // 处理第一个命令
    printf("\n--- 命令1: 设置云台模式 ---\n");
    process_network_command(&mode_frame);
    sleep(1);
    
    // 处理第二个命令
    printf("\n--- 命令2: 设置云台角度 ---\n");
    process_network_command(&angle_frame);
    sleep(1);
    
    // 处理第三个命令
    printf("\n--- 命令3: 获取云台状态 ---\n");
    process_network_command(&get_frame);
    
    /* 步骤4: 等待所有任务完成 */
    printf("\n步骤4: 等待所有线程任务完成...\n");
    sleep(3);
    
    printf("\n=== 演示完成 ===\n");
    
    return 0;
}