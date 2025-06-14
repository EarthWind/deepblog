# NVMe-oF
> SPDK NVMe over Fabrics target是一个用户空间程序，可以通过以太网、Inifiband或光纤通道来到处块设备，SPDK目前支持RDMA和TCP两种传输模式；
>
> 如果想要终止应用程序，需要发送SIGTERM信号，这样应用会释放掉共享内存资源；如果使用SIGKILL信号，应用程序则不会释放共享内存资源，需要手动进行释放；

## RDMA传输模式
#### 安装依赖
```SHELL
# debian
$ sudo apt-get install libibverbs-dev librdmacm-dev
# redhat
$ sudo yum install libibverbs-devel librdmacm-devel
```

#### 编译
```SHELL
$ ./configure --with-rdma
```

#### 挂载必要模块
```SHELL
$ sudo modprobe ib_cm
$ sudo modprobe ib_core
$ sudo modprobe ib_ucm
$ sudo modprobe ib_umad
$ sudo modprobe ib_uverbs
$ sudo modprobe iw_cm
$ sudo modprobe rdma_cm
$ sudo modprobe rdma_ucm
```

#### RDMA关联网络设备
```SHELL
$ ls /sys/class/infiniband/*/device/net
enp0s9
# Mellanox ConnectX-3
$ sudo modprobe mlx4_core
$ sudo modprobe mlx4_ib
$ sudo modprobe mlx4_en
# Mellanox ConnectX-4
$ sudo modprobe mlx5_core
$ sudo modprobe mlx5_ib
```

#### 配置文档
```
[Nvmf]
  AcceptorPollRate 10000
[Transport]
  Type RDMA
  #MaxQueueDepth 128
  #MaxQueuesPerSession 4
  #InCapsuleDataSize 4096
  #MaxIOSize 131072
  #IOUnitSize 131072
  #MaxAQDepth 32
  #NumSharedBuffers 512
  #BufCacheSize 32
  #MaxSRQDepth 4096
[Nvme]
  TransportID "trtype:PCIe traddr:0000:00:0e.0" Nvme0
  RetryCount 4
  TimeoutUsec 0
  ActionOnTimeout None
  AdminPollRate 100000
  IOPollRate 0
  HotplugEnable No

[Subsystem1]
  NQN nqn.2016-06.io.spdk:cnode1
  Listen RDMA 10.0.2.5:4420
  AllowAnyHost Yes
  Host nqn.2016-06.io.spdk:init
  SN SPDK00000000000001
  Namespace Nvme0n1 1
```

#### host配置
```SHELL
# 安装命令
$ sudo apt install nvme-cli
# 加载模块
$ sudo modprobe nvme-rdma
# 发现nvme
$ sudo nvme discover -t rdma -a 10.0.2.5 -s 4420 -w 10.0.2.6

Discovery Log Number of Records 1, Generation counter 4
=====Discovery Log Entry 0======
trtype:  rdma
adrfam:  ipv4
subtype: nvme subsystem
treq:    not specified
portid:  0
trsvcid: 4420
subnqn:  nqn.2016-06.io.spdk:cnode1
traddr:  10.0.2.5
rdma_prtype: not specified
rdma_qptype: connected
rdma_cms:    rdma-cm
rdma_pkey: 0x0000
# 连接
$ sudo nvme connect -t rdma -n "nqn.2016-06.io.spdk:cnode1" -a 10.0.2.5 -s 4420 -w 10.0.2.6
$ sudo nvme list
Node             SN                   Model                                    Namespace Usage                      Format           FW Rev
---------------- -------------------- ---------------------------------------- --------- -------------------------- ---------------- --------
/dev/nvme1n1     SPDK00000000000001   SPDK bdev Controller                     1          10.74  GB /  10.74  GB    512   B +  0 B   19.04
# 取消连接
$ sudo nvme disconnect -n "nqn.2016-06.io.spdk:cnode1"
NQN:nqn.2016-06.io.spdk:cnode1 disconnected 1 controller(s)
```

## TCP传输模式
> 不依赖其他模块，默认编译
#### 配置文档
```
[Nvmf]
  AcceptorPollRate 10000

[Transport]
  Type TCP
  #MaxQueueDepth 128
  #MaxQueuesPerSession 4
  #InCapsuleDataSize 4096
  #MaxIOSize 131072
  #IOUnitSize 131072
  #MaxAQDepth 32
  #NumSharedBuffers 512
  #BufCacheSize 32

[Nvme]
  TransportID "trtype:PCIe traddr:0000:00:0e.0" Nvme0
  RetryCount 4
  TimeoutUsec 0
  ActionOnTimeout None
  AdminPollRate 100000
  IOPollRate 0
  HotplugEnable No

[Subsystem1]
  NQN nqn.2016-06.io.spdk:cnode1
  Listen TCP 192.168.249.194:4420
  AllowAnyHost Yes
  Host nqn.2016-06.io.spdk:init
  SN SPDK00000000000001
  MaxNamespaces 20
  Namespace Nvme0n1 1
```

#### host配置
```SHELL
# 安装命令
$ sudo apt install nvme-cli
$ sudo modprobe nvme_tcp
$ sudo nvme discover -t rdma -a 10.0.2.5 -s 4420 -w 10.0.2.6
```

## 参考列表
- [HowTo Compile Linux Kernel for NVMe over Fabrics](https://community.mellanox.com/s/article/howto-compile-linux-kernel-for-nvme-over-fabrics)

