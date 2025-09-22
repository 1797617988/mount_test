#ifndef __SS_LOG_H__
#define __SS_LOG_H__

#include <errno.h>
#include <stdio.h>
#include <string.h>

//OT_LOG_LEVEL_FATAL_REF
#define ss_log_f(format, ...) fprintf(stdout, LIGHT_RED"%s_%s_%d error:" format NONE,MYFILE(__FILE__),__FUNCTION__,__LINE__,##__VA_ARGS__)
//OT_LOG_LEVEL_ERROR_REF
#define ss_log_e(format, ...) fprintf(stdout, LIGHT_RED"%s_%s_%d error:" format NONE,MYFILE(__FILE__),__FUNCTION__,__LINE__,##__VA_ARGS__)
//OT_LOG_LEVEL_WARNING_REF
#define ss_log_w(format, ...) fprintf(stdout, LIGHT_PURPLE"%s_%s_%d warning:" format NONE,MYFILE(__FILE__),__FUNCTION__,__LINE__,##__VA_ARGS__)
//OT_LOG_LEVEL_INFO_REF
#define ss_log_i(format, ...) fprintf(stdout, LIGHT_PURPLE"%s_%s_%d info:" format NONE,MYFILE(__FILE__),__FUNCTION__,__LINE__,##__VA_ARGS__)
//OT_LOG_LEVEL_DEBUG_REF
#define ss_log_d(format, ...) fprintf(stdout, LIGHT_BLUE"%s_%s_%d:" format NONE,MYFILE(__FILE__),__FUNCTION__,__LINE__,##__VA_ARGS__)
//OT_LOG_LEVEL_PRINT_REF
#define ss_log_p(format, ...) fprintf(stdout, LIGHT_GREEN"%s_%s_%d:" format NONE,MYFILE(__FILE__),__FUNCTION__,__LINE__,##__VA_ARGS__)

/* ----------------------------------------------------------------- */
#define OT_APPCOMM_RETURN_IF_PTR_NULL(p, errCode) do { \
    if ((p) == NULL) { \
        MLOGE("pointer[%s] is null\n", #p); \
        return (errCode); \
    } \
} while (0)

#define OT_APPCOMM_RETURN_IF_EXPR_FALSE(expr, errCode) do { \
    if (!(expr)) { \
        MLOGE("expr[%s] false\n", #expr); \
        return (errCode); \
    } \
} while (0)

#define OT_APPCOMM_LOG_AND_RETURN_IF_EXPR_FALSE(expr, errCode, errString) do { \
    if (!(expr)) { \
        MLOGE("[%s] failed\n", (errString)); \
        return (errCode); \
    } \
} while (0)

#define OT_APPCOMM_RETURN_IF_FAIL(ret, errCode) do { \
    if ((ret) != 0) { \
        MLOGE("Error Code: [0x%08X]\n\n", (ret)); \
        return (errCode); \
    } \
} while (0)

#define OT_APPCOMM_LOG_IF_FAIL(ret, errString) do { \
        if ((ret) != 0) { \
            MLOGE("[%s] failed[0x%08X]\n", (errString), (ret)); \
        }                                                       \
    } while (0)

#define OT_APPCOMM_LOG_AND_RETURN_IF_FAIL(ret, errCode, errString) do { \
    if ((ret) != 0) { \
        MLOGE("[%s] failed[0x%08X]\n", (errString), (ret)); \
        return (errCode); \
    } \
} while (0)

#define OT_APPCOMM_LOG_AND_GOTO_IF_EXPR_FALSE(expr, label, errString) do { \
    if (!(expr)) { \
        MLOGE("[%s] failed\n", (errString)); \
        goto label; \
    } \
} while (0)

#define OT_APPCOMM_LOG_AND_GOTO_IF_FAIL(ret, label, errString) do { \
    if ((ret) != 0) { \
        MLOGE("[%s] failed[0x%08X]\n", (errString), (ret)); \
        goto label; \
    } \
} while (0)

#define OT_APPCOMM_LOG_IF_EXPR_FALSE(expr, errString) do { \
    if (!(expr)) { \
        MLOGE("[%s] failed\n", (errString)); \
    } \
} while (0)

#define OT_APPCOMM_LOG_ERRCODE_IF_EXPR_FALSE(expr, errCode, errString) do { \
    if (!(expr)) { \
            MLOGE("[%s] failed[0x%08X]\n", (errString), (errCode)); \
        } \
    } while (0)

#define OT_APPCOMM_CHECK_RANGE(value, min, max) (((value) <= (max) && (value) >= (min)) ? 1 : 0)

#define OT_APPCOMM_SAFE_FREE(p) do { \
    if ((p) != NULL) { \
        free(p); \
        (p) = NULL; \
    } \
} while (0)

#define OT_APPCOMM_ALIGN(value, base) (((base) > 0) ? (((value) + (base) - 1) / (base) * (base)) : (value))

/* 注意:该颜色只用来输出错误信息 */
#define NONE          "\033[m"
#define RED           "\033[0;32;31m"
#define LIGHT_RED     "\033[1;31m"              
#define GREEN         "\033[0;32;32m"
#define LIGHT_GREEN   "\033[1;32m"
#define BLUE          "\033[0;32;34m"
#define LIGHT_BLUE    "\033[1;34m"
#define DARY_GRAY     "\033[1;30m"
#define CYAN          "\033[0;36m"
#define LIGHT_CYAN    "\033[1;36m"
#define PURPLE        "\033[0;35m"
#define LIGHT_PURPLE  "\033[1;35m"
#define BROWN         "\033[0;33m"
#define YELLOW        "\033[1;33m"
#define LIGHT_GRAY    "\033[0;37m"
#define WHITE         "\033[1;37m"

#define MYFILE(x) strrchr(x,'/')?strrchr(x,'/')+1:x

#define HexPrintf(data, dataLen) \
    do {    \
        int cnt = 0;     \
        fprintf(stdout,"[%s][%s][%d] Lenght:%d\n",MYFILE(__FILE__),__FUNCTION__,__LINE__,(int)dataLen);  \
        while(dataLen > cnt) {        \
            printf("%02X ", data[cnt]);    \
            cnt += 1;        \
            if( cnt % 16 == 0 )     \
                printf("\n");    \
        }    \
        printf("\n");    \
    } while(0) 

#endif
