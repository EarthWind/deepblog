# Block Device
> SPDK的块设备层，通常简称为bdev，它是和传统操作系统块存储层相类似的C语言库，提供了如下的功能：
> - 一种可插拔模块API，用于实现与不同类别的块存储设备接口的块设备
> - 提供了NVMe,malloc(ramdisk),Linux AIO,virtio-scsi,Ceph RBD,Pmem和Vhost-SCSI Initiator等设备的驱动模块
> - 用于枚举和声明SPDK块设备，然后再这些块设备上执行操作(读取、写入、取消映射等)的应用程序API
> - 创建复杂I/O管道的工具，包括逻辑卷管理(lvol)和分区支持(GPT)
> - 通过JSON-RPC配置块设备
> - 请求队列，超时，重置处理
> - 将I/O发送到块设备的多个无锁队列

## Ceph RBD
> 需要安装librbd和libradows两个库
```SHELL
./configure --with-rbd
```

## Crypto Virtual Bdev Module
> 该模块依赖DPDK CryptoDev来提供所有加密功能，当前主要提供以下两种加密
> - AESN-NI多缓冲加密轮询驱动：RTE_CRYPTO_CIPHER_AES128_CBC
> - 英特尔（R）QuickAssist（QAT）加密轮询驱动：RTE_CRYPTO_CIPHER_AES128_CBC
>
> 对于读取，提供给加密模块的缓冲区将用作未加密数据的目标缓冲区。 但是，对于写入，临时暂存缓冲区用作加密的目标缓冲区，然后作为写缓冲区传递给底层bdev。 这样做是为了避免加密原始源缓冲区中的数据，这可能会在某些场景中引起问题

## GPT
> 自动检测bdev的SPDK GPT分区表，不需要进行任何的配置

## SPDK GPT分区表
> SPDK分区类别的GUID是7c5222bd-8f5d-4087-9c00-bf9843c7b58c，SPDK bdevs可以使用NBD暴露给操作系统然后使用标准的工具进行分区；

## iSCSI 
> SPDK iSCSI bdev驱动依赖libiscsi库默认没有编译，需要使用--with-iscsi-initiator进行配置安装

## Linux AIO
> SPDK AIO使SPDK块层能具有够访问Linux块设备或文件系统内某个文件的能力，该模块使用O_DIRECT绕过了操作系统的的页缓存，rpc中使用`construct_aio_bdev`命令进行创建；

## OCF虚拟bdev
> OCF bdev基于高性能块存储缓存元库[Open CAS Framework](https://github.com/Open-CAS/ocf)，可以用来为地产的设备提供缓存，使用`--with-ocf`使能该模块，rpc中使用`construct_ocf_bdev`进行配置

## Malloc 
> Malloc是ramdisk，由于它的性质，所以是不稳定的，它是从给与SPDK的大页中闯将的

## Null
> SPDK null bdev驱动程序是一个虚拟块I/O目标，它会丢弃所有写入并返回未定义的读取数据。 它可以用最小的块设备开销对bdev I/O堆栈的其余部分进行基准测试，并用于测试使用Malloc bdev无法轻松创建的配置。rpc使用`construct_null_bdev`创建。

## NVMe
> 在SPDK中基于NVMe设备有两种方式来创建块设备，第一种是本地的PCIe驱动，第二种NVMe-oF设备，两种方式rpc都使用`construct_nvme_bdev`进行创建

## Logical volumes
> Logical Volumes库是可扩展的存储空间管理系统，可以用来在其他的bdevs之上创建和管理大小可变的虚拟块设备；在创建逻辑卷之前，需要先创建lvols store，负责管理路基卷底层bdevs的元数据，rpc使用`construct_lvol_store`进行创建；逻辑卷使用`construct_lvol_bdev`创建

## Passthru
> SPDK Passthru虚拟块设备模块用作如何编写虚拟块设备模块的示例。 它实现了vbdev模块所需的功能，并演示了一些其他基本功能，例如每个I/O上下文的使用。

## Pmem
> SPDK pmem bdev驱动程序使用pmemblk池作为块I/O操作的目标，默认不参与编译，使用`-with-pmdk`进行编译，rpc使用`create_pmem_pool`命令；[PMDK(Persistent Memory Development Kit)](http://pmem.io/pmdk/)

## Virtio Block
> Virtio-Block驱动程序允许从Virtio-Block设备创建SPDK bdev

## Virtio-SCSI
> Virtio-SCSI驱动程序允许从Virtio-SCSI LUN创建SPDK块设备
