# PERF&FIO
## perf
> 主要用来测试IOPS，带宽和延时，既可以测试本地的也可以测试远端的；(`$SPDK_HOME/examples/nvme/perf`)
```
[-q io队列深度]
[-o io大小，以字节为单位]
[-n 每个命名空间中io队列的数量，默认为1]
[-U 每个控制器未使用的io队列数量，默认为0]
[-w io类型，可以为read,write,randread,randwrite,rw,randrw]
[-M 读写混合比列 (100为读操作，0为写操作)]
[-L 通过软件来使能时延追踪，默认disabled]
        -L 延时统计, -LL 可以统计详细的时延柱状图
[-l 通过ssd来使能时延追踪，默认为disabled]
[-t 测试时间，默认时间单位为秒]
[-c IO操作的CPU掩码，默认为1]
[-D 禁用控制器内存缓冲区中的提交队列, 默认为启用状态]
[-H 为TCP传输启用标头摘要, 默认为禁用]
[-I 为TCP传输启用数据摘要，默认为禁用]
[-r 传输ID]
    格式: 'key:value [key:value] ...'
    Keys:
    trtype      传输类别 (e.g. PCIe, RDMA)
    adrfam      地址类型 (e.g. IPv4, IPv6)
    traddr      传输地址 (e.g. 0000:04:00.0 for PCIe or 192.168.100.8 for RDMA)
    trsvcid     传输服务标识符，用在RDMA (e.g. 4420)
    subnqn      子系统 NQN (default: nqn.2014-08.org.nvmexpress.discovery)
    案列: -r 'trtype:PCIe traddr:0000:04:00.0' for PCIe or
          -r 'trtype:RDMA adrfam:IPv4 traddr:192.168.100.8 trsvcid:4420'(NVMf)
[-e 元数据配置]
    Keys:
    PRACT      保护信息标志位 (PRACT=1 or PRACT=0)
    PRCHK      保护信息检查控制 (PRCHK=GUARD|REFTAG|APPTAG)
    案列: -e 'PRACT=0,PRCHK=GUARD|REFTAG|APPTAG'
             -e 'PRACT=1,PRCHK=GUARD'
[-k 以毫秒为单位保持活动超时时间]
[-s DPDK大页大小，单位为MB]
[-m 每次投票的最大完成数量]
        (default: 0 - unlimited)
[-i 共享内存的组ID]
[-G 激活调试日志 (必须使用--enable-debug编译选项)
```
> - `sudo ./perf -q 32 -o 4096 -w randwrite -t 10 -c 0x1 -r 'trtype:PCIe traddr:0000:00:0e.0'`
> 
> 对PCIe地址为0000:00:0e.0的SSD进行测试，队列深度为32，IO的大小为4096，IO的方式为随机读写，测试时间为10s，CPU掩码为0x1

## fio
> SPDK提供两种形态的fio_plugin：
> - 基于裸盘fio_plugin，其特点为I/O通过SPDK直接访问裸盘，常用于评估SPDK用户态驱动在裸盘上的性能。
> - 基于bdev的fio_plugin，其特点为I/O测试基于SPDK块设备bdev之上，所有I/O经由块设备层bdev，再传送至裸盘设备。常用于评估SPDK块设备bdev的性能。

#### 编译
```SHELL
$ git clone https://github.com/axboe/fio
$ cd fio && git checkout fio-3.3
$ make
$ cd SPDK_HOME
$ ./configure --with-fio=$FIO_REPO
$ make
```

#### 运行
```SHELL
# fio脚本
[global]
filename=key=value [key=value] ... ns=value 
#filename=trtype=PCIe traddr=0000.04.00.0 ns=1或filename=trtype=RDMA adrfam=IPv4 traddr=192.168.100.8 trsvcid=4420 ns=1
ioengine=spdk
thread=1
group_reporting=1
direct=1
verify=0
time_based=1
ramp_time=0
runtime=2
iodepth=128
rw=randrw
bs=4k

[test]
numjobs=1
```

```SHELL
$ LD_PRELOAD=$SPDK_HOME/examples/nvme/fio_plugin/fio_plugin fio fio_conf
```

## 参考列表
- [SPDK生态工具（二）：性能评估工具](https://www.sdnlab.com/21080.html)
- [一文读懂SPDK加速关键应用：解析SPDK Perf应用](https://www.sdnlab.com/22808.html)
- [github spdk fio_glugin](https://github.com/spdk/spdk/tree/master/examples/nvme/fio_plugin)

