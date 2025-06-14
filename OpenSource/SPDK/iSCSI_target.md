# iSCSI
## 1. configuration
#### 1.1 配置iscsi.conf
```
[Global]
[Bdev]
[iSCSI]
  NodeBase "iqn.2016-06.io.spdk"
  AuthFile /usr/local/etc/spdk/auth.conf
  MinConnectionsPerCore 4
  Timeout 30
  DiscoveryAuthMethod Auto
  DefaultTime2Wait 2
  DefaultTime2Retain 60
  FirstBurstLength 8192
  ImmediateData Yes
  ErrorRecoveryLevel 0
#  Syntax:
#    Portal <Name> <IP address>[:<port>[@<cpumask>]]
[PortalGroup1]
  Portal DA1 192.168.249.194:3260

[InitiatorGroup1]
  InitiatorName ANY
  Netmask 192.168.249.0/24

[Nvme]
  TransportID "trtype:PCIe traddr:0000:00:0e.0" Nvme0
  RetryCount 4
  TimeoutUsec 0
  ActionOnTimeout None
  AdminPollRate 100000
  IOPollRate 0
  HotplugEnable No
  HotplugPollRate 0

[TargetNode1]
  TargetName nvmeNvme0
  TargetAlias "Data Nvme0"
  Mapping PortalGroup1 InitiatorGroup1
  AuthMethod Auto
  AuthGroup AuthGroup1
  UseDigest Auto
  LUN0 Nvme0n1
  QueueDepth 128
```

#### 1.2 运行target
```SHELL
$ sudo $SPDK_HOME/scripts/setup.sh
0000:00:0e.0 (80ee 4e56): nvme -> uio_pci_generic
$ cd app/iscsi_tgt/
$ sudo ./iscsi_tgt -c iscsi.conf
Starting SPDK v19.04-pre / DPDK 19.02.0 initialization...
[ DPDK EAL parameters: iscsi --no-shconf -c 0x1 --log-level=lib.eal:6 --base-virtaddr=0x200000000000 --match-allocations --file-prefix=spdk_pid2657 ]
app.c: 635:spdk_app_start: *NOTICE*: Total cores available: 1
reactor.c: 230:_spdk_reactor_run: *NOTICE*: Reactor started on core 0
```

## 2. initialtor
#### 2.1 安装客户端
```SHELL
# debian
$ sudo apt install iscsi-initialtor
# redhat
$ yum install -y iscsi-initiator-utils
```

#### 2.2 配置客户端
```SHELL
$ sudo vim /etc/iscsi/iscsid.conf
...
node.session.cmds_max = 4096
node.session.queue_depth = 128
...
# 重启iscsid服务
$ sudo systemctl restart iscsid
```

#### 2.3 连接target
```SHELL
$ sudo iscsiadm -m discovery -t sendtargets -p 192.168.249.194
192.168.249.194:3260,1 iqn.2016-06.io.spdk:nvmeNvme0
$ sudo iscsiadm -m node -l
Login to [iface: default, target: iqn.2016-06.io.spdk:nvmeNvme0, portal: 192.168.249.194,3260] successful.
$ sudo iscsiadm -m session -P 3 | grep Attached
Attached SCSI devices:
    Attached scsi disk sdc          State: running
$ sudo lsblk
sdc                     8:32   0    10G  0 disk
```

#### 2.4 Initiator参数优化
```SHELL
$ sudo echo noop > /sys/block/sdc/queue/scheduler
$ sudo echo "2" > /sys/block/sdc/queue/nomerges
$ sudo echo "1024" > /sys/block/sdc/queue/nr_requests
```

