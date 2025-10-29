# 1 gcc编译工具的用法

## gcc编译.c文件的命令

gcc  <.c源文件列表>  -o  <输出文件名>

编译过程，例如有一个名为 test.c的文件：

1，预处理：宏替换，头文件包含。 gcc  -E  test.c  -o  test 

2，编译：生成汇编代码。汇编和处理器架构 相关。gcc  -S  test.c  -o test.S

3，汇编：生成目标文件。gcc  -c  test.c  -o test.o

4，链接：生成最终的可执行程序。 gcc   test.c  -o test



//打印二进制文件中的符号

objdump t <目标文件名/可执行文件>  >  <输出文件>

## g++ 即可以编译.c文件，也可以编译.cpp（C++源文件）文件

gcc和g++两者对.c文件的编译结果是有区别的，比如，C语言是按照函数名称来区分函数的，而C++是按照函数原型来区分函数的，这就要求C++能够识别出函数的重载。

函数原型：函数名，参数列表，返回值。

-w    关闭所有警告

-Wall   打开所有警告



# 2 静态库和动态库

windows系统中，静态库名称后续为 .lib，动态库名称后缀为 .dll

linux系统中，静态库名称后缀为 .a，动态库名称后缀为 .so

(以linux为基准)

## 2.1 静态库

在linux系统中以.a结尾的文件是静态库文件。

静态库提供了某种功能（函数和变量），在编译程序时，可以把静态库文件链接到目标文件，可以使用静态库中提供的功能（函数和变量），静态库在程序编译时会被链接到目标代码中，程序运行时不再需要静态库也就是说，静态库和要编译的源文件合成为最终的可执行程序。因此体积较大。

### 2.1.2 静态库的编译方法

gcc   -c   <.c源文件列表，若有多个文件以空格分开>   -o   <目标文件名>

ar     rcs   <lib+文件名+ .a>   <目标文件名>

### 2.1.3 静态库的链接方法

gcc  <.c源文件列表>   -L<静态库的存放路径，注意-L后无空格>    -l<静态库文件名，注意省略文件名前缀lib和后面缀.a>   -o   <输出文件名> 

静态库链接说明：

-L<库文件路径> ： 一定要加上库文件的存放路径

-l<库文件名>： 若静态库文件名称为libmytest.a，则<库文件名>应该是: mytest
下面是在 Linux/macOS 环境下完整的命令行操作流程，包含代码实现、静态库编译和链接步骤：

---

### 一、代码实现
#### 1. 创建头文件 `test.h`
```c
// test.h
#ifndef TEST_H
#define TEST_H

void Hello(const char *arg);

#endif
```

#### 2. 创建静态库源文件 `test.c`
```c
// test.c
#include <stdio.h>
#include "test.h"

void Hello(const char *arg) {
    printf("Hello %s\n", arg);
}
```

#### 3. 创建调用程序 `main.c`
```c
// main.c
#include "test.h"

int main() {
    Hello("World");  // 调用静态库中的函数
    return 0;
}
```

---

### 二、编译静态库 `libtest.a`
```bash
# 1. 编译 test.c 为目标文件 test.o
gcc -c test.c -o test.o

# 2. 将目标文件打包为静态库
ar rcs libtest.a test.o
```
**生成文件**：  
- `test.o`（目标文件）
- `libtest.a`（静态库文件）

> **注**：`ar rcs` 参数含义：  
> - `r`：替换或添加文件到库  
> - `c`：创建新库（若不存在）  
> - `s`：生成索引加速链接

---

### 三、编译 `main.c` 并链接静态库
```bash
# 编译并链接静态库生成可执行文件
gcc main.c -o main -L. -ltest
```
**关键参数解释**：  
- `-L.`：指定库搜索路径为当前目录  
- `-ltest`：链接名为 `libtest.a` 的库（省略 `lib` 前缀和 `.a` 后缀）  
- `-o main`：输出可执行文件名为 `main`

---

### 四、运行程序
```bash
./main
```
**输出结果**：  
```
Hello World
```

---

### 常见问题解决
1. **头文件找不到**：  
   确保 `test.h` 和 `main.c` 在同一目录，否则需用 `-I` 指定头文件路径：  
   ```bash
   gcc main.c -o main -I/path/to/headers -L. -ltest
   ```

2. **库链接失败**：  
   - 检查库文件名格式是否为 `lib<name>.a`  
   - 确认 `-L` 路径包含库文件所在目录

3. **函数未定义错误**：  
   确保头文件中的函数声明与库实现一致（如参数类型和函数名拼写）

---

### 完整操作流程示例
```bash
# 步骤 1：创建所有文件
touch test.c main.c

# 步骤 2：写入代码（复制上述代码到对应文件）

# 步骤 3：编译静态库
gcc -c test.c -o test.o
ar rcs libtest.a test.o

# 步骤 4：编译主程序并链接静态库
gcc main.c -o main -L. -ltest

# 步骤 5：运行
./main
```

> **注意**：Windows 下可用 MinGW 或 MSVC 实现类似流程（静态库扩展名为 `.lib`），编译命令需替换为 MSVC 的 `cl /c` 和 `lib` 工具。


## 2.2 动态库 （共享库）

在linux系统中，动态库一般以后缀.so结尾，在编译时可以不链接到可执行程序，而只在程序运行时动态加载动态库到程序进程中。

### 2.2.1编译动态库的方法：

gcc   -fPIC   -Wall  -c  <.c源文件列表，若有多个源文件以空格分开>

gcc  -shared  -o   <lib+文件名+.so>   <目标文件>

-fPIC :  编译出与地址无关的二进制文件

-shared:  编译为动态库文件

-Wall : 编译过程中允许 gcc 输出警告信息



### 2.2.2 动态库路径配置

1， LD_LIBRARY_PATH : 这是一个系统环境变量，用来保存动态库的路径，程序在运行时会从此变量保存中路径中去查找要加载的动态库。

export  LD_LIBRARY_PATH=$LD_LIBRARY_PATH:<要加载的动态库路径>



或者修改/etc/profile 文件，比如在末尾添加

export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}: 动态库路径

然后运行 source /etc/profile

2，在配置文件中设置

在/etc/ld.so.conf.d路径中添加 自己的.conf文件，在.conf文件中添加动态库路径

然后运行 sudo  ldconfig

#### 课堂作业：

1，实现一个整型数组的选择排序算法，文件名为 mysort.c

2，分别把mysort.c编译为用静态库和动态库

3，实现一个man.c 文件，此文件中的main函数中，定义一个整型数组，调用mysort.c中的排序算法。并输出排序结果。

4，把main.c以静态库的链接方法链接mysort.c编译的静态库libmysort.a

5，把main.c以动态库的链接方法罗拉 mysort.c动态库编译出的动态库libmysort.so，注意怎样去设置动态库的路径。