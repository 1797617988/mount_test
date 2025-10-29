# GDB调试流程

编译可调试的程序时 要 加上 -g选项，比如:  gcc -g main.c -L./ -lmy_add -o main

调试方法：gdb <可执行文件>

查看源文件代码： l

给源文件加断点：b  <行号>

运行程序 ：r  （会运行到断点处）

查看断点： info  b

查看变量值 ： p  <变量名>

单步执行：n 或s

恢复程序运行：c

设置变量的值 ： set variable x=n  /*设置变量x的值为n*/



# 1 Makefile 编译工具

Makefile是一个编译管理工具，它用来编译各种源代码，它的编译原理是增量编译，即只编译改动过的源文件（通过时间戳识别文件是否被改动过）。

## Makefile文件中的三要素

目标：要生成的最终输出文件名

依赖：生成目标文件所需要的依赖文件

命令：生成目标文件的命令

make命令会读取Makefile文件，解析并执行该文件中的命令

make命令执行时若后面不带目标参数，则会执行对应Makefile中的第一个目标，若make命令后带有目标参数，则会执行生成该目标参数的命令。



Makefile格式：

target： dependency files

​	command

注意：命令前面是[tab]键，不是空格

## 1.2 Makefile中创建和使用变量

1，Makefile中可用 ‘=’ 给变量赋值

VAR_A=A

VAR_B=$(VAR_A)B

VAR_A=AA

经过上面的赋值后，最后VAR_B的值是AAB，而不是AB。在执行make命令时，会把整个Makefile都展开，最后决定变量的值。

2，Makefile中可用 ‘:=’ 给变量赋值

相比较上面用 = 给变量赋值，:= 表示直接赋值，赋予当前的值。

VAR_A:=A

VAR_B:=$(VAR_A)B

VAR_A:=AA

最后变量VAR_B的值是AB。即根据当前代码行所处位置变量的值给另一个变量赋值。

3，Makefile中可用  '?=' 给变量赋值

?= 表示如果该变量没有被赋值，则给该变量赋值，若该变量已经被赋值，则不再给此变量幅值。

4, Makefile中可用 += 给变量赋值，+= 会将变量原有的值 和新值 用 空格 连接起来。

VAR_A=A

VAR_A+=B 

VAR_A的值 就是A B 



## 1.3 Makefile中特殊符号的含义

以下这几个符号在Makefile命令中的含义：

$@   目标的名称

$^    依赖文件列表中所有文件的名称，文件名之间以空格分开

$<  依赖文件列表中第一个文件的名称

$*  不包含扩展名的目标文件名称

$?  所有时间戳比目标件文件新的依赖文件，以空格分开



## 1.4 Makefile目标规则：

1. 执行make命令时，若不指定要生成的目标时，默认会执行Makefile中的第一个目标，若指定了要生成的目标，则会执行生成该目标的命令。
2. make命令执行时，默认会执行当前目录下的Makefile文件，可以用 -f 选项设置要执行的Makefile文件
3. 在顶层的Makefile中可用export  <variable>导致变量，那么在子目录中或后续的Makefile中就可以直接使用这个变量。



## 1.5 伪目标

```makefile
 # Makefile文件内容
 out:
 	@echo 'Hello World!'
```



若Makefile中的目标与该目录下的文件名相同，则该目标的命令不会被执行的原因分析：

对于具有依赖项的目标来说，如果目标不存在或目标所依赖的文件比目标新，就会执行目标的命令去产生或更新目标文件

因为 out 目标没有依赖，又由于当前目录下存在与out同名的文件，make会认为out是最新的，所以不会去执行out目标的命令。

```makefile
 # Makefile文件内容
 .PHONY: out
 out:
 	@echo 'Hello World!'
```



.PHONY是一个伪目标，Makefile中将一个目标声明为.PHONY，就是告诉Makefile 这个目标不是文件目标，而是一个命令。那么make在执行Makefile中的命令时就不会根据目标文件的依赖是否比目标文件新的规则而跳过该命令。

## 1.6 常用的Makefile语法

1， %.o : %.c : 为所有后缀.c的文件生成对应的.o文件（所有目标后缀为.o的文件依赖于同名的后缀为.c的文件）

2，.c.o   所有后缀为.o的文件依赖于于同名的后缀为.c的文件

3，shell 

​	语法： $(shell  <shell command>) 

​	作用：执行一个shell命令，并将shell命令的执行结果返回

​	作用和  `<shell command>` 相同，``是反引号

## 1.7 Makefile中的常用函数

Makefile中内置了一些常用的函数，利用这些函数可以简化Makefile文件的编写

函数的调用语法如下：

$(<function>  <arguments...>)  

<function> 是函数名称

<arguments> 是参数列表，如果有多个参数，参数之间为逗号(,)分开

函数名和参数之间用空格分开

### 1.7.1 字符串函数

字符串替换函数：$(subst <from>, <to>, <text>)

功能：把字符串<text>中的<from>替换为<to>

返回：替换后的字符串

```make
#Makefile内容
all:
	@echo  $(subst t, e, maktfilt) 

```

### 1.7.2 模式字符串替换函数 

### $(patsubst  <pattern>, <replacement>, <text>)

功能：查找<text>中的单词（单词以 ‘空格’，'tab'，‘换行’来分割），是否符合<pattern>，若符合则用<replacement>来替换

返回：返回替换一的字符串

```ma
#Makefile 内容
all:
	@echo $(patsubst %.c,%.o,a.c b.c c.c d.c)
```

其它的函数省略（......），请参照另一个文档



## Makefile中for循环的用法，举例

for item in $(SUBDIR); do \

$(MAKE) -C $$item; \

done



## Makefile中的参数传递

make VAR=XXX

```makefile
VAR?=YYY
output:
	@echo $(VAR)
	
# make VAR=Hello
给VAR传送参数
```

## Makefile中导出变量

比如：

```makefile
CC=g++
export CC

#以上就将CC的定义导出，在子目录中的Makefile就可以使用CC，其值 就是g++
```



作业1：

1，实现一个整型数组的选择排序算法，文件名为 mysort.c

2，分别把mysort.c编译为用静态库和动态库

3，实现一个man.c 文件，此文件中的main函数中，定义一个整型数组

4，把main.c以静态库的链接方法链接mysort.c，并调用静态库中的排序函数。

5，把main.c以动态库的链接方法调用上面编译的mysort.c动态库中的排序算法。

6，编写Makefile 实现动态库编译与链接

7，编写Makefile实现静态库的编译与链接



作业2：

1，创建一个目录 mysort，将mysort.c放在mysort目录下。mysort.c中实现了对整型数组的排序 。

2，实现一个man.c 文件，此文件中的main函数中，定义一个整型数组，并调用mysort.c中的排序方法，并输出结果。

3，编写Makefile，将mysort.c编译为静态库。并链接此静态库。生成可执行文件。





