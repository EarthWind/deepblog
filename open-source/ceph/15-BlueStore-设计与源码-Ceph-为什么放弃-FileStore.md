# BlueStore 设计与源码：Ceph 为什么放弃 FileStore

## 这篇文章要解决什么问题

前面几篇我们已经把 Ceph 的数据面主线拆得比较完整了：

- 客户端请求怎么进来
- `OSD` 怎么组织执行
- `PG` 怎样承担一致性和恢复边界
- 消息层如何把节点间通信串起来

但这些主线最后都会落到同一个终点问题上：

**OSD 真正把数据写到本地的时候，底下到底是什么？**

在今天的 Ceph 里，这个问题的主角基本就是：

- `BlueStore`

可如果你稍微了解一些 Ceph 历史，又会立刻碰到另一个问题：

- Ceph 以前不是有 `FileStore` 吗？
- 为什么后来要转向 `BlueStore`？
- `BlueStore`、`BlueFS`、`RocksDB`、`block.db`、`block.wal` 这些东西之间到底是什么关系？
- 为什么明明已经有 `BlueStore` 了，里面还会出现一个“小文件系统” `BlueFS`？

如果只允许用一句话先给结论，那就是：

**BlueStore 是 Ceph 为 OSD 工作负载重新设计的本地对象存储后端：它直接管理裸块设备，把对象数据和元数据事务拆开处理，用 RocksDB 保存元数据，再用 BlueFS 作为 RocksDB 的内部文件系统，从而绕开传统文件系统那层性能和复杂度包袱。**

这一篇的目标，就是把这句话彻底展开。

## 先建立第一条边界：BlueStore 不是“又一个文件系统”，而是 OSD 的对象存储后端

这是理解 BlueStore 的第一步。

很多人第一次听到 BlueStore，会下意识把它想成：

- Ceph 自己造的文件系统

这个理解不够准确。

更准确地说：

- `BlueStore` 是 OSD 的本地对象存储后端

也就是说，它解决的是：

- OSD 怎样把对象数据和相关元数据存到本地设备上

### 它和普通文件系统的区别是什么

普通文件系统主要服务的是：

- 通用文件/目录语义

而 BlueStore 服务的是：

- Ceph OSD 的对象存储工作负载

这也是官方文档为什么会强调：

- 它是专门为 Ceph OSD 工作负载设计的后端

所以理解 BlueStore 时，一定不要先把它套进“ext4/xfs 替代品”这个框里。

更好的理解是：

- 它是 Ceph 为 OSD 本地持久化专门设计的一套存储引擎

## 第 1 层：为什么 Ceph 要放弃 FileStore

这一层是全篇最重要的动机问题。

如果不先理解 FileStore 的局限，后面你就很难真正理解 BlueStore 为何长成现在这样。

### FileStore 的基本思路是什么

可以先粗略理解成：

- 对象数据放在传统文件系统里
- OSD 在上层再维护自己的日志和元数据逻辑

比如历史上常见的组合就是：

- `FileStore + XFS`

### 这个路线的问题在哪

Ceph 文档对这个问题说得很直接：

- FileStore 依赖传统文件系统保存对象数据，存在一系列性能缺陷

换句话说，Ceph 在 FileStore 路线下，等于把 OSD 工作负载建立在：

- 通用文件系统

之上。

这会天然带来几个问题。

## 问题一：多了一层通用文件系统抽象，开销和复杂度都更高

Ceph OSD 的工作负载不是普通“用户文件存取”。

它关心的是：

- 对象写入
- 小写放大
- 日志提交
- 元数据查找
- 校验
- 压缩
- 恢复和回填时的大量后台读写

如果这些操作先要绕过一层通用文件系统，再到底层块设备，天然就会带来：

- 更多路径长度
- 更多元数据开销
- 更多难以控制的写放大

也就是说，FileStore 的问题不只是“慢一点”，而是：

- OSD 的工作负载和传统文件系统的抽象模型不完全匹配

## 问题二：Ceph 想要精确控制写入、校验、压缩和空间布局，但 FileStore 模型不够直接

随着 Ceph 演进，它越来越需要直接掌控：

- 数据落盘方式
- 校验和
- 压缩策略
- 元数据组织
- 设备分层布局

如果对象数据都先变成：

- 文件系统上的文件

那么很多事情都会被文件系统那层间接化。

Ceph 社区后来对 BlueStore 的描述里，反复强调的一点就是：

- 它直接管理裸设备

这句话的含义不是“听起来更底层更酷”，而是：

- Ceph 希望真正把 OSD 的数据布局掌握在自己手里

## 问题三：性能和健壮性目标逼着 Ceph 走向更专用的后端

迁移文档和后续版本文档里给出的官方判断非常明确：

- BlueStore 在性能和健壮性上优于 FileStore
- 到 Reef 时 FileStore 已不再受支持

这不是简单的“新版本换新实现”，而是说明：

- Ceph 最终认为 FileStore 路线已经不再适合作为主流未来

换句话说，BlueStore 的出现不是锦上添花，而是：

- Ceph 本地存储后端的一次架构换代

## 所以 BlueStore 的设计目标到底是什么

把前面这些问题压缩起来，BlueStore 的设计目标可以总结成三句话：

### 1. 直接管理块设备

尽量去掉传统文件系统这层中间抽象。

### 2. 把对象数据和元数据事务分开组织

避免把所有东西都塞进同一种存储路径里。

### 3. 为 OSD 工作负载做专用优化

包括：

- 校验
- 压缩
- 更好的写路径控制
- 更适合 SSD/HDD 分层部署

所以理解 BlueStore 时，最重要的心智切换就是：

- 它不是“把 FileStore 代码写得更快”
- 它是“重新定义 OSD 本地存储后端”

## 第 2 层：BlueStore 的核心思想不是“一个统一大盘”，而是“数据和元数据分治”

这是 BlueStore 设计里最关键的结构感。

如果只用一句话概括：

**BlueStore 把一次对象事务拆成两类东西：对象数据 IO 和元数据 KV 事务。**

这一步非常重要，因为它决定了后面一切组件关系。

### 对象数据是什么

比如：

- 真正的对象内容
- extent 数据
- 大块用户数据

### 元数据是什么

比如：

- 对象名到物理位置的映射
- onode / blob / freelist 之类的内部状态
- 统计和分配相关信息

BlueStore 的关键设计，不是让这两类东西继续混在同一个通用文件系统语义里，而是：

- 数据走自己的设备与 IO 逻辑
- 元数据走 KV 路线

这就是后面为什么会自然出现：

- `RocksDB`

## 第 3 层：为什么 BlueStore 需要 RocksDB

很多人第一次接触 BlueStore 会问：

- 既然 BlueStore 都已经直接管理块设备了，为什么里面还要嵌一个 RocksDB？

答案很简单：

- 因为 BlueStore 仍然需要一套高效、可事务化的元数据 KV 存储

文档里也明确说了：

- BlueStore 的内部元数据保存在嵌入式 RocksDB 里

### 这意味着什么

意味着 BlueStore 不是一个“纯裸块顺序写系统”，它依然需要管理大量内部元数据，比如：

- 对象到磁盘块位置的映射
- 分配器状态
- 各种内部对象状态和统计

这些东西非常适合落到：

- KV 数据库

而 RocksDB 正好提供了：

- 嵌入式
- 成熟
- 高性能
- 批处理事务友好

的元数据存储能力。

所以这里要建立一个非常关键的认知：

- BlueStore 不等于“自己重写所有元数据数据库”
- 它选择把元数据管理建立在 RocksDB 之上

## 第 4 层：BlueFS 为什么会出现

讲到这里，自然会冒出下一个问题：

- 好，BlueStore 用 RocksDB 管元数据
- 那 RocksDB 文件又放在哪里？

这时就轮到：

- `BlueFS`

出场了。

### 最容易误解的一点

很多人一看到 `BlueFS`，就会觉得：

- 这不还是文件系统吗？
- 那不是又绕回去了吗？

这正是最容易误解的地方。

更准确地说：

- BlueFS 不是给用户用的通用文件系统
- 它是 BlueStore 为 RocksDB 准备的内部小文件系统

也就是说，它的存在不是为了“重新回到 FileStore”，而是为了：

- 给 RocksDB 提供一个受 BlueStore 控制的文件环境

这个区别非常关键。

## 为什么不直接让 RocksDB 落在 ext4/xfs 上

如果走这条路，你就又把 RocksDB 文件 IO 放回了：

- 通用文件系统

这和 BlueStore 想要摆脱 FileStore 路线的核心动机就冲突了。

所以 BlueStore 的选择是：

- 我接受 RocksDB 作为 KV 数据库
- 但 RocksDB 的文件 IO 仍然要在我自己的控制范围内

这就是 BlueFS 的价值。

它不是“再套一层通用文件系统”，而是：

- 为 RocksDB 提供一个最小、受控、面向 BlueStore 设备布局的内部文件系统

所以你可以把这句话直接记下来：

**BlueFS 不是给 Ceph 用户挂载的文件系统，而是给 RocksDB 用的内部文件系统。**

## 第 5 层：BlueStore、BlueFS、RocksDB 三者到底是什么关系

如果这一篇只记一张结构图，我建议你记这张：

```text
BlueStore
  |
  +-- 主对象数据 on block
  |
  +-- 元数据事务 -> RocksDB
                       |
                       v
                    BlueFS
                       |
                       v
         block.db / block.wal / 主设备中的相关区域
```

这张图里最关键的关系是：

### `BlueStore`

- 是 OSD 本地对象后端

### `RocksDB`

- 是 BlueStore 的元数据 KV 数据库

### `BlueFS`

- 是 RocksDB 背后使用的内部文件系统环境

所以三者不是并列竞争关系，而是：

- 一层包一层、各司其职

## 第 6 层：`block`、`block.db`、`block.wal` 到底分别是什么

这几个名字是运维里最常见、也是最容易讲乱的。

先给出最实用的理解：

### `block`

- BlueStore 的主设备
- 主要放对象数据

### `block.db`

- 优先给 RocksDB 元数据使用的更快设备

### `block.wal`

- 专门给更小、更快的 WAL/journal 类写入路径使用的设备

### 为什么这样分层有意义

因为对象数据和元数据的访问模式并不相同。

很多时候，更贵更快的设备空间应该优先让给：

- RocksDB / metadata 路径

而不是全部拿去堆大块对象数据。

这也是 BlueStore 相比旧路线更有工程弹性的地方：

- 它允许你把不同类型的本地持久化需求放到不同速度层上

## 一个特别容易误解的点：为什么通常更推荐优先给 `block.db`

文档里对这一点说得很清楚：

- 如果有足够快盘空间，通常优先考虑 `block.db`
- 因为如果有 `block.db` 而没有单独 `block.wal`，WAL 也会隐式和 DB 共置在更快设备上

这意味着：

- 单独给 WAL 不一定总是收益最大
- 更大的元数据设备往往更有综合价值

这类设计细节也能反过来说明：

- BlueStore 不是简单一块盘写到底
- 它在设备布局上是高度有策略感的

## 第 7 层：BlueStore 的写路径为什么能概括成“数据 IO + KV 提交”

如果从源码看，一条最适合讲的主线入口就是：

- `BlueStore::queue_transactions()`

这几乎可以看成 BlueStore 的本地事务总入口。

### 为什么这个入口特别适合讲解

因为它把 BlueStore 的核心设计哲学完整体现出来了：

- 一次上层事务进来
- 不是直接变成一个“写文件”动作
- 而是进入一个更复杂的提交过程

这里最关键的载体就是：

- `TransContext`

你可以把它理解成：

- BlueStore 的一次提交控制块

它同时承载：

- AIO 相关上下文
- KV 事务对象
- 修改过的对象/节点状态
- 分配与释放信息

这说明 BlueStore 真正处理的是：

- 一个事务生命周期

而不是：

- 一个简单写系统调用

## 第 8 层：`TransContext` 为什么值得重点讲

这是 BlueStore 源码里非常值得建立心智模型的对象。

如果说 OSD 那边最重要的抽象是：

- PG / PeeringState

那么 BlueStore 这边最值得记住的控制块之一就是：

- `TransContext`

### 为什么它重要

因为它把一次提交里所有关键状态都放在一起：

- 数据 IO 何时发
- AIO 何时完成
- 哪些 onode / blob 被改了
- 哪些分配区间被占用或释放
- 哪个 KV 事务正在积累

也就是说，BlueStore 不只是“接收事务”，而是：

- 用 `TransContext` 显式组织事务生命周期

这一点特别重要，因为它解释了为什么后面会有一整套状态推进逻辑。

## 第 9 层：BlueStore 的写路径不是一个点动作，而是一个状态机

这一层非常值得讲，因为它和我们前面在 `PG`、`PeeringState` 里看到的设计风格很一致。

BlueStore 的事务推进并不是：

```text
收到事务 -> 写盘 -> 完成
```

而更像：

```text
PREPARE
  -> AIO_WAIT
  -> IO_DONE
  -> KV_QUEUED / KV_SUBMITTED
  -> KV_DONE
  -> FINISHING
  -> DONE
```

### 为什么这样设计很合理

因为 BlueStore 一次提交同时涉及：

- 数据区写入
- 元数据 KV 更新
- 顺序控制
- 完成回调

这些事情天然不可能用一个瞬间完成。

所以 BlueStore 也延续了 Ceph 一贯的系统设计风格：

- 把复杂过程组织成显式状态推进

## 第 10 层：为什么 AIO 完成和 KV 提交要分开看

这一步非常关键，它正好体现了 BlueStore 的核心结构。

### AIO 完成表示什么

- 相关数据 IO 已经完成到某个阶段

### KV 提交表示什么

- 相关元数据事务已经写进 RocksDB

这两者显然不是一回事。

而 BlueStore 之所以能更清楚地组织这条路径，正是因为它一开始就把：

- 数据
- 元数据

分成了两套处理逻辑。

这就是为什么我们前面一直强调：

- BlueStore 的核心思想不是“少一层文件系统”
- 而是“重构本地持久化模型”

## 第 11 层：`db->submit_transaction()` 是 BlueStore 元数据真正进入 RocksDB 的关键点

从代码主线上看，一个非常值得抓的落点是：

- `db->submit_transaction(...)`

### 为什么这里重要

因为它是 BlueStore 元数据事务真正进入 KV 后端的关键桥。

从抽象上看，BlueStore 上层只需要面对：

- `KeyValueDB::Transaction`

然后再由底层 RocksDB 实现把它真正映射成：

- `WriteBatch`
- `db->Write(...)`

这一步特别适合讲给读者，因为它说明：

- BlueStore 并不直接依赖 RocksDB 的所有具体细节
- 中间仍然保留了一个 KV 抽象层

这也是 Ceph 常见的工程风格：

- 先有抽象接口
- 再由当前主流实现落地

## 第 12 层：BlueFS 和 RocksDB 是怎样真正接上的

前面我们已经在概念上说了：

- RocksDB 文件最终通过 BlueFS 承载

如果从实现角度看，这个桥梁的关键就是：

- `BlueRocksEnv`

### 它的作用是什么

可以先非常直接地理解：

- 它是 RocksDB 访问 BlueFS 的适配层

也就是说，RocksDB 原本期望自己能通过一套文件环境接口去：

- 打开文件
- 读文件
- 写文件
- 删除文件
- 重命名文件

而 `BlueRocksEnv` 就负责把这些 RocksDB 期望的文件 API，翻译成：

- BlueFS 的对应调用

这一步非常关键，因为它把前面那句抽象关系真正落到了代码上：

- RocksDB 逻辑上是 KV 数据库
- 物理文件环境却不再是 ext4/xfs，而是 BlueFS

## 第 13 层：为什么 BlueFS 最多会碰到三类设备

在源码和文档里，你会看到 BlueFS 相关逻辑经常围绕三类块设备展开：

- `BDEV_DB`
- `BDEV_WAL`
- `BDEV_SLOW`

这其实就对应了我们前面讲的设备布局思想：

### 更快的小设备

- 适合放 WAL
- 或更关键的 RocksDB 热路径内容

### 更大的快设备

- 适合承载 DB

### 慢但容量大的区域

- 可以作为 spillover 或主数据区的一部分

这说明 BlueFS 本身也不是一个“抽象黑盒文件系统”，而是非常理解：

- BlueStore 设备分层布局

的内部实现。

## 第 14 层：BlueStore 为什么能比 FileStore 更像“为 OSD 而生”的后端

如果把这篇所有组件关系收回来，你会发现 BlueStore 比 FileStore 更“像 OSD 后端”的原因主要有四个：

### 1. 它直接面向对象存储工作负载设计

而不是通用文件目录语义。

### 2. 它直接管理块设备

不再把关键路径交给传统文件系统间接控制。

### 3. 它把数据和元数据分治

对象数据走主设备路径，元数据走 RocksDB/KV 路线。

### 4. 它允许更灵活的设备布局

`block`、`block.db`、`block.wal` 让不同类型的 IO 能落到更合适的介质上。

这四点合起来，才是 BlueStore 替代 FileStore 的真正底层原因。

## 用一句话重新概括 BlueStore / BlueFS / RocksDB 的关系

如果你想记最压缩版本，可以记成下面这样：

### `BlueStore`

- 管对象数据和本地事务总流程

### `RocksDB`

- 管 BlueStore 的内部元数据 KV

### `BlueFS`

- 管 RocksDB 文件的底层文件环境

### `BlueRocksEnv`

- 把 RocksDB 文件接口翻译成 BlueFS 调用

这四句几乎就构成了这篇最重要的结构骨架。

## 把 BlueStore 的写路径压缩成一条最短主线

如果你只想记一条写路径骨架，可以记成下面这样：

```text
OSD 把事务交给 BlueStore::queue_transactions()
  ->
创建 TransContext
  ->
准备数据 IO 与对象/节点修改
  ->
AIO 完成
  ->
把元数据修改整理成 KV 事务
  ->
db->submit_transaction() 提交到 RocksDB
  ->
RocksDB 文件 IO 通过 BlueRocksEnv 走 BlueFS
  ->
BlueFS 再落到 block.db / block.wal / 主设备相关区域
  ->
事务完成并收尾
```

这条链一旦记住，后面你看：

- `queue_transactions`
- `TransContext`
- `_txc_apply_kv`
- `BlueRocksEnv`
- `BlueFS`

这些关键点时就不会散。

## 初学者最容易混淆的 9 个点

### 1. 认为 BlueStore 就是 Ceph 版通用文件系统

不对。它是 OSD 的对象存储后端。

### 2. 认为 BlueStore 只是 FileStore 去掉 XFS 的简单重写

不对。它重构了数据和元数据的本地持久化模型。

### 3. 认为 BlueStore 不需要任何数据库

不对。它依然需要 RocksDB 这种 KV 数据库来管理内部元数据。

### 4. 认为 BlueFS 是给用户挂载的文件系统

不对。它主要是 RocksDB 的内部文件系统环境。

### 5. 认为 RocksDB 仍然直接跑在 ext4/xfs 上

不对。在 BlueStore 路线里，它通常通过 BlueFS/BlueRocksEnv 工作。

### 6. 认为 `block.wal` 总是比 `block.db` 更值得优先单独配置

不对。很多情况下优先给 `block.db` 更有综合收益。

### 7. 认为 BlueStore 写路径就是一个 write 调用

不对。它是 `TransContext` 驱动的一整套事务生命周期。

### 8. 认为 AIO 完成就等于元数据事务也提交了

不对。数据 IO 和 KV 提交是两件不同的事。

### 9. 认为 BlueStore 的重点只有性能

不对。它同样是在解决控制权、复杂度、设备分层和健壮性问题。

## 这一篇最应该留下的 5 个直觉

### 直觉一：BlueStore 是为 OSD 工作负载专门设计的后端

不是通用文件系统替代品。

### 直觉二：BlueStore 的核心设计是“数据和元数据分治”

这是理解后面所有组件关系的前提。

### 直觉三：RocksDB 解决的是元数据 KV 问题，BlueFS 解决的是 RocksDB 文件承载问题

这两层不能混。

### 直觉四：`block`、`block.db`、`block.wal` 体现了 BlueStore 的设备分层思想

不是部署层面的偶然命名。

### 直觉五：BlueStore 写路径的本质是事务状态机，不是单点写入动作

`TransContext` 是抓住这条线的关键。

## 下一篇看什么

既然这一篇已经把：

- BlueStore 为什么替代 FileStore
- BlueStore / RocksDB / BlueFS 的关系
- `block` / `block.db` / `block.wal` 的意义
- 本地事务如何真正推进

这条主线讲清楚了，下一步最自然的事情，就是继续把 OSD 层复制协议和 BlueStore 持久化真正拼到一起：

**一次对象写入到底什么时候算 ack，什么时候算 apply，什么时候算 commit？**

所以下一篇建议接：

**《对象写入落盘全过程：事务、日志、提交语义与一致性保证》**
