CC=gcc

# -I 的作用从当前目录下的include目录下查找所包含的文件
CFLAGS=-I./include
APP=main

#获取当前目录下的所有.c文件, 多个文件间以空格分隔
SRC=$(wildcard *.c)

#获取当前目录下的所有子目录, 多个子目录间以空格分隔, 赋值给SUBDIR
SUBDIR=$(shell ls -d */)

#获取当前目录下的所有子目录下的所有以.c结尾的文件 
SUBSRC=$(shell find $(SUBDIR) -name '*.c')

$(APP):$(SRC) $(SUBSRC)
	$(CC) $^ -o $@  $(CFLAGS)

out:
	@echo $(SRC)
	@echo $(SUBDIR)
	@echo $(SUBSRC)