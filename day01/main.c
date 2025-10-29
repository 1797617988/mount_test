// main.c
#include "stdio.h"
void Hello(const char * arg);

int main() {
    Hello("World");  // 调用静态库中的函数
    return 0;
}

//gcc main.c -o main -L. -ltest

/*
以下是将 `test.c` 编译为动态库（`.so`），并在 `main.c` 中链接使用的完整步骤和指令：

---

### 📁 一、文件结构准备
1. **头文件 `test.h`**（声明函数接口，供 `main.c` 调用）：
   ```c
   // test.h
   #ifndef TEST_H
   #define TEST_H
   void Hello(const char *arg);
   #endif
   ```

2. **动态库源文件 `test.c`**：
   ```c
   // test.c
   #include <stdio.h>
   #include "test.h"
   void Hello(const char *arg) {
       printf("Hello %s\n", arg);
   }
   ```

3. **主程序 `main.c`**：
   ```c
   // main.c
   #include "test.h"
   int main() {
       Hello("World");  // 调用动态库中的函数
       return 0;
   }
   ```

---

### 🔧 二、编译动态库 `libtest.so`
在终端执行以下命令：
```bash
# 编译为位置无关代码（-fPIC），并生成动态库（-shared）
gcc -fPIC -shared -o libtest.so test.c
```
- **关键参数**：
  - `-fPIC`：生成位置无关代码（必需，确保动态库可被多进程共享）。
  - `-shared`：指定生成动态库（而非可执行文件）。
- **输出文件**：`libtest.so`（命名格式必须是 `lib<名称>.so`）。

---

### 🔗 三、编译 `main.c` 并链接动态库
```bash
# 链接动态库，生成可执行文件
gcc main.c -o main -L. -ltest
```
- **关键参数**：
  - `-L.`：指定动态库搜索路径为当前目录。
  - `-ltest`：链接名为 `libtest.so` 的库（省略 `lib` 前缀和 `.so` 后缀）。
- **输出文件**：可执行文件 `main`。

---

### ⚠️ 四、解决动态库加载问题
直接运行 `./main` 可能报错 **`cannot open shared object file`**，因为系统默认在 `/usr/lib` 等目录搜索动态库，需手动指定当前目录：
#### 方法 1：临时设置环境变量（推荐测试用）
```bash
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH  # 将当前目录加入库搜索路径
./main  # 再运行程序
```

#### 方法 2：永久配置（需 root 权限）
```bash
sudo cp libtest.so /usr/lib         # 复制库到系统目录
sudo ldconfig                       # 更新库缓存
./main                              # 直接运行
```

#### 方法 3：通过 `rpath` 硬编码路径（编译时指定）
```bash
gcc main.c -o main -L. -ltest -Wl,-rpath='$ORIGIN'  # 运行时优先搜索程序所在目录
./main  # 无需额外配置
```

---

### ✅ 五、验证运行
```bash
./main
```
**输出**：
```
Hello World
```

---

### 💡 完整流程示例
```bash
# 1. 生成动态库
gcc -fPIC -shared -o libtest.so test.c

# 2. 编译主程序并链接动态库
gcc main.c -o main -L. -ltest

# 3. 设置库搜索路径（临时）
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH

# 4. 运行
./main
```

---

### ⚠️ 常见问题
1. **头文件缺失**：  
   确保 `test.h` 和 `main.c` 在同一目录，否则编译时需用 `-I` 指定头文件路径：
   ```bash
   gcc main.c -o main -I./include -L. -ltest  # 假设头文件在 ./include 目录
   ```

2. **符号冲突**：  
   动态库中的函数名需唯一，避免与其他库冲突。

3. **跨平台兼容**：  
   Windows 动态库为 `.dll`，需用 `__declspec(dllexport)` 导出函数（参考）。
*/