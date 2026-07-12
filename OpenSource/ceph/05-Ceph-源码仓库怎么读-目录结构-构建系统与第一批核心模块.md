# Ceph 源码仓库怎么读：目录结构、构建系统与第一批核心模块

## 这篇文章要解决什么问题

到前面几篇为止，我们已经知道了：

- Ceph 的本体是统一的分布式对象存储底座
- `MON`、`MGR`、`OSD`、`MDS`、`RGW` 的角色边界
- 在主机世界里可以用 `cephadm` 编排 Ceph
- 在 Kubernetes 世界里可以用 Rook 编排 Ceph

接下来最自然的一步，就是开始真正读仓库。

但对大多数第一次打开 Ceph 源码的人来说，第一感觉往往不是“豁然开朗”，而是“信息量爆炸”：

- 顶层目录很多，`src`、`doc`、`qa`、`admin`、`container`、`man` 到底谁更重要？
- `src` 里又有 `mon`、`osd`、`mgr`、`mds`、`msg`、`common`、`client`、`librados`、`librbd`、`crimson`，到底先看哪个？
- 从构建系统上看，Ceph 是怎么把这些模块组织起来的？
- 为什么有些目录像“业务模块”，有些又像“基础设施层”？
- 如果我只想先找到一条主线，应该从哪些入口文件开始？

这一篇的目标，就是帮你建立一张**真正能拿来读源码的地图**。

它不是单纯罗列目录，而是要回答三个更重要的问题：

1. 仓库按什么层次组织？
2. 构建系统是如何把这些层次串起来的？
3. 第一批最值得认识的模块，分别代表 Ceph 的哪一层？

如果你读完这篇，至少应该能做到：

- 打开仓库时不会再被目录名淹没
- 知道哪些目录是“主进程实现”，哪些是“基础设施”，哪些是“客户端库”
- 知道下一篇如果讲 `MON`、`OSD`、`librados`、消息层时，你该从哪里进

## 先给出总原则：不要把 Ceph 仓库当成“一个大程序”，而要把它看成“一个系统的代码集合”

这是读 Ceph 仓库的第一条原则。

很多人第一次读源码，潜意识会寻找：

- 一个总 `main()`
- 一条从头到尾的主调用链
- 一个能解释整个项目的单一入口

这在小项目里很常见，但在 Ceph 里并不成立。

因为 Ceph 不是一个单进程程序，而是一整个分布式系统的代码集合。仓库里同时包含：

- 多个守护进程实现
- 多个客户端库
- 公共基础设施层
- 文档
- 测试
- 运维和构建脚本

所以你必须先放弃“我要先找到 Ceph 的唯一入口”这个想法，改成：

**我要先找到 Ceph 这个系统的几个主要入口。**

一旦思路变成这样，仓库结构就会立刻清晰很多。

## 第一张地图：顶层目录到底怎么分

从仓库顶层看，最值得先建立认知的不是每个目录的所有细节，而是“它属于哪一类”。

先用一个阅读者视角把顶层目录粗分成五类：

### 1. 主代码

- `src/`

这是你后面绝大部分源码阅读时间都会花进去的地方。

### 2. 文档

- `doc/`
- `man/`

其中 `doc/` 是系统性文档主目录，`man/` 是手册页相关内容。

### 3. 测试

- `src/test/`
- `qa/`

这两个目录都和测试有关，但性质完全不同，后面会单独讲。

### 4. 构建与开发辅助

- `CMakeLists.txt`
- `do_cmake.sh`
- `admin/`

它们共同决定“怎么把仓库变成可运行、可调试、可生成文档的东西”。

### 5. 打包、部署、系统集成相关

- `debian/`
- `container/`
- `etc/`
- `selinux/`

这些目录也很重要，但通常不是第一次源码入门的优先入口。

所以对初学者来说，最应该先记住的顶层顺序是：

```text
先看 src
再看 doc
再理解构建入口
最后把测试和系统集成目录拼回来
```

## `src/` 才是主战场，但它内部也不是平铺的

理解 `src/` 的最好方法，不是去硬记所有子目录，而是先把它拆成四层：

- 守护进程实现层
- 公共基础设施层
- 客户端与库层
- 工具与测试辅助层

这四层大致对应如下：

### 守护进程实现层

- `src/mon`
- `src/osd`
- `src/mgr`
- `src/mds`
- `src/rgw`

它们对应 Ceph 最核心的几类服务进程。

### 公共基础设施层

- `src/common`
- `src/msg`
- `src/auth`
- `src/global`
- `src/osdc`

这些目录并不直接代表某一个“产品功能”，但几乎所有核心模块都会依赖它们。

### 客户端与库层

- `src/librados`
- `src/librbd`
- `src/client`
- `src/include`

这一层更多对应“客户端 API、库接口、上层抽象”。

### 新实现/特殊架构层

- `src/crimson`

这是 Ceph 在新架构方向上的独立实现区域，阅读策略和传统 `src/osd` 不完全一样。

一旦你先建立这个分层，再回头看目录树，就不会觉得它只是十几个名字并排堆在一起。

## 第二张地图：从构建系统倒着看仓库组织

如果你想真正理解一个大项目的目录结构，最有效的方法之一，就是看构建系统怎么组织它。

Ceph 的根构建入口非常明确：

- 根入口是 [CMakeLists.txt](file:///e:/projects/ceph/CMakeLists.txt)
- 开发者常用构建脚本是 [do_cmake.sh](file:///e:/projects/ceph/do_cmake.sh)
- 根 README 也明确给出了 `./do_cmake.sh -> cd build -> ninja` 这条开发构建路径：[README.md:L79-L97](file:///e:/projects/ceph/README.md#L79-L97)

### 为什么先看构建入口很重要

因为构建系统会直接告诉你：

- 哪些子目录是顶层一级模块
- 哪些是条件编译模块
- 哪些最终会生成可执行程序
- 哪些只是库

这比只看文件夹名字更可靠。

## 根 `CMakeLists.txt`：Ceph 仓库的总装配线

在根 [CMakeLists.txt](file:///e:/projects/ceph/CMakeLists.txt#L1-L5) 里，项目被定义为一个 CMake 工程：

```cmake
project(ceph
  VERSION 20.2.1
  LANGUAGES CXX C ASM)
```

这里只需要抓住一个重点：

- Ceph 的总装配入口在仓库根
- 但真正的大部分模块装配发生在 `src/CMakeLists.txt`

也就是说，根 CMake 更像“总调度器”，而 `src/CMakeLists.txt` 更像“主代码装配中心”。

## `do_cmake.sh`：开发者最常用的构建捷径

如果说根 `CMakeLists.txt` 是正式入口，那 [do_cmake.sh](file:///e:/projects/ceph/do_cmake.sh#L16-L18) 更像开发者日常使用的快捷入口。

从脚本内容里可以直接看出几件事：

- 默认创建 `build/` 目录
- 默认使用 `-GNinja`
- 会根据系统选择 Python 版本和编译器
- 运行 CMake 后还会生成一份最小 `ceph.conf`

这说明它不是简单的：

```bash
cmake ..
```

而是把 Ceph 开发构建里一批高频默认选项预先封装好了。

对于初学者来说，这个脚本传递出的关键信息是：

**Ceph 的开发构建体验，是围绕 `CMake + Ninja + build 目录` 组织起来的。**

## `src/CMakeLists.txt`：真正理解 Ceph 模块划分的关键文件

如果你只打算为源码入门读一个构建文件，那最值得读的就是 [src/CMakeLists.txt](file:///e:/projects/ceph/src/CMakeLists.txt)。

原因很简单：它几乎像一张“官方模块地图”。

你能直接看到这些子目录被装进来：

- `common`
- `msg`
- `mgr`
- `mon`
- `osd`
- `mds`
- `tools`
- `test`
- `crimson`
- `librados`

更重要的是，它不仅 add 子目录，还明确构建出主守护进程可执行文件：

- `ceph-mon`
- `ceph-osd`
- `ceph-mds`

这意味着你可以从构建层直接得出一个非常有用的判断：

**如果某个目录既有独立子目录、又最终对应独立可执行程序，那么它大概率就是 Ceph 的核心守护进程实现区。**

这也是为什么我建议很多人第一次读仓库时，先看 `src/CMakeLists.txt`，再看各模块源码。

## 第三张地图：先认识“主守护进程目录”

对于第一次读 Ceph 源码的人，最容易建立直觉的入口，其实就是几类主进程目录。

它们是：

- `src/mon`
- `src/osd`
- `src/mgr`
- `src/mds`
- `src/rgw`

之所以先看它们，不是因为它们最容易，而是因为它们最能和你前几篇建立起来的系统角色认知对上号。

## `src/mon`：集群控制面的核心之一

如果你前面已经理解 `MON` 是集群地图、仲裁和关键状态维护的核心角色，那么 `src/mon` 就是你第一次把这个认知落到代码上的地方。

从扫描结果看，最值得先记住的入口是：

- [ceph_mon.cc](file:///e:/projects/ceph/src/ceph_mon.cc)
- [Monitor.h](file:///e:/projects/ceph/src/mon/Monitor.h)
- [Monitor.cc](file:///e:/projects/ceph/src/mon/Monitor.cc)

### 为什么先看 `ceph_mon.cc`

因为这是守护进程级入口。它会告诉你：

- monitor 进程是怎么启动的
- 初始化时依赖哪些基础设施
- 最终如何把控制权交给 `Monitor` 主类

### 为什么再看 `Monitor.h/.cc`

因为这两个文件会把 monitor 的职责展开成真正的代码组织。你会看到它不是一个“单功能类”，而是整合了：

- `Elector`
- `Paxos`
- `OSDMonitor`
- `MDSMonitor`
- `MgrMonitor`

这和我们之前的架构认知完全对应：

**`MON` 不是单一表结构维护者，而是多个控制面服务的集合宿主。**

## `src/osd`：Ceph 最厚重的数据面实现区

如果说 `src/mon` 代表控制面，那 `src/osd` 就是你第一次真正面对 Ceph 数据面复杂度的地方。

最值得先记住的入口是：

- [ceph_osd.cc](file:///e:/projects/ceph/src/ceph_osd.cc)
- [OSD.h](file:///e:/projects/ceph/src/osd/OSD.h)
- [OSD.cc](file:///e:/projects/ceph/src/osd/OSD.cc)

以及后续一定会不断遇到的几个名字：

- `PG`
- `PrimaryLogPG`
- `PeeringState`
- `ReplicatedBackend`
- `ECBackend`

### 为什么 `src/osd` 往往最让人望而生畏

因为它同时承担了太多真实问题：

- 对象读写
- 主副本语义
- PG 状态机
- peering
- recovery / backfill
- scrub
- 持久化调用

所以对初学者来说，第一次读 `src/osd` 最好的策略不是“从头看到尾”，而是：

1. 先看 `ceph_osd.cc`
2. 再看 `OSD` 主类
3. 然后围绕一个主题逐步进 `PG`、peering、backend

不要一开始就试图吃下整个 `src/osd`。

## `src/mgr`：管理平面和模块化生态的入口

很多人第一次读 Ceph 仓库时，会低估 `src/mgr`，因为它不像 `src/osd` 那样“看起来就很核心”，也不像 `src/mon` 那样容易和集群仲裁直接挂钩。

但它对理解现代 Ceph 非常重要。

最典型的入口包括：

- [ceph_mgr.cc](file:///e:/projects/ceph/src/ceph_mgr.cc)
- [Mgr.h](file:///e:/projects/ceph/src/mgr/Mgr.h)
- [MgrStandby.cc](file:///e:/projects/ceph/src/mgr/MgrStandby.cc)

### 为什么这里的关键字是“模块宿主”

因为 `MGR` 不只是一个管理进程，它还是一整套管理扩展能力的宿主环境。

从源码结构里你会看到：

- `Mgr`
- `ClusterState`
- `DaemonServer`
- `PyModuleRegistry`

这说明它本质上是在做两件事：

- 汇聚集群运行状态
- 承载 Python manager modules

这也是为什么后面你想理解 Dashboard、Prometheus、orchestrator 这些能力时，必须回到 `src/mgr` 和 `src/pybind/mgr`。

## `src/mds`：CephFS 元数据面的核心世界

对没读过 CephFS 的人来说，`src/mds` 往往是第二个“很吓人”的目录，因为里面充满了文件系统语义、缓存对象、日志、锁、迁移和状态机。

最值得先记住的入口包括：

- [ceph_mds.cc](file:///e:/projects/ceph/src/ceph_mds.cc)
- [MDSDaemon.h](file:///e:/projects/ceph/src/mds/MDSDaemon.h)
- [MDSDaemon.cc](file:///e:/projects/ceph/src/mds/MDSDaemon.cc)

以及后续深入 CephFS 时一定会反复碰到的几个核心类：

- `MDSRank`
- `MDCache`
- `Server`
- `Locker`
- `MDLog`

### 读 `src/mds` 的正确姿势

不要把它当成“另一个 OSD”，也不要把它当成“Ceph 全局元数据目录”。

更好的理解是：

- 这是 CephFS 元数据语义成立的地方
- 这里的复杂度来自文件系统，而不是对象存储本身

一旦接受这点，你对 `src/mds` 的耐心会高很多。

## `src/rgw`：对象网关世界

`src/rgw` 也是大目录，但对于初学者来说，它通常不是第五篇就必须全面展开的重点。

你先只需要知道：

- 它代表对象网关层
- 它主要服务于 S3/Swift 风格协议
- 它不是 Ceph 底层对象存储本体，而是对外协议出口

所以入门阶段对它的优先级通常可以放在：

- `mon` / `osd` / `mgr`
- 之后再 `mds` / `rgw`

这也是一种降低认知负担的好方法。

## 第四张地图：再认识“基础设施层”

很多初学者在读 Ceph 时容易犯一个错误：

- 只盯守护进程目录
- 看到 `common`、`msg` 这种目录就跳过

这样做短期内会觉得轻松，但很快你就会发现：

- 几乎所有守护进程入口都绕不开这些目录

所以你必须尽早认识两块真正的“公共地基”：

- `src/common`
- `src/msg`

## `src/common`：Ceph 的公共运行时地基

`src/common` 不是某个单独服务的目录，它更像 Ceph 多个进程共享的运行时基础设施层。

最值得先记住的入口是：

- [common_init.h](file:///e:/projects/ceph/src/common/common_init.h)
- [common_init.cc](file:///e:/projects/ceph/src/common/common_init.cc)

以及几个经常会碰到的概念：

- `CephContext`
- 配置
- 日志
- perf counter
- admin socket

### 为什么这里值得先看 `common_init`

因为几乎所有守护进程入口最终都会走到类似的初始化套路：

- 创建上下文
- 装载配置
- 初始化日志和公共服务线程

这使得 `common_init` 非常适合作为“Ceph 守护进程共通启动路径”的第一站。

如果你把 Ceph 看成城市，`src/common` 就像：

- 道路
- 水电
- 管网
- 标准接口

它平时不一定最显眼，但所有东西都建在它上面。

## `src/msg`：节点间通信抽象层

Ceph 是分布式系统，所以消息层不是“附属模块”，而是核心地基之一。

最推荐的第一入口是：

- [Messenger.h](file:///e:/projects/ceph/src/msg/Messenger.h)

再往后可以看：

- [AsyncMessenger.cc](file:///e:/projects/ceph/src/msg/async/AsyncMessenger.cc)
- `ProtocolV2.cc`

### 为什么先读 `Messenger.h`

因为它先给你一个抽象层面的答案：

- Ceph 如何抽象 messenger、connection、dispatcher
- 消息收发、绑定、启动、关闭的接口大致长什么样

如果你一上来就扎进某个协议实现文件，很容易在细节里迷失。先看抽象，再看具体实现，会轻松很多。

## 第五张地图：客户端与库层

如果只看守护进程目录，你会形成一种错觉：

- Ceph 源码主要就是服务端源码

这当然不对。Ceph 的很多关键体验和上层抽象，其实体现在客户端和库层。

这一层最重要的三个目录是：

- `src/librados`
- `src/librbd`
- `src/client`

## `src/librados`：最贴近 RADOS 的客户端库

如果你想理解：

- 用户态程序如何直接访问 Ceph 对象底座
- `RadosClient`、`IoCtx` 这些概念到底从哪里来

那 `src/librados` 就是最好的入口。

最值得先记住的文件包括：

- [librados_c.cc](file:///e:/projects/ceph/src/librados/librados_c.cc)
- [RadosClient.h](file:///e:/projects/ceph/src/librados/RadosClient.h)
- [RadosClient.cc](file:///e:/projects/ceph/src/librados/RadosClient.cc)

### 为什么这里要同时看 C API 和核心类

因为很多上层调用最终会从 API 入口落到内部客户端主类。如果你只看 API，不看 `RadosClient`，会停留在接口层；只看 `RadosClient`，又容易不知道外部入口从哪来。

这两个视角结合起来，理解才完整。

## `src/librbd`：块存储抽象在代码中的落点

如果你想把“RBD 是把对象拼成块设备语义”这个概念落实到代码里，最值得看的就是：

- [librbd.cc](file:///e:/projects/ceph/src/librbd/librbd.cc)
- [ImageCtx.h](file:///e:/projects/ceph/src/librbd/ImageCtx.h)

### 为什么 `ImageCtx` 很关键

因为在 `librbd` 世界里，它像很多操作共享的中心状态对象。很多读写、快照、克隆、锁和特性相关能力，最后都会围绕它组织。

所以对于 `librbd`，你不必一上来就钻所有子目录，而是先建立一个认知：

- `librbd.cc` 像 API 入口层
- `ImageCtx` 像内部状态中心

## `src/client`：CephFS 客户端一侧的世界

如果你只读 `src/mds`，你会看到 CephFS 的服务端元数据控制面；但 CephFS 的另一半，其实在 `src/client`。

最值得先记住的文件包括：

- [Client.h](file:///e:/projects/ceph/src/client/Client.h)
- [Client.cc](file:///e:/projects/ceph/src/client/Client.cc)

这里会逐步把你带进：

- 挂载
- 路径遍历
- inode / dentry
- cap
- 与 MDS/OSD 的交互

所以对于 CephFS 这条线来说，`src/mds` 和 `src/client` 最好始终成对理解。

## `src/crimson`：不要太早把它和传统 OSD 栈混在一起

很多人第一次看 Ceph 目录时，会问：

- `src/crimson` 是不是“新版 OSD”

这个理解不能说错，但会让你忽略它阅读上的特殊性。

从扫描结果看，它的典型入口是：

- [main.cc](file:///e:/projects/ceph/src/crimson/osd/main.cc)
- [osd.h](file:///e:/projects/ceph/src/crimson/osd/osd.h)

这里采用的是另一套更现代的异步架构思路，和传统 `src/osd` 的阅读体验不一样。

所以对大多数初学者来说，更合理的顺序通常是：

1. 先读传统 `mon` / `osd` / `mgr` / `mds`
2. 等对 Ceph 基础架构足够熟悉后，再回来看 `crimson`

这不是因为它不重要，而是因为它更适合“对照着读”，而不是“抢先读”。

## 文档目录 `doc/`：不是“补充材料”，而是阅读仓库的重要导航层

很多工程师读源码时会下意识轻视文档目录，觉得“文档不如代码真实”。这在某些项目里可能成立，但在 Ceph 里通常会让你走很多弯路。

`doc/` 之所以重要，有两个原因：

- Ceph 本身是复杂分布式系统，很多模块先有概念和运维模型，再有代码
- 仓库里的文档和源码组织往往是相互映射的

比如你会看到：

- `doc/cephadm/*`
- `doc/mgr/*`
- `doc/cephfs/*`
- `doc/rados/*`

这其实和源码里的：

- `src/mgr`
- `src/mds`
- `src/osd`
- `src/client`

形成了天然呼应。

所以如果你以后要读某个大模块，最稳妥的路线通常不是直接冲源码，而是：

```text
先读对应文档子目录
再读构建入口
再读主进程/主类入口
最后读细分实现
```

## `src/test` 和 `qa/`：为什么 Ceph 有两套测试世界

这是初学者非常容易混淆的一点。

### `src/test`

更偏源码树内测试，和模块实现靠得更近。

从 `src/test/CMakeLists.txt` 可以直接看出，它会纳入很多模块相关测试：

- `common`
- `librados`
- `librbd`
- `mds`
- `rgw`
- `system`

这意味着它更像“源码邻近测试世界”。

### `qa`

则是另一套更偏集群级、系统级、场景级的 QA 世界。

仓库里的 `qa/README` 明确提到：

- `clusters/`
- `suites/`
- YAML 片段
- 与 teuthology 的关系

也就是说，`qa` 不只是“更多单元测试”，而更像：

**围绕真实集群行为组织的测试编排体系。**

这个差别很重要，因为它直接反映了 Ceph 的项目性质：

- 它不是只靠单元测试就能覆盖的系统
- 它需要大量真实分布式场景验证

## 如果你只能记住第一批入口文件，应该记什么

对源码入门来说，最有价值的往往不是“看过很多目录”，而是“记住几组可靠入口”。

我建议先记住下面这组：

### 守护进程级入口

- [ceph_mon.cc](file:///e:/projects/ceph/src/ceph_mon.cc)
- [ceph_osd.cc](file:///e:/projects/ceph/src/ceph_osd.cc)
- [ceph_mgr.cc](file:///e:/projects/ceph/src/ceph_mgr.cc)
- [ceph_mds.cc](file:///e:/projects/ceph/src/ceph_mds.cc)

### 主类级入口

- [Monitor.h](file:///e:/projects/ceph/src/mon/Monitor.h)
- [OSD.h](file:///e:/projects/ceph/src/osd/OSD.h)
- [Mgr.h](file:///e:/projects/ceph/src/mgr/Mgr.h)
- [MDSDaemon.h](file:///e:/projects/ceph/src/mds/MDSDaemon.h)

### 公共基础设施入口

- [common_init.h](file:///e:/projects/ceph/src/common/common_init.h)
- [Messenger.h](file:///e:/projects/ceph/src/msg/Messenger.h)

### 客户端与库入口

- [RadosClient.h](file:///e:/projects/ceph/src/librados/RadosClient.h)
- [librbd.cc](file:///e:/projects/ceph/src/librbd/librbd.cc)
- [Client.h](file:///e:/projects/ceph/src/client/Client.h)

如果你能先把这些入口在脑子里挂起来，后面继续读任何模块都会轻松很多。

## 一个实用阅读顺序：第一次读 Ceph，不要按字母顺序逛目录

这是我最想给初学者的建议之一。

第一次读 Ceph，更好的顺序通常是：

### 第一步：先看构建与仓库装配

- 根 `README`
- `do_cmake.sh`
- 根 `CMakeLists.txt`
- `src/CMakeLists.txt`

目标是回答：

- 这个仓库怎么被装起来？

### 第二步：再看守护进程入口

- `ceph_mon.cc`
- `ceph_osd.cc`
- `ceph_mgr.cc`
- `ceph_mds.cc`

目标是回答：

- 这些核心进程分别从哪里起？

### 第三步：再看公共基础设施层

- `common_init`
- `Messenger`

目标是回答：

- 守护进程共享什么运行时地基？

### 第四步：最后按兴趣进入某一条专题线

比如：

- 想看控制面就进 `mon`
- 想看数据面就进 `osd`
- 想看客户端 IO 就进 `librados`
- 想看 CephFS 就进 `mds + client`
- 想看块存储就进 `librbd`

这样读，效率会比“今天随机打开一个目录”高得多。

## 初学者最容易犯的 8 个源码阅读误区

### 1. 试图找到 Ceph 的唯一总入口

Ceph 是系统，不是单程序。入口天然是多点的。

### 2. 一上来就扎进 `src/osd`

`src/osd` 很重要，但如果没有前置地图，它会把你淹没。

### 3. 只看业务目录，不看 `common` 和 `msg`

这样很快会失去对共通初始化和消息抽象的理解。

### 4. 只看源码，不看仓库里的文档和构建文件

这样你会更难分辨模块边界。

### 5. 把 `qa` 当成普通单元测试目录

它更像系统级 QA 场景编排世界。

### 6. 把 `crimson` 当作传统 `osd` 的直接平替入口

它值得读，但不适合作为第一站。

### 7. 看到 `librados`、`librbd` 就觉得“这是边角料”

这些库层恰恰是理解上层语义如何落到底座的重要桥。

### 8. 按目录名随缘乱读

Ceph 需要“带着地图读”，而不是“逛街式阅读”。

## 这一篇最应该留下的 5 个直觉

### 直觉一：顶层先分 `src`、`doc`、构建入口、测试入口

不要把所有顶层目录视为同一优先级。

### 直觉二：`src` 里先分守护进程层、基础设施层、客户端库层

这样才能真正建立结构感。

### 直觉三：`src/CMakeLists.txt` 是官方模块地图之一

它能帮助你快速识别哪些目录是核心装配点。

### 直觉四：第一次源码阅读，先从进程入口和主类入口开始

比从某个随机工具类或细节实现开始更稳。

### 直觉五：Ceph 仓库不是一棵普通代码树，而是一整个分布式系统的实现集合

你越早接受这一点，越不容易在阅读时迷路。

## 下一篇看什么

现在你已经有仓库地图了，下一步最自然的事情，就是沿着一条真正的数据主线把这些目录串起来：

**一次写请求，从客户端进入 Ceph 之后，到底是怎么一路跑到数据落盘的？**

所以下一篇建议接：

**《从一次写请求看 Ceph：客户端到落盘的主路径总览》**

那一篇会把这一篇里分散的目录和模块，真正用一条“写路径”串起来，让“仓库地图”变成“运行路径地图”。
