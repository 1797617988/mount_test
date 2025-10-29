#ifndef __MAVLINK_THREADPOOL_H__
#define __MAVLINK_THREADPOOL_H__

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* ========================== 1. 基础定义（与原工程一致） ============================ */

/* 任务结构体 - 与原工程完全一致 */
typedef struct mavlink_job_s {
    void (*function)(void* arg);       // 任务函数指针
    void* arg;                        // 任务参数
    struct mavlink_job_s* next;       // 下一个任务
} mavlink_job_t;

/* 线程池结构体 - 与原工程完全一致 */
typedef struct mavlink_threadpool_s {
    pthread_mutex_t lock;              // 互斥锁
    pthread_cond_t notify;             // 条件变量
    pthread_t* threads;                // 线程数组
    mavlink_job_t* head;              // 任务队列头
    mavlink_job_t* tail;              // 任务队列尾
    int thread_count;                  // 线程数量
    int queue_size;                   // 队列大小
    bool shutdown;                     // 关闭标志
} mavlink_threadpool_t;

/* ========================== 2. 函数声明（与原工程函数签名一致） ============================ */

/* 初始化线程池 */
mavlink_threadpool_t* mavlink_threadpool_create(int thread_count);

/* 添加任务到线程池 - 与原工程函数签名一致 */
int mavlink_threadpool_add(mavlink_threadpool_t* pool, void (*function)(void*), void* arg);

/* 销毁线程池 */
void mavlink_threadpool_destroy(mavlink_threadpool_t* pool);

/* 获取线程池状态 */
int mavlink_threadpool_get_queue_size(mavlink_threadpool_t* pool);

/* ========================== 3. 全局线程池实例 ============================ */

extern mavlink_threadpool_t* g_mavlink_threadpool;

/* ========================== 4. 简化接口函数 ============================ */

/* 初始化线程池（简化接口） */
int mavlink_threadpool_init(int num_threads);

/* 添加任务（简化接口） */
int mavlink_thpool_add_work(void (*function)(void*), void* arg);

/* 清理线程池（简化接口） */
void mavlink_threadpool_cleanup(void);

#endif