# 12. BIO、VEA 与 NVMe：数据为什么能高效地下沉到块设备

## 为什么 VOS 下面还要再看一层

上一篇我们把 `VOS` 讲清了，知道它是 DAOS 在单个 shard 上的本地版本化对象存储内核。

但只要再往下追一步，就会马上遇到一个新问题：

- VOS 里的数据如果不全放在 SCM，而是需要下沉到 NVMe，那到底是谁在负责这条路径？

再展开一点，这个问题其实会分裂成三类更具体的问题：

- 谁在和 NVMe / SPDK 打交道？
- 谁在维护“哪个设备、哪个 blob、哪个 xstream”这些本地元数据？
- 谁在 blob 内部继续把空间切成适合 VOS 使用的小块？

这三件事在 DAOS 里并不是由一个模块包打天下，而是被刻意拆成了三层：

- `BIO`
- `VEA`
- NVMe / SPDK blobstore / blob

所以这一篇的重点，不是再讲一次“VOS 会把数据放到 NVMe”，而是把这条更底层的数据通路拆开：

1. `BIO` 在 SCM 和 NVMe 双层介质之间扮演什么角色？
2. `SMD`、DMA buffer、device owner xstream 分别解决什么问题？
3. `VEA` 为什么不是普通 allocator，而是专门为 VOS + NVMe blob 设计的一层版本化 extent allocator？

## 先给结论：VOS 不直接管理裸 NVMe，它通过 BIO 连接 SPDK，再通过 VEA 管 blob 内部空间

如果把职责先压缩成一句话，可以这样记：

- **`VOS` 负责本地版本化对象语义**
- **`BIO` 负责把本地 I/O 语义接到 SPDK / NVMe 执行环境**
- **`VEA` 负责把一个 SPDK blob 内部空间切成适合 VOS 分配和回收的块**

也就是说，数据真正下沉到 NVMe 的过程，不是：

- `VOS -> NVMe`

而更接近：

- `VOS -> BIO -> SPDK blob/blobstore -> NVMe`

同时在 blob 内部，实际块空间管理又是：

- `VOS -> VEA -> blob 内部 block allocation`

只有把这两条线都看清，才算真正理解了 VOS 之下的存储执行栈。

## 第一层：BIO 到底解决什么问题

### README 的第一句已经定调了

`src/bio/README.md` 开头说得很直接：

- Blob I/O module 是为在 NVMe SSD 上发起 I/O 而实现的

并且它覆盖：

- NVMe SSD 支持
- 故障检测
- 设备健康监控
- 热插拔
- VMD 识别

这一下就说明，BIO 的角色绝不是“给 VOS 提供几个 read/write 包装函数”这么简单。

它实际上承担了两类职责：

### 1. 数据路径职责

包括：

- 接入 SPDK
- 管理 blobstore / blob
- 组织 DMA 安全缓冲
- 把本地 I/O 请求变成 NVMe 上的异步 I/O

### 2. 设备管理职责

包括：

- 设备与 xstream 映射
- 设备状态维护
- 故障检测与驱逐
- 健康监控
- 热插拔与替换

换句话说，BIO 同时站在：

- 数据路径
- 设备生命周期管理

这两条线上。

## 为什么 DAOS 要引入 SPDK，而不是直接走内核 NVMe 栈

### README 给出的动机非常明确

`src/bio/README.md` 在 SPDK 一节里强调：

- SPDK 是用户态 C 库
- 相比标准内核 NVMe 驱动可以显著提升性能
- 零拷贝
- 无 syscall
- 轮询完成而不是依赖中断
- 异步、无锁

这些特征和 DAOS 的设计目标几乎天然契合：

- 用户态数据平面
- 高并发、低延迟
- 尽量减少内核切换
- 更强的可控性

因此 BIO 不是简单“支持 NVMe”，而是选择了：

- 用 SPDK 作为 DAOS 的 NVMe 执行底座

### SPDK 在这里其实又分成三层

README 里点出了 SPDK 相关的三个组成：

- NVMe driver
- bdev layer
- blobstore

在 DAOS 语境里，它们大致可以这样理解：

#### NVMe driver

负责：

- 直接和 NVMe 设备交互
- 用户态、轮询、异步 I/O

#### bdev layer

负责：

- 把不同后端设备统一成块设备抽象

#### blobstore

负责：

- 在块设备之上提供“大块分配单元”
- 即 SPDK blob

这也解释了为什么 README 会特别强调：

- blobstore 本身适合做较大的块分配
- 而 DAOS 还需要额外 allocator 处理更细粒度分配

这个“额外 allocator”，就是后面要讲的 `VEA`。

## BIO 和 SPDK 是怎么接上的

### README 给出的是 blobstore / blob + xstream 两条集成线

`src/bio/README.md` 的 SPDK Integration 部分说得很清楚：

- NVMe SSD 会分配给各个 DAOS server xstream
- 每个 NVMe SSD 上创建 SPDK blobstore
- 每个 per-xstream VOS pool 会关联自己的 SPDK blob
- SPDK I/O channel 会与对应 xstream 关联
- NVMe completion poller 会集成进 server polling ULT

这说明 BIO 的真正工作不是“调用几个 SPDK API”，而是把 DAOS 的执行模型和 SPDK 的线程/通道模型对齐。

### 这里最关键的不是 blob，而是“谁拥有 blobstore 操作权”

从 `src/bio/bio_context.c` 可以看到一个非常重要的注释：

- blobstore 的 metadata operations，比如 blob open/close/create/delete
- 总是由 device owner xstream 发起
- 非 owner xstream 需要通过 `spdk_thread_send_msg()` 把操作发给 owner xstream

这段代码非常能说明 BIO 的本质。

因为它表明：

- BIO 不是“每个线程都随便直接碰设备”
- 它在 xstream 之间建立了明确的 ownership 和转发规则

这就是下面要展开的 device owner model。

## `SMD`：为什么还需要一份“每个 server 的本地持久设备元数据”

### README 对 SMD 的定位很直接

`src/bio/README.md` 说：

- SMD 是 BIO 的重要子组件
- 它是一个存放在 SCM 上的 PMDK pmemobj pool
- 用来跟踪每个 server 的本地元数据

这里有个很关键的关键词：

- **per-server metadata**

也就是说，SMD 不是 pool/container 那种 replicated service metadata，也不是纯 DRAM 运行时缓存，而是：

- 单机本地
- 持久化
- 与设备和 blob 绑定的 server 侧元数据

### 它到底记录什么

README 列了两张核心表：

- NVMe Device Table
- NVMe Pool Table

Device Table 记录：

- NVMe SSD 到 xstream 的映射
- 设备持久状态，比如 `NORMAL` / `FAULTY`

Pool Table 记录：

- NVMe SSD
- xstream
- SPDK blob ID
- blob size
- pool 到 blob 的映射

这两张表其实就回答了很多低层启动与恢复问题：

- server 重启后，怎么知道之前有哪些 device / blob？
- 某个 pool target 之前挂在哪个 blob 上？
- device 热替换后，哪些 pool / target 要被迁移或恢复？

### 代码里也能看到 SMD 真的是“设备映射数据库”

从 `src/bio/smd/smd_pool.c` 和 `src/bio/smd/smd_device.c` 可以看到很多直观接口：

- `smd_pool_add_tgt(...)`
- `smd_pool_get_blob(...)`
- `smd_dev_set_state(...)`
- `smd_dev_replace(...)`

这说明 SMD 不是一个抽象概念，而就是：

- device / target / blob / pool 映射的本地持久数据库

### 为什么必须持久化在 SCM 上

因为这些信息不能只靠运行时重新推断。

例如：

- 某个 blob ID 挂在哪个 pool target 上
- 设备之前是否已被标记 faulty
- 替换设备时旧设备与新设备的关系

如果这些都不持久化，那么：

- 重启恢复会更复杂
- 热插拔和替换流程会丢状态
- 设备故障后的自动流程也很难闭环

所以 SMD 的位置非常关键：

- 它不是集群级元数据
- 但它又比纯运行时状态更稳定

它正好填补了本机设备管理与 I/O 恢复之间的持久状态空白。

## DMA Buffer：为什么不能直接把客户端数据原样丢给 NVMe

### README 对 DMA buffer 的说明非常具体

`src/bio/README.md` 说：

- BIO 会管理 per-xstream 的 DMA-safe buffer
- 这个 buffer 通过 SPDK memory allocation API 分配
- 可以按需增长

并且它还明确说明了 bulk update / fetch 的路径：

#### bulk update

- 客户端数据先 RDMA 到这个 DMA buffer
- 然后本地再通过 SPDK blob I/O 从 buffer DMA 到 NVMe

#### bulk fetch

- 先从 NVMe DMA 到这个 buffer
- 再由 RDMA 传回客户端

这段描述其实非常重要，因为它说明在 NVMe 路径上，BIO 不是纯“控制层”，它还负责：

- 数据搬运路径的中转与对齐

### 为什么需要 DMA-safe buffer

因为 NVMe / SPDK 这条路径对内存区域有明确要求：

- 必须是适合 DMA 的内存
- 访问与对齐方式要符合设备和 SPDK 要求

因此 BIO 提供这个 per-xstream buffer，本质上是在做两件事：

- 对上承接 DAOS/RDMA 侧的数据形态
- 对下满足 SPDK/NVMe 的 DMA 约束

### `bio_context.c` 也能看出 DMA 是一等概念

从 `src/bio/bio_context.c` 的实现能看到：

- `bic_inflight_dmas`
- 关于 DMA page 对齐的断言
- inflight DMA 计数的维护

这说明 BIO 不是把 DMA 当成透明细节，而是显式管理：

- 哪些 DMA 正在进行
- 哪些范围还不能立刻回收或重用

这和上一篇 VOS 里关于 extent 生命周期的讨论是衔接起来的。

## Device Owner Xstream：为什么不能所有 xstream 平等地管理同一块设备

### README 给出的 threading model 非常关键

`src/bio/README.md` 在 NVMe Threading Model 里定义了两个关键角色：

- Device Owner Xstream
- Init Xstream

其中 Device Owner 的职责是：

- 维护和更新 blobstore health data
- 处理 device state transition
- 处理 media error events

非 owner xstream 要把相关事件转发给 owner。

这意味着 BIO 在设备层面采取的不是：

- 多 xstream 随意共享管理

而是：

- 明确 owner，其他线程转发

### 为什么这么设计

因为设备管理和 blobstore 元数据操作如果在多个 xstream 上随意并发，会带来很多问题：

- 谁负责最终状态推进
- 设备健康数据由谁更新
- 热插拔和故障回调由谁串行处理
- blob 打开/关闭等 metadata ops 谁说了算

owner 模型的好处就是：

- 设备状态演进有唯一执行点
- 跨 xstream 转发可以保持无锁或低锁设计
- 出问题时更容易把设备级副作用集中收口

### `bio_context.c` 的注释把这个规则写得非常直接

代码里明确写到：

- blobstore metadata operations 总是由 owner xstream 发起
- 非 owner xstream 通过 `spdk_thread_send_msg()` 转发
- completion callback 在 owner xstream 上执行

这说明 device owner 不是 README 里的概念图，而是实际代码强制遵守的并发边界。

## Init Xstream：为什么设备热插拔和全局初始化要单独放一个角色

README 说 Init Xstream 负责：

- 初始化和收尾 SPDK bdev
- 注册 hotplug poller
- 维护当前 SPDK bdev 列表
- 处理热移除、热插入和 VMD LED 事件

这和 Device Owner 是另一种职责分离。

如果说 owner 关注的是：

- 某块设备的持续运行状态

那么 init xstream 更像关注：

- 全局设备发现、接入和生命周期入口

这使 BIO 的设备管理从一开始就不是混乱地分散在各个 I/O 执行流里，而是被拆成：

- 全局接入管理
- 单设备所有权管理

这很符合 DAOS 一贯的设计风格。

## 设备健康监控、故障驱逐、热插拔：为什么这些能力要和数据路径放在同一模块

### 因为在 NVMe 路径里，设备状态会直接反过来影响数据路径

`src/bio/README.md` 的后半部分讲了：

- Device Health Monitoring
- Faulty Device Detection
- NVMe SSD Hot Plug
- Device States

如果只从功能分类上看，似乎这些更像“运维功能”。

但在 DAOS 里它们必须和 BIO 放在一起，因为：

- 设备一旦变 faulty，blobstore / blob / channel 的可用性就会变
- 驱逐设备后，受影响 targets 要 down，rebuild 要触发
- 热替换后，新的设备要重新接回原有数据路径

所以这不是旁路运维逻辑，而是：

- 设备生命周期和 I/O 生命周期交叉处的核心逻辑

### `bio_device.c` 很能体现这种交叉

从 `src/bio/bio_device.c` 能看到：

- `bio_replace_dev(...)`
- `revive_dev(...)`
- `replace_dev(...)`
- `smd_dev_set_state(...)`
- `smd_dev_replace(...)`
- `setup_bio_bdev`

这说明一个“替换设备”动作，并不是纯控制面记录一下就完了，而是会联动：

- SMD 持久状态
- in-memory `bio_bdev`
- blobstore 状态流转
- 后续 reintegration 触发

因此 BIO 的设备管理不是装饰功能，而是数据路径的一部分。

## 第二层：为什么有了 SPDK blobstore 还需要 `VEA`

### README 其实已经给出了答案

`src/bio/README.md` 说：

- SPDK blobstore 分配出来的是较大的 blobs
- blobs 设计上通常比较大，至少几百 KB
- 因而还需要另一个 allocator，为 DAOS 提供高效的小块分配

这就是 `VEA` 的存在理由。

也就是说：

- SPDK blobstore 解决的是“在 NVMe 上给 DAOS 划出较大的 blob”
- `VEA` 解决的是“在某个 blob 内部，如何继续给 VOS 分配细粒度块空间”

### `VEA` 是 blob 内部空间分配器，而不是裸设备分配器

`src/vea/README.md` 开头说：

- Versioned Block Allocator 被 VOS 用来管理 NVMe SSD 上的 blocks
- 更准确说，它管理的是 SPDK blob 内部的块空间

而 `src/include/daos_srv/vea.h` 也直接写到：

- `VEA` 用于管理 SPDK blob 内部空间

所以理解 `VEA` 最稳妥的说法应该是：

- 它不是和 SPDK blobstore 平级的 whole-device allocator
- 它是建立在 blob 之上的 finer-grained extent allocator

## `VEA` 为什么不是普通 allocator

### 第一，它的元数据放在 SCM，不在 NVMe 上

`src/vea/README.md` 明确说：

- free extent metadata 放在 SCM
- 和 VOS index trees 一起放

这点非常关键，因为它决定了：

- 块分配元数据更新
- VOS 索引更新

可以被纳入同一个 PMDK transaction。

这就是为什么 README 会强调：

- allocation 和 index update 可以很容易做成单个 PMDK 事务

换句话说，`VEA` 不是只为性能设计，也是为：

- **和 VOS 持久化事务模型协同**

而设计的。

### 第二，它显式支持 delayed atomicity

`src/vea/README.md` 里最重要的一段，可能就是 delayed atomicity action：

1. 先 reserve 空间，只在 DRAM 里跟踪
2. 启动 RDMA / DMA 把数据落到预留空间
3. 最后在一个 PMDK transaction 里把 reservation 变成 persistent allocation，并更新 VOS index

这段设计非常值得停下来理解，因为它回答了一个核心问题：

- 为什么 DAOS 能一边做高性能 DMA/I/O，一边又保证块分配和索引更新的原子性？

答案就是：

- 先临时预留
- 等数据真的落好
- 再一次性把“分配成立”与“索引指向它”一起持久化提交

### 这不是普通 malloc/free 模式，而是 reserve -> publish / cancel 模式

`src/include/daos_srv/vea.h` 直接定义了：

- `vea_reserve(...)`
- `vea_cancel(...)`
- `vea_tx_publish(...)`
- `vea_free(...)`

并且注释语义非常清楚：

- `vea_reserve()` 只是预留 extent
- `vea_cancel()` 取消预留
- `vea_tx_publish()` 让 reservation 变成 persistent allocation，且必须属于调用者事务的一部分
- `vea_free()` 才是释放已分配 extent

这正好和 delayed atomicity 模型一一对应。

## `VEA` 的并发模型为什么看起来“反而没那么复杂”

### 因为 README 直接说它受益于 shared-nothing 架构

`src/vea/README.md` 说：

- 得益于 DAOS server 的 shared-nothing architecture
- scalable concurrency 不是 VEA 的主要设计难点
- 因此不需要像某些 allocator 那样再做 zone 切分来缓解争用

这点很有意思，因为它说明：

- DAOS 的 xstream / target 模型，已经帮 VEA 消解了很多跨线程竞争压力

所以 `VEA` 可以把设计重点放到：

- 空间碎片控制
- 事务协同
- hint locality

而不必首先围着全局锁争用打转。

## hint allocation：为什么 `VEA` 关心 I/O stream

### README 的假设非常实用

`src/vea/README.md` 说：

- 不同 I/O stream 发起的分配，其释放时机往往也有局部性
- 同一个 I/O stream 的分配更可能一起被释放
- 因此让同一 stream 的分配更连续，可以减少外部碎片

这个假设非常符合 DAOS 的工作负载形态。

README 甚至点名：

- 每个 VOS container 有两个主要 I/O stream
- 一个是前台 regular updates
- 一个是后台 aggregation updates

所以 `VEA` 不是盲目全局最优分配，而是带着 workload locality 假设做分配优化。

### 代码里也能看到 hint 在分配流程里的位置

从 `src/vea/vea_alloc.c` 可以看到：

- `reserve_hint(...)`
- `hint_get(...)`
- `hint_update(...)`

也就是说，hint 不是理论概念，而是 reserve 流程的真实优先路径之一。

这进一步说明 `VEA` 是按 DAOS 自身 I/O 模式量身设计的 allocator。

## `vea_alloc.c`：为什么它能看出 `VEA` 的核心设计哲学

### 分配不是一种策略，而是多级策略组合

`src/vea/vea_alloc.c` 中可以看到 reserve 路径会在几种策略之间切换：

- hint reserve
- small extent / size tree
- large extent reserve
- bitmap chunk reserve

这说明 `VEA` 不是单一“best fit”或“first fit”。

它会根据：

- 请求大小
- hint 是否命中
- 当前 free extent 结构

选择不同路径。

### 大 extent 的处理甚至会主动做半拆分

在 `reserve_extent()` 里可以看到：

- 如果最大的 free extent 足够大
- 可能会 half-and-half 拆分
- 再从第二半里预留

这类策略就很能体现 `VEA` 的目标不是只“分得出去”，而是：

- 尽量兼顾后续碎片和局部性

### 这也是为什么它叫 extent allocator，而不是 generic allocator

`VEA` 的基本抽象单位不是对象、不是字节片段，而是：

- block extents

它思考的问题也不是“小对象缓存”，而是：

- blob 内部连续块空间如何预留、持久化、回收与合并

## BIO 和 VEA 是怎么配合的

这是这一篇最关键的交汇点。

### BIO 负责“把 I/O 真正送进 blob / NVMe”

它关注的是：

- SPDK 环境
- blobstore / blob 生命周期
- DMA-safe buffer
- I/O channel
- 设备状态

### VEA 负责“这个 blob 里面有哪些块可用”

它关注的是：

- free extents
- reservation
- publish / cancel
- free / aging / flush
- hint locality

### 两者合在一起，才构成 VOS 之下的 NVMe 落盘路径

最实用的理解方式是：

1. `VOS` 先决定某次更新需要落到 NVMe。
2. `VEA` 在目标 blob 内先 reserve 合适的块空间。
3. `BIO` 组织 DMA buffer 和 SPDK blob I/O，把数据搬到对应设备。
4. 成功后，调用者把 reservation 通过 `vea_tx_publish()` 和 VOS 索引更新一起提交。
5. 未来释放时，再通过 `vea_free()` 回收到 blob 内自由空间结构。

所以：

- `BIO` 解决“怎么写到设备”
- `VEA` 解决“写到设备的哪里”

这两个问题缺一不可。

## 为什么这层设计能同时兼顾性能和一致性

如果只看性能，大家可能会觉得：

- 用户态 SPDK
- DMA-safe buffer
- owner xstream
- 本地 extent allocator

这些都在为高性能服务。

这没错，但还不够完整。

这层设计真正厉害的地方在于，它不只是快，还把“一致性边界”放在了正确的位置：

### 设备级状态一致性

由：

- `SMD`
- device owner xstream
- init xstream

来维持。

### 空间分配与索引一致性

由：

- `VEA` reservation / publish 模型
- SCM 上的 allocation metadata
- 与 VOS index 的单事务提交

来维持。

### 数据传输与设备执行一致性

由：

- `BIO`
- DMA-safe buffer
- SPDK async I/O

来维持。

所以这一层真正的设计思想不是“把 NVMe 用起来”，而是：

- **把设备、空间、事务三个维度各自安放到最合适的层里。**

## 一个更实用的阅读方法：把 VOS 之下再拆成三层

以后你再往 `src/bio` 和 `src/vea` 里读时，最好的方法不是直接陷进 SPDK 调用细节，而是先问自己现在看到的是哪一层问题。

### 第一层：设备与执行环境层

看的是：

- NVMe
- SPDK
- blobstore / blob
- I/O channel
- owner/init xstream

这一层回答“谁在控制设备和执行 I/O”。

### 第二层：本机持久设备元数据层

看的是：

- `SMD`
- device table
- pool table
- state / blob mapping / replacement

这一层回答“server 如何记住本地设备与 blob 状态”。

### 第三层：blob 内部空间管理层

看的是：

- `VEA`
- reserve / cancel / publish / free
- hint
- aging / fragmentation

这一层回答“blob 内部块空间如何为 VOS 服务”。

只要这样分层，BIO / VEA 就会好读很多。

## 小结

`BIO`、`VEA` 和 NVMe/SPDK 这一层，是理解“VOS 之下还有什么”的关键收束。

其中：

- `BIO` 负责把 DAOS 本地 I/O 语义接到 SPDK / NVMe 执行栈
- `SMD` 负责持久记录 server 本地 device / target / blob 映射和状态
- `DMA-safe buffer` 负责打通 RDMA 与 NVMe DMA 两条搬运路径
- `device owner xstream` 和 `init xstream` 负责收束设备级并发与生命周期管理
- `VEA` 负责在 SPDK blob 内做细粒度、事务友好的 extent 分配

如果把这一篇压缩成一句话，那就是：

**VOS 解决“本地版本化存什么”，BIO 解决“怎么和 NVMe 设备打交道”，VEA 解决“在 blob 里把空间分到哪里”，三者拼起来，DAOS 才真正完成从对象语义到底层块设备的闭环。**

## 下一篇看什么

存储内核阶段到这里可以先收一收，下一步最自然的是回到客户端入口，补上“请求是怎么进来的”：

**`libdaos`、`libdfs` 与客户端初始化：用户请求如何进入 DAOS**

因为走完服务端和存储端之后，再回头看客户端初始化、API 分层和 agent 角色，会更容易形成完整全链路视图。
