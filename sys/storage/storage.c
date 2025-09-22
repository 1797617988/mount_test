

#include "storage.h"

#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include "ss_log.h"
#include <pthread.h>
#define CAPTURE_PATH 	"/mnt/sensing/media_file"
#define RECORD_PATH		"/mnt/sensing/media_file"

#define MAX_LINE_LEN            256
#define UEVENT_BUFFER_SIZE      1024

#define PARTTITION_CHECK        "/proc/partitions"
#define SD_ROOT_DIR	            "/mnt"
#define MOUNT_OPTION            "umask=077"

#define SD_DEV_NAME_POS         5
#define SD_DEV_NAME_LEN         16
#define SD_DEV_NAME_IS_INVALID  (g_devName[SD_DEV_NAME_POS] == '\0')
#define SD_DEV_NAME_SET_INVALID            \
    {                                      \
        g_devName[SD_DEV_NAME_POS] = '\0'; \
    }

static char g_devName[32] = "/dev/";
static char g_line[MAX_LINE_LEN];
static char g_nextLine[MAX_LINE_LEN];

static pthread_t g_storageDetectThread  = 0;
static bool g_storageDetect_exit     = true;
static FileSytemType g_eCurrentFS       = FS_TYPE_NUM;

static bool StorageFormatFlag = false;
static bool StorageMountFlag  = false;

bool StorageIsMounted(void)
{
    return StorageMountFlag;
}

char *get_capture_path(void)
{
	return CAPTURE_PATH;
}

char *get_record_path(void)
{

	return RECORD_PATH;
}


static const char *mount_filesystem_type(FileSytemType eFsType) {
    if (eFsType == FS_TYPE_FAT32) {
        return "vfat";  
    } else if (eFsType == FS_TYPE_EXFAT) {
        printf("***exfat  ****\n");
        return "exfat";  
    }
    return NULL;
}


// static char *fReadLine(FILE *stream)
// {
//     int32_t count = 0;
//     while(!feof(stream) && (count < MAX_LINE_LEN) && ((g_line[count++] = getc(stream)) != '\n'));

//     if(!count)
//         return NULL;

//     g_line[count - 1] = '\0';
//     return g_line;
// }

static char *fReadLine(FILE *stream)
{
    // 使用 fgets 读取一行。fgets 会自动在字符串末尾添加 '\0'
    if (fgets(g_line, sizeof(g_line), stream) == NULL) {
        return NULL; // 读取失败或文件结束
    }

    // 可选：去除 fgets 读入的换行符 '\\n'
    // 如果不需要去除换行符，可以删除下面两行
    size_t length = strlen(g_line);
    if (length > 0 && g_line[length - 1] == '\n') {
        g_line[length - 1] = '\0'; // 将换行符替换为字符串结束符
    }

    return g_line;
}

static char* fReadNextLine(FILE *stream)
{
    if (fgets(g_line,sizeof(g_nextLine),stream) == NULL) {
        return NULL;
    }

    size_t length = strlen(g_nextLine);
    if (length > 0 && g_line[length -1] == '\n') {
        g_nextLine[length - 1] = '\0';
    }

    return g_line;
}

// static char *fReadNextLine(FILE *stream)
// {
//     int32_t count = 0;
//     while(!feof(stream) && (count < MAX_LINE_LEN) && ((g_nextLine[count++] = getc(stream)) != '\n'));

//     if(!count)
//         return NULL;

//     g_nextLine[count - 1] = '\0';
//     return g_nextLine;
// }

static int32_t DetectStorageDev(void)
{
    FILE *pFile      = NULL;
    char *pCurLine   = NULL;
    char *pBlock     = NULL;
    char *pPartition = NULL;

    SD_DEV_NAME_SET_INVALID

    pFile = fopen(PARTTITION_CHECK, "r");
    if(pFile){
        while((pCurLine = fReadLine(pFile)) != NULL){
            pBlock = strstr(pCurLine, "mmcblk1p1");
            if(pBlock){
                break;
            }
        }

        if(pBlock){
            while((pCurLine = fReadNextLine(pFile)) != NULL){
                pPartition = strstr(pCurLine, pBlock);
                if(pPartition){
                    memcpy(&g_devName[SD_DEV_NAME_POS], pPartition, SD_DEV_NAME_LEN);
                    break;
                }
            }

            if(!pPartition){
                memcpy(&g_devName[SD_DEV_NAME_POS], pBlock, SD_DEV_NAME_LEN);
            }
        }

        fclose(pFile);
    }

    return 0;
}

static int32_t init_hotplug_sock(void)
{
    struct sockaddr_nl snl;
    const int32_t buffersize = 16 * 1024;
    struct timeval timeout    = {1, 0};
    int32_t retval;

    memset(&snl, 0x00, sizeof(struct sockaddr_nl));
    snl.nl_family    = AF_NETLINK;
    snl.nl_pid       = getpid();
    snl.nl_groups    = 1;
    int32_t hotplug_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if(hotplug_sock == -1){
        printf("error getting socket: %s", strerror(errno));
        return -1;
    }
    /* set receive buffersize */
    setsockopt(hotplug_sock, SOL_SOCKET, SO_RCVBUFFORCE, &buffersize, sizeof(buffersize));
    /* set receive timeout */
    setsockopt(hotplug_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));
    retval = bind(hotplug_sock, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl));
    if(retval < 0){
        printf("bind failed: %s", strerror(errno));
        close(hotplug_sock);
        hotplug_sock = -1;
        return -1;
    }
	
    return hotplug_sock;
}

static int32_t Storage_Create_Dir(void)
{
	int32_t ret = 0;
	DIR * dir = NULL;	
	char *pathname[64];

	memset(pathname, 0, sizeof(pathname));
	memcpy(pathname, RECORD_PATH, sizeof(RECORD_PATH));
	if((dir = opendir((const char *)pathname)) == NULL){
		// ret = mkdir((const char *)pathname, 0755);
        ret = system("mkdir /mnt/sensing/media_file/ -p");
		if(ret != 0){
			perror("mkdir");
			return -1;
		}
	}

	memset(pathname, 0, sizeof(pathname));
	memcpy(pathname, CAPTURE_PATH, sizeof(CAPTURE_PATH));
	if((dir = opendir((const char *)pathname)) == NULL){
		// ret = mkdir((const char *)pathname, 0755);
        ret = system("mkdir /mnt/sensing/media_file/ -p");
		if (ret != 0){
			perror("mkdir");
			return -1;
		}
	}

	return ret;
}

int32_t StorageMount(void)
{
    int32_t             ret        = 0;
    uint64_t           mountflags = MS_NOATIME | MS_NODIRATIME;
    struct statfs      diskInfo;

    if(SD_DEV_NAME_IS_INVALID)
        return -1;

    ret = access(g_devName, F_OK);
    if(ret != 0){
    	printf("dev name is invalid!\n");
        return -1;
    }

    for(uint8_t i = 0; i < FS_TYPE_NUM; i++){
        g_eCurrentFS = (FileSytemType)i;
        // const char* fstype = mount_filesystem_type(g_eCurrentFS);
        //     if (fstype == NULL) {
        //         printf("Unsupported filesystem type: %d\n", i);
        //         continue;
        //     }
            
        //     printf("Attempting to mount with filesystem type: %s\n", fstype);
        //     ret = mount(g_devName, SD_ROOT_DIR, fstype, mountflags, MOUNT_OPTION);
        //     if (ret == 0) {
        //         printf("Successfully mounted with filesystem type: %s\n", fstype);
        //         break;
        //     } else {
        //         printf("Failed to mount with filesystem type %s: %s\n", fstype, strerror(errno));
        //     }
        switch(g_eCurrentFS) {
            case FS_TYPE_FAT32:
                printf("Attempting to mount with filesystem type: vfat\n");
                ret = mount(g_devName, SD_ROOT_DIR, "vfat", mountflags, MOUNT_OPTION);
                if (ret == 0) {
                    printf("Successfully mounted with filesystem type: vfat\n");
                } else {
                    printf("Failed to mount with filesystem type vfat: %s\n", strerror(errno));
                }
                break;
                
            case FS_TYPE_EXFAT:
                printf("Attempting to mount with filesystem type: exfat (using FUSE)\n");
                char mount_cmd[256];
                snprintf(mount_cmd, sizeof(mount_cmd), "/sbin/mount.exfat-fuse -o nonempty %s %s", g_devName, SD_ROOT_DIR);
                ret = system(mount_cmd);
                if (ret == 0) {
                    printf("FUSE mount successful\n");
                } else {
                    printf("FUSE mount failed: %d\n", ret);
                }
                break;
                
            default:
                printf("Unsupported filesystem type: %d\n", i);
                continue;
        }
    }
	
    if(ret != 0){
        perror("mount error");
		
        return ret;
    }
    else{
        printf("mount ok\n");
		Storage_Create_Dir();
    }

    ret = statfs(SD_ROOT_DIR, &diskInfo);
    if(ret != 0){
        perror("statfs fail");
        goto L_STAT_ERR;
    }
    else{
        printf("statfs ok\n");
    }

#if 1
    uint64_t blockSize     = 0;
    uint64_t availableSize = 0;
    uint64_t usedSize      = 0;
	uint64_t u64TotalSize  = 0;

    blockSize = diskInfo.f_bsize;
    u64TotalSize = blockSize * diskInfo.f_blocks;
    availableSize = blockSize * diskInfo.f_bavail;
    usedSize = u64TotalSize - availableSize;

    ss_log_p("total size [%lu]\n", u64TotalSize);
    ss_log_p("used  size [%lu]\n", usedSize);
    ss_log_p("free  size [%lu]\n", availableSize);
#endif

	StorageMountFlag = true;

    return ret;

L_STAT_ERR:
    ret = umount2(SD_ROOT_DIR, MNT_DETACH);
    if(ret != 0){
        perror("unmount error");
        return ret;
    }
    else{
        printf("unmount ok\n");
        return -1;
    }
}

int32_t StorageUnMount(void)
{
    int32_t ret = 0;

    ret = umount2(SD_ROOT_DIR, MNT_DETACH);
    if(ret != 0){
        perror("unmount error");
        return ret;
    }
    else{
        printf("unmount ok\n");
    }

	StorageMountFlag = false;

    return ret;
}

void *StorageDetectTask(void *argv)
{
    int32_t ret = 0;
	int32_t g_hotplug_sock = -1;


    g_hotplug_sock = init_hotplug_sock();
    if(g_hotplug_sock == -1){
        pthread_exit(NULL);
    }

	StorageUnMount();
    DetectStorageDev();

    if(!SD_DEV_NAME_IS_INVALID){
        ret = access(g_devName, F_OK);
		
        if(ret == 0){
            printf("[%s] Exist\n", g_devName);
			
            ret = StorageMount();
        }
    }

    while(g_storageDetect_exit == false){
        char buf[UEVENT_BUFFER_SIZE] = {0,};
        char action[16] = {0,};

        ret = recv(g_hotplug_sock, &buf, sizeof(buf), 0);

        if(ret == -1)
            continue;

        sscanf(buf, "%[^'@']", action);

        if(!StorageIsMounted() && strncmp(action, "add", sizeof(action)) == 0){
            DetectStorageDev();
            if(!SD_DEV_NAME_IS_INVALID){
                if(strstr(buf, &g_devName[SD_DEV_NAME_POS])){
                    ret = StorageMount();
                }
            }
        }
        else if(StorageIsMounted() && strncmp(action, "remove", sizeof(action)) == 0){
            if(!SD_DEV_NAME_IS_INVALID){
                if(strstr(buf, &g_devName[SD_DEV_NAME_POS])){
                    StorageUnMount();
                    SD_DEV_NAME_SET_INVALID
                }
            }
        }
    }

    close(g_hotplug_sock);
    pthread_exit(NULL);

	return 0;
}

void *StorageFormat_Proc(void *p)
{
	int32_t ret = -1;
	char *dev = (char *)p;
	char cmd[64];
	// uint8_t status;

	sprintf(cmd, "rm -rf %s/*", SD_ROOT_DIR);
	system(cmd);

    ret = StorageUnMount();
    if(ret != 0){
    	return NULL;
    }

	sprintf(cmd, "mkfs.vfat %s", dev);
	system(cmd);

	g_eCurrentFS = FS_TYPE_FAT32;

    ret = StorageMount();
    if(ret < 0){
        ss_log_d("format fail\n");
		//send app
        // status = 0;
    }
    else{
        ss_log_d("format done\n");
		//send app
        // status = 1;
    }

	StorageFormatFlag = false;

	return NULL;
}

int get_sdcard_storage(unsigned long *total_mb,unsigned long *available_mb,unsigned long*used_mb) {
   

    struct statvfs stat;
    if (statvfs("/mnt", &stat) != 0) {
        perror("无法获取/mnt文件系统信息");
        return -1;
    }

    unsigned long block_size = stat.f_frsize;
    unsigned long total_blocks = stat.f_blocks;
    unsigned long free_blocks = stat.f_bfree;
    unsigned long avail_blocks = stat.f_bavail;

    *total_mb = (total_blocks * block_size) / (1024 * 1024);
    *available_mb = (avail_blocks * block_size) / (1024 * 1024);
    *used_mb = *total_mb - *available_mb;

    return 0;
}

int32_t StorageFormat(void)
{
    int32_t ret = -1;
	pthread_t format_thread = 0;

    if(SD_DEV_NAME_IS_INVALID){
		return ret;
    }

	if(StorageFormatFlag != false){
		ss_log_d("格式化未完成\n");
		return ret;
	}

	StorageFormatFlag = true;

    ret = pthread_create(&format_thread, 0, StorageFormat_Proc, (void *)g_devName);

    return ret;
}

void StorageStartDetectThread(void)
{
    if(true != g_storageDetect_exit){
        printf("alread start\n");
        return;
    }

    g_storageDetect_exit = false;
    int32_t ret = 0;

    ret = pthread_create(&g_storageDetectThread, NULL, StorageDetectTask, NULL);
    if(0 == ret){
        prctl(g_storageDetectThread, "storage_task");
    }
    else{
        printf("pthread_create failed\n");
    }
}

void StorageStopDetectThread(void)
{
    g_storageDetect_exit = true;
    pthread_join(g_storageDetectThread, NULL);
}
