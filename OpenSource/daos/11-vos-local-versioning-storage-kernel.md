# 11. VOS：DAOS 真正的本地存储内核

## 为什么 VOS 这一篇是前面所有文章的落点

前面几篇我们已经从上往下铺了很多层：

- pool 讲资源边界
- container 讲事务与版本视图边界
- object 讲 `dkey/akey`、布局、复制和数据保护
- `RDB/RSVC` 讲服务元数据的强领导复制

但这些东西到最后都要面对一个更底层的问题：

- 单个 target 上的数据到底落到哪里？
- 为什么 DAOS 能接受乱序 epoch 更新，还能正确读到某个时间点的可见版本？
- abort、discard、aggregate 这些“版本历史处理”到底靠谁完成？
- checksum、DTX、MVCC 为什么最终都要在本地存储层落地？

这些问题的答案都指向同一个模块：

- `VOS`

也就是 `Versioning Object Store`。

这一篇的重要性非常高，因为它决定了前面 object/container 文章里那些语义，到底在本地 target 上怎样被实现成真实的数据结构和执行规则。

如果要用一句话概括 VOS 的角色，那就是：

**VOS 是 DAOS 在单个 shard 上的本地版本化对象存储内核。**

## 先给结论：VOS 不是“本地 KV 存储”，而是能维护版本历史的 shard 级对象存储

`src/vos/README.md` 开头对 VOS 的定义非常明确：

- 它为 DAOS pool 的单个 shard 提供持久对象存储
- 支持 byte-granular access
- 支持 versioning
- 元数据放在 persistent memory
- 数据可以放在 SCM 或 block storage

这几句话至少说明了四件事。

### 1. VOS 的作用范围是单个 shard

它不是整个 DAOS 集群的全局存储视图，也不是 pool/container 的 replicated metadata service。

它工作的边界是：

- 单个 storage target
- 单个 shard

这点很重要，因为它说明：

- `RDB/RSVC` 处理的是跨副本复制的服务状态
- VOS 处理的是单 target 本地数据与元数据

### 2. VOS 原生支持版本化

这不是“顺便在记录里带一个 timestamp”，而是模块设计目标本身。

README 明确说：

- VOS 的主要目的是捕获并记录任意时间顺序的 object updates
- 再把这些更新整合进一个可高效遍历的 ordered epoch history

也就是说，VOS 从一开始就不是“覆盖式存储”，而是“维护版本历史的存储”。

### 3. 它既要快，又要能承载复杂版本语义

README 反复强调：

- 延迟和带宽要尽可能接近底层硬件上限
- 内部持久/非持久数据结构都要支持高并发
- 还必须验证持久数据完整性

这说明 VOS 不只是“理论上支持版本历史”，它还得在高并发、高性能、低开销条件下把这件事做出来。

### 4. 它是 object/container 语义真正落地的地方

object 层定义：

- `dkey`
- `akey`
- single / array
- checksum
- DTX

container 层定义：

- epoch state
- snapshot 可见性边界

这些东西最终都要落到 VOS 的本地对象层级、索引、版本记录和事务辅助结构里。

## 为什么它叫 Versioning Object Store

### 关键不在 object，而在 versioning

很多人第一次看 VOS，会先注意到 “Object Store”，但这个名字里真正最值得盯住的词，其实是：

- `Versioning`

因为 README 对 VOS 最核心的描述就是：

- 它能按任意时间顺序接收更新
- 然后把这些更新整合到按 epoch 排序的历史中

这意味着 VOS 解决的不是简单“存下当前值”，而是：

- 保留过去的更新历史
- 支持在给定 epoch 上看见正确值
- 支持 later update 不覆盖 earlier history
- 支持 abort / discard / aggregate 这些围绕历史做的维护动作

所以 VOS 的第一原则不是“当前状态”，而是：

- **可查询的版本历史**

### README 里的例子已经说透了设计动机

README 给的例子很有代表性：

- 两个应用进程可以按自己的节奏独立写入
- 即使这些更新不是按时间顺序集成到本地
- VOS 仍然可以在读取时按 epoch 解析出正确顺序

这说明 VOS 的核心价值之一，就是把并行更新和版本顺序解耦。

如果没有这一层，很多并行 I/O 场景就会被迫：

- 依赖更重的串行化
- 或者更频繁地全局同步

而 VOS 正是在本地 shard 上接住了这件事。

## VOS 的层级结构：container -> object -> dkey -> akey -> value

### 这是前面 object/container 文章在本地的真实映射

README 在 VOS Concepts 一节给出了很清楚的层级：

- 一个 vpool 可以容纳多个 containers
- container 下面有 object index
- object 下面有 DKEY index
- DKEY 下面有 AKEY index
- AKEY 再指向 single value index 或 array index

所以如果你把前面几篇文章和 VOS 对齐，会发现它们正好一一对应：

- container service 的全局 container 视图，落到 target 后会映射成 VOS container
- object 的 OID，落到 VOS object index
- `dkey/akey`，落到多级索引树
- single value / array value，落到不同的本地值索引结构

也就是说，VOS 不是另起一套模型，而是 object/container 语义在本地 shard 上的具体化。

### `vos_container.c` 说明 container 在 VOS 里是真实实体

从 `src/vos/vos_container.c` 能看到：

- container UUID 会分配对应的 `vos_cont_df`
- 它会创建 object root B-tree
- 容器销毁时还要清理 timestamp、GC 和 DTX 相关结构

这说明 VOS 里的 container 不是轻量标签，而是一个真实本地持久对象层级的根。

## near-epoch read：为什么 VOS 能读出“某个时间点看见的最新值”

这几乎是理解 VOS 的第一核心能力。

### single value 的 near-epoch 规则

README 对 single value 的描述很清楚：

- 查找时会找到 `<= requested epoch` 的最高 epoch
- 如果找到 value 或 negative entry 就返回
- 如果什么都没有，则返回 miss

也就是说，一个 key 在 VOS 里不是只有一个当前值，而是一串带 epoch 的版本记录。

读取时做的不是“查当前”，而是：

- 查请求 epoch 可见范围内最近的那个版本

这就是 near-epoch retrieval。

### array value 的 near-epoch 规则更复杂

对于 array，README 说：

- 读取会收集请求 extent 中每一段在 `<= requested epoch` 条件下可见的最高版本片段
- 结果里既可能有真实数据
- 也可能有 punched extents
- 也可能有 miss

这说明 array 的版本查询不是单点查找，而是：

- 对一段 extent 空间做版本拼装

这也是为什么 array 版本历史一定要用专门的数据结构，而不能简单复用 single value 路径。

### 这就是 VOS 能承接 snapshot read 的本地基础

container 层前面说过：

- snapshot 本质上是某个 epoch 的可见性边界

那在本地 shard 上，真正把“这个 epoch 上该看见什么”算出来的，正是 VOS 的 near-epoch read 逻辑。

所以最准确的说法应该是：

- Container Service 定义 snapshot epoch
- VOS 负责在本地按该 epoch 求值

## 为什么 VOS 能接受乱序写入

### README 把这个设计目标写得很明确

VOS 必须支持：

- update 发生在 epoch 10
- 之后又来了 epoch 5 的 update
- 仍能正确维护版本历史

这对传统“按时间追加”的简化模型来说并不容易，但对 VOS 是原生要求。

### 它的关键在于“按 key + epoch 组织历史”

README 在 KV 部分解释得很清楚：

- index 的排序首先按 key
- 然后按 epoch
- 同一个 key 的不同 epoch 会尽量落在一起

并且每个更新或 punch 都带有：

- epoch validity range

也就是说，VOS 不是简单存“某个 key 在某个 epoch 的一条记录”，而是会维护：

- 某条记录从哪个 epoch 开始有效
- 到哪个 epoch 结束有效

这样即使未来来了更早或更晚的版本，也可以修正已有记录的有效范围，而不破坏整条历史。

### 这就是为什么 VOS 适合并发并行 I/O

一旦本地 shard 能独立接收乱序 epoch 更新，并且还能按读取 epoch 正确解析可见版本，那么系统就不需要把所有写都在时间上严格串行化。

这正是 README 所说的：

- conflicting updates 不必在时间上先被序列化，仍然可以在 VOS 中被正确排序和读取

## Single Value 和 Array Value 为什么要分开实现

### Single Value 更像多版本 KV

README 直接说：

- Single value update / lookup / punch / enumerate 走的是 KV store 路径

这类场景关注的是：

- 一个 key 在不同 epoch 上的值演化

所以它更像：

- 多版本 KV 索引

### Array Value 更像版本化 extent 空间

array value 完全不是“一个大 value”，而是：

- 一组可能彼此重叠、彼此覆盖、带 punch 语义的 extent 记录

README 用 EV-tree 来承接这件事：

- extent range 是一维
- epoch validity range 是另一维
- 组成二维矩形

所以 array 的本地模型更像：

- 版本化 extent 空间索引

### 这两类数据结构的差异，决定了 object I/O 路径的复杂度

同样是 object fetch：

- single value 更像 near-epoch KV 查询
- array value 则要做 extent overlap 搜索、拆分、排序和可见性拼装

这也是为什么 object 层虽然对外统一成 `iod`，但真正落到 VOS 之后，single 和 array 的执行路径差别会很大。

## EV-tree：为什么 array extent 版本历史必须用专门结构

### README 对 EV-tree 的描述非常关键

对于 array，README 说：

- VOS 使用 specialized R-tree，叫 Extent-Validity Tree
- 一个矩形的一维是 extent range
- 另一维是 epoch validity range

这就是 VOS 里最有代表性的专用结构之一。

因为 array update 会带来几个天然复杂点：

- extent 可以重叠
- 更新可以部分覆盖旧数据
- punch 可以覆盖一部分范围
- 读取时要按 epoch 选出可见片段并拼起来

如果没有 EV-tree，array 版本历史很难在查询成本和写入灵活性之间取得平衡。

### fetch 不是简单返回一条命中记录

README 说 EV-tree fetch 的核心分两步：

1. 找出所有重叠 extent
2. 再按 extent start 和 epoch 排序、拆分、标记哪些片段真正可见

这说明 array fetch 的本质其实是：

- 先收集候选版本片段
- 再做一次“可见性求值”

所以 near-epoch read 在 array 上不是单点查找，而是局部重建。

## MVCC 为什么必须落在 VOS，而不是 object 或 container service

这也是一个非常容易误判的问题。

### container 定义全局 epoch 语义，但 VOS 持有本地冲突检测上下文

前面 container 文章说过：

- container service 维护 definitive epoch state

这没有错。

但 VOS README 又明确说：

- conditional operations
- read/write races
- serializability guarantees

这些是通过 timestamp cache 和 MVCC 规则在 VOS 里实现的。

这两个表述并不矛盾，因为它们解决的是不同层面的问题：

- container service 定义全局事务/快照语义
- VOS 负责本地 shard 上读写冲突检测和可见性执行

### timestamp cache 是 VOS 的本地 MVCC 加速结构

README 把 timestamp cache 讲得很细：

- 有 negative entry cache
- 有 positive entry cache

前者是：

- 每个 target 上的全局数组
- 主要记录“不存在的实体”对应的时间戳状态

后者是：

- 每个 target 上的 LRU cache
- 记录已有 container / object / dkey / akey 的时间戳状态

这说明 timestamp cache 本质上是：

- 本地、运行时、为 MVCC 冲突检测服务的缓存层

它不属于 replicated metadata，也不应该放到高层服务里。

### 读写 timestamp 分别解决不同问题

README 继续说明：

- read timestamps 有 low / high 两个值
- write timestamps 也会保留最近两个写时间戳

它们分别用来：

- 跟踪某个子树最近的读取范围
- 检测 uncertainty violation 和读写冲突

这类信息高度依赖本地访问路径，而且需要非常低开销地更新，所以天然应该落在 VOS。

### 代码里也能看到 VOS I/O 路径在直接使用 timestamp set

从 `src/vos/vos_io.c` 和 `src/vos/vos_obj.c` 可以看到：

- `vos_ts_set_allocate(...)`
- `vos_ts_set_add(...)`
- `vos_ts_wcheck(...)`
- `vos_ts_set_update(...)`
- `vos_ts_set_wupdate(...)`

这些调用就出现在本地 I/O 和对象访问路径上。

也就是说，MVCC 在 DAOS 里不是“高层事务模块统一处理完再通知 VOS”，而是：

- 高层定义事务边界
- VOS 在本地执行路径里落实冲突检查和时间戳推进

## punch propagation：为什么 emptiness 语义也要在 VOS 里解决

README 里有一个很容易被忽略，但非常体现 VOS 本地语义复杂度的点：

- punch propagation

它的背景是：

- conditional operation 依赖 emptiness 判断
- 但如果每次都递归扫完整个子树看是不是空，会非常贵

所以 VOS 的做法是：

- 当 punch 导致某个下层实体为空时，把这种“空”的信息往父层传播

这说明 VOS 并不只是“被动存版本”，它还在维护很多为了让上层语义可高效执行而设计的本地派生状态。

## discard：为什么 abort 一定要落到 VOS 的 epoch 历史上

### README 对 discard 的定义非常直接

VOS 的 discard 用来：

- forcefully remove epochs without aggregation
- 服务 abort 请求
- 并且 abort 需要同步 discard

这意味着 discard 的作用不是“后台清理垃圾”，而是事务 abort 的语义组成部分。

### 为什么 discard 很关键

README 在 VOS Concepts 部分已经讲过：

- 某个 epoch 和 process group 相关的更新必须能够整体变得不可见
- 这样事务 abort 才能真正回滚

所以 VOS 必须支持：

- 按 epoch range 回滚一批本地记录
- 同时修复之前版本记录的 validity，使 near-epoch 读取仍然正确

README 甚至点明了 discard 的关键动作：

- 如果某条旧记录因为后来某个已丢弃 epoch 的写而被截断了有效范围
- discard 时要把它的 end-epoch 重新扩展回 infinity

这说明 discard 并不是“删掉一些记录”那么简单，而是：

- 在版本历史图上做回滚修复

### 真实入口在 `vos_discard()`

`src/vos/vos_aggregate.c` 中：

- `vos_discard(...)`

会：

- 进入 discard 模式
- 根据是否指定 object 决定是容器范围还是对象范围 discard
- 按 epoch expression 配置迭代范围
- 通过统一 iterator + aggregate/discard 参数执行清理

这说明 discard 不是旁路逻辑，而是 VOS 主体迭代与版本维护框架的一部分。

## aggregate：为什么它不是简单 GC，而是版本历史压缩

### README 给出的定义非常值得直接记住

aggregate 的目标是：

- 保留 latest update to a key/extent-range
- 保留 persistent snapshot 可见的版本
- 删除 hidden entries
- 合并连续 partial extents

这说明 aggregate 的本质不是“谁旧删谁”，而是：

- 在不改变 latest view 和 snapshot view 的前提下压缩历史

所以 aggregate 更像：

- 版本历史压缩

而不是普通意义上的 GC。

### 为什么它不能太激进

如果聚合错了，就可能破坏：

- latest state
- snapshot 可见性
- near-epoch 读取结果

所以 aggregate 的正确性条件其实很强：

- 不能影响任何仍可见的历史断点

### `vos_aggregate()` 的实现也体现了这一点

从 `src/vos/vos_aggregate.c` 看，`vos_aggregate(...)` 会：

- 设置 iterate epoch range
- 对象级迭代
- 对 EV-tree 使用 sorted logical rectangle 视图
- 维护 merge window
- 在 flush 时把旧 physical entries 替换为新的 coalesced entries

尤其 merge window 这套机制很说明问题：

- 聚合不是简单删旧记录
- 它经常需要重新组织 extent 物理条目、搬运数据、重算 checksum、再写回新的合并结果

也就是说，aggregate 是真正的“重写历史表示形式”，只不过不能改变语义。

### 它为什么放后台 ULT

README 明确说：

- aggregation 很昂贵
- 但不必跑在关键路径
- 有专门 aggregation ULT 频繁 yield，避免阻塞前台 I/O

这说明 DAOS 对 aggregate 的定位非常清晰：

- 必须做
- 但尽量不打断前台 I/O

## checksum 为什么最终也必须落在 VOS

### object 层负责协议与流程，VOS 负责持久化存取

前一篇说过：

- checksum 是 object I/O 协议的一部分

而 VOS README 则把下一层补全了：

- checksum 会跟随对象记录一起存进 VOS 持久结构
- single value 存一个 checksum
- array value 可按 chunk size 存多个 checksum

这说明 checksum 在层次上的分工是：

- object 层负责把 checksum 带进 I/O 流程
- VOS 负责把 checksum 和本地记录一起落盘、取回、校验

### README 还明确给了 update/fetch junction points

比如：

- `vos_update_end -> akey_update_single -> svt_rec_store`
- `vos_fetch_begin -> akey_fetch_single -> svt_rec_load`
- `vos_update_end -> akey_update_recx -> evt_insert`
- `vos_fetch_begin -> akey_fetch_recx -> evt_fill_entry`

这很重要，因为它说明 checksum 不是独立跑一条边路，而是嵌在 VOS 单值/数组值更新与读取链路里。

## DTX 为什么必须落在 VOS，而不是只留在 object 层

### README 已经把答案说得很直白

Replica Consistency 一节指出：

- replicated / EC object 更新时，每个相关 shard 都会启动本地 DTX
- 每个 shard 先做本地修改
- 然后把 DTX 状态记进本地 DTX table
- 记录还会回指相关修改记录

这说明 DTX 虽然是 object 数据保护与事务语义的一部分，但它在每个 shard 上必须有真实本地落点。

而这个落点正是 VOS。

### 为什么一定要在 VOS 本地记录 DTX 状态

因为读写可见性要依赖它。

README 说得很清楚：

- fetch 时如果遇到 prepared DTX
- 可能要等 commit / abort
- 非 leader 副本可能要让客户端重试 leader
- 非事务读则可能忽略 prepared 记录，返回最新 committed 数据

这些行为都要求：

- 本地记录知道某条数据对应哪个 DTX
- 本地能判断该 DTX 当前是 prepared、committable、committed 还是 aborted

如果 DTX 状态不落在 VOS，本地 fetch 根本无法决定该记录是否可见。

### README 还点明了 DTX 表是 container 级的

README 说：

- 每个 container 维护自己的 DTX table
- 组织成两个 B+trees
- 一个 active，一个 committed

而 `src/vos/vos_container.c` 里也能直接看到：

- `vc_dtx_active_hdl`
- `vc_dtx_committed_hdl`
- `dbtree_create_inplace_ex(VOS_BTR_DTX_ACT_TABLE, ...)`
- `dbtree_create_inplace_ex(VOS_BTR_DTX_CMT_TABLE, ...)`

这说明：

- DTX 不是 pool 全局表
- 不是 object 单独表
- 而是落在每个 VOS container 下的一组本地事务状态结构

这和前面 container 作为事务边界的语义是完全对齐的。

### `vos_dtx.c` 则说明这不是轻量标记，而是一套完整本地状态机

从 `src/vos/vos_dtx.c` 和 `vos_layout.h` 可以看到：

- active / committed DTX blob
- DTX local id
- 记录类型 `DTX_RT_ILOG / SVT / EVT`
- 遇到 in-progress DTX 时返回 `-DER_INPROGRESS`
- 还会带 membership / target / group 等信息

这说明 DTX 在 VOS 里不是“顺手记个 bit”，而是一整套本地事务元数据与可见性判断框架。

## `vos_obj_update()` / `vos_obj_fetch()`：为什么它们能看作 VOS 的本地 I/O 入口

### `vos_io.c` 里的注释给出了准确定位

`src/vos/vos_io.c` 直接说：

- `vos_obj_update()` / `vos_obj_fetch()` 是 inline update/fetch helper
- 被 `rdb`、rebuild 和一些测试程序使用

而它们内部主线也非常清楚：

#### update

- `vos_update_begin(...)`
- 如有 SGL 则 `vos_obj_copy(...)`
- `vos_update_end(...)`

#### fetch

- `vos_fetch_begin(...)`
- 如不是 size-only 就执行 `vos_obj_copy(...)`
- `vos_fetch_end(...)`

这说明 VOS 对本地 I/O 的组织方式并不是一个“大一统函数”，而是：

- 先建立上下文
- 再做数据准备/搬运
- 最后提交或结束

### `vos_io_context` 也印证了 VOS 真的是本地执行枢纽

在 `vos_io.c` 开头的 `vos_io_context` 里可以看到很多前面几篇熟悉的概念同时出现：

- epoch range
- oid
- container
- object reference
- checksum list
- timestamp set
- SCM / NVMe reservation
- dedup
- recx lists

这说明一旦 object I/O 落到本地 shard，真正把这些细节统筹起来的，不是 object service 顶层，而是 VOS I/O 上下文。

## VOS 和上层模块的边界到底怎么记

这可能是读源码时最容易混的地方。

### Container Service 做什么

- 定义 container 的全局事务/快照语义
- 管理 container properties、handle、snapshot 元数据

### Object Service 做什么

- 定义 object 模型
- 处理 class、placement、replication / EC、checksum 协议和 DTX 分布式流程

### VOS 做什么

- 在单 shard 上存 container/object/dkey/akey/value 层级
- 实现 near-epoch read
- 维护本地版本历史
- 执行本地 MVCC / timestamp cache 检查
- 存储 checksum
- 维护本地 DTX 表和记录可见性
- 执行 discard / aggregate

如果要压缩成一句话：

- **上层定义语义，VOS 执行这些语义在单个 shard 上的本地版本化实现。**

## 一个更实用的阅读方法：把 VOS 拆成五层

以后你再读 `src/vos` 时，最好的方法不是一上来就陷进所有细节，而是先判断自己现在在看哪一层。

### 第一层：本地对象层级

看的是：

- container
- object
- dkey
- akey
- value

这一层回答“数据在本地按什么层级组织”。

### 第二层：版本索引层

看的是：

- single value tree
- EV-tree
- ilog / validity range
- near-epoch lookup

这一层回答“历史版本怎么存、怎么找”。

### 第三层：并发控制层

看的是：

- timestamp cache
- MVCC rules
- conflict / uncertainty check

这一层回答“本地读写如何避免冲突和不确定性”。

### 第四层：历史维护层

看的是：

- discard
- aggregate
- iterator
- merge window

这一层回答“历史如何回滚、压缩和清理”。

### 第五层：完整性与分布式事务落地点

看的是：

- checksum storage
- DTX active/committed tables
- prepared / committed visibility

这一层回答“本地记录如何与端到端完整性和副本事务语义接上”。

只要先按这五层去读，`src/vos` 会容易很多。

## 小结

VOS 之所以值得单独写一篇长文，是因为它不是一个普通的本地存储模块，而是 DAOS 在单 shard 上真正成立的底层内核。

它最重要的几个角色是：

- **本地对象层级存储**
- **版本历史维护者**
- **near-epoch read 求值器**
- **本地 MVCC 与 timestamp cache 执行层**
- **discard / aggregate 的历史维护层**
- **checksum 与 DTX 的本地落地点**

所以，如果把这一篇压缩成一句话，那就是：

**VOS 负责把 container/object 层定义出来的事务、快照、版本、校验和事务提交语义，真正变成单个 target 上可存、可查、可回滚、可压缩的本地版本化对象存储。**

理解了这一层，前面几篇文章里那些“上层语义”才真正完成闭环。

## 下一篇看什么

把 VOS 讲清以后，最自然的下一步就是继续往下钻到双层介质和块设备执行栈：

**BIO、VEA 与 NVMe：数据为什么能高效地下沉到块设备**

因为从那里开始，就会进入：

- SCM / NVMe 双介质协同
- BIO 的缓冲与 DMA 路径
- VEA 的块空间分配

也就是 VOS 之下真正和块设备打交道的那一层。
