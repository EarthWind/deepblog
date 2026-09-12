# MGR 原理与源码：为什么 Ceph 需要管理平面与插件生态

## 这篇文章要解决什么问题

上一篇我们讲了 `MON`，核心结论是：

- `MON` 是 Ceph 的控制面大脑
- 它维护 cluster map 的权威副本
- 它通过选举、quorum 和 Paxos 保证关键状态的一致性

那接下来一个非常自然的问题就是：

**既然已经有 `MON` 了，Ceph 为什么还要再引入一个 `MGR`？**

这是很多人第一次接触现代 Ceph 时都会疑惑的点。

直觉上看，`MON` 已经能：

- 管地图
- 管状态
- 管集群一致性

那为什么还不够？

原因在于，Ceph 在发展过程中逐渐发现，有一大类能力虽然非常重要，但它们并不适合继续塞进 `MON`：

- 监控指标与运行期统计
- Dashboard Web 管理界面
- Prometheus 导出
- orchestrator 接口
- 各种运维与管理插件
- 需要快速演进、频繁迭代的管理逻辑

这些能力有几个共同点：

- 它们很重要
- 它们面向运维与管理平面
- 它们通常不在普通数据 IO 主路径
- 它们需要更灵活的扩展方式

也正因为如此，Ceph 才把这类能力逐步沉淀成一个新的角色：

- `MGR`

如果只允许用一句话先给结论，那就是：

**`MGR` 是 Ceph 的管理平面与插件宿主，它消费 `MON` 和各 daemon 提供的运行时信息，再把这些信息变成监控、编排、Dashboard 和模块化管理能力。**

## 先建立第一条边界：`MON` 负责权威状态，`MGR` 负责运行期管理视图

理解 `MGR` 的最好方式，不是先记 API，也不是先看 Dashboard，而是先把它和 `MON` 的分工切开。

### `MON` 更偏什么

- 权威状态源
- cluster map 主副本
- 仲裁与合法性
- 一致性提交

### `MGR` 更偏什么

- 运行期监控视图
- 状态聚合
- 管理与编排接口
- 模块扩展
- 面向用户和运维的服务化能力

这条边界如果不先建立，后面你看 `MGR` 源码时会很容易误判：

- 以为它在“复制一遍 MON 的工作”

其实不是。

更准确地说：

- `MON` 负责“什么是真实、合法、权威的集群状态”
- `MGR` 负责“如何把这些状态加工成可观测、可编排、可扩展的管理能力”

## 为什么这些能力不应该继续堆进 `MON`

如果把 `MGR` 产生的背景讲得再直接一点，可以概括成一句话：

**Ceph 需要一个管理平面，但这个管理平面不应该污染 monitor 的核心一致性职责。**

这背后至少有四个非常实际的原因。

### 1. `MON` 已经很重，继续加功能会让控制面更脆弱

`MON` 已经承担了：

- monitor 集群成员管理
- 选举与 quorum
- Paxos
- 多类关键 map 服务

如果再把：

- Web 服务
- 丰富监控逻辑
- 编排后端
- 插件生态

都继续往里面塞，控制面核心会越来越难维护。

### 2. 管理能力的变化速度通常快于一致性核心

比如：

- Dashboard 页面要迭代
- Prometheus 暴露逻辑要调整
- orchestrator 后端要扩展
- 新管理模块要不断出现

这些能力天然更适合模块化，而不是和 monitor 的核心协议栈绑死。

### 3. 很多管理功能不需要进入 Paxos 主路径

比如：

- 展示指标
- 汇总 daemon 上报
- 暴露 REST/Web 接口
- 和外部编排器对接

这些事情很重要，但不值得把它们也放进 monitor 的强一致提交流程里。

### 4. Ceph 需要一个对外“长服务化”的管理宿主

像：

- Dashboard
- Prometheus exporter
- orchestrator CLI / backend

本质上都更像“管理服务”，而不是“仲裁核心”。

所以从架构上说，把它们放在 `MGR` 里，是一种非常合理的解耦。

## 第二张图：MGR 在 Ceph 总体架构中的位置

先建立最重要的一张全景图：

```text
                     +----------------------+
                     |        MON           |
                     | 权威状态 / cluster map |
                     +----------+-----------+
                                |
                                v
                     +----------------------+
                     |        MGR           |
                     | 状态聚合 / 管理平面   |
                     | Python 模块宿主       |
                     +----+---------+-------+
                          |         |
                          |         +----------------------+
                          |                                |
                          v                                v
                 Dashboard / REST                   Prometheus / Alerts
                          |
                          +------------+
                                       |
                                       v
                                  Orchestrator
```

这张图最重要的结论是：

- `MGR` 不是新的数据面
- `MGR` 也不是新的权威一致性核心
- `MGR` 是一个“消费状态、再暴露管理能力”的中间层

所以你可以把它理解成：

**Ceph 控制面和运维界面之间的管理平面枢纽。**

## 从文档定义开始：MGR 是 monitoring、orchestration 和 plug-in modules 的 endpoint

仓库文档对 `MGR` 的定位其实写得很直接：

- 它是 monitoring、orchestration、plug-in modules 的 endpoint

这句话非常值得反复咀嚼，因为它几乎已经把 `MGR` 的全部精髓浓缩出来了：

### monitoring

- 说明它不是只管命令
- 它要面向监控、运行期状态和指标

### orchestration

- 说明它不是只会“展示”
- 它还要为编排和控制动作提供统一接口

### plug-in modules

- 说明 `MGR` 的设计目标从一开始就不是“单一内建功能集合”
- 而是“一个可以承载插件生态的平台”

这也正是理解 `MGR` 的关键。

## 第 1 层：`MGR` 先是一个 active/standby 守护进程体系

很多人第一次看 `ceph-mgr` 进程时，会以为它和普通 daemon 一样：

- 起一个 manager
- 然后所有管理逻辑都在那里跑

但真实情况更接近 monitor 的思路：

- `MGR` 也不是只有一个实例
- 它也存在 active / standby 语义

这一点在源码中最明显的入口就是：

- `src/ceph_mgr.cc`
- `src/mgr/MgrStandby.cc`

### 为什么入口关键是 `MgrStandby`

如果你看 `ceph_mgr.cc`，会发现 `main()` 很短，它并没有直接把全部逻辑塞进来。真正的运行枢纽更接近：

- 先进入 `MgrStandby`
- 再根据 `MgrMap` 判断自己是不是 active

这说明 `MGR` 的启动模型，从一开始就是按“高可用管理角色”设计的，而不是“只有一个永远主进程”的模型。

## `MgrStandby`：MGR 世界里的第一站

`MgrStandby` 的意义非常像上一篇里的 monitor 早期阶段：

- 它先进入系统
- 建立与 monitor 的关系
- 获取 `MgrMap`
- 再决定自己是继续做 standby，还是切换成 active `Mgr`

### 这一步很重要，因为它说明：

- `MGR` 自己的主备关系，不是脱离集群状态的本地判断
- 它是由 `MgrMap` 驱动的

这也意味着：

- `MGR` 的 active/standby 切换，本质上也受 MON 维护的全局控制面状态约束

从系统设计上看，这非常合理：

- monitor 维护谁是当前 active mgr
- mgr 自己根据这份全局视图决定运行角色

所以这里又一次体现出 Ceph 的整体性：

- `MGR` 虽然从 `MON` 中分离出来
- 但它仍然站在 monitor 维护的全局状态之上运行

## 第 2 层：active `Mgr` 才是真正的管理平面总控

一旦当前实例被 `MgrMap` 选中成为 active，后续的核心逻辑才真正进入：

- `Mgr`

也就是：

- `src/mgr/Mgr.h`
- `src/mgr/Mgr.cc`

这里建议你把 `Mgr` 理解成：

**active manager 的总控对象。**

它不是具体的 Dashboard，也不是具体的 Prometheus 模块，更不是某一个 orchestrator backend。它更像：

- 把集群状态、daemon 状态、模块运行时和对外服务能力组织到一起的主控制器

## `Mgr` 到底在启动时做什么

如果把源码里这段初始化逻辑翻译成架构语言，大致可以理解成几件事：

### 1. 向 MON 订阅/获取关键运行期状态

比如：

- `MgrMap`
- `FSMap`
- `ServiceMap`
- 相关 digest 与状态信息

### 2. 向 OSD 获取 OSDMap 与运行期信息

说明 `MGR` 的视角不是单纯只看 monitor，而是会消费整个集群多个角色提供的信息。

### 3. 启动 daemon 通信面

也就是后面会讲的：

- `DaemonServer`

### 4. 启动 Python 模块体系

也就是后面会讲的：

- `PyModuleRegistry`
- `ActivePyModules`

这说明 `Mgr` 不是一个“纯逻辑类”，而是真正承担：

- 状态汇聚
- 对外控制
- 模块宿主

的管理平面核心。

## 第 3 层：`ClusterState` 只管“集群级视图”

如果你问：

- `MGR` 内部谁来保存“集群当前状态”的统一视图？

最值得先看的是：

- `src/mgr/ClusterState.h`
- `src/mgr/ClusterState.cc`

### 为什么它叫 `ClusterState`

因为它关注的是：

- 集群级状态

而不是某个单独 daemon 的连接会话细节。

从职责上，你可以把它理解成：

- `MGR` 管理平面里的“集群状态仓库”

它会承载类似这些东西：

- `FSMap`
- `MgrMap`
- `ServiceMap`
- `PGMap`
- health
- mon_status

### 这一步很值得强调

因为它说明 `MGR` 不是随手拿到一个消息就立刻丢给模块，而是先把：

- 集群级信息

整理进统一状态对象，再让模块去消费。

这就是一个非常典型的平台化设计：

- 先有统一状态层
- 再有模块消费层

## 第 4 层：`DaemonServer` 只管“daemon 通信与控制面”

如果 `ClusterState` 更偏“读视图”，那么 `DaemonServer` 更偏：

- daemon 通信与控制面

对应源码主要在：

- `src/mgr/DaemonServer.h`
- `src/mgr/DaemonServer.cc`

### 它解决什么问题

可以先简单理解成：

- 各类 daemon 怎么把状态、metadata、report 发给 active mgr
- active mgr 怎么对这些 daemon 发命令或处理相关控制动作

所以它更像：

- `MGR` 和集群各 daemon 之间的消息与控制桥梁

这一步特别重要，因为它补上了 `MGR` 的另一半：

- `ClusterState` 负责“状态存在哪里”
- `DaemonServer` 负责“状态怎么进来、命令怎么出去”

也就是说，`MGR` 并不是凭空知道全局运行态，而是通过 daemon 上报、通信和内部汇聚形成的。

## 第 5 层：为什么 `MGR` 真正的灵魂是 Python 模块体系

如果说到这里你还只把 `MGR` 理解成“一个状态汇总器”，那还是低估了它。

`MGR` 之所以和 `MON` 真正拉开差距，核心就在于：

- 它不是只管状态
- 它还是一个插件平台

这也是现代 Ceph 管理平面的核心设计思想：

**不要把所有管理能力写死在单一 C++ 主流程里，而要通过模块体系提供扩展能力。**

这套模块体系在源码里的关键对象主要包括：

- `PyModuleRegistry`
- `ActivePyModules`
- `StandbyPyModules`

## `PyModuleRegistry`：模块装载与生命周期管理中心

如果你问：

- 谁负责扫描 Python 模块
- 谁负责导入模块
- 谁决定 active/standby 模块如何切换

答案基本都会指向：

- `src/mgr/PyModuleRegistry.*`

### 它的核心价值是什么

可以先概括成一句话：

**`PyModuleRegistry` 不是具体业务模块本身，而是模块世界的“装载器和调度器”。**

它负责的事情包括：

- 初始化 Python 运行时
- 扫描模块路径
- 导入 Python 模块
- 根据当前角色启动 active 或 standby 模块容器
- 在角色切换时做模块层的切换

这意味着 `MGR` 的插件生态并不是“随便 import 一些 Python 文件”，而是一套明确的运行时框架。

## `ActivePyModules`：active manager 下真正承载模块实例的运行容器

如果说 `PyModuleRegistry` 更像模块管理器，那么：

- `ActivePyModules`

更像是：

- 真正承载 active 模块实例并给它们提供运行环境的容器

这一层非常关键，因为它会把：

- `ClusterState`
- `DaemonStateIndex`
- `DaemonServer`
- 配置、KV、通知机制

这些能力统一暴露给 active 模块。

### 这说明了一个非常重要的设计取向

Ceph 的 Python 模块不是各自直接乱连整个系统，而是通过一套受控运行环境拿到：

- 状态
- 配置
- 通知
- 通信接口

这就是为什么 `MGR` 模块生态能持续扩展，而不至于彻底失控。

## standby 模块为什么存在

初学者第一次听到 standby module，通常会很疑惑：

- 既然 active mgr 才真正提供功能，standby 模块还有什么意义？

答案是：

- standby 也可能需要提供有限能力

最典型的场景就是：

- Dashboard standby 实例做跳转

也就是说，standby 模块不是完整功能副本，而是：

- 在 standby mgr 角色下，提供受限但有用的服务能力

这也说明 `MGR` 的 active/standby 设计不是只在守护进程层，而是一直贯穿到模块层。

## 第 6 层：通知机制让模块世界知道“集群状态变了”

如果 `MGR` 是平台，那模块就必须知道：

- 集群状态什么时候变了

而这正是 `notify` 机制的意义。

从主流程上看：

- `MGR` 从 MON/OSD 等处接收运行期状态
- 更新 `ClusterState`
- 再通过 `py_module_registry->notify_all(...)`
- 让 interested modules 感知变化

然后模块自己再根据需要调用：

- `mgr.get(...)`

或相关 API 去取新状态。

### 这一步很值得强调

因为它说明 `notify` 不是“把全部最新状态直接塞给模块”，而更像：

- 一个状态变化提示信号

这是一种比较干净的解耦设计：

- 平台负责提示“状态变了”
- 模块按自己的需求拉取和消费数据

## 现在终于可以讲最常见的几个 `MGR` 模块了

到这里为止，`MGR` 作为平台的主结构已经够清楚了。接下来再看：

- Dashboard
- Prometheus
- orchestrator

就会更容易理解。

## Dashboard：为什么它最能代表 `MGR` 的平台价值

Dashboard 是最容易被用户感知到的 `MGR` 模块之一。

从仓库文档和源码都可以很清楚地看到：

- Dashboard 是 `ceph-mgr` 里的一个模块
- 它通过 CherryPy 提供 Web 服务
- 它不是一个独立于 `MGR` 的外置管理系统

这点非常重要，因为它说明：

- Dashboard 不是“额外附送的 UI”
- 它是 `MGR` 作为管理平面宿主的直接成果

### 为什么 Dashboard 非常能说明 `MGR` 的存在价值

因为 Dashboard 这种能力如果塞进 `MON`，会非常别扭：

- 需要 Web 服务
- 需要鉴权
- 需要 REST API
- 需要大量前后端迭代

而放在 `MGR` 模块体系里，就非常自然：

- 有状态视图
- 有模块运行时
- 有 active/standby 模式
- 有对外服务能力

所以 Dashboard 几乎是“为什么 Ceph 需要 `MGR`”这个问题最直观的答案之一。

## Prometheus：为什么指标暴露天然适合放在 `MGR`

Prometheus 模块同样是理解 `MGR` 的绝佳案例。

它要解决的问题本质上是：

- 汇总集群和 daemon 运行指标
- 以适合 Prometheus 抓取的方式暴露出去

这类能力显然更适合放在：

- 一个能汇聚运行期状态、并对外提供服务接口的平台

而不是：

- 让每个 monitor 或每个 OSD 各自拼出一套对外监控世界

所以 Prometheus 模块再次说明：

**`MGR` 不是存储本体的一部分，而是把存储系统变成“可被运维和观测”的关键中间层。**

## orchestrator：为什么 `MGR` 不只是“看板”，还是统一管理入口

如果 Dashboard 和 Prometheus 代表了“看”，那 orchestrator 代表的就是：

- “管”

Ceph 的 orchestrator 体系本质上提供了一个统一抽象：

- host
- service
- daemon
- apply / remove / status

然后再由不同 backend 实现，例如：

- `cephadm`
- `rook`

而 orchestrator 模块之所以适合挂在 `MGR` 下，原因也非常直接：

- 它要消费集群视图
- 它要对外暴露统一管理接口
- 它要和 Dashboard / CLI 等管理入口协作

所以你可以把 orchestrator 看成 `MGR` 的另一个关键维度：

- 不是只做观察
- 也做管理与编排接口抽象

这正好补全了 `MGR` 的角色：

- 既是观察平面
- 也是统一管理平面

## 用一句话重新概括 `ClusterState / DaemonServer / PyModuleRegistry / ActivePyModules`

这四个对象很容易让初学者混淆，所以这里给一个最压缩的记忆方式。

### `ClusterState`

- 存“集群级状态视图”

### `DaemonServer`

- 管“daemon 连接、上报和控制面通信”

### `PyModuleRegistry`

- 管“模块的发现、加载与角色切换”

### `ActivePyModules`

- 给 active 模块提供“真正可运行的平台环境”

如果你记住这四句话，后面再回源码里看，结构会清晰很多。

## 为什么说 `MGR` 是 Ceph 从“存储系统”走向“平台系统”的关键一步

如果没有 `MGR`，Ceph 当然仍然可以是一个分布式存储系统。

但有了 `MGR` 之后，Ceph 才真正具备了更强的平台属性：

- 可以模块化扩展
- 可以承载 Web 管理界面
- 可以暴露统一监控指标
- 可以接入编排后端
- 可以让新功能更快落地，而不用持续侵入 monitor 一致性核心

也就是说，`MGR` 的价值不在于：

- 替代 `MON`

而在于：

- 把 Ceph 的“管理能力”从核心一致性引擎里抽离出来，形成一个可扩展的平台层

这就是为什么现代 Ceph 基本离不开 `MGR`。

## 初学者最容易混淆的 8 个点

### 1. 认为 `MGR` 是“弱化版 MON”

不是。它不是小号 monitor，而是独立的管理平面角色。

### 2. 认为 `MGR` 也维护 cluster map 的权威副本

不是。权威状态源仍然在 `MON`。

### 3. 认为 `MGR` 只是个 Dashboard 宿主

太窄了。Dashboard 只是模块生态中的一类能力。

### 4. 认为 `MGR` 只是指标收集器

也不对。它还承担 orchestrator、模块管理、对外接口等职责。

### 5. 把 `ClusterState` 和 `DaemonServer` 当成同一种对象

一个偏状态仓库，一个偏通信与控制面。

### 6. 认为 Python 模块只是“脚本插件”

其实它们运行在明确的 `MGR` 模块框架之上。

### 7. 认为 standby mgr 没什么意义

standby 不只是热备进程，它还能承载受限的 standby 模块能力。

### 8. 认为 `MGR` 在普通数据写路径上很关键

通常不是。它关键的是管理平面，而不是普通数据面。

## 这一篇最应该留下的 5 个直觉

### 直觉一：`MGR` 是 Ceph 的管理平面，不是权威一致性核心

它依赖 `MON` 提供的状态，但不替代 `MON`。

### 直觉二：`MGR` 的价值在于“状态聚合 + 对外服务 + 模块扩展”

不是单一功能进程。

### 直觉三：active/standby 模型贯穿到模块层

不仅守护进程有 active/standby，模块世界也有。

### 直觉四：`ClusterState`、`DaemonServer`、`PyModuleRegistry`、`ActivePyModules` 构成了 `MGR` 的平台骨架

理解这四者，就等于抓住了 `MGR` 主结构。

### 直觉五：Dashboard、Prometheus、orchestrator 都是 `MGR` 平台价值的直接体现

它们不是零散功能，而是 `MGR` 存在理由的一部分。

## 下一篇看什么

既然这一篇已经把：

- `MGR` 为什么从 `MON` 中分离出来
- 它如何成为管理平面和模块宿主
- Dashboard、Prometheus、orchestrator 为什么都挂在这里

这条主线讲清楚了，下一步最自然的事就是继续往用户最容易感知的地方走：

**Dashboard 到底在代码里是怎样组织前后端、REST API 和管理页面协作的？**

所以下一篇建议接：

**《Ceph Dashboard 架构与实现：管理界面、REST API 与前后端协作》**
