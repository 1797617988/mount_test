# src/Makefile
CC = gcc
CFLAGS = -Wall -g
TARGET = mymain
SRCS = mymain.c         # 仅保留主程序源文件
OBJS = $(SRCS:.c=.o)
LIB_NAME = libmy_qsort.a
LIB_SRC = my_qsort.c    # 库的源文件独立定义

all: $(TARGET)

$(TARGET): $(OBJS) $(LIB_NAME)  # 显式依赖静态库
	$(CC) $(CFLAGS) -o $@ $^ -L. -lmy_qsort

# 生成静态库规则（需先编译 my_qsort.o）
$(LIB_NAME): my_qsort.o
	ar crs $@ $<

# 主程序目标文件构建
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理所有生成文件
clean:
	rm -f $(TARGET) $(OBJS) $(LIB_NAME) *.o

.PHONY: all clean  # 声明伪目标

# APP=mymain

# $(APP):mymain.c libmy_qsort.a
# 	gcc -g mymain.c -L./ -lmy_qsort -o $(APP)

# libmy_qsort.a:my_qsort.c
# 	gcc -c my_qsort.c -o my_qsort.o
# 	ar crs libmy_qsort.a my_qsort.o

# clean:
# 	rm -rf *.o $(APP) *.a