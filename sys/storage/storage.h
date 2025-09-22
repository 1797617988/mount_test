#ifndef __STORAGE_H__
#define __STORAGE_H__

// #include "sample_comm.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum _FileSytemType {
    FS_TYPE_FAT32 = 0,
    FS_TYPE_EXFAT,      // 添加exFAT文件系统类型
    FS_TYPE_NUM,
} FileSytemType;

#ifdef __cplusplus
extern "C" {
#endif

bool StorageIsMounted(void);
void StorageStartDetectThread(void);
void StorageStopDetectThread(void);
int32_t StorageFormat(void);

char *get_capture_path(void);
char *get_record_path(void);
int get_sdcard_storage(unsigned long *total_mb,unsigned long *available_mb,unsigned long*used_mb);
#ifdef __cplusplus
}
#endif


#endif
