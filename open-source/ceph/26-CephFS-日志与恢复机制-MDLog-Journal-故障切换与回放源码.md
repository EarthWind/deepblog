# CephFS 日志与恢复机制：MDLog、Journal、故障切换与回放源码

## 这篇文章要解决什么问题

前面两篇我们已经把 CephFS 的两条关键主线拆开讲过了：

- `MDS` 如何处理元数据请求、维护 subtree authority，并在多 `MDS` 之间协调
- `MDCache`、`Locker`、`Capability`、`SessionMap` 如何一起维持元数据缓存与一致性

但如果继续往 CephFS 真正的“可用性”问题再追一步，就会碰到一个更根本的问题：

**为什么 `MDS` 挂了以后，CephFS 还能恢复？**

也就是：

- 元数据操作做到一半，MDS 崩了怎么办？
- 客户端 session、caps、open file、subtree 边界这些状态靠什么找回来？
- standby / standby-replay 到底在缩短什么时间？
- `replay`、`resolve`、`reconnect`、`rejoin`、`clientreplay` 这几个状态到底分别在干什么？
- `MDLog`、`Journaler`、`LogEvent` 和 `MDSRank` 状态机之间到底是什么关系？

如果只允许用一句话先给结论，那就是：

**CephFS 之所以能在 `MDS` 故障后继续工作，不是因为 standby 把内存原封不动接过去了，而是因为运行中的元数据变化会被写入 journal，接管方通过 `JournalPointer -> Journaler -> MDLog -> LogEvent::replay` 这条链把关键状态重新构建出来，再经过 `resolve -> reconnect -> rejoin -> clientreplay -> active` 这条恢复状态机，与 peer MDS 和客户端重新收敛一致性，最终恢复服务。**

这一篇的目标，就是把这条恢复链真正讲清楚。

## 先建立第一条边界：CephFS 恢复不是“把 MDS 进程重启一下”

这是理解整篇文章的第一步。

很多人第一次听到：

- MDS 故障切换

直觉会以为：

- monitor 发现一个 MDS 挂了
- 再拉起另一个
- 新进程继续接请求

这当然是现象层面的一部分，但远远不够。

因为 CephFS 的 MDS 在运行过程中持有大量不能凭空丢失的状态，比如：

- 已提交和未提交的元数据更新
- session 信息
- open file 相关状态
- subtree authority 视图
- 多 MDS 分布式操作相关状态

如果这些状态只存在于内存里，那 MDS 一旦挂掉，文件系统名字空间的一致性就会立刻出问题。

所以理解 CephFS 恢复时，最重要的第一条直觉是：

- **恢复的本质不是“重启一个服务”，而是“重建一个元数据控制面状态”。**

## 第 1 层：为什么 CephFS 一定要有 journal

一旦你意识到恢复的核心是“重建状态”，下一步就会自然问：

- 靠什么重建？

答案就是：

- journal

### journal 的第一作用

- 一致性

也就是：

- 让 MDS 在崩溃后还能重新找回关键元数据状态

### journal 的第二作用

- 性能

也就是：

- 元数据修改不必每一步都立刻以最慢方式同步到最终稳定结构里
- 可以先组织成日志化事件，再推进提交和后续整理

### 所以最准确的理解方式是

- journal 在 CephFS 里既是恢复基础设施，也是运行期提交路径的一部分

这点非常关键。

## 第 2 层：为什么要先区分 `Journaler`、`MDLog`、`LogEvent`

很多人第一次看 CephFS 日志相关代码时，最容易混淆的就是这三个名字。

如果先把层次立住，后面会顺很多。

## `Journaler`

- 更偏底层 journal 读写器

### 它负责什么

- journal header
- trimmed / expire / write 等位置指针
- journal 可读/可写状态切换
- 读 entry / 写 entry
- recover 低层流

### 可以把它理解成

- “底层日志流引擎”

## `MDLog`

- 更偏 CephFS MDS 的日志子系统

### 它负责什么

- 打开/重开 journal
- submit entry
- 启动 recovery thread / replay thread
- 把 `LogEvent` 写入 journal
- 把 journal entry 回放回 `MDSRank`

### 可以把它理解成

- “MDS 对 journal 的高级封装和控制器”

## `LogEvent`

- 日志里的事件对象抽象

### 它负责什么

- encode / decode
- update_segment
- replay

### 可以把它理解成

- “写入 journal 的一条条可回放元数据语义事件”

### 所以三者关系一句话就够了

- `Journaler` 管日志流
- `MDLog` 管 MDS 日志系统
- `LogEvent` 管每条可回放事件语义

## 第 3 层：为什么 `JournalPointer -> Journaler -> MDLog` 是恢复主线的第一段

如果把源码视角下的恢复起点压缩成最短骨架，通常是这样：

```text
JournalPointer
  ->
Journaler
  ->
MDLog recovery thread
  ->
MDLog replay thread
  ->
LogEvent::decode_event
  ->
LogEvent::replay(mds)
```

### 这里每一步在干什么

## `JournalPointer`

- 告诉 MDS 当前该去找哪份 journal

## `Journaler`

- 接手实际 journal 对象和位置指针

## `MDLog`

- 启动恢复线程，建立可回放环境

## `LogEvent::replay`

- 把日志里的语义事件重新作用回当前接管的 `MDSRank`

### 这意味着什么

意味着 CephFS 的恢复本质上不是：

- “读一堆原始字节，然后猜怎么恢复”

而是：

- “读出一串有明确语义类型的事件，再按事件语义重放”

这就是整个设计最优雅的地方之一。

## 第 4 层：为什么 `MDLog` 是理解 CephFS 恢复最值得盯住的类

如果这一篇只能盯一个类，我会优先盯：

- `MDLog`

因为它正好站在两个世界中间：

- 上面连着 MDS 的元数据提交语义
- 下面连着 Journaler 的日志流

### 运行期它做什么

- `submit_entry`
- append
- wait_for_safe
- 维护 segment
- trim/expire 配合

### 恢复期它做什么

- open / reopen
- 启动 recovery 线程
- 启动 replay 线程
- 逐条读 journal entry
- decode 成 `LogEvent`
- 调 `replay(mds)`

所以如果你要用一句话记它：

- `MDLog` 是 CephFS 把“运行期元数据变化”变成“可恢复事件流”的核心桥梁

## 第 5 层：为什么 `LogEvent` 是 CephFS 恢复真正的“语义载体”

单有日志流还不够，关键还在于日志里装的是什么。

CephFS 的 journal 不是简单记录：

- 某个对象字节改了什么

而是记录一组具备元数据语义的事件对象。

### 常见事件类型

比如：

- `ESessions`
- `EUpdate`
- `EOpen`
- `ECommitted`
- `ESubtreeMap`
- 以及 export/import、fragment、table 相关事件

### 为什么这很重要

因为这些事件不是“调试日志”，而是：

- 恢复时真正会被 decode 并 replay 的对象

也就是说，CephFS 恢复靠的不是：

- 重新扫全量名字空间

而是：

- 重新执行一串有明确语义边界的元数据事件

这就解释了为什么 `LogEvent::replay()` 如此关键。

## 第 6 层：最值得优先理解的 4 类日志事件是什么

如果第一次读 CephFS journal，不需要把所有事件一口气全吞下去。

最值得优先理解的，通常是下面四类。

## `ESessions`

- 恢复客户端 session 相关状态

### 为什么重要

因为 failover 后接管方必须重新知道：

- 原来有哪些客户端会话
- 它们大概是什么状态

## `EUpdate`

- 恢复最核心的元数据更新

### 为什么重要

因为这类事件往往真正对应：

- inode / dentry / metablob 等元数据状态变化

## `EOpen`

- 恢复 open file 相关状态

### 为什么重要

因为打开文件语义不是简单路径名能完全重新推出的

## `ESubtreeMap`

- 恢复 subtree authority 视图

### 为什么重要

因为多 MDS 下，接管方不仅要知道“有哪些目录”，还要知道：

- 名字空间边界此前是如何划分的

### 所以可以把这 4 类事件看成四条恢复线

- session 线
- 元数据更新线
- open file 线
- subtree 边界线

这四条线拼起来，MDS 的核心世界才逐渐完整。

## 第 7 层：为什么 segment、trim、expire 不只是“日志文件维护细节”

很多人讲 journal 时容易只盯 replay，却忽略：

- segment
- trim
- expire

这些词。

其实它们非常重要。

### 为什么

因为 journal 不可能无限增长。

如果旧日志永远不被整理，最终会带来两个直接问题：

- 日志空间膨胀
- replay 时间越来越长

所以 CephFS journal 设计里，segment 和 trim/expire 并不是附属能力，而是：

- 运行期和恢复期成本之间的平衡机制

### 这意味着什么

意味着一个 MDS 平时 journal 管理得不好，最后不只是“多占点空间”，而可能直接表现为：

- failover 后 replay 很慢

这点很值得在文章里单独点出来。

## 第 8 层：为什么 standby-replay 能显著缩短故障接管时间

这也是 CephFS failover 里非常值得讲清楚的一个点。

很多人知道：

- 有 standby
- 还有 standby-replay

但不一定知道两者差别到底在哪。

### 普通 standby 在做什么

- 等待被 monitor 选中去接替某个 rank

### standby-replay 在做什么

- 跟随 active rank 的 journal 进度
- 提前做一部分 replay 准备

### 所以差别的本质是什么

- 普通 standby 更像“冷备”
- standby-replay 更像“热得更多一点的备”

它真正缩短的，不是“起进程”的时间，而是：

- 接管时需要补 replay 的那段时间

这点非常关键。

## 第 9 层：为什么 MDS 恢复不是 replay 完就结束了

这是理解 CephFS 恢复状态机的第一关键点。

很多人一看到日志回放，就会自然觉得：

- 回放完日志，状态就该恢复了

其实还不够。

因为单靠 replay，你最多恢复的是：

- journal 里明确记录下来的历史状态

但 CephFS 是个分布式文件系统，接管方还必须继续解决：

- peer MDS 之间未收敛的分布式操作
- 客户端当前还持有哪些 caps / requests
- failover 时哪些客户端还活着
- 本地 cache 与其他 rank 的视图如何重新对齐
- 未提交请求如何重放或放弃

所以 replay 只是：

- 整个恢复状态机的第一大阶段

而不是全部。

## 第 10 层：CephFS 恢复状态机为什么要分成这么多阶段

CephFS 文档和源码里最常见的一串状态是：

- `REPLAY`
- `RESOLVE`
- `RECONNECT`
- `REJOIN`
- `CLIENTREPLAY`
- `ACTIVE`

很多人第一次看会觉得太复杂。

其实只要理解每一段在消除哪一类不确定性，这组状态就会非常顺。

## `REPLAY`

- 从 journal 重建本地关键元数据状态

## `RESOLVE`

- 多 MDS 下消歧分布式操作、收敛 peer 间状态

## `RECONNECT`

- 等客户端重新连回来，报告自己手里还持有什么状态

## `REJOIN`

- 让 cache 和 rank 间协作状态重新接轨

## `CLIENTREPLAY`

- 重放 replay queue 里的客户端请求

## `ACTIVE`

- 重新进入正常服务态

### 所以恢复状态机的本质是什么

- **按来源不同，逐层消除“不确定状态”。**

这组分层设计非常漂亮。

## 第 11 层：为什么 `REPLAY` 先恢复的是“日志里的历史”，而不是“客户端现在手里还有什么”

这个顺序非常值得强调。

在 `REPLAY` 阶段，MDS 先做的是：

- 回放 journal 里的 `LogEvent`

它并不会先去问客户端：

- 你现在还有什么 caps？
- 你是否还保留某个未完成请求？

### 为什么

因为接管方首先得建立自己的：

- 本地最小一致元数据视图

否则连后面该怎么和客户端谈、怎么和 peer MDS 对齐都没基础。

所以 `REPLAY` 的角色是：

- 先把“历史上我应该长成什么样”找回来

而不是：

- 立刻把全局所有活跃状态一次性找齐

## 第 12 层：为什么多 MDS 还需要 `RESOLVE`

这是单 MDS 和多 MDS 恢复路径最本质的区别之一。

如果只有单 MDS，journal replay 完之后，接下来更多是：

- 跟客户端收敛

但如果是多 MDS，情况会复杂很多，因为系统里可能还存在：

- peer 间正在进行的分布式操作
- subtree authority 变化
- 迁移中断留下的中间态

这时就需要：

- `RESOLVE`

### 它本质上在解决什么

- 先和其他 MDS rank 把“分布式元数据操作的歧义状态”消掉

所以最准确的理解是：

- `REPLAY` 恢复本地历史
- `RESOLVE` 恢复多 MDS 世界里的共识边界

## 第 13 层：为什么 `RECONNECT` 这么关键

这一阶段和上一阶段完全不同，它把视角转向了：

- 客户端

当新 MDS 接管后，它不能假设客户端手里的状态已经自动失效或自动匹配。

客户端可能仍持有：

- caps
- open file 相关状态
- dirty metadata / flush 语义
- 尚未释放的授权

所以 `RECONNECT` 阶段的核心就是：

- 要求客户端重新汇报自己仍持有的状态

### 这说明什么

说明 CephFS 的 failover 不是纯服务端内部完成的，而是：

- 一定要把客户端重新拉进一致性收敛过程

这也是 `Server::reconnect_clients()` 这类逻辑如此重要的原因。

## 第 14 层：为什么 reconnect 超时后要驱逐客户端

这一点和上一篇 session/eviction 正好呼应。

如果 MDS 在 `RECONNECT` 里无限等待一个客户端，会发生什么？

- 系统无法收敛
- 其他客户端继续卡在不确定状态上
- failover 迟迟不能完成

所以 reconnect 阶段一定有超时和驱逐语义。

### 这并不是“简单粗暴”

而是：

- 分布式一致性恢复必须有边界条件

否则一个坏客户端就能把整个文件系统的恢复拖死。

所以驱逐客户端在这里的本质仍然是：

- 为了让一致性恢复有终点

## 第 15 层：为什么还要有 `REJOIN`

很多人容易把 `RECONNECT` 和 `REJOIN` 混在一起。

更准确地说：

- `RECONNECT` 更偏客户端视角
- `REJOIN` 更偏 MDS cache 视角

### `REJOIN` 在做什么

- 让当前 rank 的 cache、subtree、peer 关系重新接上整个多 MDS 世界

所以它解决的不是：

- 客户端是否回来报到了

而是：

- MDS 内部和 peer 间的 cache / authority / join 关系是否重新稳定

这一步在多 MDS 环境里尤其重要。

## 第 16 层：为什么最后还要 `CLIENTREPLAY`

这是恢复链最后一个特别容易被忽视，但非常重要的阶段。

即使：

- 日志已经 replay 完了
- peer MDS 已经 resolve 了
- 客户端也 reconnect 了

系统里仍可能有一类东西没有完全收束，那就是：

- 需要重放的客户端请求队列

这就是：

- `CLIENTREPLAY`

### 它在解决什么

- 把那些在故障前后处于中间态、但又允许重放的客户端请求重新推进一遍

### 所以恢复真正结束的标准是什么

不是“journal replay 完”，而是：

- 直到 `CLIENTREPLAY` 结束，并安全推进到 `ACTIVE`

## 第 17 层：把整个 failover 恢复链压缩成一条最实用的时序

如果不陷入所有细节，可以先把 CephFS MDS failover 压缩成下面这条线：

```text
active MDS 故障
  ->
monitor 选择 standby / standby-replay 接管
  ->
新 rank 根据 JournalPointer 找到 journal
  ->
Journaler 建立日志流
  ->
MDLog recovery/replay 线程逐条读取 LogEvent
  ->
REPLAY: 恢复本地元数据历史
  ->
RESOLVE: 与 peer MDS 消歧分布式状态
  ->
RECONNECT: 等客户端重新汇报 caps/session
  ->
REJOIN: 让 cache 与多 rank 关系重新接轨
  ->
CLIENTREPLAY: 重放允许重放的客户端请求
  ->
ACTIVE: 恢复正常服务
```

这条线里最重要的一句话是：

- **CephFS 恢复是“日志回放 + 分布式重新收敛”的组合。**

## 第 18 层：为什么 `cephfs-journal-tool` 很值得在架构文章里提一下

虽然这一篇主线是架构与源码，但 `cephfs-journal-tool` 仍然值得提一句，因为它能帮助读者建立一个非常实用的直觉：

- journal 不是抽象概念，而是真正可 inspect、可 export、可 reset、可 recover_dentries 的恢复对象

### 这说明什么

说明 CephFS 的恢复体系不是完全黑盒。

当你理解了：

- journal 里装的是事件
- replay 是逐条执行 `LogEvent::replay`

再回头看 journal tool，就会更容易明白它为什么能：

- 查看事件
- 导出日志
- 在灾难恢复时做更底层处理

这对整套心智模型很有帮助。

## 第 19 层：为什么说 CephFS 的恢复能力本质上建立在“事件可重演”之上

这是整篇最想让读者留下的认知。

如果把这篇所有技术细节都抽掉，只留下一个最本质的设计思想，那就是：

- **MDS 把关键元数据变化组织成一串可以重新执行的事件。**

这意味着系统在故障后不必依赖：

- 某个内存镜像
- 某个不可见的黑盒快照

而是可以依赖：

- 一串有顺序、有类型、有 replay 语义的日志事件

再配合后续：

- peer MDS 收敛
- 客户端 reconnect
- request replay

完成最终恢复。

这就是 CephFS 恢复体系最漂亮的地方。

## 用一句话重新概括这篇

如果把这篇全部内容压缩成一句尽量准确的话，我会这样说：

**CephFS 的 `MDS` 故障恢复并不是简单的进程替换，而是依靠 `JournalPointer -> Journaler -> MDLog -> LogEvent::replay` 先从 journal 中重建关键元数据历史，再通过 `REPLAY -> RESOLVE -> RECONNECT -> REJOIN -> CLIENTREPLAY -> ACTIVE` 这条状态机，与 peer MDS 和客户端逐层消除不确定状态，最终把整个元数据控制面重新收敛起来。**

## 把整篇压缩成一条最短骨架

如果你只想记一条骨架，可以记成下面这样：

```text
运行期元数据变化写入 MDLog/journal
  ->
故障后新 MDS 通过 Journaler 找到并读取 journal
  ->
逐条 decode LogEvent 并 replay
  ->
恢复本地元数据历史
  ->
与 peer MDS resolve
  ->
与客户端 reconnect
  ->
cache/state rejoin
  ->
client request replay
  ->
重新进入 active
```

只要这条骨架记住了，CephFS “为什么 MDS 挂了还能恢复” 这件事就不会乱。

## 初学者最容易混淆的 10 个点

### 1. 认为 failover 就是 monitor 拉起另一个进程

不对。关键是元数据状态重建。

### 2. 认为 journal 只是调试日志

不对。它是恢复基础设施和运行期提交路径的一部分。

### 3. 认为 `Journaler`、`MDLog`、`LogEvent` 是同一层东西

不对。一个管日志流，一个管 MDS 日志系统，一个管事件语义。

### 4. 认为 replay 只是把字节读回来

不对。它是逐条 `LogEvent::replay`。

### 5. 认为 replay 完就代表恢复结束

不对。后面还有 resolve、reconnect、rejoin、clientreplay。

### 6. 认为单 MDS 和多 MDS 恢复流程差不多

不对。多 MDS 还需要 resolve 和更多边界收敛。

### 7. 认为 reconnect 只是客户端重新建 TCP 连接

不对。它还要重新汇报 caps/session 等状态。

### 8. 认为驱逐客户端只是运维动作

不对。它也是恢复收敛的一部分。

### 9. 认为 standby-replay 只是“另一个 standby”

不对。它通过跟随 journal 缩短接管时延。

### 10. 认为 journal trim 只是省空间

不对。它也直接影响未来 replay 成本。

## 这一篇最应该留下的 5 个直觉

### 直觉一：CephFS 恢复的本质是重建元数据控制面状态

这是第一原则。

### 直觉二：journal 记录的是可重演的元数据事件

这就是恢复能力的基础。

### 直觉三：`MDLog` 是运行期提交和恢复期回放的桥梁

这个类非常值得记住。

### 直觉四：replay 只恢复“历史”，不自动恢复“全局活跃关系”

所以后面才需要 resolve/reconnect/rejoin/clientreplay。

### 直觉五：CephFS failover 是“日志回放 + 分布式重新收敛”

这句话几乎概括了整条主线。

## 下一篇看什么

既然这一篇已经把：

- `MDLog`
- `Journaler`
- `LogEvent`
- `REPLAY`
- `RESOLVE`
- `RECONNECT`
- `REJOIN`
- `CLIENTREPLAY`

这条 CephFS 恢复主线讲清楚了，下一步最自然的事情，就是把视角从 MDS 侧切回客户端侧，真正深入 `src/client`：

**客户端自己的 inode、dentry、cap、本地路径解析和请求状态流转到底是怎样实现的？**

所以下一篇建议接：

**《CephFS 客户端通用实现：`src/client` 里的 inode、dentry 与 cap 机制》**
