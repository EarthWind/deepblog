# Ceph 架构和代码详解博客系列

## 系列定位

- 目标读者：已经会 Linux/C++、了解分布式存储基本概念，但还没真正啃过 Ceph 源码的工程师
- 写作顺序：先建立系统全景，再拆核心模块，最后进入关键数据路径、异常恢复、性能调优和源码阅读方法
- 每篇建议固定结构：问题背景、架构图、核心流程、关键类/线程、源码入口、调试方法、常见误区
- 推荐节奏：前 6 篇讲“为什么这么设计与怎么落地”，中间 32 篇讲“代码怎么跑起来”，最后 4 篇讲“怎么定位问题、分析一致性与参与贡献”

## 循序渐进博客列表

1. 《Ceph 是什么：从单机文件系统思维切换到分布式存储思维》：介绍对象存储、块存储、文件存储的统一底座，解释 RADOS、`librados`、`RBD`、`CephFS`、`RGW` 的关系；正文文件：`blog/01-ceph-是什么-从单机文件系统思维切换到分布式存储思维.md`
2. 《Ceph 总体架构图解：MON、MGR、OSD、MDS、RGW 各自负责什么》：建立全局心智模型，讲清控制面、数据面与网关层的边界，以及不同客户端请求大致如何流动；正文文件：`blog/02-ceph-总体架构图解-MON-MGR-OSD-MDS-RGW-各自负责什么.md`
3. 《部署一个最小可用 Ceph 集群：从节点角色到 cephadm 编排》：介绍 MON、MGR、OSD、MDS、RGW 的基础部署方式，讲清最小集群拓扑、设备准备、网络规划、cephadm 的核心思路，以及 Prometheus/Grafana 监控接入与仪表盘导入；正文文件：`blog/03-部署一个最小可用-Ceph-集群-从节点角色到-cephadm-编排.md`
4. 《Rook 架构与实现：Kubernetes 中如何编排和管理 Ceph 集群》：介绍 Rook Operator、CephCluster CRD、存储编排与生命周期管理，讲清 Rook 与 cephadm、Ceph MGR 编排接口之间的关系；正文文件：`blog/04-Rook-架构与实现-Kubernetes-中如何编排和管理-Ceph-集群.md`
5. 《Ceph 源码仓库怎么读：目录结构、构建系统与第一批核心模块》：带读者认识 `src/mon`、`src/osd`、`src/mgr`、`src/mds`、`src/common`、`src/msg`、`src/crimson` 的职责；正文文件：`blog/05-Ceph-源码仓库怎么读-目录结构-构建系统与第一批核心模块.md`
6. 《从一次写请求看 Ceph：客户端到落盘的主路径总览》：先不陷入细节，用一条主线把客户端、PG、主副本、事务、持久化串起来；正文文件：`blog/06-从一次写请求看-Ceph-客户端到落盘的主路径总览.md`
7. 《MON 原理与源码：集群地图、选举、Quorum 为什么是 Ceph 的大脑》：重点讲 `monmap`、`osdmap`、`pgmap` 的管理逻辑，以及 MON 为什么不在数据路径上；正文文件：`blog/07-MON-原理与源码-集群地图-选举-Quorum-为什么是-Ceph-的大脑.md`
8. 《MGR 原理与源码：为什么 Ceph 需要管理平面与插件生态》：解释 MGR 与 MON 的分工，介绍指标、dashboard、orchestrator、模块化设计；正文文件：`blog/08-MGR-原理与源码-为什么-Ceph-需要管理平面与插件生态.md`
9. 《Ceph Dashboard 架构与实现：管理界面、REST API 与前后端协作》：聚焦 `mgr/dashboard` 模块，讲清 Dashboard 如何集成认证、REST API、前端页面、Grafana 链接与集群管理能力；正文文件：`blog/09-Ceph-Dashboard-架构与实现-管理界面-REST-API-与前后端协作.md`
10. 《OSD 进程详解：Ceph 最核心的数据面执行单元》：讲 OSD 线程模型、请求队列、Peering、复制、恢复与后台任务，配合 `src/osd`；正文文件：`blog/10-OSD-进程详解-Ceph-最核心的数据面执行单元.md`
11. 《CRUSH 算法讲透：Ceph 为什么能不依赖中心元数据做数据定位》：从 bucket、rule、failure domain 讲到对象如何映射到 PG，再映射到 OSD；正文文件：`blog/11-CRUSH-算法讲透-Ceph-为什么能不依赖中心元数据做数据定位.md`
12. 《PG 是 Ceph 的灵魂：为什么不是直接把对象映射到 OSD》：重点写 PG 的设计动机、状态机、Peering 流程和它在一致性与扩展性上的作用；正文文件：`blog/12-PG-是-Ceph-的灵魂-为什么不是直接把对象映射到-OSD.md`
13. 《客户端 IO 路径源码：`librados`、消息层、对象操作是如何发出去的》：跟踪一次对象读写请求从客户端封装、编码、发送到 OSD 的关键调用链；正文文件：`blog/13-客户端-IO-路径源码-librados-消息层-对象操作是如何发出去的.md`
14. 《Ceph 消息通信层：Messenger、Connection、Dispatch 机制详解》：拆解 `src/msg` 与 `src/common`，帮助读者理解节点间 RPC 风格通信；正文文件：`blog/14-Ceph-消息通信层-Messenger-Connection-Dispatch-机制详解.md`
15. 《BlueStore 设计与源码：Ceph 为什么放弃 FileStore》：介绍 BlueStore、BlueFS、RocksDB、WAL/DB/Main device 的关系，以及写入路径和元数据组织；正文文件：`blog/15-BlueStore-设计与源码-Ceph-为什么放弃-FileStore.md`
16. 《对象写入落盘全过程：事务、日志、提交语义与一致性保证》：把 OSD 层复制协议与 BlueStore 持久化串起来，讲清 ack、commit、apply 的区别；正文文件：`blog/16-对象写入落盘全过程-事务-日志-提交语义与一致性保证.md`
17. 《RADOS EC 实现详解：纠删码池、条带布局与读改写流程》：讲清 EC pool、K+M 编码、对象条带切分、读改写与恢复路径，以及 `ECBackend`、`ECCommon` 等关键实现；正文文件：`blog/17-RADOS-EC-实现详解-纠删码池-条带布局与读改写流程.md`
18. 《RBD 架构入门：Ceph 块存储是如何建立在 RADOS 之上的》：讲清 image、object、striping、snapshot、clone 的基础概念，以及 RBD 和 `librbd`、`krbd`、QEMU 的关系；正文文件：`blog/18-RBD-架构入门-Ceph-块存储是如何建立在-RADOS-之上的.md`
19. 《RBD 读写路径源码：一次块设备 IO 如何落到 Ceph 集群》：跟踪用户态或内核态块请求如何进入 `librbd` 或 `krbd`，再经由 `librados` 下发到 OSD；正文文件：`blog/19-RBD-读写路径源码-一次块设备-IO-如何落到-Ceph-集群.md`
20. 《RBD 高级特性详解：快照、克隆、特性位与镜像元数据设计》：重点写 layering、object map、exclusive lock、journaling、deep copy 等能力的设计动机与代码组织；正文文件：`blog/20-RBD-高级特性详解-快照-克隆-特性位与镜像元数据设计.md`
21. 《RBD 性能测试实战：基准方法、压测工具与指标拆解》：围绕 `rbd bench`、`fio`、`rbd-nbd`、`krbd` 等测试路径，讲清吞吐、IOPS、时延和队列深度该如何测、如何看；正文文件：`blog/21-RBD-性能测试实战-基准方法-压测工具与指标拆解.md`
22. 《RBD 性能调优指南：客户端、网络、OSD 与 BlueStore 的联动优化》：从镜像特性、客户端栈、缓存、网络、PG 分布到 BlueStore 设备布局，系统梳理 RBD 性能瓶颈与调优抓手；正文文件：`blog/22-RBD-性能调优指南-客户端-网络-OSD-与-BlueStore-的联动优化.md`
23. 《CephFS 架构总览：MDS、客户端与对象存储如何协作》：先建立 CephFS 的全局模型，解释元数据面、数据面、客户端缓存与 MDS 协调关系；正文文件：`blog/23-CephFS-架构总览-MDS-客户端与对象存储如何协作.md`
24. 《CephFS MDS 核心实现：请求状态机、子树划分与多 MDS 协调》：聚焦 `src/mds` 的核心控制流，讲清 MDS 如何处理元数据请求、维护子树边界并进行负载迁移；正文文件：`blog/24-CephFS-MDS-核心实现-请求状态机-子树划分与多-MDS-协调.md`
25. 《CephFS 元数据缓存与一致性：inode、dentry、cap 和 session 如何协作》：重点拆解 `MDCache`、`Locker`、cap/session 机制，解释客户端和 MDS 如何维持元数据一致性；正文文件：`blog/25-CephFS-元数据缓存与一致性-inode-dentry-cap-和-session-如何协作.md`
26. 《CephFS 日志与恢复机制：MDLog、Journal、故障切换与回放源码》：分析元数据日志、故障恢复、replay 和 failover 流程，帮助读者理解 CephFS 为什么能在 MDS 故障后继续工作；正文文件：`blog/26-CephFS-日志与恢复机制-MDLog-Journal-故障切换与回放源码.md`
27. 《CephFS 客户端通用实现：`src/client` 里的 inode、dentry 与 cap 机制》：聚焦用户态和内核态共享的客户端语义，讲清目录树、会话、缓存一致性与请求状态流转；正文文件：`blog/27-CephFS-客户端通用实现-src-client-里的-inode-dentry-与-cap-机制.md`
28. 《Ceph-FUSE 源码详解：用户态文件系统如何接入 CephFS》：结合 `src/ceph_fuse.cc` 和相关客户端代码，分析 FUSE 请求如何映射为 CephFS 操作；正文文件：`blog/28-Ceph-FUSE-源码详解-用户态文件系统如何接入-CephFS.md`
29. 《Linux 内核 CephFS 客户端实现：内核态挂载、页缓存与回写路径》：讲清内核 `kclient` 的挂载、VFS 对接、page cache、writeback 与一致性处理思路；正文文件：`blog/29-Linux-内核-CephFS-客户端实现-内核态挂载-页缓存与回写路径.md`
30. 《CephFS 性能测试实战：元数据压测、数据压测与关键指标解读》：围绕 `smallfile`、`fio`、`mdtest` 等方法，讲清 CephFS 的元数据性能、顺序吞吐、随机访问与客户端缓存命中该如何测；正文文件：`blog/30-CephFS-性能测试实战-元数据压测-数据压测与关键指标解读.md`
31. 《CephFS 性能调优指南：MDS、客户端缓存与后端 RADOS 的联动优化》：从 MDS 数量、caps、客户端缓存、目录分布、网络和 OSD/BlueStore 侧联动入手，系统梳理 CephFS 的常见瓶颈与调优抓手；正文文件：`blog/31-CephFS-性能调优指南-MDS-客户端缓存与后端-RADOS-的联动优化.md`
32. 《CephFS 性能问题排查案例：元数据热点、目录倾斜与 MDS 饱和如何定位》：通过典型慢操作案例，串起客户端缓存、MDS 指标、会话状态、热点目录和后端 OSD 观测，形成可复用的排障路径；正文文件：`blog/32-CephFS-性能问题排查案例-元数据热点-目录倾斜与-MDS-饱和如何定位.md`
33. 《CephFS 使用 NFS 导出：NFS-Ganesha、Ceph Orchestrator 与导出管理实现》：讲清 CephFS 如何通过 NFS-Ganesha 对外提供访问，分析导出配置、服务编排与管理接口的实现思路；正文文件：`blog/33-CephFS-使用-NFS-导出-NFS-Ganesha-Ceph-Orchestrator-与导出管理实现.md`
34. 《RGW 架构总览：S3/Swift 网关如何建立在 RADOS 之上》：先建立 RGW 的整体模型，解释前端协议层、认证鉴权、元数据组织与后端对象存储之间的关系；正文文件：`blog/34-RGW-架构总览-S3-Swift-网关如何建立在-RADOS-之上.md`
35. 《RGW 请求处理源码：HTTP 请求如何流经前端、认证和操作执行层》：结合 `rgw_op`、认证模块和前端适配代码，分析一次 PUT/GET 请求如何被解析、鉴权并执行；正文文件：`blog/35-RGW-请求处理源码-HTTP-请求如何流经前端-认证和操作执行层.md`
36. 《RGW 桶与对象元数据设计：索引、命名空间与多租户如何实现》：重点讲 bucket index、object metadata、omap 组织方式，以及租户、用户、配额等核心元数据模型；正文文件：`blog/36-RGW-桶与对象元数据设计-索引-命名空间与多租户如何实现.md`
37. 《RGW 高级能力详解：多站点同步、生命周期与事件通知源码》：介绍 multisite、sync、lc、notification 等关键能力的设计动机、模块边界和主要代码入口；正文文件：`blog/37-RGW-高级能力详解-多站点同步-生命周期与事件通知源码.md`
38. 《一致性与高可用：Ceph 如何在工程上平衡性能、可靠性与复杂度》：对比强一致、最终一致、主副本复制、故障域隔离，把分散知识点收束起来；正文文件：`blog/38-一致性与高可用-Ceph-如何在工程上平衡性能-可靠性与复杂度.md`
39. 《Ceph 性能分析与调试实战：从慢请求到源码定位的方法论》：讲 perf counter、日志、admin socket、常见 health 告警、热点调用链和源码断点思路；正文文件：`blog/39-Ceph-性能分析与调试实战-从慢请求到源码定位的方法论.md`
40. 《Ceph 常见源码阅读误区：哪些模块要先跳过，哪些入口最值得盯》：帮助读者避免一上来就陷入模板、宏、状态机细节；正文文件：`blog/40-Ceph-常见源码阅读误区-哪些模块要先跳过-哪些入口最值得盯.md`
41. 《从贡献者视角看 Ceph：如何阅读 issue、提交 patch、跑测试与验证修改》：面向想深入参与社区或团队二次开发的读者；正文文件：`blog/41-从贡献者视角看-Ceph-如何阅读-issue-提交-patch-跑测试与验证修改.md`

## 推荐分阶段发布

- 第一阶段 6 篇：`1-6`，目标是让读者建立整体地图，并知道裸机与 Kubernetes 场景下集群如何真正落地
- 第二阶段 7 篇：`7-13`，目标是掌握核心控制面、管理平面和主数据路径
- 第三阶段 10 篇：`14-23`，目标是真正理解“消息、存储、恢复、EC、RBD 与性能优化”几大难点
- 第四阶段 15 篇：`24-38`，目标是扩展到 CephFS、RGW 等上层服务并具备系统视角
- 收官 3 篇：`39-41`，目标是形成一致性分析、排障与持续贡献能力

## 每篇都建议带上的源码锚点

- 架构篇优先看 `src/mon`、`src/osd`、`src/mgr`、`src/mds`、`src/rgw`
- Rook 篇重点看 `src/pybind/mgr/rook`、Rook Operator/CRD 交互路径，以及与编排接口相关的管理模块
- Dashboard 篇重点看 `src/pybind/mgr/dashboard`、`src/pybind/mgr/dashboard/frontend`、`src/pybind/mgr/cephadm/templates/services/grafana`
- 通信篇重点看 `src/msg`、`src/common`
- 存储篇重点看 `src/os/bluestore`
- EC 篇重点看 `src/osd/ECBackend*`、`src/osd/ECCommon*`、`src/osd/ECUtil*`、`src/osd/PrimaryLogPG` 与相关编码插件路径
- 客户端篇重点看 `src/librados`、`src/client`
- RBD 篇重点看 `src/librbd`、`src/librbd/api`、`src/tools/rbd`、`src/tools/rbd_nbd`
- RBD 性能篇可结合 `rbd bench`、`fio`、`rbd-nbd`、`krbd` 路径与 BlueStore/网络观测指标一起分析
- CephFS 篇重点看 `src/mds`、`src/client`、`src/ceph_fuse.cc`
- CephFS 深入篇可重点跟 `MDCache`、`Locker`、`MDLog`、`MDSRank`、`Server` 等核心类
- CephFS 性能篇可结合 `mdtest`、`smallfile`、`fio`、客户端缓存命中、MDS 性能计数器与后端 OSD 指标联动分析
- CephFS 排障篇可重点观察慢请求、热点目录、MDS cache、session/cap 状态与 OSD 背景负载之间的关联
- CephFS NFS 导出篇可结合 `doc/cephfs/nfs.rst`、`src/pybind/mgr/cephadm/services/nfs.py`、`src/cephadm/cephadmlib/daemons/nfs.py`、`src/pybind/mgr/dashboard/controllers/nfs.py`
- RGW 篇重点看 `src/rgw`、`src/rgw/driver`、`src/radosgw`
- 部署篇可结合 `cephadm`、`doc`、编排相关模块、Dashboard 与监控组件入口理解整体落地方式
- 调试篇补充 `src/tools`、测试相关目录与配置解析代码

## 写作建议

- 每篇只回答一个核心问题，比如“PG 为什么存在”“BlueStore 为什么更快”，不要一篇塞完整模块
- 每篇至少画 1 张图：组件图、时序图、状态流转图三选一
- 每篇至少跟 1 条真实代码路径：入口函数、关键类、状态切换点、最终结果
- 每篇结尾给“下一篇阅读前置”，把系列串成学习路径而不是文章堆砌

## 可直接采用的系列标题

- 《从架构到源码吃透 Ceph》
- 《Ceph 源码拆解实战》
- 《Ceph 分布式存储原理与代码详解》
- 《面向工程师的 Ceph 架构与源码阅读路线》
