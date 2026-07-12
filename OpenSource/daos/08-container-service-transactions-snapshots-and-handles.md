# 08. Container 服务：事务、快照和句柄状态为什么落在这一层

## 为什么到了这一篇，Container 不能再被理解成“目录”

如果只从名字看，很多人第一次接触 DAOS 时，会很自然地把 container 理解成：

- pool 里的一个逻辑子目录
- 一个比 pool 更细粒度的命名空间
- 一个方便隔离对象的壳

这种理解不能说完全错，但远远不够。

因为只要顺着 `src/container/README.md` 和 `src/container` 里的服务端代码往下看，你很快就会发现，container 在 DAOS 里承担的职责明显重得多：

- 它定义对象地址空间边界
- 它承载 handle 状态
- 它承载 epoch state
- 它承载 snapshot 元数据
- 它还是 OID allocator 的挂载点

这就说明，container 不是“对象之上的一层包装”，而是 DAOS 里非常核心的**事务与版本视图边界**。

所以这一篇要回答的核心问题有三个：

1. Container 为什么不只是逻辑隔离，而是事务和快照的承载层？
2. container handle、epoch state、snapshot 分别由谁管理？
3. OID allocator 为什么会挂在 container 层，而不是 object 层或 pool 层？

## 先给结论：Container 是 DAOS 的版本视图边界

`src/container/README.md` 开头先给了一个最基础的定义：

- container 表示 pool 内部的一个 object address space
- 应用想访问 container，必须先连 pool，再 create 或 open container
- 成功后会拿到 container handle

光看这几句，其实就已经能看出 container 的关键地位：

- 它不是独立于 pool 的资源单位，而是建立在 pool 之上的对象地址空间
- 访问 object 之前，必须先进入 container
- container handle 不只是标识符，还携带 capability

也就是说，container 把下面几件事绑到了一起：

- “我在访问哪一组对象地址空间”
- “我以什么权限访问”
- “我当前的版本/事务视图如何定义”

因此，如果 pool 是资源边界，那么 container 更像是：

**建立在 pool 之上的数据视图边界、事务边界和句柄边界。**

## 第一层：Container 为什么不是简单隔离层

### 它定义的是对象地址空间，不只是名字空间

README 写得很明确：

- container represents an object address space inside a pool

这句话非常重要。因为它说明 container 不是拿来做“目录树管理”的，而是拿来定义：

- 哪一批 object ID 属于同一个逻辑空间
- 哪一组事务和快照语义落在一起
- 哪些 handles 共享同一份容器级元数据

这和传统文件系统目录的职责并不一样。

### 它同时持有持久属性和运行态句柄状态

`src/container/README.md` 里的 metadata layout 说明，Container Service 的 top-level `root` 下有两个核心子 KVS：

1. Containers KVS
2. Container Handles KVS

前者存的是每个 container 的属性 KVS。

后者存的是由应用打开出来的 container handle 元数据，按 handle UUID 索引，其中包括：

- capability
- per-handle epoch state

这件事一旦看明白，就能理解为什么 container 不是轻量命名空间：

- 它既有“container 本身”的持久元数据
- 也有“这个 container 当前被哪些应用以何种句柄打开”的运行态元数据

也就是说，container 这一层同时承接了：

- 静态属性
- 活跃句柄
- 事务/epoch 相关状态

## 第二层：为什么事务和快照落在 Container 层

这其实是整篇最重要的问题。

### README 已经把答案写出来了

`src/container/README.md` 在 Epoch Protocol 一节里直接说明：

- Container service 管理 container 的 epochs
- definitive epoch state 是 container metadata 的一部分
- target service 对全局 epoch state 所知较少
- epoch commit、discard、aggregate 都由 container service 驱动

这几句话基本可以直接作为结论来记：

**事务/epoch 的全局真相在 container service，不在 object 层，也不在 target 本地缓存里。**

### 为什么不放在 object 层

因为 object 层关注的是：

- object model
- dkey / akey
- placement
- data protection

而 epoch、snapshot、handle 这些语义，天然需要一个比单个 object 更高、但又比整个 pool 更细的边界。

这个边界正好就是 container。

如果把事务状态下沉到 object 层，会马上出现几个问题：

- 同一应用对 container 内多个对象的版本视图难以统一
- snapshot 很难表达“这个 container 当前的可读历史断点”
- handle 的 capability 和 epoch state 无法自然绑定

### 为什么也不放在 Pool 层

pool 太粗了。

pool 解决的是：

- targets 资源集合
- pool map
- pool handle
- 集群级资源和复制服务元数据

而 container 解决的是：

- 某个对象地址空间内部的事务和快照语义
- 对这个空间的细粒度句柄与 capability

所以 container 恰好卡在一个非常合适的位置：

- 比 pool 细
- 比 object 粗

因此，它天然适合作为事务与版本视图边界。

## Container Service 的元数据到底长什么样

### `cont_svc` 和 `pool_svc` 一样，也是 replicated service

`src/container/README.md` 说得很清楚：

- container metadata 以层次化 KVS 组织
- 这些 KVS 在多个 server 上复制
- 背后依赖 Raft 和 strong leadership
- `cont_svc` 派生自 `rsvc`

也就是说，和上一篇的 Pool Service 一样，Container Service 也不是单副本本地状态，而是：

- leader 负责真正处理请求
- followers 负责复制状态并在必要时给 leader hint

这也解释了为什么 container 的事务和 snapshot 元数据可以成为“全局真相”。

### 顶层元数据分成“容器属性”和“句柄状态”两大块

按照 README 的图示和描述：

- `Containers KVS` 存 container properties KVS，按 container UUID 索引
- `Container Handles KVS` 存 handle 元数据，按客户端生成的 handle UUID 索引

这两层的分工非常清晰：

- container properties 是“容器本身”的元数据
- container handle store 是“当前访问视图”的元数据

这种设计直接把“实体”和“会话/句柄”分开了。

### snapshot 也是容器级元数据

README 进一步说明：

- 用户可以创建、删除、查询 persistent snapshots
- snapshot 本质上是不会被 aggregation 清掉的 epoch
- container 可以回滚到某个 snapshot

这说明 snapshot 不是 object 级标签，也不是 pool 级全局断点，而是：

- 属于某个 container 的历史可见性边界

所以从语义上说，snapshot 和 container 是强绑定的。

## `cont_module` 也说明 Container 是真正的服务模块

从 `src/container/srv.c` 可以看到 `cont_module` 的定义：

- `sm_name = "cont"`
- `sm_mod_id = DAOS_CONT_MODULE`
- `sm_init = init`
- `sm_proto_fmt`
- `sm_handlers`
- `sm_key = &cont_module_key`

并且 `cont_module_key` 的 TLS 初始化里会创建：

- per-thread container cache
- per-thread container handle hash

这件事很重要，因为它说明 container 不只是 replicated metadata service，还有明确的 target 侧和执行流本地缓存结构。

从运行时角度看，Container 模块至少同时覆盖三层：

- service 级元数据
- IV 传播状态
- per-thread target 本地缓存

## `CONT_OPEN` 为什么能说明 Container 的真实职责

如果只选一条链路来理解 container，最值得看的就是 `daos_cont_open` 对应的服务端流程。

### 协议层先把信息暴露出来了

从 `src/container/rpc.h` 可以看到，`CONT_OPEN` 的输入输出非常有代表性。

输入里包括：

- pool handle UUID
- container UUID
- container handle UUID
- open flags
- property query bits

输出里包括：

- queried properties
- latest snapshot
- snapshot count
- number of open handles
- metadata open/modify times

也就是说，`CONT_OPEN` 不是一个“仅仅返回成功”的 RPC，它在语义上已经说明 open 的结果是一整套容器级视图：

- 权限成立
- handle 成立
- 属性可见
- 快照状态可见
- 活跃句柄数量可见

### `ds_cont_op_handler()` 是统一入口

`src/container/srv_container.c` 里，客户端相关的 container 请求最终统一进入：

- `ds_cont_op_handler(crt_rpc_t *rpc)`

而在更底层，`cont_op_with_cont()` 会根据 opcode 分发：

- `CONT_OPEN`
- `CONT_CLOSE`
- `CONT_DESTROY`
- snapshot/epoch/property/ACL 等操作

这说明 DAOS 在 container 层的设计不是“每个动作各走一套完全独立的服务入口”，而是围绕一个统一的 container service 状态机去分发不同语义操作。

## `cont_open()` 真正做了什么

### 1. 先检查 handle 是否已经存在且是否冲突

`srv_container.c` 中的 `cont_open()` 一开始就会检查：

- 当前 `ci_hdl` 对应的 container handle 是否已经存在

如果存在，还要检查：

- `ch_flags` 是否和当前请求 flags 冲突

冲突则直接返回：

- `-DER_EXIST`

这一步说明 container handle 不是随便生成出来的 token，而是被容器服务明确持久跟踪和约束的状态。

### 2. 再读取 container properties 做权限和状态判断

之后 `cont_open()` 会读取完整属性：

- `cont_prop_read(tx, cont, DAOS_CO_QUERY_PROP_ALL, &prop, false)`

然后基于这些属性去判断：

- 布局版本要求
- 容器状态是否健康
- 是否允许当前 open flags
- 安全 capability 是否满足

这说明 container open 的核心不只是“能不能拿个 handle”，而是“能不能在当前 container 状态和属性约束下建立一个合法视图”。

### 3. 更新 metadata time，并把属性推到 IV

接下来它会：

- 更新/读取 metadata open time、modify time
- 调用 `cont_iv_prop_update(...)`

这一段很值得注意，因为它表明：

- RDB 里的 container 属性是权威持久状态
- target 侧和其他执行路径需要通过 IV 获得更新后的属性视图

也就是说，container open 不只是 service 内部行为，还会推动属性状态向运行时传播。

### 4. 再把 capability 推到 IV

紧接着代码会调用：

- `cont_iv_capability_update(...)`

把：

- handle UUID
- container UUID
- flags
- security capabilities

更新到 IV。

这说明 container handle 的权限信息不是只存在 RDB 里，也不是只留在客户端，而是要通过 IV 传播到 target 侧，让后续数据访问路径知道这个 handle 具备什么能力。

### 5. 最后把 handle 正式写入元数据

只有前面这些步骤都通过之后，`cont_open()` 才会真正把 handle 写入：

- `cs_hdls`
- `c_hdls`

并在回复里带回：

- `snap_count`
- `latest snapshot`
- `nhandles`
- requested properties

所以 open 的真实含义不是“生成一个 UUID”，而是：

**在容器服务里创建一个新的可追踪、可传播、带 capability 和版本视图信息的访问会话。**

## `CONT_CLOSE` 反过来说明 handle 状态为什么必须在 Container 层

`srv_container.c` 里的 close 路径也很有代表性。

### 关闭不只是删一条本地缓存

`cont_close_recs()` 先做的事之一是：

- `cont_iv_capability_invalidate(...)`

也就是先把 capability 从 IV 里失效掉。

然后 `cont_close_hdls()` 才会在 RDB 事务里：

- 更新 nhandles
- 从 `c_hdls` 删除
- 从 `cs_hdls` 删除

这说明 close 的本质是三层同步收尾：

- 运行时 capability 失效
- 容器级 handle 状态删除
- 句柄计数与相关元数据更新

如果 handle 不是由 container service 统一管理，这一套就很难保持一致。

## snapshot 为什么一定要落在 Container Service

### README 已经给出语义定义

snapshot 在 README 里被定义为：

- 不会被 aggregation 清掉的 epoch
- 一直到显式 destroy 之前都可读
- container 可 rollback 到某个 snapshot

这一定义本身就要求：

- snapshot 必须有容器级统一元数据视图
- 不能只存在于 target 本地

### `srv_epoch.c` 展示了 snapshot 的真实元数据流

在 `src/container/srv_epoch.c` 里，可以看到：

- snapshot 列表从 RDB 里读
- `c_snaps` KVS 负责持久存储 snapshot epochs
- `nsnapshots` 会被增减更新
- OIT OID 也会在 snapshot 创建时一起生成和写入

例如：

- `read_snap_list()` 通过 `rdb_tx_iterate()` 读取快照列表
- `snap_create_bcast()` 在更新 `c_snaps` 的同时更新 `nsnapshots`

这说明 snapshot 的“存在性”和“数量”都是明确的容器元数据。

### snapshot 创建还会通知 target

`snap_oit_create()` 里会：

- 创建 `CONT_TGT_SNAPSHOT_NOTIFY`
- 广播到 targets

然后再把：

- snapshot epoch
- OIT OID

记录进 RDB。

这里的分工特别清楚：

- Container Service 决定 snapshot 元数据
- target 侧收到通知后调整本地运行态和后续聚合行为

所以 snapshot 不是“先有 target 局部状态，再回填服务元数据”，而是：

- 服务层主导
- target 侧配合

### target 侧聚合也依赖 container snapshot 视图

从 `srv_target.c` 可以看到，target 侧在做聚合时会关注：

- snapshot list 是否已经刷新
- 哪些 epoch 被 snapshot 卡住不能聚合

也就是说，target 侧并不拥有 snapshot 的权威定义，它依赖的是 container service 传播下来的 snapshot 视图。

这再次证明：

**snapshot 的权威归属在 container service。**

## epoch protocol 为什么也落在这一层

README 对 epoch protocol 的描述可以压缩成一句话：

- target 侧急切写入 VOS
- container service 维护 definitive epoch state
- commit / discard / aggregate 由 container service 驱动

这背后的设计意图很清晰。

### target 负责本地写入承接

target 侧更接近 VOS，本地 I/O 到来时可以快速把更新记进对应的 VOS container。

这有利于：

- 并发写入
- 就地记录版本化更新

### container service 负责“全局是否成立”

但某个 epoch 最终是否：

- commit
- discard
- 被 snapshot 固化
- 可被 aggregate 清理

这些事情不能只看某个 target 本地结果，而必须由 container service 统一定义。

所以最准确的理解应该是：

- target 承接版本化写入
- container service 决定这些写入最终在全局视图里意味着什么

这就是为什么 epoch protocol 落在 container 层，而不是直接落在 VOS 内部。

## OID allocator 为什么挂在 Container 层

这也是一个非常适合讲清楚层次边界的问题。

### README 的解释很直接

`src/container/README.md` 说：

- OID allocator 用来在一个 container 内部分配唯一的一组 64 位整数
- 最大已分配 ID 会保存在 container properties KVS 里

这已经说明了关键点：

- OID 的唯一性范围是 container 内
- 因此 allocator 的元数据最自然地挂在 container metadata 上

### 代码也验证了这一点

`srv_container.c` 里的：

- `ds_cont_oid_fetch_add(...)`

会：

1. 找到 container service leader。
2. 开启 RDB 事务。
3. 查找目标 container。
4. 从 `cont->c_prop` 里读取 `ds_cont_prop_alloced_oid`。
5. 返回当前值并按请求数量递增。
6. 再把新值写回 RDB。

这说明 OID allocator 本质上就是一条 container metadata update 路径。

### 为什么不放在 object 层

因为 object 还没创建之前，你就可能需要先分配 OID。

而 allocator 本身要保证的是：

- 对整个 container 地址空间的唯一性

这个范围明显比单个 object 更大。

### 为什么也不放在 Pool 层

如果放在 pool 层，就会把所有 container 的 OID 空间绑在一起，既不必要，也会放大争用范围。

container 正好提供了最自然的唯一性边界：

- 每个 container 一套 alloced_oid
- 每个 container 独立推进自己的 object ID 空间

所以 OID allocator 放在 container 层，既符合语义，也符合扩展性。

## Container 层和 VOS、Object 层的关系

### Container 在语义上高于 VOS

`src/vos/README.md` 说得很清楚：

- VOS 为 DAOS pool 的单个 shard 提供本地 versioning object store
- 一个 vpool 可以容纳多个 containers
- VOS 负责记录对象更新并组织 epoch history

这意味着：

- VOS 是本地 target 上的版本化对象存储后端
- container 是跨 targets、带全局服务语义的容器级视图

换句话说：

- Container 规定“这个视图是什么”
- VOS 承接“这个视图在本地 shard 上如何被存下来”

### target service 把 global container address space 映射到 local VOS container

README 在 Target Service 一节已经明确说：

- target service 会把 global object address space 映射到 target 本地 VOS container
- 它缓存 per-thread container objects 和 open handles

这和 `srv_target.c` 里的逻辑是对得上的。

在 target 侧打开 container handle 时，可以看到它会：

- 查找或创建 thread-local handle
- 打开对应 local container
- 做 `dtx_cont_open(...)`
- 必要时触发 DTX resync

这说明 target 侧关注的是：

- 本地容器句柄缓存
- 本地 VOS container 打开
- 本地 DTX 与校验相关运行态

而不是定义 container 的全局事务真相。

### Object 层则建立在 container 提供的边界之上

object 之后会处理：

- dkey / akey
- 对象布局
- 副本与 EC
- 数据保护和 checksum

但它默认工作在“某个 container 已经成立”的前提之下。

也就是说，对 object 来说，container 像一个已经被定义好的地址空间和事务视图边界。

## 一个更实用的理解方式：把 Container 拆成三层

以后你再读 `src/container` 时，可以先问自己现在读到的是哪一层。

### 第一层：容器实体元数据

看的是：

- properties
- ACL / owner / label
- alloced_oid
- snapshot 列表

这一层对应“container 本身是什么”。

### 第二层：容器句柄与事务视图

看的是：

- container handles KVS
- capability
- per-handle epoch state
- open / close

这一层对应“谁正在以什么视图访问它”。

### 第三层：target 本地运行态

看的是：

- cont IV
- capability IV
- target local handle cache
- local VOS container open
- DTX resync

这一层对应“这个容器视图如何在每个 target 上真正落地”。

把这三层分开，container 相关代码就会容易很多。

## 小结

Container 在 DAOS 里绝不是“pool 里的一个子目录”这么简单。

它更准确的角色是：

- **对象地址空间边界**
- **事务与 epoch 边界**
- **snapshot 视图边界**
- **container handle 与 capability 边界**

这也是为什么：

- `cont_svc` 要作为 replicated service 存在
- handle 状态要落在 container metadata 里
- snapshot 和 epoch 要由 container service 主导
- OID allocator 要挂在 container 层

如果把这篇压缩成一句话，那就是：

**Pool 决定资源范围，Container 决定版本视图和事务范围，VOS 决定这些视图在本地 target 上如何被版本化存储。**

理解了这一层，后面再进入 object 服务时，就更容易明白 object 模型其实是运行在一个已经定义好事务与可见性边界的 container 之上。

## 下一篇看什么

理解完 container 之后，下一步最自然的就是进入对象模型本身：

**Object 服务：从 dkey/akey 到对象类与数据保护**

因为到这一步，我们已经知道对象是“在哪个 pool、哪个 container、以什么版本视图”里被访问的，接下来就该看对象本身如何被布局、复制和校验了。
