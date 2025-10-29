# include Makefile.param
#
## Object file name
TARGET ?= sensing_app_test

#
## Clangd config
USE_CLANGD := yes

ifeq ($(USE_CLANGD), yes)
	BEAR = $(shell which bear)
endif

#
## Cross-compilation tool
CROSS	= aarch64-mix210-linux-

AS		= $(CROSS)as
CC		= $(CROSS)gcc
CXX		= $(CROSS)g++
LD		= $(CROSS)g++
AR		= $(CROSS)ar
OC		= $(CROSS)objcopy
OD		= $(CROSS)objdump
STRIP	= $(CROSS)strip

#
# C 编译器选项
CFLAGS := -Wall -Wno-unused-function -Wno-unused-variable  # 启用警告，但忽略未使用的函数和变量
CFLAGS += -O2                                              # 优化级别 2
CFLAGS += -Wno-attributes                                  # 忽略属性相关的警告
CFLAGS += -lpthread -lm -ldl -lstdc++ -fsigned-char        # 链接的库（线程、数学、动态加载、C++ 标准库等）
CFLAGS += -mcpu=cortex-a53                                 # 指定 CPU 架构
CFLAGS += -fno-aggressive-loop-optimizations               # 禁用激进的循环优化
CFLAGS += -ffunction-sections -fdata-sections              # 将函数和数据放到独立的段中（便于链接优化）
CFLAGS += -fstack-protector-strong                         # 启用栈保护
CFLAGS += -fPIC -fPIE -pie -s                              # 生成位置无关代码，并去除符号表
CFLAGS += -fsigned-char -Wno-date-time                     # 强制 char 为有符号类型，忽略日期时间警告
CFLAGS += -D_FILE_OFFSET_BITS=64                           # 支持大文件（64 位文件偏移）
CFLAGS += -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE        # 启用大文件支持
CFLAGS += -DOT_RELEASE                                     # 定义编译为发布版本
CFLAGS += -DVER_X=1 -DVER_Y=0 -DVER_Z=0 -DVER_P=0 -DVER_B=10  # 定义版本号
CFLAGS += -DUSER_BIT_64 -DKERNEL_BIT_64                    # 定义用户和内核为 64 位
CFLAGS += -DOT_ACODEC_TYPE_HDMI -DOT_ACODEC_TYPE_INNER     # 定义音频编解码类型
CFLAGS += -DOT_VQE_USE_STATIC_MODULE_REGISTER              # 启用静态模块注册（VQE）
CFLAGS += -DOT_AAC_USE_STATIC_MODULE_REGISTER              # 启用静态模块注册（AAC）
CFLAGS += -DOT_AAC_HAVE_SBR_LIB                            # 启用 AAC 的 SBR 库支持
CFLAGS += -DSENSOR0_TYPE=$(SENSOR0_TYPE)                   # 定义传感器 0 类型
CFLAGS += -DSENSOR1_TYPE=$(SENSOR1_TYPE)                   # 定义传感器 1 类型
CFLAGS += -DHNR_SCENE_AUTO_USED                            # 启用场景自动处理
CFLAGS += -DSYSTEM_ARCH_LINUX                              # 定义系统架构为 Linux
# CFLAGS += -fsanitize=address -g -rdynamic                # 调试选项（被注释掉了）


# C++ 编译器选项
CXXFLAGS := -Wno-narrowing  # 忽略窄化转换警告


#
## Linker option
LDFLAGS := -fno-common
LDFLAGS += -mcpu=cortex-a55
LDFLAGS += -fno-aggressive-loop-optimizations
LDFLAGS += -Wl,-z,relro -Wl,-z,noexecstack -Wl,-z,now,-s
LDFLAGS += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
LDFLAGS += -ldl -rdynamic
# LDFLAGS += -fsanitize=address	#debug temporary open

#
## Include directory
ROOT_DIR = ./

HISI_SDK_INC_DIR := $(ROOT_DIR)/lib/hisi_lib/include
HISI_SDK_LIB_DIR := $(ROOT_DIR)/lib/hisi_lib/lib

FFMPEG_INC_DIR := $(ROOT_DIR)/lib/ffmpeg/include
FFMPEG_LIB_DIR := $(ROOT_DIR)/lib/ffmpeg/lib

INFIR_INC_DIR := $(ROOT_DIR)/lib/infir/include
INFIR_LIB_DIR := $(ROOT_DIR)/lib/infir/lib

LIVE555_INC_DIR	:= $(ROOT_DIR)/lib/live555/include
LIVE555_LIB_DIR := $(ROOT_DIR)/lib/live555/lib

PQ_INC_DIR := $(ROOT_DIR)/lib/pq_lib/include
PQ_LIB_DIR := $(ROOT_DIR)/lib/pq_lib/lib

OSD_INC_DIR := $(ROOT_DIR)/lib/osd_lib/include
OSD_LIB_DIR := $(ROOT_DIR)/lib/osd_lib/lib

INI_INC_DIR := $(ROOT_DIR)/lib/ini_lib/include
INI_LIB_DIR := $(ROOT_DIR)/lib/ini_lib/lib
 	



INCDIRS		=	-I$(HISI_SDK_INC_DIR) \
				-I$(HISI_SDK_INC_DIR)/npu \
				-I$(HISI_SDK_INC_DIR)/svp_npu \
				-I$(FFMPEG_INC_DIR) \
				-I$(FFMPEG_INC_DIR)/libavformat \
				-I$(FFMPEG_INC_DIR)/libavcodec \
				-I$(INFIR_INC_DIR) \
				-I$(LIVE555_INC_DIR)/BasicUsageEnvironment \
				-I$(LIVE555_INC_DIR)/groupsock \
				-I$(LIVE555_INC_DIR)/liveMedia \
				-I$(LIVE555_INC_DIR)/UsageEnvironment \
				-I$(LIVE555_INC_DIR)/mediaServer \
				-I$(PQ_INC_DIR) \
				-I$(OSD_INC_DIR) \
				-I$(OSD_INC_DIR)/SDL2 \
				-I$(INI_INC_DIR) \
				-I./app \
				-I./bll \
				-I./bll/capture \
				-I./bll/infir \
				-I./bll/record \
				-I./bll/rtsp \
				-I./bll/pip \
				-I./bll/pod_control \
				-I./mpp \
				-I./mpp_compo \
				-I./mpp_compo/avs \
				-I./mpp_compo/common \
				-I./mpp_compo/dis \
				-I./mpp_compo/isp \
				-I./mpp_compo/ive \
				-I./mpp_compo/pq \
				-I./mpp_compo/scene_auto \
				-I./mpp_compo/scene_auto/include \
				-I./mpp_compo/scene_auto/src \
				-I./mpp_compo/scene_auto/src/core \
				-I./mpp_compo/scene_auto/tools/configaccess/include \
				-I./mpp_compo/sys \
				-I./mpp_compo/vdec \
				-I./mpp_compo/venc \
				-I./mpp_compo/vgs \
				-I./mpp_compo/vi \
				-I./mpp_compo/vo \
				-I./mpp_compo/vpss \
				-I./mpp_compo/rgn \
				-I./sys \
				-I./sys/data_struct \
				-I./sys/storage \
				-I./sys/config_ini \
				-I./link/net_ex_link \
				-I./link/mavlink \
				-I./bll/zoom \
				-I./bll/gimbal 
				

#
## Source file directory
SRCDIRS		=	./ \
				./app \
				./bll \
				./bll/capture \
				./bll/infir \
				./bll/record \
				./bll/rtsp \
				./bll/pip \
				./bll/pod_control \
				./mpp \
				./mpp_compo \
				./mpp_compo/avs \
				./mpp_compo/common \
				./mpp_compo/dis \
				./mpp_compo/isp \
				./mpp_compo/ive \
				./mpp_compo/pq \
				./mpp_compo/scene_auto \
				./mpp_compo/scene_auto/src \
				./mpp_compo/scene_auto/src/core \
				./mpp_compo/scene_auto/tools/configaccess/src \
				./mpp_compo/sys \
				./mpp_compo/vdec \
				./mpp_compo/venc \
				./mpp_compo/vgs \
				./mpp_compo/vi \
				./mpp_compo/vo \
				./mpp_compo/vpss \
				./mpp_compo/rgn \
				./sys \
				./sys/data_struct \
				./sys/storage \
				./sys/config_ini \
				./link/net_ex_link \
				./link/mavlink \
				./bll/zoom \
				./bll/gimbal 


#
## Library file directory
# MPI_LIBA := $(HISI_SDK_LIB_DIR)/npu/libadump_server.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libascend_protobuf.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libascendcl.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libcpu_kernels_context.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libdrv_aicpu.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libdrv_dfx.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libdrvdevdrv.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/liberror_manager.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libmmpa.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/npu/libregister.a

# MPI_LIBA += $(HISI_SDK_LIB_DIR)/svp_npu/libprotobuf-c.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/svp_npu/libsvp_acl.a

# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libaac_comm.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libaac_dec.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libaac_enc.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libaac_sbr_dec.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libaac_sbr_enc.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libdetail_ap.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libfileformat.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libhdr_ap.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libheif.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libmfnr_ap.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libot_isp.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsecurec.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsfnr_ap.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_imx347_slave.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_imx415.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_imx485.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_imx586.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_imx989.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_os04a10.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_os05a10_2l_slave.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_os08a20.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_os08b10.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libsns_ov50h.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_acs.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_ae.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_avsconvert.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_avslut.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_awb.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_bnr.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_calcflicker.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_cipher.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_crb.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_dehaze.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_dnvqe.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_dpu_match.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_dpu_rect.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_drc.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_dsp.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_extend_stats.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_fisheye_calibrate.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_hdmi.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_hnr.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_ir_auto.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_isp.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_ive.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_klad.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_ldci.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_mau.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_mcf.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_mcfcalibrate.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_mcf_vi.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_md.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_motionfusion.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_mpi.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_otp.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_pciv.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_photo.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_pos_query.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_pqp.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_snap.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_tde.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_upvqe.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_uvc.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libss_voice_engine.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_aec.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_agc.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_anr.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_eq.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_hpf.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_record.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_res.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_talkv2.a
# MPI_LIBA += $(HISI_SDK_LIB_DIR)/libvqe_wnr.a

# MPI_LIBA += -L$(HISI_SDK_LIB_DIR)

# FFMPEG_LIBA := $(FFMPEG_LIB_DIR)/libavformat.a
# FFMPEG_LIBA += $(FFMPEG_LIB_DIR)/libavcodec.a
# FFMPEG_LIBA += $(FFMPEG_LIB_DIR)/libavutil.a
# FFMPEG_LIBA += $(FFMPEG_LIB_DIR)/libswresample.a
# FFMPEG_LIBA += $(FFMPEG_LIB_DIR)/libswscale.a
# FFMPEG_LIBA += $(FFMPEG_LIB_DIR)/libx264.a
# FFMPEG_LIBA += $(FFMPEG_LIB_DIR)/libx265.a

# INFIR_LIBA := $(INFIR_LIB_DIR)/libircmd.a
# INFIR_LIBA += $(INFIR_LIB_DIR)/libiri2c.a
# INFIR_LIBA += $(INFIR_LIB_DIR)/libirparse.a
# INFIR_LIBA += $(INFIR_LIB_DIR)/libirprocess.a
# INFIR_LIBA += $(INFIR_LIB_DIR)/libirtemp.a
# INFIR_LIBA += $(INFIR_LIB_DIR)/libiruvc.a
# INFIR_LIBA += $(INFIR_LIB_DIR)/libusb-1.0.a

# LIVE555_LIBA :=	$(LIVE555_LIB_DIR)/libgroupsock.a
# LIVE555_LIBA +=	$(LIVE555_LIB_DIR)/libliveMedia.a
# LIVE555_LIBA +=	$(LIVE555_LIB_DIR)/libmediaServer.a
# LIVE555_LIBA +=	$(LIVE555_LIB_DIR)/libUsageEnvironment.a
# LIVE555_LIBA +=	$(LIVE555_LIB_DIR)/libBasicUsageEnvironment.a

# PQ_LIBA := $(PQ_LIB_DIR)/lib_pqcontrol.a
# PQ_LIBA += $(PQ_LIB_DIR)/libbin.a

# OSD库文件
OSD_LIBA := $(OSD_LIB_DIR)/libfreetype.a
OSD_LIBA += $(OSD_LIB_DIR)/libSDL2.a
OSD_LIBA += $(OSD_LIB_DIR)/libSDL2_ttf.a

# MAVLink库文件
MAVLINK_LIBA := -L./link/mavlink

# INI_LIBA := $(INI_LIB_DIR)/libiniparser.a


LIBS = -lrt -lpthread -lm -ldl \
		$(MPI_LIBA) \
		$(FFMPEG_LIBA) \
		$(INFIR_LIBA) \
		$(LIVE555_LIBA) \
		$(PQ_LIBA) \
		$(OSD_LIBA) \
		$(INI_LIBA)

ifeq ($(TARGET),sensing_app_ziyan_microquad)
    INCDIRS += $(PSDK_ZIYAN_INC) $(PSDK_ZIYAN_INC_DIR) 
    SRCDIRS += $(PSDK_ZIYAN_SRC)
    LIBS += $(PSDK_ZIYAN_LIBA)
	CFLAGS += -DZIYAN_LINK_ENABLED
endif

ifeq ($(TARGET),sensing_app_dji_microquad)
    INCDIRS += $(PSDK_DJI_INC_DIR) $(PSDK_DJI_INC) 
    SRCDIRS += $(PSDK_DJI_SRC)
    LIBS += $(PSDK_DJI_LIBA)
	CFLAGS += -DDJI_LINK_ENABLED
endif

#
## Retrieve all files recursively
NAME		:= $(notdir $(CURDIR))
SFILES		:= $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.s))
CFILES		:= $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.c))
CPPFILES	:= $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.cpp))
RMFILES		:= $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*~))
RMFILEO		:= $(foreach dir,$(SRCDIRS),$(wildcard $(dir)/*.o))

RM			:= -rm -rf
OBJS		:= $(SFILES:.s=.o) $(CFILES:.c=.o) $(CPPFILES:.cpp=.o)
DEPS		:= $(OBJS:.o=.d)

#
## pseudo-target
.PHONY: clean all print_info install ziyan ziyan_clean dji dji_clean

$(NAME):$(OBJS)
	@echo -e '\e[34m  LD	\e[0m' $(TARGET)
	@$(LD) $(LDFLAGS) -o $(TARGET) $^ -Wl,--start-group $(LIBS) -Wl,--end-group && $(STRIP) $(TARGET)

%.o:%.s 
	@echo -e '\e[32m  AS	\e[0m' $@
	@$(AS) $(ASFLAGS) $(INCDIRS) $< -o $@
%.o:%.c
	@echo -e '\e[32m  CC	\e[0m' $@
	@$(CC) $(CFLAGS) $(INCDIRS) -c $< -MMD -o $@
%.o:%.cpp
	@echo -e '\e[32m  CXX	\e[0m' $@
	@$(CXX) $(CXXFLAGS) $(INCDIRS) -c $< -MMD -o $@

-include $(DEPS)
clean:
	@$(RM) $(OBJS) $(DEPS) $(NAME) $(RMFILES) $(RMFILEO) $(TARGET) 
	@find . -type f $ -name "*.d" -o -name "*.o" $ -exec rm -f {} + || true
	@echo ----------------------------------------
	@echo ----------- Clean Completed! -----------

print_info:
	@echo -e '\n\e[32mCROSS: \e[0m'		$(CROSS)
	@echo -e '\n\e[32mCFLAGS: \e[0m'	$(CFLAGS)
	@echo -e '\n\e[32mCXXFLAGS: \e[0m'	$(CXXFLAGS)
	@echo -e '\n\e[32mLDFLAGS: \e[0m'	$(LDFLAGS)
	@echo -e '\n\e[32mBUILD: \e[0m'

all:print_info
	@$(MAKE) $(NAME) -j16
	@echo
	@echo -----------------------------------------
	@echo ----------- Compile complete! -----------

rebuild: clean all
	@echo -----------------------------------------
	@echo ----------- Rebuild complete! -----------

install:
	@echo 
	@echo -----------------------------------------
	@echo ----------- Install complete! -----------




