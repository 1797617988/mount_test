#ifndef __SS_MAIN_H__
#define __SS_MAIN_H__

/**
 * @file ss_main.h
 * @brief 主程序头文件，定义全局变量和函数声明
 */

// 以下头文件被注释掉，未在当前版本中使用
// #include "sample_comm.h"  // 示例通信库头文件（未使用）

/**
 * @def SENSING_CAMERA_VERSION
 * @brief 定义摄像头版本号
 * @note 被以下函数引用：
 * - main()（在 ss_main.cpp 中）
 */
#define SENSING_CAMERA_VERSION 0x10000003

#ifdef __cplusplus
extern "C" {
#endif

// 以下全局变量被注释掉，未在当前版本中使用
// extern td_u32 g_main_signal_flag;  // 主程序信号标志（未使用）

#ifdef __cplusplus
}
#endif

#endif
