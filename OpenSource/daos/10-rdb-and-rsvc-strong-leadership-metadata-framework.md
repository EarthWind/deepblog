# 10. RDB 与 RSVC：DAOS 的强领导复制元数据框架

## 为什么这一篇必须把两条“一致性主线”拆开

写到这里，很多读者会开始把两件事慢慢混在一起：

- pool/container 元数据为什么能高可用
- object 数据为什么能复制、做 EC、做 checksum

这两件事看起来都和“一致性”有关，但在 DAOS 里，它们其实是两条完全不同的主线。

前者讲的是：

- 服务元数据怎么复制
- leader 怎么选
- 更新怎么串行化
- 客户端怎么找到正确 leader

后者讲的是：

- 用户数据怎么布局
- 对象副本怎么协作
- parity 怎么计算
- 校验和怎么端到端验证

如果把这两条线混成一个问题，就很容易误解：

- 以为 Raft 也在复制用户对象数据
- 以为 object replication/EC 就等于 pool/container metadata HA
- 以为所有写请求都要先经过一套统一共识协议

这些理解都不对。

所以这一篇最重要的任务，不是介绍一个新模块，而是把前面几篇反复出现却还没单独讲透的底层框架拆清楚：

- `RSVC` 解决什么问题
- `RDB` 解决什么问题
- 为什么 pool/container service 必须先把请求变成“可复制的状态更新”
- 为什么这条元数据复制链和 object 数据保护链是两回事

## 先给结论：`RSVC` 是服务框架，`RDB` 是复制数据库，Raft 是它们下面的共识引擎

`src/rsvc/README.md` 对 replicated service 的分层图画得非常直白：

- 上层是 `pool_svc`、`cont_svc`
- 中间是 `ds_rsvc`
- 再下面是 `rdb`
- 再下面是 `raft`
- 最底下是 `vos`

这张图几乎可以直接当作这篇的总纲。

它说明：

- `pool_svc`、`cont_svc` 是业务服务
- `RSVC` 是通用 replicated service framework
- `RDB` 是带事务的层次化 KVS
- `Raft` 提供 replicated log
- `VOS` 提供本地持久存储载体

所以如果要把角色压缩成一句话：

- **`RSVC` 负责“怎么把一个业务服务做成 replicated service”**
- **`RDB` 负责“怎么把这个服务的状态做成可复制、可事务化的数据库”**

## 第一层：为什么 DAOS 要把 service request 先变成 state query / state update

这其实是整个 replicated service 设计的核心。

### `rsvc/README` 给出了最本质的定义

`src/rsvc/README.md` 说：

- 一个 RPC service 根据当前 service state 处理请求
- 要复制这个 service，本质上就是复制它的 state
- service 会把 request 转换成 state queries 和 deterministic state updates
- 所有 state updates 必须先提交到 Raft log，再应用到状态

这几句话决定了整个架构的思维方式。

它不是说：

- “把 handler 的执行过程原样复制到各个副本”

而是说：

- “把 handler 最终造成的状态变化，抽象成确定性的更新，并以同样顺序在所有副本上重放”

### 为什么必须是 deterministic update

因为 replicated service 最终要保证：

- 每个副本看到同样的更新集合
- 每个副本按同样顺序应用更新
- 每个副本走过一致的状态历史

如果某个 handler 的更新逻辑带有不可确定性，比如：

- 依赖局部时钟随意生成状态
- 依赖本地非复制状态做分支
- 在不同副本上可能走不同代码路径

那么即使大家收到了同一条 log entry，最终状态也可能不同。

所以 replicated service 真正复制的不是“请求本身”，而是：

- 对状态的确定性解释

这就是为什么文章系列前面说 pool/container 的 handler 本质上都在做：

- 查询 RDB 状态
- 组装 RDB 事务更新
- 提交并等待更新变成权威状态

## 第二层：为什么只有 leader 能处理服务 RPC

### `rdb/README` 和 `rsvc/README` 的结论是一致的

两份 README 都强调：

- 只有当前 leader 能处理服务 RPC
- follower 不负责真正执行业务更新
- non-leader replica 只会拒绝请求，并尽可能给客户端 leader hint

这不是一个“为了简化实现的工程选择”，而是 strong leadership 复制模型的核心。

### 为什么不能多个副本同时处理

因为 pool/container service 维护的是系统级元数据，例如：

- pool map
- container properties
- handle store
- snapshot 列表

这些状态一旦允许多个副本并发独立处理，就会立刻面临几个问题：

- 谁先谁后怎么定义
- 不同副本各自接收了不同客户端请求怎么办
- 冲突更新如何线性化
- 客户端读到的 leader 视图到底应该信谁

strong leadership 给出的答案很直接：

- 只有一个 leader 接受更新请求
- 所有更新都先进入 leader 领导下的 replicated log
- 所有副本按同样顺序应用

这样才能把服务状态变成单一线性历史。

### `ds_rsvc_lookup_leader()` 是这层语义的直接代码体现

在 `src/rsvc/srv.c` 里：

- `ds_rsvc_lookup_leader(...)`

会先查找服务对象，再检查它是否处于 `up` 状态。

如果不是当前可服务 leader，就会：

- 调用 `ds_rsvc_set_hint(...)`
- 通过 `rdb_get_leader(...)` 填充 term 和 rank hint
- 返回 `-DER_NOTLEADER`

只有确认当前副本就是可服务 leader，才会：

- 获取 leader reference
- 把 `svc` 返回给业务 handler

这说明“只有 leader 处理请求”不是 README 里的抽象表述，而是框架层直接强制执行的规则。

## `RSVC` 到底提供了哪些共性能力

### 它本质上是在替业务服务消除复制逻辑样板代码

`src/rsvc/README.md` 直接说：

- `rsvc` 的主要目的，是避免不同 replicated service 实现之间的代码重复

也就是说，Pool Service 和 Container Service 并不是各自手搓一套：

- leader 查找
- replica 生命周期
- leadership change 处理
- map distribution / replica membership 变更

而是复用 `RSVC` 这层。

### `RSVC` 分成 server/client 两部分

README 明确写到：

- `ds_rsvc`：server-side framework
- `dc_rsvc`：client-side library

这两个部分刚好对应 replicated service 的两端难点。

#### server 侧

server 侧主要解决：

- 服务对象注册与查找
- leader 状态管理
- step up / step down 生命周期
- distributed start / stop
- replica 增删与 map distribution

从 `src/rsvc/srv.c` 里也能看到这些痕迹，比如：

- `ds_rsvc_start(...)`
- `ds_rsvc_stop(...)`
- `ds_rsvc_add_replicas(...)`
- `ds_rsvc_remove_replicas(...)`
- `ds_rsvc_begin_stepping_up(...)`
- `ds_rsvc_end_stepping_up(...)`

这说明 `RSVC` 不只是一个“查询 leader 的小工具”，而是 replicated service 生命周期框架。

#### client 侧

client 侧主要解决：

- 怎么找到当前 leader
- leader 变更后怎么重试
- candidate replica 列表怎么维护

README 对这点讲得很清楚：

- 客户端维护 candidate replica 列表
- 非 leader 会返回重定向 hint
- 不运行该服务的节点会被从候选列表剔除
- 候选列表可能为空时，还会联系管理服务节点获取更新的 replica 列表

这层很重要，因为 strong leadership 架构不只是 server 内部约束，还要求 client 能够在 leader 漂移时继续找到正确入口。

## `RDB` 到底是什么：不是普通 KV，而是可被 Raft 复制的事务性层次化 KVS

### `rdb/README` 给出的定义很清楚

`src/rdb/README.md` 说：

- replicated service 建立在 Raft replicated log 上
- 服务模块把 RPC 转成 state query 和 deterministic state update
- 更新先进入 replicated log，再被应用

这已经说明 `RDB` 不只是一个本地数据库，而是专门服务于 replicated service 的状态存储层。

### `daos_srv/rdb.h` 则把数据模型说得更具体

`src/include/daos_srv/rdb.h` 开头把 `RDB` 定义为：

- 一个由层次化 KVS 组成的数据库
- 父 KVS 下的某个 key 的值还可以是子 KVS
- 因而一个 KVS 可以用 path 表示

这套模型很像文件系统目录树：

- root
- 子 KVS
- KVS 下的 key/value
- path 导航

这也解释了为什么 pool/container README 里都在说：

- top-level KVS
- child KVS
- properties KVS
- handles KVS

因为它们底下真正用的就是 RDB 这套层次化 KVS 模型。

### 为什么这个模型特别适合 pool/container

pool 和 container 的元数据天然就不是平面表结构，而是分层组织的，例如：

- root
- containers
- handles
- snapshots
- user attributes

如果底层没有 path + child KVS 这种原生模型，业务层就得自己再造一层目录式组织。

而 `RDB` 直接把这个能力提供出来了。

## `RDB` 事务为什么是 pool/container handler 的基本工作单位

### `rdb.h` 把事务语义写得很清楚

`src/include/daos_srv/rdb.h` 说明：

- 所有访问数据库的操作都通过 TX 完成
- query-only TX 可以只读结束
- update 会先在 TX 里排队
- `rdb_tx_commit()` 之前，这些更新对查询不可见
- commit 时按顺序应用
- 如果其中一个更新失败，则整个 TX 回滚

这段语义很关键，因为它回答了“服务端请求为什么必须先变成状态更新事务”。

对于 pool/container 这类元数据服务来说，很多 RPC 都不是单点修改，而是要一起维护一组一致状态，例如：

- 创建 container 时要同时更新实体 KVS 和相关属性
- 打开 container 时要同时更新 handle store、handle 数量和时间属性
- pool connect 时要同时写 handle 状态和相关计数

如果没有事务，业务 handler 很难保证这些状态在 leader crash 或中途失败时仍保持一致。

### 真实业务代码里，到处都是 `rdb_tx_begin()` / `rdb_tx_commit()`

从 `src/pool/srv_pool.c` 和 `src/container/srv_container.c` 可以直接看到：

- `rdb_tx_begin(svc->ps_rsvc.s_db, svc->ps_rsvc.s_term, &tx)`
- `rdb_tx_begin(svc->cs_rsvc->s_db, svc->cs_rsvc->s_term, &tx)`
- 随后配套 `rdb_tx_commit(&tx)`

这说明 pool/container service 的 handler 并不是“顺手写点 KV”，而是始终通过：

- 当前 service leader 的 `s_db`
- 当前 service term
- 一次明确的 RDB TX

来完成状态更新。

这也正是 README 里“service defines its state in terms of the RDB data model, and implements updates using RDB transactions”的真实代码落点。

## 查询为什么不一定经过 replicated log

这是很多人刚看强领导复制时会有的一个疑问。

既然更新都要先进 replicated log，为什么查询不都一样走一遍？

### `rdb/README` 给出的答案很精确

README 说：

- query 可以直接从 service state 读
- 但为了保证不读到 stale state，handler 必须确认期间没有 leadership change
- 如果 leader 已丢失领导权，就要中止请求并重定向客户端

这段设计特别值得注意。

因为它说明 replicated service 不是“任何读都要共识”，而是：

- 写必须进日志
- 读可以直接读状态
- 但读必须建立在 leader 仍然有效的前提上

这在性能和一致性之间做了一个很实用的平衡。

### 为什么这样是安全的

因为只要：

- 当前副本仍是合法 leader
- 它的状态已经包含所有已完成更新 RPC 的效果

那么直接在该 leader 上读取 service state，就是安全的。

一旦 leadership 发生变化，旧 leader 就不能继续声称自己代表最新全局权威状态，于是请求必须中止。

## `RDB` 不只是存状态，它还负责把 Raft 和 VOS 接起来

### `rsvc/README` 的模块分层已经点出这一点

`rsvc/README` 说：

- `raft` 是核心协议库
- 它和 VOS、CaRT 的集成是在 `rdb` 里做的

也就是说，`RDB` 不只是“上层数据库接口”，它还是：

- Raft log 与实际存储/通信环境之间的整合层

### `rdb/README` 还提到两个非常工程化的职责

`rsvc/README` 对 `rdb` 还有两条非常值得注意的描述：

- leader 会监控可用持久存储空间
- 空间低于阈值时，会在追加 Raft log 之前拒绝新事务
- 它还会周期性触发旧版本聚合，以压缩存储

这几件事说明 `RDB` 不只是“理论上的 replicated KVS”，而是已经把复制数据库在工程上必须面对的问题也纳进来了：

- 空间耗尽如何避免把服务拖死
- 旧日志和旧版本如何 compact / aggregate

这也是为什么 `RDB` 不能被简单理解成“Raft + KV 的封装”。

## Pool / Container Service 是怎么复用这条框架的

### `pool_svc` 和 `cont_svc` 都是同一种 replicated service 模式

前面两篇其实已经多次用到这一点，但现在可以把它正式串起来。

`src/pool/README.md` 说：

- `pool_svc` 的元数据 KVS 复制在多个 server 上
- 背后基于 Raft 和 strong leadership
- `pool_svc` 派生自通用 replicated service 模块 `rsvc`

`src/container/README.md` 也说：

- `cont_svc` 的元数据 KVS 复制在多个 server 上
- 背后基于 Raft 和 strong leadership
- `cont_svc` 同样派生自 `rsvc`

这说明对业务服务来说，复用方式高度一致：

1. 用 `RDB` 定义服务状态模型。
2. 用 `RDB TX` 实现查询和更新。
3. 用 `RSVC` 承接 leader 生命周期和 replica 管理。
4. 让 client 通过 `dc_rsvc` 去寻找当前 leader。

### 所以 pool/container 真正自己写的是什么

它们真正自己负责的是：

- 定义自己的元数据 schema
- 编写各自 RPC handler
- 决定某个业务请求如何转成状态查询、状态更新和必要的 target-side RPC

而不是自己再造一套：

- 共识协议
- leader 选举
- 复制数据库
- 客户端重定向逻辑

这正是分层设计的价值所在。

## 为什么“元数据高可用”和“对象数据副本一致性”必须分开理解

这是整篇最重要的结论。

### 元数据高可用主线

这一条线关心的是：

- Pool Service
- Container Service
- `RSVC`
- `RDB`
- `Raft`

它要解决的问题是：

- service state 如何复制
- leader 如何确定
- 状态更新如何线性化
- 客户端如何始终找到权威入口

### 数据保护主线

另一条线关心的是：

- object class
- placement
- replication
- EC
- checksum
- DTX
- VOS

它要解决的问题是：

- 用户对象怎么布局
- 数据 shard 如何复制或编码
- 校验和怎么验证
- 分布式 I/O 如何完成

### 两条线为什么不能混

因为它们的复制对象、时延目标、执行路径都不一样。

元数据复制复制的是：

- pool map
- container properties
- handles
- snapshots

而数据保护复制的是：

- object 数据内容
- parity
- shard 更新

如果把两者混成一个统一机制，不仅会概念混乱，工程上也会失去各自独立优化的空间。

所以最重要的一句分界线应该这样记：

**`RDB/RSVC/Raft` 复制的是服务状态，object replication/EC 复制的是用户数据。**

## 一个更实用的阅读方法：以后看到“服务元数据”先问自己在这条栈的哪一层

以后你再读 pool/container 相关代码时，最实用的办法是先问自己，现在看到的问题属于哪一层。

### 第一层：业务服务层

看的是：

- `pool_svc`
- `cont_svc`
- handler
- metadata schema

这一层回答“业务上想改什么状态”。

### 第二层：复制服务框架层

看的是：

- `ds_rsvc`
- `dc_rsvc`
- leader lookup
- step up / step down
- replica membership

这一层回答“这个业务服务如何活成 replicated service”。

### 第三层：复制数据库层

看的是：

- `rdb`
- KVS path
- TX
- query / update / commit

这一层回答“服务状态如何作为数据库被组织和修改”。

### 第四层：共识与本地持久层

看的是：

- Raft log
- VOS 持久化
- compaction / aggregation

这一层回答“这些状态变化如何真正复制并落盘”。

只要这样分层，很多以前看起来混乱的地方就会清晰很多。

## 小结

`RSVC` 和 `RDB` 是理解 DAOS 服务层高可用的钥匙。

其中：

- `RSVC` 负责把业务服务抽象成 replicated service
- `RDB` 负责把服务状态组织成可事务化、可复制的层次化 KVS
- `Raft` 负责提供强领导下的 replicated log
- `VOS` 负责承接这些状态在本地的持久化

因此，pool/container 的高可用不是“额外加一点副本逻辑”，而是建立在一整条分层基础设施栈之上的。

如果把这篇压缩成一句话，那就是：

**DAOS 先用 `RSVC + RDB + Raft + VOS` 把元数据服务做成强领导复制系统，再用 object 层自己的 replication/EC/checksum 机制保护用户数据，这两条主线相互配合，但绝不是同一件事。**

## 下一篇看什么

把 `RDB/RSVC` 讲清以后，下一步最自然的就是下钻到这条栈最底下的本地存储内核：

**VOS：DAOS 真正的本地存储内核**

因为从那里开始，版本历史、近 epoch 读取、aggregate、DTX 落地这些底层机制才会真正完整闭环。
