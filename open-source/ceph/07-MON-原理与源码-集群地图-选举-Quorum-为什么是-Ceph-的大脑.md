# MON 原理与源码：集群地图、选举、Quorum 为什么是 Ceph 的大脑

## 这篇文章要解决什么问题

前面我们已经沿着一次写请求，从客户端一路走到了 `OSD`、`PG`、复制后端和 BlueStore。那条主路径里，有一个非常重要、却一直被默认“已经存在”的前提：

- 客户端为什么知道集群长什么样
- `Objecter` 为什么能定位目标 OSD
- `OSD` 为什么知道当前规则、成员和状态
- 整个集群为什么能对“谁在、谁不在、哪张 map 是最新的”形成统一视图

这组问题，最后都会指向同一个核心角色：

- `MON`

很多人第一次接触 Ceph 时，会把 `MON` 想成：

- “保存配置的服务”
- “元数据中心”
- “集群的注册中心”
- “一个负责仲裁的控制节点”

这些理解都抓到了一部分，但还不够完整。

更准确地说，`MON` 之所以是 Ceph 的大脑，不是因为它“管理一切”，而是因为它同时解决了三件 Ceph 无法绕开的事情：

1. 谁是 monitor 集群成员，也就是 `MonMap`
2. 谁在当前时刻拥有合法领导权，也就是选举和 quorum
3. 整个集群的关键地图与状态，如何在多个 monitor 之间保持一致，也就是 Paxos 和各类 PaxosService

这一篇的目标，就是把这三层真正串起来。

如果只允许用一句话先给结论，那就是：

**Ceph MON 的本质不是“存几张 map”，而是“先靠 MonMap 组织成员、靠 Elector 建立 leader/quorum，再靠统一 Paxos 串行提交多个集群元数据服务”。**

## 先建立最重要的边界：MON 是控制面核心，但不在普通数据路径中转

讲 `MON` 前，必须先强调一个边界，否则后面所有设计都会被误解。

`MON` 很重要，但它的重要性不是：

- 每次读写请求都要经过它
- 所有客户端数据都从它这里转发

恰恰相反，Ceph 的设计目标之一，就是让普通数据路径尽量避免中心化转发。前面第 6 篇里你已经看到：

- 客户端请求最终是直接朝 OSD 去的
- `MON` 提供的是地图和控制面状态，而不是普通 IO 中转

所以这篇要始终带着一个前提来读：

- `MON` 决定规则和视图
- `OSD` 承担真实数据执行

这也是为什么理解 `MON` 时，关键词不是“转发”，而是：

- cluster map
- election
- quorum
- Paxos

## 第一张图：MON 不是一个单模块，而是“总控 + 多个子服务”

第一次进 `src/mon` 时，很多人会误以为：

- `Monitor` 是一个单体大类，里面塞了所有逻辑

这其实只看对了一半。

更准确地说，MON 的实现组织方式是：

- 一个总控类 `Monitor`
- 一个选举器 `Elector`
- 一个统一复制内核 `Paxos`
- 一组基于 Paxos 的子服务 `PaxosService`

你可以先把关系理解成这张图：

```text
ceph-mon 进程
   |
   v
Monitor
   |
   +-- MonMap / quorum / rank / state
   +-- Elector
   +-- Paxos
   +-- PaxosService[]
          |
          +-- MonmapMonitor
          +-- OSDMonitor
          +-- MgrMonitor
          +-- MDSMonitor
          +-- AuthMonitor
          +-- ConfigMonitor
          +-- HealthMonitor
          +-- ...
```

这张图是理解 MON 的真正入口。

因为它说明：

- `Monitor` 不是单一 map 管理器
- `Elector` 只解决“谁说了算”
- `Paxos` 只解决“大家如何一致”
- 各个 `*Monitor` 子服务才分别解决“具体维护哪一类集群状态”

一旦接受这个分层，Ceph MON 的复杂度就会变得可理解很多。

## 从 `ceph-mon` 启动入口开始看：先有进程，再有总控

如果要从代码入口开始，最适合第一眼看的文件是：

- `src/ceph_mon.cc`

它的意义不是“把所有逻辑讲完”，而是告诉你：

- `ceph-mon` 进程是怎么启动的
- monitor store 和基础环境是怎么准备的
- 最终如何把控制权交给 `Monitor`

对源码入门来说，这一步的意义主要是建立一个直觉：

- `MON` 当然是守护进程
- 但真正的核心逻辑不是散在 `main()` 里，而是进入 `Monitor::init()` 之后展开

所以在阅读顺序上，一个很自然的切法是：

1. `ceph_mon.cc`
2. `Monitor.h`
3. `Monitor.cc`

## `Monitor` 是总调度器，而不是单个业务服务

如果你打开 `Monitor.h`，很快就会看到它内部维护了很多关键成员：

- monitor 自身状态
- `MonMap`
- quorum 相关信息
- `Elector`
- `Paxos`
- 各类 `PaxosService`

这说明 `Monitor` 的真正角色更像：

- 控制面总调度器

它做的不是某一个 map 的具体业务，而是：

- 组织 monitor 自身生命周期
- 管理选举、leader、peon、probing 等状态
- 路由 monitor 收到的各种消息
- 协调 Paxos 与各类 map 服务

所以你可以把 `Monitor` 理解成：

**MON 世界里的“总控制器”，而各个 `*Monitor` 才是 map 级业务服务。**

## 第 1 层：MonMap 是 monitor 集群自身的成员表

理解 `MON` 的第一步，不是先讲 `OSDMap`，而是先讲 `MonMap`。

因为 monitor 必须先回答一个更根本的问题：

- 我们这组 monitor 到底有哪些成员？
- 每个 monitor 的地址、名字、rank 是什么？
- 谁有资格参与选举和 quorum？

这正是 `MonMap` 的职责。

### 为什么 MonMap 比 OSDMap 更适合作为 MON 入口

因为从 monitor 自身视角看：

- 先得知道 monitor 集群自己长什么样
- 后面才谈得上 monitor 去维护整个 Ceph 集群的其他地图

也就是说，`MonMap` 是 MON 的“自我认识”，而 `OSDMap`、`MgrMap`、`FSMap` 则更像 MON 维护给整个集群看的“系统状态”。

### 从启动流程上看，MonMap 是真正的第一批关键状态

在 `Monitor::bootstrap()` 这一类流程里，monitor 会基于 `MonMap`：

- 计算自己的 rank
- 确认其他 monitor 成员
- 做 probing 和联系同伴
- 决定后续是否进入选举

这正好说明：

**没有 MonMap，monitor 连“自己属于哪个 monitor 集群”都无法确定。**

## 一个容易忽略但很重要的点：rank 不是装饰，而是选举和 quorum 的坐标系

在 monitor 排障文档里，你能看到 `mon_status` 输出里经常出现：

- `rank`
- `quorum`
- `outside_quorum`

这几个字段非常值得讲给读者，因为它们能把抽象选举状态变成具体观测对象。

比如文档里会告诉你：

- `quorum` 里记录的是 rank，不是 monitor 名字
- rank 来源于当前 `MonMap`

这就说明：

- `MonMap` 不只是成员地址本
- 它还定义了 monitor 集群内部共同理解的成员编号语义

这也是为什么很多 monitor 相关现象，最后都要回到 `MonMap` 才能解释清楚。

## 第 2 层：`Elector` 解决“谁说了算”

成员关系有了，还不够。monitor 集群还必须回答第二个问题：

- 当前到底谁是 leader？

这一步不是靠“固定主节点”解决的，而是靠选举。

在 Ceph 里，负责这一层逻辑的核心类是：

- `Elector`

### 为什么选举是 MON 的刚需

因为 monitor 不是单实例角色。为了高可用，Ceph 通常部署多个 monitor：

- 3 个
- 5 个

一旦有多个 monitor，就必须回答：

- 谁来主导当前这轮集群控制面推进
- 谁来代表当前合法的领导视图

否则多个 monitor 各说各话，整个控制面就不可能稳定。

### `Elector` 的职责边界

这里一定要讲清楚一个边界：

- `Elector` 解决的是“leader 的合法产生”
- 它不直接解决所有地图内容的一致复制

也就是说：

- 先选主
- 再由合法 leader 去驱动后续 Paxos 提交

这是 MON 设计中最核心的一次职责拆分。

如果把这两件事混在一起，读源码时会非常痛苦。

## quorum：为什么不是“有 leader 就够了”

很多人理解分布式控制面时，会先想到 leader，但在 Ceph MON 里，和 leader 一样重要的另一个概念是：

- quorum

### quorum 到底是什么

可以先用最工程化的说法来理解：

- quorum 就是当前能形成多数共识的一组 monitor 成员

它的意义不是“谁在线”，而是：

- 哪些 monitor 共同构成了当前合法的一致性基础

这就是为什么 monitor 故障排障文档会反复强调：

- 只要还有足够 monitor 存活并形成 quorum，集群就不一定会整体失效

### 为什么 quorum 比“活着的 monitor 数量”更重要

因为 Ceph 关心的不是：

- 有几个进程仍然在跑

而是：

- 是否还有一个多数集合，能对集群关键状态形成合法一致认知

这就是 monitor 真正的高可用逻辑。

## 选举之后发生了什么：leader / peon / quorum 状态切换

在 `Monitor` 的状态机里，你会看到类似这些状态：

- `PROBING`
- `ELECTING`
- `LEADER`
- `PEON`
- `SYNCHRONIZING`

这很适合在博客里给读者建立一个动态直觉：

### `PROBING`

- 我先确认自己和同伴当前到底处在什么状态

### `ELECTING`

- 我们要为当前轮次确定 leader

### `LEADER`

- 我是这轮合法主 monitor

### `PEON`

- 我不是 leader，但我属于当前合法 quorum

### `SYNCHRONIZING`

- 我需要先追上当前一致状态，不能贸然参与提供完整服务

这组状态非常重要，因为它能把“monitor 很复杂”这件事拆成几个更容易消化的阶段。

## 第 3 层：Paxos 解决“大家如何一致”

选举解决了“谁说了算”，但还没有解决第三个关键问题：

- leader 怎么把关键状态可靠地复制给其他 monitor？

这一步由 `Paxos` 解决。

### 为什么 Ceph 在 monitor 层使用 Paxos

因为 monitor 维护的是集群最关键的控制面状态：

- `MonMap`
- `OSDMap`
- `MgrMap`
- `FSMap/MDSMap`
- 认证、配置、健康等关键状态

这些状态一旦出现分叉，整个集群就会失去统一视图。

所以 Ceph 不能只靠：

- “leader 本地改一份”
- “其他 monitor 之后再想办法同步”

而是需要一套明确的一致性提交机制。

这就是 Paxos 在 MON 里的意义。

### 这里最容易误解的一点：Paxos 不是每张 map 各自乱用一套协议

更准确地说，Ceph monitor 有一个统一的 Paxos 复制内核，而不同类型的集群状态通过 `PaxosService` 抽象挂在这套内核之上。

这也是 MON 代码设计里最漂亮的地方之一：

- 一套一致性复制骨架
- 多类集群状态服务复用

这使得 monitor 不是一堆各自为政的小系统，而是一套统一控制面平台。

## 第 4 层：`PaxosService` 是 MON 里真正的“业务插件接口”

如果说：

- `Elector` 解决选主
- `Paxos` 解决复制

那中间还需要一个非常关键的抽象层，来回答：

- 具体哪类集群状态要如何接收请求、准备变更、形成 pending、提交并刷新内存视图？

这正是 `PaxosService` 的职责。

你可以把 `PaxosService` 理解成：

- MON 里每种集群状态服务的统一抽象基类

它典型会承担：

- 请求预处理
- 非 leader 时转发到 leader
- leader 上准备更新
- 组织 pending
- 驱动 Paxos proposal
- commit 后从持久状态刷新内存视图

所以理解 `PaxosService` 非常重要，因为它把 MON 的设计彻底“分层化”了：

- 领导权：`Elector`
- 一致复制：`Paxos`
- 具体业务：`PaxosService` 子类

## 现在终于可以讲那几张最重要的地图了

到这里为止，逻辑顺序已经足够清楚了：

1. `MonMap` 先定义 monitor 集群自己
2. `Elector` 决定谁是 leader
3. quorum 决定谁拥有合法多数
4. `Paxos` 负责一致复制
5. 各种 `PaxosService` 子类维护具体集群状态

接下来再讲：

- `OSDMap`
- `MgrMap`
- `FSMap/MDSMap`

就不会显得突兀了。

## `OSDMonitor`：为什么 OSDMap 是 MON 最关键的地图之一

在 MON 维护的各种地图里，对普通数据路径影响最大的，通常就是：

- `OSDMap`

因为它直接决定了：

- 哪些 OSD 存活
- 哪些 OSD `up/in`
- pool、PG、CRUSH 相关全局视图
- 客户端和 OSD 如何理解当前的数据放置规则

而负责这张地图的核心服务就是：

- `OSDMonitor`

### 为什么 `OSDMap` 对客户端写路径如此重要

回忆上一篇里的写路径：

- `Objecter` 要定位目标 OSD
- `OSD` 要知道当前 acting/up 集合
- PG peering 和副本关系要知道当前集群成员状态

这些事情都离不开 `OSDMap`。

所以从系统角度看，`OSDMap` 不是 monitor 保存的一份“运维信息”，而是：

- 整个数据面能够正确运行的控制面前提

### `OSDMonitor` 在代码里的职责感

如果你去看它的实现，会发现它负责的不是简单的一份表，而是大量和 OSD 世界紧密相关的状态：

- OSD 状态变化
- 池配置
- CRUSH 相关管理
- 地图增量与刷新
- active 之后的周期性维护动作

这也解释了为什么 `OSDMonitor` 会是 MON 里最厚重的子服务之一。

## `MgrMonitor`：为什么 `MGR` 自己也需要一张 map

现代 Ceph 里，`MGR` 是管理平面和模块生态的宿主。所以 MON 当然也要维护：

- 当前有哪些 `mgr`
- 哪个是 active
- 哪些是 standby
- 模块相关状态和能力信息

这就是：

- `MgrMonitor`
- `MgrMap`

### 这一步的关键意义

它说明一个很重要的事实：

- MON 维护的不是“只有存储数据面的地图”
- 它维护的是整个 Ceph 控制面关键角色的合法视图

所以 `MgrMonitor` 的存在，本身就在提醒你：

- `MON` 的职责是全局控制面状态，不只是 OSD 相关状态

## `MDSMonitor`：CephFS 控制面为什么也要挂在 MON 上

同样地，CephFS 作为上层文件系统语义，也需要有一张受全局控制面管理的地图：

- 哪些 MDS 存在
- 当前文件系统状态如何
- 哪些 MDS `up/in`
- beacon 如何驱动状态变化

这就对应：

- `MDSMonitor`
- `FSMap / MDSMap`

### 这一步非常有助于理解 Ceph 的整体性

因为它说明：

- Ceph 不是“MON 管对象底座、MDS 完全另起炉灶”
- CephFS 虽然有自己的元数据服务，但其全局控制面视图仍然要通过 MON 维护和发布

这也是为什么 Ceph 能保持“多个上层服务共享同一控制面大脑”的系统结构。

## `Monitor::dispatch_op()`：MON 的总消息路由器

如果你要在代码里找一个最像“MON 总控路由中心”的地方，非常值得看的就是：

- `Monitor::dispatch_op()`

这一层的价值是：

- 它把 monitor 收到的各种消息，按类型路由到不同的服务或内核组件

你会在这里清楚看到一种非常典型的分工：

- election 消息去 `Elector`
- Paxos 消息去 `Paxos`
- OSD 相关消息去 `OSDMonitor`
- MDS beacon 去 `MDSMonitor`
- MGR beacon 去 `MgrMonitor`
- mon join 去 `MonmapMonitor`

也就是说，`Monitor` 的总控角色，在这个函数里体现得非常明显：

- 它不是自己处理所有业务
- 它负责把业务送到正确的控制面子系统

这对理解整个 `src/mon` 目录非常关键。

## 为什么说 MON 是“大脑”，而不是“数据库”

很多人一想到 monitor，就很容易把它理解成：

- 保存元数据的地方

这个理解太弱了。

如果你从这一篇的主线回头看，会发现 MON 真正像“大脑”的地方在于：

### 1. 它先定义成员与合法性

- 谁是 monitor 集群成员
- 谁在当前 quorum 里
- 谁是 leader

### 2. 它维持统一视图

- 各种 map 由它维护 master copy
- 客户端和 OSD 依赖这些地图形成共同认知

### 3. 它把多类控制面服务组织进一套一致性机制

- 不是“每张 map 单独乱改”
- 而是统一挂到 `Paxos + PaxosService` 框架里

这三点加起来，才配得上“大脑”这个说法。

而如果你只说“MON 存了几张 map”，那就低估了它的系统价值。

## 为什么 MON 很重要，但依然不该进入普通写路径中转

到这里可以再次回到全文一开始的边界问题。

既然 `MON` 是大脑，为什么 Ceph 不让普通数据写路径都经过它？

答案其实正来自前面讲过的所有内容：

- `MON` 负责的是全局一致视图和控制面合法性
- 它的价值在于“规则正确”
- 不在于“替你搬运所有数据”

如果让它进入普通数据中转路径，会带来三个直接问题：

### 1. 性能瓶颈

所有 IO 都压到控制面节点上，扩展性会立刻恶化。

### 2. 故障放大

控制面故障会直接拖垮所有数据请求。

### 3. 职责混乱

控制面和数据面边界被打破，后续一致性和性能问题都会变得更难分析。

所以 Ceph 的设计选择非常清晰：

- `MON` 提供地图和合法视图
- 客户端和 OSD 基于这些地图直接走数据面

这也正是 Ceph 能在大规模集群里保持扩展性的关键之一。

## 初学者最容易混淆的 8 个点

### 1. 认为 `Monitor` 就是“保存 map 的一个类”

不对。它是总控类，内部组织了选举、quorum、Paxos 和多个子服务。

### 2. 认为有 leader 就够了，不需要 quorum 概念

不对。合法多数才是一致性基础，leader 只是其中的主导者。

### 3. 认为选举和一致复制是同一件事

不是。`Elector` 解决“谁说了算”，`Paxos` 解决“大家如何一致”。

### 4. 认为每张地图都有自己完全独立的一套协议

不是。Ceph 用统一 Paxos 内核，加 `PaxosService` 子类来组织不同 map 服务。

### 5. 认为 `OSDMap` 是唯一重要的 map

它很重要，但不是唯一。`MonMap`、`MgrMap`、`FSMap/MDSMap` 同样是控制面核心对象。

### 6. 认为 MON 会中转所有客户端写请求

不会。普通数据路径应尽量绕开 MON。

### 7. 认为 monitor 故障就等于整个集群立刻不可用

不一定。只要还有足够 monitor 形成 quorum，集群控制面仍可能继续工作。

### 8. 看到 `src/mon` 很大就想一口气读完整个目录

更好的方式是沿本文主线：

- `ceph_mon.cc`
- `Monitor`
- `MonMap`
- `Elector`
- `Paxos`
- `PaxosService`
- `OSDMonitor / MgrMonitor / MDSMonitor`

## 这一篇最应该留下的 5 个直觉

### 直觉一：MON 的本质是控制面大脑，不是数据路径中转器

它负责规则和一致视图，不负责普通 IO 搬运。

### 直觉二：理解 MON，要先理解 `MonMap`

因为 monitor 集群必须先认识自己，后面才能维护整个集群。

### 直觉三：选举和 quorum 先于所有地图提交

不先确定 leader 和合法多数，就没有后续一致推进的基础。

### 直觉四：Paxos 是 MON 的统一复制内核

它不是附属概念，而是 monitor 控制面一致性的中心机制。

### 直觉五：`OSDMonitor`、`MgrMonitor`、`MDSMonitor` 是挂在 MON 之上的业务服务

它们共同解释了 MON 为什么是“多类集群地图的总控平台”。

## 下一篇看什么

既然这一篇已经把：

- monitor 成员关系
- 选举与 quorum
- Paxos 与各类 map 服务

这条控制面主线讲清楚了，下一步最自然的事，就是继续看：

**为什么 Ceph 除了 MON 之外，还需要一个单独的管理平面角色 `MGR`？**

所以下一篇建议接：

**《MGR 原理与源码：为什么 Ceph 需要管理平面与插件生态》**
