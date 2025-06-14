# SPDK
> 超高性能运用了两个关键技术：运行于用户态和轮询模式
> - 运行于用户态：
>
> SPDK提供了用户态的NVMe驱动程序，这避免了内核上下文切换和中断将会节省大量的处理开销,也减少了内核和用户空间的数据拷贝，SPDK的指导原则是通过消除每一处额外的软件开销来提供最少的延迟和最高的效率，内核提供的通用计算用例可能不适用于专用存储堆栈；
>
> - 轮询模式驱动(PMDs)：
>
> 应用程序提交读写请求后继续执行其他工作，以一定的时间间隔回头检查I/O是否已经完成。这种方式避免了中断带来的延迟和开销并使得应用程序提高了I/O的效率；

> 从下往上构建：
> - 硬件驱动：
>> - NVMe Driver: 
>> - Intel IOAT: 因特尔I/O加速技术，是一种基于Xeon处理器平台的copy offload引擎
> - 后端块设备
>> - NVMe over Fabrics(NVMe-oF) initiator:
>> - Ceph RADOS Block Device(RBD): 使Ceph成为SPDK的后端设备
>> - Blobstore Block Device: 由SPDK Blobstore分配的块设备
>> - Linux Ansychrounous I/O(AIO): 允许SPDK与内核设备交互
> - 存储服务
>> - Block device abstration layer(BDAL):
>> - Blobstore: 为SPDK是新一个高精简的文件式语义
> - 存储协议
>> - iSCSI target: 建立通过以太网的块流量规范，默认使用内核TCP/IP栈
>> - NVMe-oF taget: 
>> - vhost-scsi target: KVM/QEMU的功能利用了SPDK NVMe驱动

## 1. 原理
#### 1.1 用户空间驱动
- 从用户空间控制硬件
> 为了SPDK能供控制一个设备，需要消除操作系统对设备的控制权，在Linux上通常写入sysfs文件系统的一个文件来取消内核驱动的绑定，然后SPDK会给设备绑定两个特殊设备驱动的其中一个：uio(userspace I/O system)或vfio((Virtual Function I/O)，这会使得操作系统不会再为设备绑定默认的驱动，但是这两个驱动并不会对设备进行初始化或配置，它们也不能够识别设备的类别是什么；uio和vfio的主要区别在于vfio能供对平台的IOMMU进行编程，它够确保用户空间驱动程序内存的安全；
>
> 用户空间驱动使用uio和vfio特性将设备的PCI BAR映射到当前的进程中，然后程序可以直接使用MMIO操作；

- 中断
> SPDK会不停的轮询设备，查看操作是否完成，而不是等待设备中断的到来；主要有以下两个原因：1.再用户空间进程将中断路由到中断处理程序对大多数硬件都是不可行的；2.中断引入了软件抖动以及上下文的切换导致了显著的开销；
>
> SPDK中几乎所有的操作都是异步的，它允许用于在操作完成后提供一个回调；

- 线程
> NVMe设备暴露了多个队列来用于提交请求到设备，线程可以在不用协调的情况下访问单独的队列，所以应用可以并行的操作设备而不需要对设备进行锁定。
>
> 一个用户空间的驱动程序是嵌入的应用中的，因此，SPDK可以直接暴露满足应用线程或进程需求的硬件队列给应用，通常为一个线程或进程分配一个队列，这就保证了线程提交的请求不需要和其他线程进行协调，达到无锁访问设备；

#### 1.2 用户驱动内存管理
> NVMe设备和系统内存之间使用DMA(Direct Memory Access)方式进行数据的传输；NVMe1.0规范要求物理内存都可以通过所谓的PRP列表进行描述，所以内存必须有以下属性：
> - 内存被分为4KiB的页面，称为设备页面
> - 第一个设备页面可以是从任何4字节对齐地址开始的部分页面。它可能延伸到当前物理页面的末尾，但不会超出
> - 如果有多个设备页面，则第一个设备页面必须以物理4KiB页面边界结束
> - 最后一个设备页面从物理4KiB页面边界开始
>
> SPDK依赖DPDK来分配固定内存(pinned memory)。DPDK通过分配大页内存(默认为2MiB)来实现固定内存，Linux内核对待大页和通常的4KiB页有所不同，特别是，操作系统从不更改大页内存的地址；所以SPDK中所有的数据缓冲区都使用`spdk_dma_malloc()`函数来分配；

#### 参考
- [SPDK官网](https://spdk.io/)
- [SPDK简介](http://aidaiz.com/spdk/)
- [The Userspace I/O HOWTO](https://www.kernel.org/doc/html/latest/driver-api/uio-howto.html)
- [VFIO - "Virtual Function I/O" [1]_](https://www.kernel.org/doc/Documentation/vfio.txt)
