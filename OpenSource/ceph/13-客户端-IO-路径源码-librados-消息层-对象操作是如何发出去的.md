# 客户端 IO 路径源码：`librados`、消息层、对象操作是如何发出去的

## 这篇文章要解决什么问题

前面几篇我们已经把 Ceph 的数据面主体讲了不少：

- 一次写请求如何进入 Ceph
- `OSD` 怎样组织执行
- `CRUSH` 怎样做数据定位
- `PG` 为什么是 Ceph 的灵魂

但如果你真的开始读源码，很快还会碰到另一个关键问题：

**客户端侧到底发生了什么？**

也就是：

- `librados` 里的 `IoCtx::write()` 到底只是一个 API 名字，还是已经做了很多事情？
- 为什么写请求到了 `Objecter` 之后，才真正开始像“集群请求”？
- `ObjectOperation` 和 `OSDOp` 到底是什么关系？
- 请求是怎么从客户端对象变成 `MOSDOp` 网络消息的？
- 回包又是怎么从 `MOSDOpReply` 收回来的？

如果只允许用一句话先给结论，那就是：

**Ceph 客户端 IO 路径的本质，是把用户态的对象操作一步步转换成“可定位、可编码、可重试、可回收”的 OSD 请求。**

这条路径里，最核心的几层分别是：

- `IoCtx`
- `IoCtxImpl`
- `ObjectOperation`
- `Objecter`
- `MOSDOp`
- Messenger / `AsyncConnection`

这一篇就专门把这条链拆开。

## 先建立第一条边界：客户端不是“把字节直接发给 OSD”

初学者第一次看 Ceph 客户端时，最容易形成一种过于直线的想象：

```text
应用调用 write
  -> 立刻发网络包
  -> OSD 收到
```

这当然不是完全错，但中间被省掉了很多真正关键的层。

Ceph 客户端实际上要完成的事情至少包括：

- 把 API 调用转成对象语义操作
- 把对象语义操作转成 `OSDOp` 数组
- 计算目标 PG 与目标 OSD
- 找到正确 session / connection
- 编码成 `MOSDOp`
- 处理 reply、重试、重定向、`-EAGAIN`

所以客户端并不是：

- 一个薄薄的 send 包装器

而更像：

- 一个请求组织和状态管理器

这也是为什么你真正看代码时，会发现：

- `Objecter`

比很多人预想的重得多。

## 第二张图：客户端 IO 路径的总览

先建立一张最重要的总图：

```text
应用
  |
  v
IoCtx API
  |
  v
IoCtxImpl
  |
  v
ObjectOperation
  |
  v
Objecter::prepare_*_op
  |
  v
Objecter::op_submit
  |
  v
_calc_target / session / connection
  |
  v
MOSDOp
  |
  v
AsyncConnection / ProtocolV1,V2
  |
  v
OSD
  |
  v
MOSDOpReply
  |
  v
Objecter::handle_osd_op_reply
```

这张图里最重要的，不是函数名，而是这几个层次的角色：

### API 层

- 让应用以简单接口发起对象读写

### 操作描述层

- 把读写语义组织成 `ObjectOperation`

### 请求组织层

- `Objecter` 把操作变成真正的集群请求

### 消息层

- 把请求编码成线上消息并发送

### 回包收敛层

- 把 reply 找回原始请求，再决定 completion、重试或重定向

只要这五层顺序记住了，后面你回源码里看就不容易迷路。

## 第 1 层：`IoCtx` 对外 API 其实非常薄

如果从最外层开始看，一个非常值得注意的点是：

- `librados::IoCtx::write()`、`read()` 这些 API 本身很薄

比如源码里你能直接看到：

- `IoCtx::write()` 只是构造 `object_t`，然后转给 `io_ctx_impl->write(...)`
- `IoCtx::read()` 也是类似地转给 `io_ctx_impl->read(...)`

### 这说明什么

说明 `IoCtx` 更像：

- 对外友好的 API 壳层

而不是真正承载客户端主逻辑的地方。

这点很重要，因为很多人第一次读源码会停在：

- `IoCtx`

然后以为已经找到客户端核心。

其实不是。

更准确地说：

- `IoCtx` 是外部入口
- `IoCtxImpl` 才是客户端对象操作真正开始成形的地方

## 第 2 层：`IoCtxImpl` 把“我要读写对象”翻译成 `ObjectOperation`

如果说 `IoCtx` 是薄入口，那么：

- `IoCtxImpl`

就是客户端对象 IO 逻辑真正开始展开的地方。

### 为什么这里最关键

因为在这一层，请求已经不再只是“某个 API 被调用”，而开始被翻译成：

- 具体对象操作集合

这一步的典型模式非常统一：

### 写请求

- 创建 `::ObjectOperation op`
- 先做必要的 assert/前置条件
- 再调用 `op.write()`、`op.append()`、`op.write_full()` 等
- 最后走 `operate()` 或某些特化的异步写入口

### 读请求

- 创建 `::ObjectOperation rd`
- 调 `rd.read()` 或相关读子操作
- 最后走 `operate_read()`

也就是说，`IoCtxImpl` 的核心作用不是：

- 直接发网络包

而是：

- 把一次 API 调用组装成一个 `ObjectOperation`

你可以先把它理解成：

**客户端对象语义到内部请求语义的第一层翻译器。**

## 第 3 层：`ObjectOperation` 为什么这么关键

如果说这一篇必须记住一个中间抽象，那一定是：

- `ObjectOperation`

因为它正好站在两个世界中间：

### 向上

- 它还保留“读对象、写对象、追加、比较扩展”等对象语义

### 向下

- 它内部已经在组织 `OSDOp` 数组、输入数据、输出缓冲和回调

这意味着 `ObjectOperation` 不是一个空壳，而是：

- 客户端侧“对象操作描述”的真正载体

## `ObjectOperation` 到底装了什么

从结构上看，它至少承载这些关键内容：

- `ops`
- `flags`
- `priority`
- `out_bl`
- `out_handler`
- `out_rval`
- `out_ec`

### 这说明了什么

说明它不仅记录：

- 我要做哪些 OSD 操作

还同时记录：

- 输出缓冲放哪
- 完成时回调怎么处理
- 每个子 op 的返回值和错误怎么回填

所以你可以把 `ObjectOperation` 理解成：

**一次对象请求在客户端侧的“操作计划表”。**

## 为什么说 `ObjectOperation` 已经很接近 OSD 世界了

这是理解客户端路径的关键一步。

虽然 `ObjectOperation` 这个名字看起来还很“对象化”，但如果你继续看它的实现，就会发现：

- `read()`
- `write()`
- `write_full()`
- `append()`

这些高层接口，最后本质上都是在往：

- `ops`

里追加具体的 `OSDOp`。

### 比如它会做什么

- `add_op()`
- `add_data()`
- `add_writesame()`

这些动作说明：

- 客户端并不是在最后一刻才临时拼消息
- 它在 `ObjectOperation` 这一层，就已经把对象操作逐步翻译成 OSD 能理解的子操作集合

所以可以说：

- `ObjectOperation` 是“对象语义”开始向“OSD 请求语义”收敛的地方

## 第 4 层：读和写在 `IoCtxImpl` 里为什么看起来像两条近似平行的主线

从代码结构上看，一个很有代表性的特点是：

- 读和写都先构造 `ObjectOperation`
- 然后分别走不同的 `prepare_*_op`

### 写路径典型入口

- `operate()`
- 异步普通写还可能直接走 `prepare_write_op()`

### 读路径典型入口

- `operate_read()`
- 异步普通读则常直接走 `prepare_read_op()`

### 为什么这点重要

因为它说明 Ceph 客户端路径既统一，又区分：

### 统一在

- 都使用 `ObjectOperation`
- 最终都走 `Objecter::op_submit`

### 区分在

- 读和写会设置不同 flags
- 读写会准备不同的输出缓冲和 snap/mtime 等语义

这是一种非常成熟的结构：

- 公共主线共享
- 关键语义差异单独处理

## 第 5 层：`Objecter::prepare_read_op / prepare_mutate_op / prepare_write_op` 是真正的“请求成形点”

如果你问：

- 客户端什么时候从“操作描述”真正变成“待发送请求”？

一个非常好的回答就是：

- 在 `Objecter::prepare_*_op` 这一层

这里最值得记住的三个入口是：

- `prepare_read_op()`
- `prepare_mutate_op()`
- `prepare_write_op()`

### 为什么这一层这么关键

因为它在做的是：

- 创建 `Objecter::Op`
- 把 `ObjectOperation` 里的 `ops/out_bl/out_handler/out_rval/out_ec` 搬进去
- 设置读写 flags、snap、mtime、reqid、priority 等

也就是说，从这一层开始，请求已经不再只是“一个对象操作集合”，而是：

- 一个带完整目标语义和回调语义的 `Objecter::Op`

你完全可以把这一层理解成：

**客户端请求正式进入 `Objecter` 世界的边界。**

## 为什么 `Objecter::Op` 比 `ObjectOperation` 更像真正的网络请求前体

这是一个非常重要的区别。

### `ObjectOperation`

- 更像操作描述体

### `Objecter::Op`

- 更像待投递的集群请求实体

因为到了 `Objecter::Op` 这一层，请求已经开始携带：

- 目标对象和 locator
- 读写标志
- snap / snapc
- mtime
- reqid
- completion / oncommit
- 输出缓冲与回调

这意味着：

- 它已经从“我想做什么”
- 变成了“我准备怎样把它发往集群并在返回时收敛”

这就是 `Objecter` 在客户端架构里的独特价值。

## 第 6 层：`op_submit()` 之前，请求还没真正开始“去找集群”

这是一个特别值得强调的点。

很多人会把 `prepare_*_op` 当成“已经发请求了”，其实还没有。

真正让请求开始朝集群走的关键入口是：

- `Objecter::op_submit()`

### 为什么这里是关键分界点

因为从这一刻开始，请求才真正进入：

- 预算/限流
- 目标计算
- session 选择
- 网络发送

这些步骤。

也就是说，在 `op_submit()` 之前，请求主要还是“本地组装”；从 `op_submit()` 开始，它才变成：

- 一个真正向外推进的请求生命周期

## 第 7 层：`op_submit -> _calc_target -> _get_session` 说明客户端真的会自己做很多路由工作

这一段特别能体现 Ceph 的客户端设计哲学。

`op_submit()` 后，请求大致会进入：

- `_op_submit_with_budget()`
- `_op_submit()`

然后关键动作包括：

- `_calc_target()`
- `_get_session()`
- `_session_op_assign()`
- `_send_op()`

### 为什么这很重要

因为它再次说明：

- 客户端不是把请求先交给某个中心节点再由中心路由

而是客户端自己就会：

- 计算目标 PG / OSD
- 找到对应 session
- 决定该把请求发到哪里

这和我们前面讲的 Ceph 去中心化数据路径完全一致。

所以如果你想真正理解：

- 为什么 Ceph 客户端不轻

那就一定要看到这段逻辑。

## 第 8 层：`_prepare_osd_op()` 是请求从 `Objecter::Op` 变成 `MOSDOp` 的关键一步

如果 `Objecter::Op` 还只是客户端内部实体，那么：

- `MOSDOp`

就是线上消息实体。

而把前者变成后者的关键动作之一就是：

- `_prepare_osd_op()`

### 它到底在做什么

可以先简单理解成：

- 新建 `MOSDOp`
- 把 `op->ops` 填进去
- 填充 snap、snapc、mtime
- 填充 flags、priority、reqid

这一步特别关键，因为它就是那条真正的分界线：

```text
客户端内部请求对象
  ->
网络上传输的 OSD 请求消息
```

所以如果你在源码里只想找“请求什么时候真正长成网络消息”，这个函数就是非常好的锚点。

## 第 9 层：`MOSDOp` 为什么是客户端 IO 路径里必须认识的消息类型

这一篇虽然不是专门讲消息层，但 `MOSDOp` 必须讲，因为它就是客户端对象请求进入网络的标准载体。

你可以把它理解成：

- 客户端发给 OSD 的对象操作消息

里面会承载：

- PG / map epoch 相关字段
- 对象与 locator
- `OSDOp` 数组
- data buffer
- flags / reqid / priority

### 为什么它对客户端理解很关键

因为一旦你认清：

- `MOSDOp` 是客户端路径的消息出口

你就能更清楚地区分：

- 这一篇重点：请求怎么被组装并发出去
- 下一篇重点：Messenger / Connection / Dispatch 机制本身如何工作

也就是说，这一篇对消息层只需要抓到：

- 请求在客户端是怎么变成 `MOSDOp` 的

就够了。

## 第 10 层：`AsyncConnection::send_message()` 才是它真正“离开客户端对象世界”的地方

请求一旦变成 `MOSDOp`，下一步就进入连接发送层。

这里最值得看的入口之一是：

- `AsyncConnection::send_message()`

### 为什么这一步很重要

因为它标志着：

- 请求已经完成客户端业务语义组装
- 开始进入消息发送设施

这时连接层会做的事情包括：

- 设置 connection / source / priority 等上下文
- 再交给协议层，比如：
  - `ProtocolV1`
  - `ProtocolV2`

这说明消息层不是“附带细节”，而是真正独立的一层：

- `Objecter` 决定发什么
- `AsyncConnection` / protocol 决定怎么发出去

## 第 11 层：协议层会在发送前进一步做编码准备

虽然这一篇不深挖消息协议，但有一件事值得点出来：

- 到了协议层，请求还会做进一步的编码准备

比如：

- `prepare_send_message()`
- `write_message()`

### 这意味着什么

意味着 `MOSDOp` 也不是“new 出来就等于网络字节流”，中间还要经过：

- `Message::encode()`
- `MOSDOp::encode_payload()`
- 协议帧封装

所以从整体上看，客户端发送链路是：

```text
IoCtx / IoCtxImpl
  -> ObjectOperation
  -> Objecter::Op
  -> MOSDOp
  -> Message encode
  -> Protocol frame
  -> socket send
```

这条链越看越能说明：

- Ceph 客户端并不薄

## 第 12 层：为什么说读写主线“发出去”之后，客户端工作还没有结束

很多人讲客户端路径时，容易在：

- `send_message()`

这里就停下。

但其实客户端还有一半同样重要的工作：

- **把 reply 收回来并正确归并到原始请求上**

这部分的关键入口之一就是：

- `Objecter::handle_osd_op_reply()`

### 为什么这一步同样重要

因为客户端不是 fire-and-forget，它还必须解决：

- 这个 reply 对应哪一个原始 op
- 这个 reply 是 ack、ondisk 还是其他阶段
- 这个 reply 是不是旧 attempt 的陈旧回复
- 是否需要 redirect
- 是否需要 `-EAGAIN` 后重试
- completion 和输出缓冲该怎么完成

这说明客户端请求生命周期并不是：

```text
submit 后就结束
```

而是：

```text
submit -> 发出 -> 收 reply -> 匹配原 op -> 决定完成/重试/重定向
```

## `handle_osd_op_reply()` 为什么很能体现 `Objecter` 的重量

如果你去看这段逻辑，会发现它做的事情非常多：

- 根据 `tid` 找回原始 `Op`
- 校验 session 和 attempt
- 处理 stray reply
- 处理 redirect reply
- 处理 `-EAGAIN`
- 处理普通 completion

### 这说明什么

说明 `Objecter` 不是只负责发送前准备，而是负责：

- **整个请求生命周期的客户端收敛**

这也是为什么我一直强调，真正理解 Ceph 客户端，不能只停留在：

- `IoCtx`

而必须走到：

- `Objecter`

## 第 13 层：同步 IO 和 AIO 的真正区别，不在“会不会走集群”，而在 completion 组织方式

Ceph 客户端路径里还有一个很容易讲浅的问题：

- 同步和异步到底差在哪？

如果从架构层看，最重要的答案是：

- 它们最终都会走到 `Objecter` 和网络发送主线

区别主要不在：

- “一个走集群，一个不走集群”

而在：

- completion 的组织方式
- 是不是直接通过 `operate()/operate_read()` 走通用路径
- 某些普通 AIO 写/读是否直接调用 `prepare_write_op()/prepare_read_op()`

### 这点为什么重要

因为它能帮助你避免另一个误区：

- 以为 AIO 是另一套完全不同的发送体系

实际上更准确的说法是：

- AIO 复用了同一条核心客户端请求组织主线，只是 completion 接口和入口略有不同

## 第 14 层：为什么这一篇讲 `Objecter`，下一篇还要单独讲消息层

到这里你可能会有个疑问：

- 既然这一篇已经讲了 `MOSDOp`、`AsyncConnection`、协议层，那下一篇为什么还要专门讲消息层？

原因很简单：

### 这一篇的重点是“请求如何被发出去”

也就是：

- 客户端侧如何组织 IO

### 下一篇的重点是“消息系统本身如何工作”

也就是：

- `Messenger`
- `Connection`
- `Dispatch`
- 协议帧
- fast dispatch

所以这两篇并不重复，而是正好前后衔接：

- 这一篇关注客户端业务请求生命周期
- 下一篇关注底层通信设施本身

## 用一句话重新概括客户端 IO 路径

如果把这篇压缩成一句足够准确的话，我会这样概括：

**Ceph 客户端 IO 路径的核心，不是“调用 API 再发包”，而是“把对象语义逐步翻译成可路由、可编码、可重试、可收敛的集群请求”，而 `Objecter` 正是这条路径的中心。**

## 把整条路径压缩成一条最短主链

如果你只想先记一条骨架，可以记成下面这样：

```text
IoCtx::read/write
  -> IoCtxImpl::read/write/operate/operate_read
  -> ObjectOperation
  -> Objecter::prepare_read_op / prepare_mutate_op / prepare_write_op
  -> Objecter::op_submit
  -> _calc_target / _get_session / _send_op
  -> _prepare_osd_op
  -> MOSDOp
  -> AsyncConnection::send_message
  -> Protocol encode / send
  -> OSD
  -> MOSDOpReply
  -> Objecter::handle_osd_op_reply
```

这条链里最值得记住的两个关键词是：

- **成形点**：`prepare_*_op`
- **中心点**：`Objecter`

## 初学者最容易混淆的 9 个点

### 1. 认为 `IoCtx` 就是客户端核心

不对。它更像外部入口壳层。

### 2. 认为 `IoCtxImpl` 直接发网络包

不对。它先组织 `ObjectOperation`。

### 3. 认为 `ObjectOperation` 只是一个高层语义对象

不对。它已经在组织 `OSDOp` 数组和输出缓冲。

### 4. 认为 `Objecter` 只负责“转发一下请求”

不对。它负责目标计算、session 选择、发送、reply 收敛和重试逻辑。

### 5. 认为 `prepare_*_op` 之后请求已经发出

不对。真正往集群推进是在 `op_submit()` 之后。

### 6. 认为 `MOSDOp` 只是消息层小细节

不对。它是客户端对象请求进入网络的标准载体。

### 7. 认为同步和异步走的是完全不同的两套逻辑

不对。核心发送主线高度共享，主要差在 completion 组织方式。

### 8. 认为 `send_message()` 就代表客户端生命周期结束

不对。reply 收敛同样是客户端主线的重要一半。

### 9. 认为客户端只负责“把请求发出去”，不参与定位

不对。客户端会自己计算目标 PG / OSD 并选择 session。

## 这一篇最应该留下的 5 个直觉

### 直觉一：Ceph 客户端不是薄壳，而是请求组织器

它承担了远超“API 封装”的工作。

### 直觉二：`ObjectOperation` 是对象语义到 OSD 请求语义的关键桥梁

这一步非常值得建立心智模型。

### 直觉三：`Objecter` 是客户端 IO 路径的真正中心

发出前和收回来都绕不开它。

### 直觉四：客户端自己就会做目标定位和连接选择

这体现了 Ceph 去中心化数据路径的设计。

### 直觉五：客户端请求生命周期包括发送和回包收敛两半

只看发送，不算真正看懂。

## 下一篇看什么

既然这一篇已经把：

- `librados`
- `IoCtxImpl`
- `ObjectOperation`
- `Objecter`
- `MOSDOp`
- reply 收敛

这条客户端对象请求主线讲清楚了，下一步最自然的事情，就是把这里反复出现、但还没有专门展开的那一层真正拿出来：

**Ceph 的消息通信系统本身，到底是怎么组织 `Messenger`、`Connection` 和 `Dispatch` 的？**

所以下一篇建议接：

**《Ceph 消息通信层：Messenger、Connection、Dispatch 机制详解》**
