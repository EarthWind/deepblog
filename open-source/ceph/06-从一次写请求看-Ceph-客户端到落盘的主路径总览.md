# 从一次写请求看 Ceph：客户端到落盘的主路径总览

## 这篇文章要解决什么问题

前面几篇我们已经把几张静态地图搭起来了：

- Ceph 是什么
- `MON`、`MGR`、`OSD`、`MDS`、`RGW` 各自负责什么
- 集群如何部署
- 仓库结构怎么读

但如果你只是记住这些模块名字，心里仍然很容易有一种“知道组件，却不知道它们怎么一起跑”的断裂感。

真正让 Ceph 从“组件列表”变成“运行中的系统”的，通常是一条具体的数据路径。

而对初学者来说，最有价值的第一条路径，往往就是：

**一次写请求，如何从客户端一路进入集群，经过主副本、PG、事务和后端存储，最后真正落盘。**

为什么这一篇非常关键？

因为后面很多难点，其实都挂在这条主线上：

- `Objecter` 到底做什么
- `PG` 为什么存在
- 主副本为什么要先接管写入
- 复制协议和本地持久化是什么关系
- `BlueStore` 在整条链里到底处在哪一段

这一篇不追求把每个细节都讲透，而是先完成一件更重要的事：

**用一条可连续追踪的主线，把客户端、消息层、`OSD`、`PG`、复制后端、事务和 BlueStore 串起来。**

也就是说，这是一篇“跑通全链路”的文章，不是“把每个模块全部展开”的文章。

## 先建立一个重要前提：本文讲的是“主路径”，不是“所有分支”

Ceph 的真实写路径当然有很多分支：

- 同步和异步写
- 副本池和 EC 池
- 普通对象写、写全对象、截断、omap 修改
- cache tier、snap、重试、重映射、恢复状态

如果一上来把所有分支都摊开，初学者几乎一定会被淹没。

所以本文主动做两个简化：

### 简化一：以副本池为主

也就是先不讲 EC 的读改写复杂度，而是沿着更典型、更容易建立直觉的 replicated pool 写路径来理解。

### 简化二：以对象写为主

也就是先从 `librados` / `Objecter` 的对象写路径看系统，不先从 `RBD` 或 CephFS 这种更高层语义切入。

这两个简化的好处是：

- 你能最快看到 Ceph 的底层本体
- 后面再接 `RBD` 或 CephFS 时，会更清楚它们只是“在这条底座路径之上又加了一层语义”

## 先给出全景图：一次写请求的大致路线

先建立这张总览图：

```text
应用
  |
  v
librados / IoCtx
  |
  v
Objecter
  |
  v
Messenger / MOSDOp
  |
  v
目标主 OSD
  |
  v
OSD -> PG -> PrimaryLogPG
  |
  v
ReplicatedBackend
  |
  +--> 发副本子操作到从 OSD
  |
  +--> 把本地事务提交到底层 ObjectStore
            |
            v
        BlueStore
            |
            v
        KV / WAL / 主设备
```

这张图里最重要的不是箭头本身，而是这几个分层：

- 客户端侧：`librados`、`Objecter`
- 网络消息侧：`Messenger`、`MOSDOp`
- 服务端接收侧：`OSD`
- PG 处理侧：`PrimaryLogPG`
- 复制与提交侧：`ReplicatedBackend`
- 本地持久化侧：`BlueStore`

你可以先记住一句话：

**客户端负责“组织请求并找到目标”，主 OSD 负责“接住请求并主导执行”，PG/后端负责“把逻辑写变成复制与事务”，BlueStore 负责“把事务真的落到本地存储”。**

## 先讲写路径的起点：为什么从 `librados` 开始看

前面我们讲过，Ceph 的本体是 `RADOS`，最贴近这层底座的客户端库就是 `librados`。

所以如果你想看一次“最原始”的对象写请求，最自然的入口不是：

- `RBD`
- CephFS
- `RGW`

而是 `librados`。

这并不是说别的接口不重要，而是说：

- `RBD` 会把块语义翻译成对象操作
- CephFS 会把文件系统语义翻译成对象操作
- `RGW` 会把 S3/Swift 请求翻译成对象操作

所以先看 `librados`，等于先把 Ceph 最底层那条写路径立住。

## 第 1 站：`IoCtxImpl` 把“我要写对象”翻译成 Objecter 请求

从代码上看，一次对象写请求最值得先盯住的入口之一，是：

- `src/librados/IoCtxImpl.cc`

扫描结果里，典型入口包括：

- `IoCtxImpl::operate()`
- `IoCtxImpl::aio_operate()`

这两个函数分别对应同步和异步写路径，但就主线认知而言，它们有一个共同点：

- 最终都会把对象操作交给 `Objecter`

也就是说，这一层的核心任务不是：

- 自己去算 PG
- 自己去连 OSD
- 自己去发网络消息

而是：

- 把用户态对象操作整理成 Ceph 内部能继续处理的请求形式

可以把这一层理解成：

**`IoCtxImpl` 是“用户请求进入 Ceph 客户端内部世界”的门口。**

## 第 2 站：`Objecter` 是客户端侧真正的“请求组织者”

很多第一次看 Ceph 的人，会以为客户端写路径最重要的类是 `IoCtx`。实际上，真正把请求送进集群主路径的关键角色，通常是 `Objecter`。

对应源码主要在：

- `src/osdc/Objecter.h`
- `src/osdc/Objecter.cc`

在扫描结果里，写请求主线大致会经过：

- `prepare_mutate_op()`
- `op_submit()`
- `_op_submit_with_budget()`
- `_op_submit()`
- `_prepare_osd_op()`
- `_send_op()`

### `Objecter` 到底负责什么

如果只用一句话概括：

**`Objecter` 负责把客户端对象操作，变成一个可被定位、封包、重试、发送的 OSD 请求。**

它承担的事情包括：

- 请求对象的内部表示
- 基于 map 做目标定位
- 找到对应 OSD session / connection
- 封装成 OSD 请求消息
- 处理重发、会话和一些客户端侧状态管理

这一步非常关键，因为它正好体现了 Ceph 的一个核心设计目标：

- 客户端不是盲目把请求交给某个中心节点
- 客户端本身就具备一定的数据路径组织能力

这也是为什么理解 `Objecter`，对后面理解 Ceph 的“去中心化数据路径”很重要。

## 第 3 站：`MOSDOp` 把写请求变成线上消息

当 `Objecter` 已经决定“要给哪个 OSD 发什么操作”之后，下一步就是把请求封进真正要在线上发送的消息体。

这里最重要的类型之一就是：

- `src/messages/MOSDOp.h`

你可以把 `MOSDOp` 理解成：

- 客户端发给 OSD 的标准对象操作消息

它承载的内容，本质上就是：

- 这次要操作哪个对象
- 具体做哪些 OSD op
- 携带什么数据
- 带什么 flags、snap、mtime 等上下文

所以在主线认知里，`MOSDOp` 非常像一个分界点：

- 在它之前，请求还更多是客户端内部对象
- 在它之后，请求已经变成“网络上传输的 Ceph OSD 消息”

## 第 4 站：Messenger 把请求送到目标 OSD

写请求一旦封成 `MOSDOp`，接下来就是消息层的工作。

这里不展开消息层所有细节，只抓主线最重要的一步：

- `AsyncConnection::send_message()`

也就是：

- `Objecter` 已经决定目标
- 消息层负责真正把这个请求通过 connection 发出去

这一步表面上看起来普通，但它其实很关键，因为它对应着前面几篇反复强调的一件事：

- Ceph 的普通数据 IO 主路径，不是经由 `MON` 做中心转发
- 客户端会尽量直接把请求发到目标 OSD

所以如果你从这条主路径去理解，就会更深刻地明白：

**`MON` 提供地图和规则，`Objecter + Messenger` 则让客户端依据这些规则直接接近数据面。**

## 第 5 站：目标 OSD 收到请求，先进入 `OSD::ms_fast_dispatch()`

请求进入服务端后的第一站，通常就是 `OSD` 的消息分发入口。

主入口之一是：

- `src/osd/OSD.cc`
- `OSD::ms_fast_dispatch()`

从扫描结果看，这里在遇到 `CEPH_MSG_OSD_OP` 时，会：

- 创建 `OpRequest`
- 识别目标 `spg_t`
- 把请求送进 `enqueue_op()`

### 这一步的意义是什么

它的本质不是“马上执行对象写”，而是：

- OSD 收到请求
- 包装成内部请求对象
- 交给后续调度与 PG 路径处理

所以这是一个非常标准的服务端入口层动作：

- 收包
- 识别类型
- 包装内部请求
- 进入内部执行队列

这里先建立一个直觉就够了：

**`OSD::ms_fast_dispatch()` 是“请求进入 OSD 世界”的门口，但不是“真正执行业务写逻辑”的地方。**

## 第 6 站：为什么写请求不会直接在 OSD 顶层处理，而要先进入 PG

这一步是理解 Ceph 的真正门槛之一。

很多人第一次看 Ceph，会下意识觉得：

- 客户端发给 OSD
- OSD 就直接把对象写盘

但 Ceph 不是这样工作的。

在服务端，写请求通常会先被送到对应的 PG 处理上下文里。也就是说：

- 不是“某个 OSD 线程直接拿对象名就写”
- 而是“某个对象对应的 PG 承载了这次操作的一致性和调度语义”

所以你才会看到请求从：

- `OSD::enqueue_op()`

继续进入：

- `PrimaryLogPG::do_request()`

这正是 Ceph 的一个核心设计思想：

**Ceph 不是直接把对象映射到裸 OSD，而是通过 PG 把定位、一致性和扩展性组织起来。**

这也是为什么后面整个第 12 篇会专门讲 PG。

## 第 7 站：`PrimaryLogPG` 才是主副本写逻辑真正展开的地方

写请求到达 PG 处理路径后，真正进入核心逻辑的地方通常是：

- `PrimaryLogPG::do_request()`
- `PrimaryLogPG::do_op()`

对应源码在：

- `src/osd/PrimaryLogPG.cc`

### 为什么类名叫 `PrimaryLogPG`

因为在典型 replicated pool 写路径里，你看到的是：

- 某个 PG 在当前 acting set 中会有 primary
- 这次客户端写请求先由 primary OSD 接住
- primary 负责组织这次写的主流程

所以 `PrimaryLogPG` 不是“普通对象操作工具类”，而是：

- 主副本视角下的 PG 处理核心

### 在这里会发生什么

对主线理解来说，可以先抓住几件事：

- 请求被 decode 和检查
- PG / 对象状态被验证
- 权限、状态、上下文被整理
- 具体 OSD op 会被执行
- 随后进入事务与复制路径

也就是说，到这里写请求已经不再只是“网络消息”，而是开始真正变成：

- 一个需要更新对象状态
- 需要写日志/事务
- 需要复制到副本
- 需要最终推进提交语义

的集群级数据操作。

## 第 8 站：`OpContext` 和事务准备，写逻辑开始“落地化”

在 `PrimaryLogPG` 里，写操作不会立刻直接往底层存储塞字节。中间通常会先形成一套更接近事务语义的执行上下文。

扫描结果里，一个关键点是：

- `PrimaryLogPG::execute_ctx()`

你可以把这一层理解成：

- 把这次对象写，整理成 PG 能提交的变更集合
- 同时准备后续复制与本地提交所需的上下文

这一步之所以重要，是因为它体现了 Ceph 的写路径不是：

```text
收到请求 -> 直接 write(fd)
```

而更像：

```text
收到请求 -> 检查状态 -> 形成操作上下文 -> 生成事务 -> 复制/提交 -> 落盘
```

这也正是 Ceph 复杂度的一部分来源：

- 它解决的不是单机文件写，而是分布式对象写

## 第 9 站：primary 通过 `ReplicatedBackend` 组织复制与本地事务

当 `PrimaryLogPG` 已经准备好写事务之后，下一步会把事情交给复制后端。

在 replicated pool 里，最值得关注的后端就是：

- `src/osd/ReplicatedBackend.h`
- `src/osd/ReplicatedBackend.cc`

对应主入口之一是：

- `ReplicatedBackend::submit_transaction()`

### 这一层到底在做什么

如果用一句话概括：

**`ReplicatedBackend` 负责把 primary 上形成的 PG 事务，拆成“本地提交动作 + 发给副本的复制动作”。**

这一步会同时发生两类事情：

### 1. 发副本子操作

也就是：

- primary 不是只在自己本地写一下就完事
- 它要把对应变更发给从副本 OSD

扫描结果里，相关动作包括：

- `issue_op()`
- `do_repop()`
- `repop_commit()`
- `do_repop_reply()`

这说明 Ceph 的副本写不是一个抽象概念，而是有一整套明确的副本消息与应答流。

### 2. 把本地事务提交到底层 ObjectStore

也就是：

- primary 还要把自己本地那份事务交给真正的存储引擎

所以在主路径上，`ReplicatedBackend` 非常像一个真正的分叉点：

- 向外，对副本说“请一起完成这次写”
- 向内，对本地存储说“请把这次事务落下去”

## 第 10 站：副本 OSD 并不是重新执行一遍完整客户端逻辑

这也是初学者很容易误解的点。

副本 OSD 当然也会处理与这次写相关的动作，但它不是“像 primary 一样重新接一遍客户端原始请求并重复全部判断逻辑”。

更准确地说：

- primary 负责主导这次写流程
- 副本更多是接收来自 primary 的复制子操作
- 副本执行自己的本地事务并回确认

这正是 `ReplicatedBackend::do_repop()` 这类路径存在的意义。

所以写路径里真正的“业务主脑”是在 primary；副本的角色更像：

- 执行主副本协议里分配给自己的那一段动作

这个分工理解清楚后，很多关于 ack、commit、apply 的概念才容易继续往下讲。

## 第 11 站：`PrimaryLogPG::queue_transactions()` 把事务交给 ObjectStore

从扫描结果可以看到，`PrimaryLogPG` 会作为 `PGBackend::Listener`，最终把事务交给：

- `osd->store->queue_transactions(...)`

这个点非常重要，因为它标志着：

- 到这里为止，逻辑仍主要停留在 PG / 复制语义层
- 从这里开始，事务开始真正进入本地存储引擎抽象层

也就是说，Ceph 在这里又做了一次清晰分层：

- 上一层解决“分布式写怎么组织”
- 下一层解决“本地事务怎么存下去”

这也是为什么 Ceph 能支持不同底层 ObjectStore 抽象，而 BlueStore 只是其中最核心、最常见的一种实现。

## 第 12 站：BlueStore 才是“真正落盘”的主战场

对现代 Ceph 来说，最值得关注的底层存储实现通常就是：

- `src/os/bluestore/BlueStore.cc`

在主路径上，对应的重要入口是：

- `BlueStore::queue_transactions()`

你可以把它理解成：

- 上层 PG / 复制后端已经把事务整理好了
- BlueStore 开始负责把这份事务真正变成底层设备上的持久化结果

### 这一层的核心不是“收到一个 write 就写文件”

BlueStore 处理的是更复杂的事情：

- 构建内部事务上下文
- 推进事务状态机
- 组织数据 IO
- 组织 KV 元数据更新
- 向底层 DB 提交事务
- 最终在合适时机完成事务收尾

扫描结果里，关键节点包括：

- `queue_transactions()`
- `_txc_finish_io()`
- `_txc_finalize_kv()`
- `_txc_apply_kv()`
- `_txc_committed_kv()`
- `_txc_finish()`

这说明真正“落盘”在 Ceph 里不是一个单动作，而是一个事务生命周期。

## 为什么 BlueStore 里会同时看到数据写和 KV 提交

很多初学者第一次读 BlueStore 会困惑：

- 既然是对象存储，为什么这里又出现 KV / DB / 元数据提交？

这是因为现代 Ceph 的对象存储并不是“只有一块平面数据区域”。

粗略理解就够了：

- 对象数据本体需要被放到主设备或相关区域
- 对象元数据、分配信息、索引信息等，又需要另一套可管理的 KV 组织方式

所以 BlueStore 的事务路径天然会同时涉及：

- 数据 IO
- KV 元数据变更

这也正是为什么后面讲 BlueStore 时，一定会连着讲：

- BlueStore
- BlueFS
- RocksDB
- WAL / DB / Main device

但在本篇里，你先只要记住这一个判断：

**BlueStore 的落盘，不是“只写用户数据”，而是“把这次对象变更对应的数据与元数据事务一起推进到稳定状态”。**

## 第 13 站：什么时候算“这次写完成了”

这其实是 Ceph 写路径里最容易让人误解的问题之一。

很多人会下意识认为：

- 只要 primary 把本地盘写完，这次写就结束了

但在 Ceph 里，真正“完成”的含义取决于你具体讨论的是哪一层语义：

- 请求是否已被主副本协议接受
- 副本是否都完成了相应阶段
- 本地事务是否已提交到后端存储
- 上层最终要等的是哪种确认语义

这也是为什么在后面专门讲“对象写入落盘全过程”时，会单独区分：

- ack
- apply
- commit

在本篇里你先不必把这些概念吃透，只需要建立一个重要直觉：

**Ceph 的“写完成”不是一个简单瞬间，而是沿着复制协议和本地事务状态逐步推进出来的。**

## 把整条写路径压缩成一条最短调用链

如果你只想先在脑子里保留一条“主路径骨架”，可以记成下面这样：

```text
IoCtxImpl::operate / aio_operate
  -> Objecter::prepare_mutate_op
  -> Objecter::op_submit
  -> Objecter::_prepare_osd_op
  -> Objecter::_send_op
  -> AsyncConnection::send_message
  -> OSD::ms_fast_dispatch
  -> OSD::enqueue_op
  -> PrimaryLogPG::do_request
  -> PrimaryLogPG::do_op
  -> PrimaryLogPG::execute_ctx
  -> PrimaryLogPG::issue_repop
  -> ReplicatedBackend::submit_transaction
  -> PrimaryLogPG::queue_transactions
  -> BlueStore::queue_transactions
  -> BlueStore::_txc_apply_kv / _txc_finish
```

这条链不是为了让你死记硬背函数名，而是为了让你建立分层感觉：

- 客户端入口
- 请求组织
- 消息发送
- OSD 收包
- PG 主处理
- 复制后端
- 本地持久化

只要这七层顺序你没丢，后面看细节时就不会总是失去方向。

## 从仓库结构角度，应该按什么顺序读这条路径

结合上一篇的“仓库地图”，我建议按下面顺序读：

### 第一组：客户端入口

- `src/librados/IoCtxImpl.cc`
- `src/osdc/Objecter.h`
- `src/osdc/Objecter.cc`

目标是回答：

- 请求是怎么从 API 变成 OSD 操作的？

### 第二组：消息层入口

- `src/messages/MOSDOp.h`
- `src/msg/async/AsyncConnection.cc`

目标是回答：

- 请求怎么在线上传？

### 第三组：OSD 接收入口

- `src/osd/OSD.cc`

目标是回答：

- 请求进入 OSD 后，怎么进 PG？

### 第四组：PG 主逻辑

- `src/osd/PrimaryLogPG.cc`

目标是回答：

- 主副本如何组织这次写？

### 第五组：复制与提交

- `src/osd/ReplicatedBackend.cc`

目标是回答：

- 副本子操作与本地事务如何并行推进？

### 第六组：本地持久化

- `src/os/bluestore/BlueStore.cc`

目标是回答：

- 事务最终如何落地？

这个阅读顺序最大的好处是：

- 你读到的每一层，都能和上一层形成明确承接

## 初学者最容易在这条写路径上迷失的 7 个点

### 1. 以为 `IoCtx` 就是整个客户端核心

其实 `Objecter` 才是更关键的请求组织者。

### 2. 以为请求发到 OSD 就直接写盘

中间还有 PG、复制后端、事务和存储引擎层。

### 3. 以为 OSD 顶层直接处理对象一致性

真正的重要一致性组织语义是在 PG 层展开的。

### 4. 以为副本 OSD 只是被动存一份数据

副本也参与复制协议和本地事务，只是角色不是 primary。

### 5. 以为 `ReplicatedBackend` 和 BlueStore 是一回事

不是。前者更偏复制协议和事务下发，后者更偏本地持久化实现。

### 6. 以为“写完成”是一个单点动作

其实它依赖协议阶段和本地事务推进。

### 7. 看到 `PrimaryLogPG.cc` 就想一口气读完整个文件

这通常会失败。正确做法是沿着主路径函数一个个读。

## 这一篇最应该留下的 5 个直觉

### 直觉一：写请求真正的客户端组织核心是 `Objecter`

不是 `MON`，也不只是 `IoCtx`。

### 直觉二：请求进入 OSD 后不会直接写盘，而是先进入 PG 语义层

这是 Ceph 设计和传统存储实现非常不一样的地方。

### 直觉三：primary OSD 主导整条写路径

副本参与执行，但主流程由 primary 组织。

### 直觉四：复制协议和本地持久化是两层不同问题

`ReplicatedBackend` 和 BlueStore 分别解决它们。

### 直觉五：Ceph 写路径的本质，是“分布式事务推进”而不是“单机 write 调用”

你越早建立这个直觉，后面越容易理解 Ceph 的复杂度来源。

## 下一篇看什么

既然这篇已经把“写请求主路径”串起来了，接下来最自然的事情，就是回头专门讲：

**是谁在支撑整个集群的地图、一致视图和仲裁？**

所以下一篇建议接：

**《MON 原理与源码：集群地图、选举、Quorum 为什么是 Ceph 的大脑》**

到那时，我们会把这篇里默认已经存在、却没有展开的控制面前提补齐：客户端为什么能定位、OSD 为什么知道规则、整个集群为什么能形成统一视图。
