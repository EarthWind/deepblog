# 文件I/O
> - read,write是不带缓冲的IO，这里的缓冲是用户空间的缓冲，它仍然有内核空间的缓冲，并且它调用的是一个系统调用；
>
> - 对于内核而言，所有打开的文件都通过文件描述符引用，文件描述符是一个非负整数，文件描述符的取值范围为0~OPEN_MAX - 1, OPEN_MAX的值可以使用`ulimit -n`查看，默认为1024，也可以使用`getconf OPEN_MAX`，每个进程可以打开的文件总量依赖系统的软限制和硬限制；当打开一个现有或创建一个新文件时，内核向进程返回一个文件描述符；
>
> - Unix系统shell把文件描述符0(STDOUT_FILENO)与进程的标准输入关联，文件描述符1(STDIN_FILENO)与标准输出关联，文件描述符2(STDERR_FILENO)与标准错误关联，文件描述符幻数0，1，2对应的宏定义在`unistd.h`头文件中定义
>
> - 由open和openat函数返回的文件描述符一定是最小的未用描述符数值。
>
> - 在创建或打开文件的时候，如果文件名称大于NAME_MAX或目录最大值PATH_MAX，则会报错；(`getconf NAME_MAX /usr`默认为255，`getconf PATH_MAX /usr`默认为4096)
>
> - 当一个进程终止时，内核会自动关闭它所有的打开文件
>
> - 当打开一个文件时，除非指定O_APPEND选项，否则该偏移量被设置为0
> 
> - 文件的偏移量可以大于文件的当前长度，这种情况下，对该文件的下一次写将加长该文件，并在文件中构成一个空洞，这是允许的，位于文件中但没有写过的字节都被读为0，文件中的空洞并不要求在磁盘上占用存储区；

> 几个重要函数：
> - dup
> - fcntl
> - sync
> - fsync
> - ioctl

#### 库函数列表
- `int open(const char *pathname, int flags, ...);`
- `int open(const char *pathname, int flags, mode_t mode);`
- `int openat(int dirfd, const char *pathname, int flags);`
- `int openat(int dirfd, const char *pathname, int flags, mode_t mode);`
  > `#include <sys/types.h>`
  >
  > `#include <sys/stat.h>`
  >
  > `#include <fcntl.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/open.2.html)
  >
  > flags标志位的可能取值：
  > - O_RDONLY: 只读打开文件
  > - O_WRONLY: 只写打开文件
  > - O_RDWR: 读、写打开文件
  > - O_EXEC: 值执行方式打开
  > - O_SEARCH: 只搜索打开，只应用于目录，对目录的权限进行验证，系统还没有实现支持
  >
  > 以上的权限必须指定且只能指定一个，下面的选项可以用‘或’方式进行指定
  > - O_APPEND: 每次写时都追加到文件的尾端
  > - O_CLOEXEC: 把FD_CLOEXEC设置为文件描述符标志，在执行EXEC时该文件描述符会被关闭
  > - O_CREAT: 如果打开的文件不存在，则创建
  > - O_DIRECTORY: 如果path不是目录，则出错
  > - O_EXEL: 和O_CREAT标志位一起指定，如果文件已经存在，则报错，否则创建，这可以用来检查文件是否存在以及确认创建新文件
  > - O_NOCTTY：如果path引用的是终端设备，则不将该设备分配作为此进程的控制终端
  > - O_NOFOLLOW: 如果path应用的是一个符号链接，则出错
  > - O_NONBLOCK：如果path引用的是一个FIFO(命名管道)、一个块特殊文件或一个特殊文件，则表示后续的IO操作设置为非阻塞模式
  > - O_SYNC: 没吃write等到IO操作完成；文件的属性和数据都会被更新
  > - O_TRUNC：如果文件存在，而且为只写或读写方式打开的话，将其长度截断为0
  > - O_TTY_INIT:
  > - O_DSYNC: 每次使用write要等到物理IO的完成，不等待文件属性的更新；
  > - O_RSYNC: 在文件的写操作完成之前所有的read操作都需要等待；
  >
  > 该函数返回的文件描述符一定是最小的未用描述符数值。这可以用来将标准输入、标准输出和标准错误输出上打开新的文件；
  >
  > 打开的文件名称和文件目录的长度会被NAME_MAX和PATH_MAX以及_POSIX_NO_TRUNC控制，超过长度的要么报错，要么截断；
  

- `int creat(const char *path, mode_t mode);`
  > `#include <sys/stat.h>`
  >
  > `#include <fcntl.h>`
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man3/creat.3p.html)

- `int close(int fd);`
  > `#include <unistd.h>`
  >
  > 关闭打开的文件，并且会释放当前进程在该文件上的所有记录锁，另外，当进程终止时，内核会自动关闭它所有的打开文件；
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/close.2.html)

- `off_t lseek(int fd, off_t offset, int whence);`
  > `#include <unistd.h>`
  >
  > `#include <sys/types.h>`
  >
  > whence:
  > - SEEK_SET: 设置距离文件开始处offset个字节
  > - SEEK_CUR: 设置距离当前位置offset个字节，可正可负数
  > - SEEK_END: 设置的相对位置为文件末尾，offset可正可负
  >
  > 每个文件在打开的时候都有一个和它关联的“当前文件偏移量”，除非指定O_APPEND选项，否则该偏移量被设置为0。lseek可以显示为一个文件设置偏移量。管道、FIFO以及网络套截至是不支持偏移量的，使用该函数时会返回-1，并将errno设置为ESPIPE
  >
  > man 7 [参考](http://man7.org/linux/man-pages/man2/lseek.2.html)
