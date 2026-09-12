# 07. Pool 服务：集群级资源与元数据是如何组织的

## 为什么从 Pool 开始进入服务层

前面几篇我们已经把 DAOS 的地基铺得差不多了：

- 知道它是分布式异步对象存储。
- 知道它有控制面和数据面双平面。
- 知道 `daos_server` 如何启动，`daos_engine` 如何装载模块。
- 也知道 gRPC、dRPC、CART 三条通信通路分别服务哪条边界。

接下来真正进入服务层时，最适合先写、也最适合先读的模块就是 `pool`。

原因很简单：Pool 在 DAOS 里不是一个“抽象名词”，而是多个核心问题的共同入口：

- 资源是按什么边界组织的？
- targets 怎么被归到一个可用的存储单元里？
- 客户端连上系统后，首先拿到的全局视图是什么？
- pool map、handle、访问权限、service replica 信息放在哪？
- 为什么 pool 元数据不是随便存本地，而是必须走一条强领导复制链？

如果这些问题不先讲清楚，后面再去读 `container`、`object`、`rebuild`，就会经常缺少一个“系统级资源背景板”。

所以这一篇聚焦回答四个问题：

1. Pool 为什么既是资源边界，又是服务边界？
2. Pool Service 和 Pool Target Service 分别负责什么？
3. Pool 元数据为什么依赖 RDB 和 RSVC 这套强领导复制框架？
4. 一个 `daos_pool_connect` 到服务端时，真实流程是怎样串起来的？

## 先给结论：Pool 在 DAOS 里绝不只是“存储池概念”

`src/pool/README.md` 开头对 pool 的定义已经说明了很多事情：

- pool 是一组分布在不同存储节点上的 targets
- 数据和元数据在这些 targets 上分布，以获得横向扩展
- 同时通过复制或 EC 获得可用性和持久性

这段定义至少说明了两点。

第一，Pool 不是某块盘，也不是某个本地目录，而是跨节点 targets 的集合。

第二，Pool 不只是容量容器，它还天然和：

- 数据分布
- 元数据组织
- 容错与可用性

绑在一起。

所以如果把 Pool 只理解成“用户看见的一个逻辑池”，会低估它在源码里的地位。更准确的说法是：

**Pool 同时是资源编组边界、服务管理边界和客户端访问入口边界。**

## 第一层：为什么 Pool 是资源边界

### Pool 把 targets 组织成一个系统级资源集合

在 DAOS 里，真正被系统调度和使用的底层执行单元是 targets，而 Pool 则把一批分散在不同节点上的 targets 组织成一个统一资源集合。

这意味着很多后续能力都要以 pool 为前提：

- 对象布局要基于 pool map 决定 targets 分布
- container 只能建在某个 pool 里
- rebuild 也总是围绕 pool 范围内的 target 状态变化来展开

从这个角度看，Pool 就像 DAOS 里的第一个“系统级资源容器”。

### pool map 是这层资源边界的核心描述

`src/pool/README.md` 里对 metadata layout 的描述已经点出：

- top-level KVS 会存 pool map

这件事非常关键，因为 pool map 实际上就是：

- 当前 pool 包含哪些 targets
- 这些 targets 处于什么状态
- 客户端和服务端后续布局决策要依据什么视图

你可以把 pool map 理解成 Pool 这个资源边界的“可计算描述”。

因此，当客户端建立连接时，服务端会尽早把 pool map 传给客户端。因为没有它，客户端后面很多请求根本没法正确落到数据面。

## 第二层：为什么 Pool 又是服务边界

如果 Pool 只是资源集合，那么理论上它可以只是一份配置或者一张 map。但 DAOS 明显不是这么做的。

`src/pool/README.md` 直接告诉我们：

- Pool Service (`pool_svc`) 存储 pool 元数据
- 它提供查询和更新 pool 配置的 API
- 它的元数据组织成层次化 KVS，并在多个 server 上复制

这意味着 Pool 在 DAOS 里不仅是“资源对象”，还是一个有独立服务职责的模块。

换句话说：

- Pool 不是被动存在的静态配置
- Pool 有自己的 service，有自己的元数据操作语义
- 客户端连 pool、查 pool、改 pool，本质上都是在和 Pool Service 交互

这就是为什么我说 Pool 既是资源边界，又是服务边界。

### `pool_module` 也印证了这一点

从 `src/pool/srv.c` 可以看到 `pool_module` 的定义：

- `sm_name = "pool"`
- `sm_mod_id = DAOS_POOL_MODULE`
- `sm_init = init`
- `sm_setup = setup`
- `sm_cleanup = cleanup`
- `sm_proto_fmt` 和 `sm_handlers`
- `sm_key = &pool_module_key`

这说明 Pool 在数据面并不是一个“普通目录”，而是被 `daos_engine` 真正装载的服务模块。

所以从运行时角度看：

- Pool 有 module
- 有 RPC 协议
- 有 TLS key
- 有 setup/cleanup 生命周期

也就是说，Pool 不是静态资源描述，而是活的服务实体。

## Pool Service 和 Pool Target Service 是怎么分工的

理解 Pool 的关键，不是只盯着 `pool_svc`，而是要看清楚 Pool Service 和目标侧本地状态之间的分工。

### Pool Service 负责什么

从 `src/pool/README.md` 和 `src/pool/srv_pool.c` 看，Pool Service 主要负责的是：

- pool 元数据持久化
- pool map 管理
- handle 状态管理
- 访问控制和连接授权
- 与 service replica 相关的复制状态

更具体一点，Pool Service 更像“控制和元数据中心”。

它关注的是：

- 这个 pool 的元数据是什么
- 当前有哪些连接句柄
- 请求是否有权限
- 当前 leader 是谁
- map 版本是什么

### Pool Target Service 负责什么

而 `src/pool/README.md` 在连接流程部分也说得很清楚：

- Pool Service 在 connect 成功后，会把连接状态分发到所有 targets
- target 侧会创建和缓存本地 pool objects，并打开本地 VOS pool

这说明 target 侧承担的是：

- pool 在本地 target 上的实际打开与缓存
- 具体 target 资源上下文的建立
- 对上层服务提供实际可访问的本地池对象

所以最简单的区分可以这样记：

- **Pool Service** 管全局元数据和连接语义
- **Pool Target Service** 管本地 target 级池状态和实际打开动作

### 一个很实用的理解方式

如果你非要把两者压缩成一句话，可以这么理解：

- Pool Service 负责决定“这个连接在系统级是否成立”
- Pool Target Service 负责让“这个连接在每个 target 上真正可用”

这两层缺一不可。

## Pool 元数据到底存了什么

`src/pool/README.md` 对 metadata layout 的描述非常关键。

它说 top-level KVS 至少会存这些东西：

- pool map
- UID / GID / mode 等安全属性
- 空间管理与自愈相关信息
- 用户自定义属性 KVS
- pool 连接信息，也就是 pool handle

这意味着 Pool 元数据不只是“池子有多大”这么简单，而是把下列几类信息统一组织在同一个服务状态里：

### 1. 资源视图

比如：

- pool map
- target 组织关系
- 相关版本信息

这决定客户端和服务端如何理解当前 pool 的资源拓扑。

### 2. 安全与访问控制信息

比如：

- owner
- owner group
- ACL / mode

这决定谁能连、能以什么权限连。

### 3. 运行态连接信息

比如：

- pool handle
- handle UUID
- handle 对应的 capability

这说明 Pool Service 不仅存静态元数据，还存活跃连接状态。

### 4. 自愈与空间相关信息

比如：

- 空间管理相关属性
- self-healing 相关状态

这说明 Pool Service 同时也是很多后续恢复和管理动作的入口状态中心。

## 为什么 Pool 元数据必须走 RDB / RSVC

这几乎是理解 Pool 实现最重要的一层。

### `pool_svc` 不是本地状态，而是复制服务

`src/pool/README.md` 里明确写到：

- pool metadata 以层次化 KVS 组织
- 这些 KVS 复制在多个 server 上
- 背后使用 Raft 和 strong leadership
- 只有 service leader 能真正处理客户端请求

这意味着 Pool Service 本质上不是“某台机器上的 pool 管理器”，而是一个 replicated service。

### `rsvc` 提供的是通用复制服务框架

`src/rsvc/README.md` 把 replicated service 的架构讲得非常清楚：

- service request 会转成状态查询和确定性状态更新
- 状态更新先写入 Raft log，再应用到状态
- 只有当前 leader 能处理请求
- 客户端需要能够搜索当前 leader

并且它明确点名：

- `pool_svc`
- `cont_svc`

都是构建在 `rsvc` 之上的 replicated service。

这意味着 Pool Service 并不是自己单独实现了一套 leader 选举和复制框架，而是复用 `rsvc` 这一层的通用能力。

### `rdb` 提供的是被 Raft 复制的层次化 KVS

`src/rdb/README.md` 则进一步说明：

- replicated service 建立在 Raft replicated log 上
- 请求会转化为状态查询和确定性状态更新
- update 必须先进入 replicated log，再应用到状态

换句话说：

- `rsvc` 更像 replicated service framework
- `rdb` 更像 replicated KVS / transaction substrate

而 Pool Service 的元数据正是定义在这层 RDB 数据模型上的。

### 为什么必须是强领导复制

Pool 元数据之所以不能“每个副本自己处理一部分”，根本原因在于它承载的是系统级控制状态：

- pool map 更新
- handle 创建与删除
- replica 列表
- 访问控制

这些状态如果没有单一 leader 串行化处理，很容易出现：

- 不同副本对当前连接状态理解不一致
- map 版本不一致
- handle 数量和具体条目不一致
- 客户端不知道该信谁

因此 strong leadership 在这里不是可选优化，而是保证 Pool Service 元数据一致性的基础。

## `ds_pool_svc_dist_create()`：Pool 为什么要从管理服务进入

`src/pool/README.md` 已经说过，pool create 完全由 Management Service 驱动，因为它涉及：

- 存储分配
- fault domain 查询
- service replica 选择

而在 `src/pool/srv_pool.c` 里，这条入口真实落到：

- `ds_pool_svc_dist_create(...)`

### 这个函数做了什么

从代码看，这个函数大致做了下面几件事：

1. 根据 targets 和 domains 生成初始 pool map。
2. 读取 pool service redundancy factor 属性。
3. 选择 pool service replica 所在 ranks。
4. 组装 `ds_rsvc_create_params`。
5. 调用 `ds_rsvc_dist_start(...)` 启动 distributed replicated service。
6. 然后再通过客户端视角向 service leader 发 `POOL_CREATE` 请求。

这条链很有代表性，因为它说明 Pool 的创建不是“直接写一份元数据”，而是：

- 先把 replicated service 的复制骨架搭起来
- 再由 leader 来初始化真正的 pool service 数据库和初始元数据

这正是 README 里那句“management module passes the control to the pool module”的具体落点。

### 为什么要这么绕一层

因为 Pool 从第一天开始就不是单副本本地状态。

如果不先把 service replicas 建起来，就没有一个真正可靠的 leader 来初始化和承接后续所有 pool 元数据操作。

所以 `ds_pool_svc_dist_create()` 的存在，本质上是在回答：

**Pool 元数据从出生开始，就是高可用复制状态，而不是后期再补 HA。**

## `daos_pool_connect` 到服务端时到底发生了什么

这一条链路特别值得展开，因为它几乎把 Pool 的全局职责都串起来了。

### README 中的概念流程

`src/pool/README.md` 对 pool connect 的概念流程已经写得很清楚：

1. 客户端调用 `daos_pool_connect`
2. 发起 `POOL_CONNECT` 请求到 Pool Service
3. Pool Service 做认证和授权
4. 把 pool map 传给客户端
5. 检查 handle 是否已存在或是否与独占句柄冲突
6. 然后把连接状态分发到 pool targets

这是理解 Pool connect 最好的概念图。

### `rpc.h` 说明了 `POOL_CONNECT` 的协议位置

`src/pool/rpc.h` 里可以看到：

- `POOL_CONNECT` 属于 `POOL_PROTO_CLI_RPC_LIST`
- 对应 handler 是 `ds_pool_connect_handler`

这说明客户端看到的 `daos_pool_connect`，落到服务端后首先进入的就是 Pool Service handler。

### 真实 handler 在 `srv_pool.c`

`ds_pool_connect_handler()` 本身只是一个薄包装，真正的逻辑在：

- `pool_connect_handler(rpc, handler_version)`

而这个 handler 中最值得注意的几段，正好对应 README 里的概念流程。

#### 1. 先做安全能力计算和权限检查

代码里会：

- 读取 ACL、owner、owner group 等属性
- 调用 `ds_sec_pool_get_capabilities(...)`
- 再通过 `ds_sec_pool_can_connect(...)` 判断是否允许连接

这一步说明 Pool connect 不是“知道 UUID 就能连”，而是一个明确的认证和授权流程。

#### 2. 然后检查 handle 状态和独占语义

代码里会从 `ps_root` 里读取：

- `ds_pool_prop_nhandles`

如果已有连接存在，还会进一步检查：

- 当前请求是否带独占标志 `DAOS_PC_EX`
- 现有 handle 是否为独占 handle

不满足条件就返回：

- `-DER_BUSY`

这正好和 README 里的语义一致：

- 已有同 UUID handle，说明连接已存在
- 如果当前或已有连接要求独占，则拒绝

#### 3. 然后把连接状态分发到 targets

这里有一个很值得注意的实现细节。

README 用概念性语言描述说：

- Pool Service 会发送 collective `POOL_TGT_CONNECT` 请求到所有 targets

而当前 `srv_pool.c` 中的关键实现落点，则是：

- `pool_connect_iv_dist(...)`

这个函数进一步调用：

- `ds_pool_iv_conn_hdl_update(...)`

也就是说，当前实现更显式地表现为：

- 通过 IV 分发连接句柄相关状态到 targets

从概念上看，它完成的仍然是 README 所说的那件事：

- 让所有 targets 建立并感知这个 pool handle 对应的本地连接状态

因此最稳妥的理解方式是：

- README 讲的是 target-side collective connect 的概念模型
- 当前代码实现则更具体地落在 IV 分发连接句柄状态这条路径上

#### 4. 最后把 handle 写入 Pool Service 元数据

当 target 侧连接状态分发成功后，handler 会：

- 分配 `pool_hdl`
- 写入 `svc->ps_handles`
- 更新 `ds_pool_prop_nhandles`

这一步非常关键，因为它说明 pool connect 的“成功”不只是客户端拿到 map，而是 Pool Service 的元数据状态也正式多了一条活跃 handle 记录。

换句话说，Pool connect 是一个同时影响：

- 客户端视图
- target 本地状态
- service 元数据状态

的复合操作。

## 为什么 Pool 是理解后续模块的前置知识

Pool 之所以值得先写透，不只是因为它重要，而是因为它会直接影响后面几个模块的理解。

### 对 `container`

container 的创建、打开、快照和句柄状态都建立在 pool 边界之内。

如果不知道：

- pool handle 是怎么来的
- pool service 在维护什么状态

那后面看 container 时就容易把很多状态来源搞丢。

### 对 `object`

对象布局依赖：

- pool map
- target 组织
- redundancy / placement 相关状态

这些都以 pool 为前提。

如果你不先理解 Pool 是“资源边界 + 元数据边界”，后面看 object class、placement、EC/replication 时就会感觉概念悬空。

### 对 `rebuild`

rebuild 本质上也是围绕：

- pool targets
- pool map 状态变化
- 自愈相关元数据

来展开的。

这也是为什么 `src/pool/README.md` 会主动提到 self-healing 相关信息。

## 一个很实用的阅读方法：把 Pool 拆成三层看

以后你再读 `src/pool` 时，最好的方法不是只记函数名，而是先问自己当前读到的是哪一层：

### 第一层：资源层

看的是：

- pool map
- target ranks
- domains
- resource boundary

### 第二层：服务层

看的是：

- `pool_svc`
- handles KVS
- leader / replica
- RDB / RSVC 上的 replicated metadata

### 第三层：target 本地状态层

看的是：

- 本地 pool object
- target cache
- VOS pool open
- target 侧连接状态

只要先把这三层分开，`src/pool` 的阅读难度会小很多。因为你不会再把：

- 全局元数据
- 本地 target 状态
- 客户端连接语义

混成一团。

## 小结

Pool 在 DAOS 里绝不是一个简单的“逻辑存储池”。

更准确地说，它同时承担三种角色：

- **资源边界**：把跨节点 targets 组织成一个可用资源集合
- **服务边界**：通过 `pool_svc` 管理 pool 元数据、handle 和 map
- **访问入口边界**：客户端通过 `daos_pool_connect` 先进入 Pool，再进入更上层对象世界

而 Pool Service 之所以重要，是因为它把下面这些关键状态统一纳入了一条强领导复制链：

- pool map
- handle
- 安全属性
- 服务副本信息
- 自愈与空间相关元数据

所以，理解 Pool 最重要的不是记住某个 handler 名字，而是记住这条主线：

**targets 形成资源集合，pool map 形成资源描述，pool_svc 形成复制元数据中心，target 侧再把连接状态落实到本地池对象。**

有了这条主线，后面再进入 `container`、`object` 和 `rebuild`，就会更容易看清它们各自是在 Pool 这块底板之上做什么。

## 下一篇看什么

理解完 Pool 之后，最自然的下一步就是进入它上面的逻辑隔离与事务视图层：

**Container 服务：事务、快照和句柄状态为什么落在这一层**

因为从这里开始，DAOS 的“资源边界”会进一步长成“数据视图边界”和“事务边界”。
