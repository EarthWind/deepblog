# UNIX基础知识
> 口令文件：口令文件/etc/passwd的登陆项由7个冒号分隔的字段组成，依次是：登陆用于名，加密口令,用户ID，用户组ID，注释字段，起始目录，shell程序；加密口令在该文件中一般使用*号代替，真正的口令放在了/etc/shadow文件中

> 只有斜线(/)和空格字符不能出现在文件中，斜线用来分隔构成路径名的个文件，空格符则用来终止一个路径名，POSIX.1推荐使字母、数字、句点、短横线和下划线为文件和目录命名

> 创建新目录时自动船舰两个文件：.(点)和..(点点)，分别代表当前目录和上级目录，在根目录下，都表示当前目录

> 每个进程都有一个工作目录(working directory),有时候也称为当前工作目录，可以使用chdir函数更改其工作目录；另外，用户在登陆成功后默认会进入口令文件中配置的起始目录中；

> 文件描述符通常是一个小的非负整数，内核用以标识一个特定进程正在访问的文件；每当运行一个新程序时，shell都为其打开3个文件描述符，即标准输入，标准输出和错误输出，对应的文件描述符为0，1，2

> 一个进程内的所有线程共享同一个地址空间、文件描述符、栈以及与进程相关的属性，线程也用ID标识，但线程ID只在它所属的进程内起作用，在另一个进程中没有意义；

> 多个线程共享进程的地址空间，每个线程都有属于它自己的局部errno以避免一个线程干扰另一个线程

> 对于errno应当注意两条规则，第一：如果没有出错，其值不会被例程清除，因此仅当函数返回指明出错时才检验errno(`strerrno`函数可以将错误码转换为错误描述输出)的值；第二：任何函数都不会将errno的值设置为0

> 一个用户可以最多属于16个其他组

> 信号用于通知进程发生了某种情况。一般由三种处理方式：
> - 忽略信号
> - 按系统默认的方式处理
> - 注册一个函数用于信号(signal)

> UNIX系统上使用过两种不同的时间值
> - 日历时间：自协调世界时(UTC)以来经过的秒数累积值
> - 进程时间：也称为CPU时间，用于计量进程使用的中央处理器资源，又分为：
>> - 时钟周期
>> - 用户CPU时间
>> - 系统CPU时间

> 系统调用和库函数的区别：
> - 库函数可以更换但是系统调用一般不更换
> - 系统调用通常提供一种小接口，而库函数通常提供比较复杂的功能

#### 宏定义
> `STDIN_FILENO`,`STDOUT_FILENO`,`STDERR_FILENO`是定义在`unistd.h`中的标准输入，标准输出和错误输出

#### 函数列表
- `DIR *opendir(const char *name);`
  > `#include <dirent.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man3/opendir.3.html)

- `struct dirent *readdir(DIR *dirp);`
  > `#include <dirent.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man3/readdir.3.html)

- `ssize_t read(int fd, void *buf, size_t count);`
  > `#include <unistd.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/read.2.html)

- `ssize_t write(int fd, const void *buf, size_t count);`
  > `#include <unistd.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/write.2.html) 

- `int getc(FILE *stream);`
  > `#include <stdio.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man3/getc.3p.html)

- `int putc(int c, FILE *stream);`
  > `#include <stdio.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man3/putc.3p.html1)

- `char *fgets(char *restrict s, int n, FILE *restrict stream);`
  > `#include <stdio.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man3/fgets.3p.html)

- `char *strerror(int errnum);`
  > `#include <string.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man3/strerror.3.html)

- `gid_t getgid(void);`
  > `#include <unistd.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/getgid.2.html)

- `uid_t getuid(void);`
  > `#include <unistd.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/getuid.2.html)

- `sighandler_t signal(int signum, sighandler_t handler);`
  > `#include <signal.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/signal.2.html)
  >
  > 信号[参考](http://man7.org/linux/man-pages/man7/signal.7.html)