#include "mavlink_threadpool.h"
#include "ss_log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ========================== 1. 全局变量定义 ============================ */

/* 全局线程池实例 */
mavlink_threadpool_t* g_mavlink_threadpool = NULL;

/* ========================== 2. 线程池实现（与原工程一致） ============================ */

/* 线程工作函数 - 与原工程完全一致 */
static void* mavlink_threadpool_worker(void* arg) {
    mavlink_threadpool_t* pool = (mavlink_threadpool_t*)arg;
    
    while (true) {
        pthread_mutex_lock(&(pool->lock));
        
        // 等待任务或关闭信号
        while (pool->queue_size == 0 && !pool->shutdown) {
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }
        
        // 检查是否需要关闭
        if (pool->shutdown) {
            pthread_mutex_unlock(&(pool->lock));
            pthread_exit(NULL);
        }
        
        // 获取任务
        mavlink_job_t* job = pool->head;
        pool->queue_size--;
        
        if (pool->queue_size == 0) {
            pool->head = pool->tail = NULL;
        } else {
            pool->head = job->next;
        }
        
        pthread_mutex_unlock(&(pool->lock));
        
        // 执行任务
        if (job->function) {
            printf("线程池: 开始执行任务，函数指针: %p\n", job->function);
            job->function(job->arg);
        }
        
        // 释放任务内存
        free(job);
    }
    
    return NULL;
}

/* 初始化线程池 - 与原工程完全一致 */
mavlink_threadpool_t* mavlink_threadpool_create(int thread_count) {
    if (thread_count <= 0) {
        thread_count = 4; // 默认4个线程
    }
    
    mavlink_threadpool_t* pool = (mavlink_threadpool_t*)malloc(sizeof(mavlink_threadpool_t));
    if (!pool) {
        ss_log_e("Failed to allocate memory for thread pool");
        return NULL;
    }
    
    // 初始化线程池结构
    pool->thread_count = thread_count;
    pool->queue_size = 0;
    pool->shutdown = false;
    pool->head = pool->tail = NULL;
    
    // 初始化互斥锁和条件变量
    if (pthread_mutex_init(&(pool->lock), NULL) != 0) {
        ss_log_e("Failed to initialize mutex");
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&(pool->notify), NULL) != 0) {
        ss_log_e("Failed to initialize condition variable");
        pthread_mutex_destroy(&(pool->lock));
        free(pool);
        return NULL;
    }
    
    // 创建线程数组
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_count);
    if (!pool->threads) {
        ss_log_e("Failed to allocate memory for threads");
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->notify));
        free(pool);
        return NULL;
    }
    
    // 创建工作线程
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, mavlink_threadpool_worker, pool) != 0) {
            ss_log_e("Failed to create thread %d", i);
            pool->shutdown = true;
            pthread_cond_broadcast(&(pool->notify));
            
            // 等待已创建的线程退出
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            
            free(pool->threads);
            pthread_mutex_destroy(&(pool->lock));
            pthread_cond_destroy(&(pool->notify));
            free(pool);
            return NULL;
        }
    }
    
    ss_log_i("Thread pool created with %d threads", thread_count);
    return pool;
}

/* 添加任务到线程池 */
int mavlink_threadpool_add(mavlink_threadpool_t* pool, void (*function)(void*), void* arg) {
    if (!pool || !function) {
        ss_log_e("Invalid parameters for threadpool add");
        return -1;
    }
    
    if (pool->shutdown) {
        ss_log_w("Thread pool is shutting down, cannot add new tasks");
        return -1;
    }
    
    // 创建新任务
    mavlink_job_t* job = (mavlink_job_t*)malloc(sizeof(mavlink_job_t));
    if (!job) {
        ss_log_e("Failed to allocate memory for job");
        return -1;
    }
    
    job->function = function;
    job->arg = arg;
    job->next = NULL;
    
    pthread_mutex_lock(&(pool->lock));
    
    // 添加到队列尾部
    if (pool->queue_size == 0) {
        pool->head = pool->tail = job;
    } else {
        pool->tail->next = job;
        pool->tail = job;
    }
    
    pool->queue_size++;
    
    pthread_cond_signal(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));
    
    return 0;
}

/* 销毁线程池 */
void mavlink_threadpool_destroy(mavlink_threadpool_t* pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&(pool->lock));
    pool->shutdown = true;
    pthread_cond_broadcast(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));
    
    // 等待所有线程退出
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // 清理剩余任务
    mavlink_job_t* job = pool->head;
    while (job) {
        mavlink_job_t* next = job->next;
        free(job);
        job = next;
    }
    
    // 释放资源
    free(pool->threads);
    pthread_mutex_destroy(&(pool->lock));
    pthread_cond_destroy(&(pool->notify));
    free(pool);
    
    ss_log_i("Thread pool destroyed");
}

/* 获取线程池队列大小 */
int mavlink_threadpool_get_queue_size(mavlink_threadpool_t* pool) {
    if (!pool) return 0;
    
    pthread_mutex_lock(&(pool->lock));
    int size = pool->queue_size;
    pthread_mutex_unlock(&(pool->lock));
    
    return size;
}