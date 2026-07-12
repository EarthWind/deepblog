# OSD 进程详解：Ceph 最核心的数据面执行单元

## 这篇文章要解决什么问题

前面几篇我们已经分别讲了：

- Ceph 的整体架构
- 一次写请求如何从客户端走到集群内部
- `MON` 为什么是控制面大脑
- `MGR` 和 Dashboard 为什么代表管理平面

但如果你真正想理解 Ceph 的数据面，最终几乎一定会走到一个角色上：

- `OSD`

因为从真正干活的角度看，Ceph 大多数“重体力劳动”最后都落在 `OSD` 上：

- 接请求
- 排队
- 找到目标 PG
- 执行 peering 相关状态推进
- 做主副本写入
- 复制
- 恢复
- 回填
- scrub
- 和底层 ObjectStore 交互

这也是为什么很多人会说：

- `OSD` 是 Ceph 最核心的数据面执行单元

这句话并不夸张。

如果只允许用一句话先给结论，那就是：

**`OSD` 不是一个简单的“对象读写进程”，而是一个把消息分发、PG 串行化、Peering 状态机、复制恢复、后台校验和本地持久化组织在一起的数据面执行引擎。**

这一篇的目标，就是把这个执行引擎拆开。

## 先建立第一条边界：OSD 不是“磁盘进程”，而是“数据面协调者 + 执行者”

初学者第一次接触 `OSD` 时，最容易有两个误解：

### 误解一：OSD 就是“把对象写进磁盘的进程”

这只说对了一小部分。

### 误解二：OSD 的重点只在存储引擎，比如 BlueStore

这也不对。

更准确地说，BlueStore 当然重要，但 OSD 的复杂度远远不只是：

- 最后怎么落盘

在落盘之前，OSD 还要解决大量更高层的问题：

- 请求从哪里进来
- 怎么进入正确 PG
- 同一个 PG 的操作如何串行化
- 什么情况下 PG 还不能提供服务
- primary 如何组织副本写
- recovery / backfill 如何推进
- scrub 什么时候跑、怎么跑

所以理解 OSD，一定不能把它缩成“存储后端的壳子”。

更准确的理解是：

- OSD 是 Ceph 数据面的大型执行框架
- BlueStore 是这个框架最终对接本地持久化的一层

## 第二张图：OSD 在 Ceph 数据面里的位置

先建立一张总览图：

```text
客户端 / librados
    |
    v
Messenger / MOSDOp
    |
    v
OSD
    |
    +--> Dispatch / 入队
    |
    +--> ShardedOpWQ
    |
    +--> PG / PrimaryLogPG
    |       |
    |       +--> PeeringState
    |       +--> 复制 / 恢复 / 回填
    |       +--> Scrub
    |
    +--> PGBackend / ReplicatedBackend
    |
    v
ObjectStore / BlueStore
```

这张图最重要的结论是：

- `OSD` 并不是“一个函数调用到 BlueStore 就结束”
- 它中间还有一大层：
  - 调度
  - 队列
  - PG
  - 状态机
  - 后端执行

也就是说，OSD 的真正价值在于：

**把对象请求变成一个受控、可并发、可恢复、可校验的数据面执行流程。**

## 第 1 层：从 `ceph-osd` 启动入口看，OSD 一开始就不是“单线程小进程”

如果要从源码第一眼进入 OSD，最值得看的文件就是：

- `src/ceph_osd.cc`

这和上一篇讲 Dashboard 时类似，入口文件的意义不在于解释全部细节，而在于告诉你：

- `ceph-osd` 进程是怎么被拉起来的
- 它在进入真正运行态之前，要准备哪些基础设施

从启动流程上看，大致会经历：

- `global_init()`
- daemonize / prefork 之类的进程初始化
- `common_init_finish()`
- 创建 messenger
- 创建 `MonClient`
- 创建 `OSD`
- `pre_init()`
- messenger `start()`
- `init()`
- `final_init()`

### 这一点很值得强调

因为它说明 OSD 的启动不是：

```text
main() -> 打开存储 -> 开始收请求
```

而是：

```text
main() -> 初始化运行时 -> 创建通信组件 -> 创建 OSD 对象
       -> 初始化 store / map / PG / 后台线程 -> 再进入收请求状态
```

从一开始，你就能看出 OSD 是一个很重的系统进程，而不是“薄封装后端”。

## `pre_init / init / final_init`：为什么 OSD 初始化要分阶段

在 OSD 启动过程中，几个阶段非常有代表性：

### `pre_init()`

这一层相对轻，主要做：

- store 使用状态检查
- 配置观察者注册

也就是说，它更像正式启动前的“前置安全检查”。

### `init()`

这是 OSD 真正的大头。

这一步会做很多关键事情，比如：

- 初始化 timer 和 finisher
- mount objectstore
- 读取 superblock
- 装载当前 `OSDMap`
- 打开 meta collection
- 加载 PG
- 初始化和 monitor / manager / objecter 相关的客户端侧对象
- 启动核心后台执行体

### `final_init()`

这一步相对更偏“收尾与管理接口”，比如：

- admin socket 命令注册

### 为什么这种分阶段值得讲

因为它体现了 OSD 的初始化并不是一个扁平过程，而是：

- 先完成安全前置条件
- 再完成数据面主体装配
- 最后补管理与辅助接口

这和它后面的运行结构是一致的：分层明确，而不是一锅粥。

## 第 2 层：OSD 的线程模型为什么比想象中复杂得多

很多人第一次接触 OSD，会先想当然地把它理解成：

- 一个主线程收请求
- 几个 worker 写磁盘

但真实的 OSD 线程模型远比这复杂。

如果先不抠所有细节，可以先抓住几类最重要的执行体：

### 1. messenger 相关线程

- 负责网络消息接收与分发

### 2. `ShardedThreadPool` / `ShardedOpWQ`

- 负责 OSD 内部大部分核心工作项处理

### 3. heartbeat 线程

- 负责 OSD 之间健康相关通信和心跳推进

### 4. timer / finisher / 其他后台线程

- 负责周期任务和异步收尾

这说明 OSD 不是“消息来了就原地处理”的模型，而是：

- 通信层、调度层、执行层、周期层分开

这也是 Ceph 能处理复杂并发和长尾后台任务的前提。

## 第 3 层：第一层队列不是 OSD 自己的，而是 Messenger 的 `DispatchQueue`

这一点特别重要，也特别容易被忽视。

很多人讲 OSD 请求队列时，会直接从 `enqueue_op()` 开始讲，但真实路径更早一层。

请求先经过的是：

- Messenger 的 `DispatchQueue`

### 为什么这层值得单独讲

因为它说明：

- OSD 并不是直接从 socket 原始读包后立刻处理业务
- 消息层本身已经有自己的接收与分发队列

在这层里，消息会先经历类似这样的流程：

- 收到消息
- 判断能否 fast preprocess / fast dispatch
- 如果可以，就快速进入下游 dispatcher
- 否则排入消息分发队列

这说明 OSD 数据面处理其实至少有两层队列语义：

### 第一层：Messenger 队列

- 管消息进入 dispatcher 的过程

### 第二层：OSD 内部工作队列

- 管真正的数据面执行顺序和并发控制

很多人把这两层混在一起，会导致对 OSD 架构理解一直模糊。

## 第 4 层：`ms_fast_dispatch()` 和 `ms_dispatch()` 不是细节，而是 OSD 请求入口分流点

在 OSD 里，消息进入后的关键入口主要是：

- `ms_fast_dispatch()`
- `ms_dispatch()`

### 为什么要有两套入口

这是为了把不同类型的消息分出不同处理路径。

可以先粗略理解成：

- 一些消息适合走 fast path，尽快包装后入 OSD 内部执行队列
- 一些消息则走常规 dispatch 路径，可能涉及不同的锁和处理方式

### 从系统角度看，这反映了什么

反映了 OSD 很清楚地区分：

- 消息接住
- 消息分流
- 真正执行业务

这些不是同一层事情。

所以当你在代码里看到：

- `CEPH_MSG_OSD_OP`
- peering 相关消息
- `CEPH_MSG_OSD_MAP`
- command

它们并不会都在同一个入口里被“统一粗暴处理”。

这正是一个成熟数据面执行引擎该有的结构。

## 第 5 层：真正的数据面核心，不是“一个全局队列”，而是 `ShardedOpWQ`

如果要说 OSD 内部最值得建立直觉的结构之一，那就是：

- `ShardedOpWQ`

这是理解 OSD 并发模型的关键。

### 为什么不是一个简单全局队列

因为 Ceph 要同时满足几件事：

- 多个 PG 并发处理
- 同一个 PG 内顺序不能乱
- 不同 PG 之间尽量并行
- 调度器还能做优先级/权重控制

如果所有请求都塞进一个全局 FIFO，很快就会同时丢掉：

- 局部顺序语义
- 并发度
- 调度灵活性

所以 OSD 采用的是：

- 分片线程池
- 每个 shard 自己有调度器
- PG 相关工作按 token 哈希到某个 shard

这就是 `ShardedOpWQ` 存在的核心理由。

## `ShardedOpWQ` 最重要的价值：同一 PG 串行、不同 PG 并行

如果要把 `ShardedOpWQ` 的意义压缩成一句话，那就是：

**Ceph 通过 shard + PG token 的方式，让同一 PG 的工作保持受控顺序，同时让不同 PG 尽量并行。**

这句话非常关键，因为它解释了 OSD 的很多后续设计：

- 为什么 PeeringState 能比较稳定地推进状态机
- 为什么一个 PG 的事件不会被多个线程乱序踩踏
- 为什么 OSD 可以同时处理很多 PG 的工作

### 这也是 OSD 和“普通线程池服务器”的重要区别

很多普通服务的并发模型是：

- 来一个请求，扔给某个 worker

但 OSD 不行，因为它不是简单无状态 RPC，它必须维护：

- PG 级别的一致状态推进

所以它的核心并发单位不是“请求”，而更接近：

- 受 PG 约束的工作项

## 第 6 层：为什么请求进入 OSD 后还不能立刻执行，要先拿 PG 条件

这也是初学者很容易误解的点。

即使请求已经进入 OSD 内部队列，也不代表它马上就能执行。因为它还需要满足一组与 PG 相关的条件，比如：

- 找到对应 PG
- PG 当前状态是否允许处理
- map epoch 是否足够新
- split / merge / peering 等前置条件是否完成

所以 `ShardedOpWQ` 并不只是“拿出来就跑”，而是会根据 PG 是否就绪，把工作放进不同等待区。

### 这说明了什么

说明 OSD 的执行语义不是：

- 到了 worker 就必须立刻执行业务

而是：

- 到了 worker 之后，还要再判断“这个 PG 现在是否真的具备执行业务的条件”

这也正是 Ceph 在复杂集群状态下还能维持一致性的原因之一。

## 第 7 层：`PG` 才是 OSD 里的真正业务承载单元

前面我们已经在第 6 篇讲过：

- 请求进入 OSD 后，真正关键的一步是进入 PG

到了这一篇，就可以把这个判断再说得更明确一点：

**在 OSD 内部，真正承载对象级业务语义的核心单位不是 OSD 顶层本身，而是 PG。**

这也是为什么你会在 OSD 代码里看到很多事情最终都要落到：

- `PG`
- `PrimaryLogPG`

### 为什么这点特别重要

因为它直接决定了你读 OSD 源码的方式：

- 不要只盯 `OSD.cc`
- 要把 `OSD.cc` 看作“调度与入口层”
- 把 `PG` / `PrimaryLogPG` 看作“业务执行层”

如果这个分层没建立起来，读 `src/osd` 时会非常容易迷路。

## 第 8 层：Peering 不是“附属流程”，而是 PG 能不能真正提供服务的前置条件

说到 PG，就必须说 Peering。

因为 Ceph 里一个 PG 并不是“只要存在就能立刻处理所有请求”。在很多情况下，它必须先完成一系列状态协商和推进，才能进入健康可服务状态。

负责这套逻辑的核心之一就是：

- `PeeringState`

### 为什么 Peering 是 OSD 理解的门槛

因为它把 OSD 从“请求执行器”提升成了：

- 有状态的分布式副本协调者

这意味着 OSD 不能只管：

- 收到请求就执行

它还必须知道：

- 当前 up / acting set 是谁
- 当前 interval 是否变化了
- 当前 PG 是不是已经 active
- 是否已经 clean
- 是否还在 recovering / backfilling

所以 Peering 不是“周边知识”，而是 OSD 行为合法性的中心前提之一。

## `PeeringState`：为什么要把状态机单独拎出来

Ceph 没有把所有 PG 状态推进逻辑都散在 `PG` 里，而是用：

- `PeeringState`

来承载核心状态机。

### 这层设计非常值得讲

因为它体现了 Ceph 的一个重要工程取向：

- 把“状态推进逻辑”从“业务数据结构”里尽可能拆出来

这带来的好处是：

- 状态机更可推理
- 事件驱动关系更清晰
- Active / Clean / Recovering / Backfilling 等语义边界更明确

### 作为读者最应该先抓住哪些状态

不是所有状态一次都要吃下去。第一轮最值得先抓这些：

- `Activating`
- `Active`
- `Clean`
- `Recovered`
- `Recovering`
- `Backfilling`
- 等待本地/远端 recovery reservation
- 等待本地/远端 backfill reservation

这已经足够建立一条主线：

- 先 peering
- 再 active
- 再 clean/recovered
- 必要时进入 recovering/backfilling

## 第 9 层：`PrimaryLogPG` 是 replicated pool 下真正的业务中枢

如果说 `PG` 是通用业务承载单元，那么在 replicated pool 的常见路径里，最核心的具体实现就是：

- `PrimaryLogPG`

### 为什么它这么重要

因为在最典型的 Ceph 数据路径里，你会反复看到：

- 客户端 op 最终落到 `PrimaryLogPG`
- 恢复主线也落到 `PrimaryLogPG`
- 回填逻辑也在这里展开
- scrub 相关关键联动也和它有关

也就是说，它不是“某个边缘类”，而是：

- OSD 业务执行层的中枢之一

### 这里最值得建立的认知

`PrimaryLogPG` 一头连接：

- PG / Peering 状态世界

另一头连接：

- `PGBackend` / `ReplicatedBackend`

所以它其实站在两个世界中间：

- 向上做决策与状态推进
- 向下做复制和数据搬运落地

## 第 10 层：复制主线里，`PrimaryLogPG` 决策，`ReplicatedBackend` 执行

这是理解 OSD 复制/恢复逻辑时最关键的一条边界。

很多人第一次看恢复和复制代码时，会把：

- `PrimaryLogPG`
- `ReplicatedBackend`

混成一个概念。

更好的理解是：

### `PrimaryLogPG`

- 决定恢复什么
- 决定先恢复副本还是 primary
- 决定是否进入 backfill
- 组织整体恢复推进

### `ReplicatedBackend`

- 把这些决策落成对象级 push / pull / delete / reply 等实际动作

也就是说：

- `PrimaryLogPG` 更偏调度与业务决策
- `ReplicatedBackend` 更偏数据搬运执行

这条边界建立起来之后，阅读恢复主线会清楚很多。

## 第 11 层：恢复不是“坏了就补”，而是一条受控的调度主线

在 OSD 里，恢复不是某种模糊后台行为，而是有明确入口和推进逻辑的。

从源码主线上看，OSD 侧会有：

- `OSD::do_recovery()`

然后再进入：

- `pg->start_recovery_ops()`

在 `PrimaryLogPG` 里，这条主线大致会按顺序考虑：

- 先恢复 replicas
- 必要时恢复 primary 缺失
- 再考虑 backfill
- 如果还有 unfound，再进一步处理

### 这说明一个非常重要的事实

恢复不是“随便扫一下缺什么就补什么”，而是：

- 一个由 primary 主导、按状态和条件逐步推进的受控过程

这也是为什么 Ceph 在大规模故障或拓扑变化后，恢复虽然复杂，但不是无序混乱的。

## 第 12 层：Backfill 为什么不能简单理解成“另一种恢复”

很多资料会把 recovery 和 backfill 一起讲，初学者很容易就把二者混成一个概念。

但从代码实现和语义上看，backfill 更适合理解成：

- 通过对象扫描区间来做集合对齐的一条路径

而不是：

- 单纯“缺日志补对象”

### 为什么这点重要

因为它说明 backfill 的思路和普通日志驱动恢复并不完全一样。

在 backfill 路径里，更强调的是：

- 扫描 primary 和 replica 的对象区间
- 比较区间头部对象
- 决定哪些要删、哪些要推
- 用游标逐步推进

所以如果你以后看到：

- `recover_backfill()`
- `scan_range_primary()`
- `scan_range_replica()`

不要把它只理解成“慢速 recovery”，而应该理解成：

- 另一种集合对齐与重建路径

## 第 13 层：Scrub 不是“顺手检查一下”，而是 OSD 独立的重要后台子系统

很多人第一次学 Ceph 时，会把 scrub 当成一个附属功能：

- 定时校验一下数据

这描述太弱了。

从 OSD 源码结构上看，scrub 已经形成了非常明确的独立子系统，核心包括：

- `OsdScrub`
- `PgScrubber`
- `PrimaryLogScrub`
- `ScrubMachine`
- `ScrubBackend`

### 这说明什么

说明 Ceph 不把数据一致性校验当成“偶尔附带做的事”，而是把它视为：

- OSD 长期健康运行必不可少的一条后台主线

### 为什么 scrub 复杂是合理的

因为它要同时解决：

- 什么时间可以开始 scrub
- 当前资源和集群状态是否允许
- primary 和 replica 如何构建 scrub map
- 如何比较差异
- 如何决定是否 repair

这天然不是一个简单函数能解决的问题。

所以在系统视角下，scrub 应该和：

- peering
- recovery
- backfill

并列看成 OSD 重要后台机制，而不是“附属工具”。

## 第 14 层：`tick()` 是 OSD 周期性推进很多后台任务的总入口

如果要找一个最像“OSD 周期调度心跳”的入口，非常值得看的就是：

- `tick()`
- `tick_without_osd_lock()`

### 为什么这里重要

因为很多后台任务并不是靠“有请求就自动发生”，而是需要周期性推进，比如：

- scrub 调度
- recovery 队列推进
- 状态上报
- beacon / 监控相关动作

这说明 OSD 不是一个纯被动事件处理器，而是：

- 事件驱动 + 周期驱动

的混合系统。

这点很重要，因为它进一步解释了 OSD 的复杂度来源：

- 它既要响应外来请求
- 又要自发推进后台治理任务

## 第 15 层：OSD 还有很多“长尾后台线程”，不是所有事都走主执行队列

再进一步说，OSD 也不是所有事情都丢给 `ShardedOpWQ` 就结束。

它还有一些其他后台执行路径，比如：

- heartbeat
- agent/tier 相关后台线程
- timer / finisher 驱动的异步收尾

### 这一步最应该留下的认知是

OSD 的运行模型不是：

- “一个线程池包打天下”

而是：

- 不同类型的任务，进入不同的执行机制

这正是大型存储系统常见的工程现实：

- 核心数据路径要高效
- 复杂后台任务要能独立推进
- 不能互相完全绑死

## 用一句话重新概括 OSD 的主结构

如果把这一篇全部内容压缩成最短结构，可以记成下面这样：

### `ceph_osd.cc`

- 进程启动和总装配入口

### `OSD`

- 消息分发、系统级调度、后台推进入口

### `ShardedOpWQ`

- OSD 核心工作队列，负责 PG 级串行化和 shard 并发

### `PG / PeeringState`

- 负责状态推进和“这个 PG 现在能不能干活”

### `PrimaryLogPG`

- replicated pool 下最核心的业务执行中枢

### `ReplicatedBackend`

- 负责对象级复制与恢复搬运执行

### `BlueStore`

- 负责本地持久化落地

如果这七层你记住了，后面读 OSD 源码会轻松很多。

## 初学者最容易混淆的 9 个点

### 1. 认为 OSD 就是“磁盘进程”

太窄了。它首先是数据面执行引擎。

### 2. 认为 OSD 收到请求后会立刻直接操作 BlueStore

中间还有消息分发、队列、PG、Peering 和后端执行层。

### 3. 认为 OSD 只有一个请求队列

不对。至少要区分 Messenger 层队列和 OSD 内部工作队列。

### 4. 认为 `ms_dispatch()` 就是全部入口

还要理解 `ms_fast_dispatch()` 和消息分流语义。

### 5. 认为 OSD 的并发单位是“请求”

更准确地说，是受 PG 约束的工作项。

### 6. 认为 PG 只是对象分组概念，和执行模型关系不大

不对。PG 是 OSD 业务执行和状态推进的核心承载单元。

### 7. 认为 Peering 是少数故障场景才需要理解的边角逻辑

不对。它是 PG 合法服务能力的前置条件。

### 8. 认为 recovery、backfill、scrub 都是“后台补丁行为”

不对。它们都是 OSD 核心后台子系统。

### 9. 认为 OSD 的重点只在 `OSD.cc`

真正读主线时，必须联动 `PG`、`PrimaryLogPG`、`PeeringState`、`ReplicatedBackend`。

## 这一篇最应该留下的 5 个直觉

### 直觉一：OSD 是 Ceph 数据面的执行引擎，不只是存储后端封装

它负责把消息、状态、复制、恢复和校验组织成一个可运行系统。

### 直觉二：OSD 的核心并发单位是 PG 约束下的工作项

`ShardedOpWQ` 的价值就在这里。

### 直觉三：PG 和 PeeringState 是 OSD 能否合法处理请求的核心前提

没有这层，OSD 只是一个收包进程，不是分布式存储执行体。

### 直觉四：`PrimaryLogPG` 决策，`ReplicatedBackend` 执行

这条边界是理解复制、恢复和回填的关键。

### 直觉五：OSD 的后台世界和前台请求世界同样重要

recovery、backfill、scrub、tick 共同决定了 OSD 的长期健康行为。

## 下一篇看什么

既然这一篇已经把：

- OSD 的启动、队列、PG、Peering、复制恢复和后台任务

这条大主线讲清楚了，下一步最自然的事情就是回到其中一个最核心、也最“像魔法”的设计点：

**Ceph 为什么能在没有中心元数据定位服务的情况下，直接把对象定位到一组 OSD？**

所以下一篇建议接：

**《CRUSH 算法讲透：Ceph 为什么能不依赖中心元数据做数据定位》**
