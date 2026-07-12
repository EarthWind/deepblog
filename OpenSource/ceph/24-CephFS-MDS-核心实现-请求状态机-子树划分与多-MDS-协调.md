# CephFS MDS 核心实现：请求状态机、子树划分与多 MDS 协调

## 这篇文章要解决什么问题

上一篇我们已经把 CephFS 的总体结构讲清楚了：

- `MDS` 管元数据控制面
- 客户端在 caps 授权下直达 `RADOS`
- CephFS 的关键是元数据面和数据面分离

但如果继续往源码里走，很快就会遇到一个更具体、也更难的问题：

**`MDS` 自己到底是怎么工作的？**

也就是：

- 客户端发来的一个元数据请求，是怎样进入 `MDS` 的？
- `MDSDaemon`、`MDSRank`、`Server`、`MDCache` 分别扮演什么角色？
- `MDRequest` 到底是什么，它为什么能把一整个元数据操作流程串起来？
- 多 `MDS` 场景下，为什么不是“目录随便分给几个节点”，而是要围绕 subtree authority 来组织？
- 子树迁移为什么要有 freeze、prep、export、import 这样一整套复杂协议？

如果只允许用一句话先给结论，那就是：

**CephFS 的 `MDS` 核心不是“收请求然后回结果”的简单服务端，而是一个围绕 `MDRequest` 内部事务对象、`MDCache` 子树 authority、`Migrator` 导出导入协议和 `MDBalancer` 负载均衡策略组织起来的元数据控制系统；单 `MDS` 时它像一个复杂的状态机执行器，多 `MDS` 时它又进一步演化成一个带子树边界和迁移协议的分布式协调系统。**

这一篇的目标，就是把这条主线真正展开。

## 先建立第一条边界：`MDS` 不是一个“大 switch + 几个 handler”

这是理解 CephFS MDS 实现的第一步。

如果你第一次打开 `src/mds`，很容易先形成一种过于简单的印象：

- 有消息进来
- 走一个分发函数
- 调某个 `handle_xxx`
- 回一个 reply

这个印象不能说完全错，但远远不够。

更准确地说，MDS 真正要做的是：

- 维护目录树和 inode/dentry 缓存
- 判定当前 rank 有没有 authority
- 建立并清理请求上下文
- 申请和释放锁
- 和 peer MDS 协调
- 处理 replay / reconnect / clientreplay
- 必要时写 journal
- 最后才能安全地 reply

所以理解 MDS 时，最重要的第一条直觉是：

- **它不是一个“收到 RPC 就直接执行”的薄服务端，而是一个元数据事务协调器。**

## 第 1 层：`MDSDaemon`、`MDSRank`、`Server` 三层到底怎么分工

如果先不纠缠细节，可以先把 MDS 运行时拆成三层。

## `MDSDaemon`

- 守护进程壳层

### 它负责什么

- 进程生命周期
- Messenger/MonClient/beacon 等守护进程级资源
- 进程级消息入口
- rank 创建与托管

### 更准确地说

它更像：

- “一个 `ceph-mds` 进程的外壳”

而不是：

- 真正执行元数据语义的主体

## `MDSRank`

- 某个 rank 的运行时中枢

### 它负责什么

- 一个 rank 的整体状态推进
- 消息路由到各子系统
- 持有 `server`、`mdcache`、`mdlog`、`locker`、`migrator` 等核心组件

### 你可以把它理解成

- “一个活跃 MDS rank 的控制中心”

## `Server`

- 客户端元数据请求执行器

### 它负责什么

- 接住客户端元数据请求
- 检查 session / replay / retry
- 创建 `MDRequest`
- 分发到 `handle_client_xxx`
- 回复客户端或写 journal 后回复

### 所以三层关系可以压缩成一句话

- `MDSDaemon` 是进程壳
- `MDSRank` 是 rank 控制中枢
- `Server` 是元数据 RPC 的主执行器

## 第 2 层：一条最重要的请求主线

如果整篇只想先记一条控制流，我建议先记下面这条：

```text
Client::make_request
  ->
Client::send_request / build_client_request
  ->
MClientRequest
  ->
MDSDaemon::ms_dispatch2
  ->
MDSRankDispatcher::ms_dispatch
  ->
MDSRank::handle_message
  ->
Server::dispatch
  ->
Server::handle_client_request
  ->
MDCache::request_start
  ->
Server::dispatch_client_request
  ->
handle_client_xxx
  ->
respond_to_request / journal_and_reply
  ->
reply_client_request
  ->
MDCache::request_finish / request_cleanup
```

这条链的意义在于：

- 它把“协议消息”一路串成了“内部事务上下文”

这恰恰就是理解 MDS 的关键。

## 第 3 层：客户端发来的并不是“文件系统操作”，而是 `MClientRequest`

MDS 请求处理的起点并不在 `src/mds`，而是在客户端。

客户端侧真正的元数据请求主入口是：

- `Client::make_request`

这一步会做几件非常关键的事：

- 为请求分配 `tid`
- 选择目标 MDS
- 确保 session 打开
- 最后构造并发送 `MClientRequest`

### 这说明什么

说明当消息进入 MDS 之前，它已经不是一个抽象的“open / mkdir / rename 意图”了，而是：

- 一个带请求头、路径、flags、releases、请求 id 的线上消息对象

所以 MDS 的第一工作不是“理解用户想干嘛”，而是：

- 把线上消息重新接回 MDS 内部的元数据语义世界

## 第 4 层：为什么 `MDSDaemon::ms_dispatch2` 只是大门，不是核心语义入口

请求进入 `ceph-mds` 进程后，先到的是：

- `MDSDaemon::ms_dispatch2`

很多人看到这里会下意识觉得：

- 主逻辑应该从这里开始

但更准确地说，这里只是：

- 进程级收包入口

### 它真正做的事情

- 持有全局 `mds_lock`
- 处理 daemon/core 级消息
- 把普通 rank 级消息进一步交给 `mds_rank`

所以这里更像：

- 门卫 + 总接线台

而不是：

- 真正处理 inode/dentry/rename/open 语义的地方

## 第 5 层：为什么 `MDSRank` 才是 rank 级控制中心

消息进入 `MDSRank` 之后，事情才真正开始像“一个活着的 MDS rank”。

`MDSRank` 最重要的价值，是把一堆核心子系统组织到一起：

- `Server`
- `MDCache`
- `MDLog`
- `Locker`
- `Migrator`
- `SessionMap`

### 这意味着什么

意味着一个元数据请求不只是经过一个 handler，而是会随着处理阶段不同，分别依赖：

- 路径和目录树缓存
- 锁和 capability 状态
- journal
- peer 协调
- 请求生命周期追踪

所以如果要给 `MDSRank` 下一个源码阅读定位，我会这样说：

- 它是 MDS 内部所有“元数据控制子系统”的总调度中枢

## 第 6 层：`Server::handle_client_request` 为什么是最值得盯住的入口

如果说整条主线里有一个最值得断点跟进去的函数，那通常就是：

- `Server::handle_client_request`

因为这里真正开始把线上消息变成 MDS 内部请求对象。

### 它做了哪些关键事

至少包括：

- 检查 `mdcache` 是否处于可服务状态
- 检查 session 是否存在、是否可接受请求
- 处理 replay / retry / completed request 语义
- trim completed request list
- 调用 `mdcache->request_start(req)` 创建 `MDRequest`
- 再进入 `dispatch_client_request(mdr)`

### 为什么这一步这么关键

因为从这里开始，请求的身份发生了变化：

- 从一个网络消息
- 变成一个 MDS 内部事务上下文

## 第 7 层：`MDRequest` 到底是什么

这是整篇文章最想讲清楚的点之一。

很多人第一次听到“请求状态机”时，会下意识去找：

- 一个大 enum
- 若干显式状态迁移图

但 CephFS 里的 `MDRequest` 不太适合这么理解。

更准确地说：

- `MDRequest` 首先是一个内部请求上下文对象

### 它里面装了什么

大致包括几类信息：

- 请求来源：client / peer / internal
- 协议消息对象
- 路径解析结果
- inode / dentry 指针
- 锁和 pin 持有情况
- peer 协调状态
- 是否已完成、是否 early reply、是否 committing
- 最终 cleanup 所需的上下文

### 所以最准确的理解方式是什么

- 它不是一个“包”
- 也不是一个“只有几个状态的结构体”
- 而更像一个把整条元数据事务链条串起来的工作上下文

这就是为什么我更愿意把它叫成：

- **MDS 内部事务对象**

## 第 8 层：为什么说 `MDRequest` 是“状态机”，但不是那种教科书式状态机

这句话听起来有点绕，但非常重要。

你当然可以说 `MDRequest` 体现了请求状态推进，因为它确实会经历：

- start
- dispatch
- path / auth / lock 处理
- 可能的 peer 协调
- reply 或 journal
- finish
- cleanup

但它并不是那种：

- 一个 `enum state`
- 一个大 `switch`
- 每次只跳一个显式状态

的经典教科书状态机。

更准确地说，它是：

- 一个由多个标志位、回调、子流程和上下文对象共同推进的事务生命周期

### 这也是为什么它难读

因为你看到的往往不是：

- “现在 state = X”

而是：

- 是否在 committing
- 是否被 aborted / killed
- 当前拿了哪些锁
- 有没有 peer 等待
- 是否有 early reply
- 是否已经进入 request_finish / cleanup

所以这篇文章里的“请求状态机”，更准确指的是：

- **一次元数据请求在 MDS 内部如何被逐步推进、协调、提交和清理**

## 第 9 层：`Server::dispatch_client_request` 为什么是元数据语义的大路由器

有了 `MDRequest` 之后，请求就进入：

- `Server::dispatch_client_request`

这一层最重要的工作非常直接：

- 按 `req->get_op()` 把请求分发到具体的 `handle_client_xxx`

比如：

- `getattr`
- `open`
- `create`
- `mkdir`
- `rename`
- `unlink`

### 但真正应该注意的不是 switch 本身

而是：

- 从这里开始，MDS 真正进入了“路径解析 + authority 判定 + 锁/一致性 + 元数据修改”语义层

也就是说，这里不是简单的 RPC handler 分发，而是：

- 文件系统元数据语义真正展开的起点

## 第 10 层：为什么一个请求经常并不是“当前 MDS 直接做完”

这正是 CephFS MDS 比普通单机文件系统更复杂的地方。

在单 MDS、且 authority 正好命中时，请求当然可能比较顺。

但只要进入更真实的场景，就会遇到更多问题：

- 当前 rank 不一定拥有目标子树 authority
- 某些元数据可能正处于迁移中
- 某些锁可能需要等待
- 某些请求要和 peer MDS 协调

这意味着：

- 元数据请求不是简单“收到了就执行”

而是：

- 需要先回答“当前谁有权处理它”

这就是多 MDS 复杂性的第一来源。

## 第 11 层：为什么 CephFS 多 MDS 不是“把目录树平均切几份”

这是理解多 MDS 的第一条关键边界。

很多人一听多 MDS，直觉就会想：

- 根目录往下均分
- 每个目录树切给一台

这个想法太静态了。

CephFS 实际上围绕的核心概念不是：

- 固定目录分片表

而是：

- **subtree authority**

也就是：

- 某一段子树当前由哪个 MDS rank 负责

### 这意味着什么

意味着元数据名字空间不是简单硬切，而是：

- 带边界
- 可迁移
- 可动态调整

这就是 CephFS 多 MDS 真正的灵魂。

## 第 12 层：为什么 `MDCache` 才是子树 authority 的真正维护者

很多人讲多 MDS 时容易把注意力全放在：

- `Migrator`
- `MDBalancer`

这些当然都很重要，但如果只看它们，会漏掉更根的层面：

- 子树边界本身由谁维护

真正站在 authority 边界中心的，其实是：

- `MDCache`

### 为什么

因为 CephFS 的目录树、inode、dentry 缓存都在这里组织。

而多 MDS 下真正要回答的问题是：

- 哪个 `CDir` / `CInode` 是 subtree root
- 哪段边界已经授权给哪个 rank
- 调整 authority 时边界如何更新

所以像：

- `adjust_subtree_auth`
- `adjust_bounded_subtree_auth`

这样的接口，本质上不是普通缓存维护函数，而是在改：

- MDS 对名字空间的控制边界

## 第 13 层：为什么说 `MDBalancer` 决定“该迁谁”，`Migrator` 决定“怎么迁”

这是理解多 MDS 协调最值得记住的一句话。

## `MDBalancer`

- 更偏策略层

### 它回答的问题

- 哪个子树太热
- 哪个目录应该 export pin
- 哪些 subtree 值得迁走
- 哪些目录需要分片

换句话说，它更像在回答：

- **该迁谁**

## `Migrator`

- 更偏协议执行层

### 它回答的问题

- 当前 subtree 如何 freeze
- 如何进入 export prep
- 如何把 subtree 状态发给 importer
- importer 如何接收并接管 authority

换句话说，它更像在回答：

- **怎么迁**

这组分工非常清楚，也非常适合写进脑图里。

## 第 14 层：为什么子树迁移必须这么复杂

第一次看到 CephFS 的 export/import 协议时，很多人都会觉得：

- 迁个目录为什么要这么麻烦？

但如果你把问题想完整，就会发现这种复杂度几乎不可避免。

因为迁移的不是一个静态对象，而是：

- 一个可能仍在被访问的目录子树
- 一组 inode / dentry / dirfrag 状态
- 一批锁、caps 和 authority 边界

所以迁移时必须回答：

- 如何阻止边界继续变化
- 如何保证迁移前后的 authority 不歧义
- 如何让 importer 在接管前看到一致的 subtree 状态
- 如何在 journal 里留下可恢复的事件

这就是为什么迁移协议必须包含：

- freeze
- prep
- warning
- export
- import
- finish

这一整套阶段。

## 第 15 层：把 export/import 压缩成一条最实用的时序理解

如果不陷进所有实现细节，可以先把它压缩成下面这条主线：

```text
balancer/migrator 选中某个 subtree
  ->
exporter 冻结 subtree，阻止边界继续变化
  ->
向 importer 发送 export_prep
  ->
双方就 authority 接管做准备
  ->
exporter 发送 subtree 相关状态
  ->
importer 建立本地 subtree 视图并接管
  ->
journal 记录 import/export 事件
  ->
最终完成 authority 转移
```

这条主线最关键的不是背消息名，而是建立一个直觉：

- **迁移的是“对一段名字空间的控制权”，而不只是缓存对象搬家。**

## 第 16 层：为什么 freeze 是迁移协议里最关键的动作之一

这一步特别值得单独讲。

如果 exporter 在迁移时不先冻结 subtree，会发生什么？

可能会出现：

- 目录项继续变化
- authority 边界继续漂移
- inode / dentry 关系在迁移过程中继续被修改

这样 importer 接到的就不是一个稳定快照，而是一团正在变化的状态。

所以 freeze 的根本意义不是：

- “暂停一下”

而是：

- **为 authority 转移制造一个可定义、可交接的稳定边界。**

这点非常关键。

## 第 17 层：目录分片为什么也是多 MDS 协调的一部分

很多人会把目录分片看成：

- 大目录优化

这当然没错，但如果只停在这里就太浅了。

在 CephFS 多 MDS 里，dirfrag 还有更深一层意义：

- 它让一个过热目录可以被切成更可迁移、更可分配的元数据单元

### 这意味着什么

意味着目录分片不只是减少单目录热点，而是：

- 为多 MDS 元数据负载分布提供更细粒度的操作对象

所以当你看到：

- `maybe_fragment`
- `queue_split`

这类逻辑时，最好不要只把它理解成缓存结构优化，而要把它理解成：

- 多 MDS 负载均衡基础设施

## 第 18 层：为什么 forward/resend 机制能说明 CephFS 的 authority 不是客户端透明的

另一个非常有意思的点是：

- 当前 MDS 不拥有 authority 时，并不总是自己悄悄代办到底

在很多场景里，MDS 会通过：

- `MClientRequestForward`

告诉客户端：

- 这个请求你应该重发到新的 MDS

### 这说明什么

说明 CephFS 的 authority 变化虽然是系统内部协调的结果，但它并不是完全对客户端“隐形”的。

更准确地说：

- authority 的最终落点会反馈到客户端请求路径选择上

这点很适合拿来帮助理解：

- 多 MDS 不是一个完全透明的后端集群
- 客户端也要参与 authority 变化后的重新路由

## 第 19 层：为什么 journal 在请求处理和迁移里都如此重要

前面我们已经讲过：

- CephFS 的可靠性最终也要落回 RADOS journal

到了 MDS 内核实现层，这件事会体现得更明显。

因为不仅普通元数据修改要经过：

- `journal_and_reply`

而且多 MDS 迁移相关事件也会被记录为：

- `EExport`
- `EImportStart`
- `EImportFinish`
- `EFragment`
- `ESubtreeMap`

### 这说明什么

说明 journal 在 CephFS MDS 里不是“顺手记个日志”，而是：

- 元数据事务可恢复性
- authority 变化可重建性
- failover/replay 正确性

的关键基础设施。

## 第 20 层：把整个 MDS 核心实现真正压缩成一张图

如果这一篇只记一张图，我建议记下面这张：

```text
客户端 MetaRequest
  ->
MClientRequest
  ->
MDSDaemon 收包
  ->
MDSRank 路由
  ->
Server::handle_client_request
  ->
MDCache::request_start 创建 MDRequest
  ->
dispatch_client_request / handle_client_xxx
  ->
如果当前 rank 有 authority:
    锁、路径、元数据修改、journal/reply、finish
  ->
如果 authority 变化或需要多 MDS 协调:
    MDBalancer 选 subtree
    MDCache 维护 authority 边界
    Migrator 执行 export/import
    必要时 forward 客户端重发
```

这张图最重要的价值是：

- 它把“请求状态机”和“多 MDS 协调”放进了同一个控制流视角里

## 第 21 层：为什么说 CephFS MDS 的真正难点不是某个 handler，而是“边界管理”

这是整篇最想让读者留下的认知。

如果只是单看某个：

- `handle_client_rename`
- `handle_client_mkdir`
- `handle_client_unlink`

你当然能学到局部元数据语义。

但 CephFS MDS 真正困难、也最有代表性的地方并不只在这些 handler 里，而在于：

- 请求上下文如何被维持
- authority 边界如何被定义
- 子树如何安全迁移
- 多个 MDS 如何在不打碎名字空间一致性的前提下分工

换句话说，CephFS MDS 真正难的并不是：

- “怎么实现一个元数据操作”

而是：

- **怎么在分布式环境里维持名字空间控制边界。**

这就是它最精彩、也最值得读源码的地方。

## 用一句话重新概括这篇

如果把这篇全部内容压缩成一句尽量准确的话，我会这样说：

**CephFS `MDS` 的核心实现，是围绕 `MDRequest` 这个内部事务对象，把客户端元数据请求从消息分发、路径解析、authority 判定、锁与 journal 处理一路推进到 reply/cleanup；而一旦进入多 `MDS` 场景，`MDCache` 负责维护 subtree authority 边界，`MDBalancer` 负责决定迁移目标，`Migrator` 负责执行 export/import 协议，从而把单机式元数据处理扩展成分布式名字空间协调系统。**

## 把整篇压缩成一条最短骨架

如果你只想记一条骨架，可以记成下面这样：

```text
客户端发出 MClientRequest
  ->
MDSDaemon 收包，MDSRank 路由
  ->
Server 接住请求
  ->
MDCache::request_start 创建 MDRequest
  ->
按元数据语义处理请求
  ->
有 authority 就本地完成并 reply/journal
  ->
authority 不稳或负载需要调整时
  ->
MDCache 维护子树边界
  ->
MDBalancer 选择迁移对象
  ->
Migrator 执行 export/import
  ->
必要时 forward 客户端重发
```

只要这条骨架记住了，CephFS MDS 的核心结构就不会乱。

## 初学者最容易混淆的 10 个点

### 1. 认为 `MDSDaemon` 就是 MDS 的全部实现

不对。它更多是进程壳层。

### 2. 认为 `Server` 只是普通 RPC handler 集合

不对。它是元数据请求执行器和协调入口。

### 3. 认为 `MDRequest` 只是“请求包”

不对。它是内部事务上下文。

### 4. 认为“请求状态机”一定意味着一个大 enum

不对。CephFS 更像多标志位和回调推进的事务生命周期。

### 5. 认为多 MDS 就是把目录树静态均分

不对。核心是 subtree authority。

### 6. 认为 authority 只由 `Migrator` 管

不对。真正的边界维护中心在 `MDCache`。

### 7. 认为 `MDBalancer` 和 `Migrator` 做的是同一件事

不对。一个更偏“选谁迁”，一个更偏“怎么迁”。

### 8. 认为 export/import 只是缓存搬家

不对。迁移的是名字空间控制权。

### 9. 认为 dirfrag 只是大目录优化

不对。它也是多 MDS 更细粒度负载分布的基础。

### 10. 认为 authority 变化对客户端完全透明

不对。forward/resend 路径就说明客户端也会感知新的目标 MDS。

## 这一篇最应该留下的 5 个直觉

### 直觉一：MDS 是元数据事务协调器，不是薄服务端

这是第一原则。

### 直觉二：`MDRequest` 是理解 MDS 内核的核心对象

它把整条生命周期串了起来。

### 直觉三：多 MDS 的灵魂不是“多台机器”，而是 subtree authority

这点必须立住。

### 直觉四：`MDBalancer` 选迁移对象，`Migrator` 执行迁移协议

这组分工非常关键。

### 直觉五：CephFS MDS 最难的地方是控制边界管理

而不是某个单独的元数据操作 handler。

## 下一篇看什么

既然这一篇已经把：

- `MDSDaemon`
- `MDSRank`
- `Server`
- `MDRequest`
- subtree authority
- `MDBalancer`
- `Migrator`

这条 MDS 控制主线讲清楚了，下一步最自然的事情，就是把视角进一步压缩到元数据缓存和一致性机制本身：

**inode、dentry、cap、session 到底是怎样在 `MDCache` 和 `Locker` 里协作起来的？**

所以下一篇建议接：

**《CephFS 元数据缓存与一致性：inode、dentry、cap 和 session 如何协作》**
