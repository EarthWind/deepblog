# Ceph 消息通信层：Messenger、Connection、Dispatch 机制详解

## 这篇文章要解决什么问题

上一篇我们已经把客户端对象请求主线讲清楚了：

- `librados` 如何把对象操作组装出来
- `Objecter` 如何把请求定位并封成 `MOSDOp`
- 请求如何被发到 OSD
- 回包如何在客户端侧收敛

但如果你继续深挖，很快就会发现一个更底层的问题：

**Ceph 的消息通信系统本身，到底是怎么工作的？**

也就是：

- `Messenger` 到底是个什么角色？
- `Connection` 为什么不是“一个裸 socket”
- `DispatchQueue` 为什么要单独存在
- `Dispatcher` 和 `DispatchQueue` 到底谁负责业务处理
- fast dispatch 和普通 dispatch 又有什么区别
- `ProtocolV1`、`ProtocolV2` 在这条链里分别扮演什么角色

如果只允许用一句话先给结论，那就是：

**Ceph 的经典消息栈，本质上是一套把“网络连接”“协议收发”“消息投递”“业务回调”明确拆层的通信框架：`Messenger` 管总线，`Connection` 管逻辑会话，`Protocol` 管编码和收发，`DispatchQueue` 管投递线程与队列，`Dispatcher` 管最终业务处理。**

这一篇的目标，就是把这条链完整讲清楚。

## 先建立第一条边界：消息层不是“附属工具”，而是 Ceph 的通用底座

很多人第一次读 Ceph 时，会把消息层当成：

- “大家都要用，但不必太懂的底层库”

这个认识太弱了。

因为你前面读过的很多核心模块，其实都深深建立在这层之上：

- `MON`
- `OSD`
- `MGR`
- `Objecter`
- `MonClient`
- `Client`

这些模块之所以能互相通信，不是因为它们各自手工写 socket 代码，而是因为它们都站在：

- `Messenger`

这套统一框架上。

所以更准确地说：

- Ceph 的消息层不是外围工具
- 它是 Ceph 各守护进程和客户端共享的通用通信底座

也正因为如此，理解它会极大帮助你理解：

- 为什么 Ceph 各模块的消息处理风格那么统一
- 为什么 fast dispatch、普通 dispatch、连接事件回调能在不同模块里复现相同模式

## 第二张图：一条消息在 Ceph 里的一生

先建立一张最重要的总图：

```text
业务模块
  |
  v
Messenger
  |
  v
Connection
  |
  v
ProtocolV1 / ProtocolV2
  |
  v
socket 收发
  |
  v
DispatchQueue
  |
  v
Messenger
  |
  v
Dispatcher
  |
  v
业务模块
```

这张图非常重要，因为它明确告诉你：

- 发送和接收都不是“业务模块直接碰 socket”
- 中间有完整的分层

也就是说，Ceph 的通信框架不是：

```text
业务代码 -> socket -> 业务代码
```

而是：

```text
业务代码 -> 通信总线 -> 逻辑连接 -> 协议层 -> 收发
        -> 投递队列 -> 回调接口 -> 业务代码
```

这正是它能支撑这么大一个分布式系统的原因之一。

## 第 1 层：`Messenger` 是总线和编排者，不是业务处理者

理解 Ceph 通信层时，最先要抓住的对象就是：

- `Messenger`

很多人第一次看到它时，容易误以为它就是：

- 某个连接对象
- 某个 socket 管理器
- 某个发消息的工具类

这些理解都只对了一部分。

更准确地说，`Messenger` 更像：

- 本端消息子系统的总线和编排者

### 它负责什么

至少包括：

- 启动和关闭消息子系统
- 管理 dispatcher 链
- 管理 fast dispatcher 链
- 组织连接创建与查找
- 提供 `send_to()` / `connect_to()` 等统一入口
- 在收到消息后，决定该把消息投递给哪个 dispatcher

### 它不负责什么

- 它不直接实现具体业务语义
- 它不直接决定某条消息代表什么业务动作

这也是为什么 `Messenger` 的真正气质更像：

- “消息总线”

而不是：

- “业务 handler”

## 为什么说 `Messenger` 更像总线，而不是“一个连接”

一个很好理解的原因是：

- 一个 `Messenger` 可以拥有很多 `Connection`
- 一个 `Messenger` 还可以同时挂很多 `Dispatcher`

这说明它从来都不是“一条线”，而更像一个枢纽：

- 往外连很多 peer
- 往内服务很多业务模块

所以如果你要给 `Messenger` 一个非常工程化的定义，我会这样说：

**`Messenger` 是 Ceph 节点内“业务模块”和“网络连接/协议栈”之间的统一消息枢纽。**

## 第 2 层：`Connection` 不是裸 socket，而是“到某个 peer 的逻辑会话”

这是理解消息层的第二个关键。

很多人一看到 `Connection`，就会自然联想到：

- TCP 连接对象

这个理解不算完全错，但不够准确。

在 Ceph 里，`Connection` 更像：

- 到某个 peer 的逻辑会话

### 为什么是“逻辑会话”，而不只是“socket”

因为它除了收发，还要承载：

- peer 身份信息
- feature 协商结果
- keepalive
- mark_down
- 连接状态
- 认证相关状态
- 可重连语义

这意味着：

- `Connection` 不只是文件描述符外壳
- 它代表的是一条可以经历建立、断开、重连、重置的逻辑通信关系

这也是 Ceph 这种大型分布式系统必须具备的抽象。

## `AsyncConnection`：为什么当前主流实现不是抽象层，而是 async 实现层

从当前主流消息栈看，真正最常见的实现是：

- `AsyncMessenger`
- `AsyncConnection`

这说明：

- 经典 Ceph 消息栈的主线实现，已经集中在 `src/msg/async`

### `AsyncConnection` 的核心作用

可以先概括成一句话：

**它是“这条逻辑连接在 async 消息栈里的具体执行体”。**

它会负责：

- 收发消息
- 驱动协议对象
- 处理 loopback 特殊路径
- 管理 write/read 事件
- 维护连接状态机

所以你真正看 Ceph 主流消息收发时，常常会从：

- `AsyncMessenger`
- `AsyncConnection`

下钻，而不是只停留在抽象接口。

## 第 3 层：`Dispatcher` 是业务回调接口，不是队列

这一点特别容易和 `DispatchQueue` 混淆。

先给一个最简定义：

- `Dispatcher` 是“业务接收者接口”

### 它的职责是什么

业务模块实现 `Dispatcher` 之后，就可以接住：

- 普通消息回调
- fast dispatch 回调
- connect / accept / reset / refused 这些连接事件回调

也就是说，它表达的是：

- “消息或连接事件来到业务层后，该怎么处理”

### 这意味着什么

意味着 `Dispatcher` 本身不负责：

- 排队
- 调度线程
- socket 收发

它只负责：

- 暴露业务处理接口

这是一条非常关键的边界，因为很多初学者会误以为：

- `Dispatcher` 既负责投递也负责处理

其实不是。投递和线程属于 `DispatchQueue`，业务处理接口属于 `Dispatcher`。

## 一个非常重要的认知：很多核心模块本身就是 `Dispatcher`

这点能帮你立刻把消息层和前面所有文章串起来。

比如典型实现者包括：

- `OSD`
- `Objecter`
- `MonClient`
- `Client`

这说明消息层不是孤立的，而是：

- 业务模块通过实现 `Dispatcher`，把自己挂到 `Messenger` 上

所以从系统架构上看，它像这样：

```text
业务模块实现 Dispatcher
  ->
Messenger 把消息分发给这些 Dispatcher
```

这也是 Ceph 为什么能让不同模块在同一套通信框架下保持统一风格。

## 第 4 层：`DispatchQueue` 是投递层，不是业务层

如果 `Dispatcher` 表示“谁来处理”，那么：

- `DispatchQueue`

表示的就是：

- “消息什么时候、按什么方式、在哪个线程里被投递”

这是通信层里非常重要的一层。

### 为什么它必须单独存在

因为消息处理系统里，网络收包和业务处理不能总是绑死在一个线程里，否则会有几个明显问题：

- 网络线程被业务阻塞
- 消息投递缺乏优先级和公平性
- 连接事件和普通消息很难统一组织

`DispatchQueue` 的存在，就是为了把这些问题抽出来。

### 它负责什么

- 普通消息排队
- 优先级和轮转
- dispatch 线程主循环
- 本地 loopback 消息投递
- fast dispatch 和普通 dispatch 的分流

所以它不是“业务队列”，而是：

- Ceph 消息子系统里的“投递层”

## 为什么 `DispatchQueue` 这个名字容易让人误解

因为很多人会自然以为：

- 既然叫 dispatch queue，那它就是 dispatch 的全部

其实不对。

更准确的理解是：

- `DispatchQueue` 负责“投递组织”
- `Dispatcher` 负责“最终处理接口”
- `Messenger` 负责“把两者串起来”

所以这三者是：

- 相关但不同层

这也是这篇最值得建立的核心边界之一。

## 第 5 层：`Messenger::create()` 和 `AsyncMessenger::ready()` 说明通信栈是被“装配起来”的

理解 Ceph 通信层时，一个很好的切入点是：

- 它不是凭空存在的，而是被装配起来的

从主线看，一个典型流程是：

- `Messenger::create()`
- 产出 `AsyncMessenger`
- 各模块 `add_dispatcher_*()`
- `ready()`

### `ready()` 为什么重要

因为它不是一个装饰性函数，而更像通信子系统的：

- 真正启动点

在这一步里，典型动作包括：

- `stack->ready()`
- 启动 processor
- 启动 `DispatchQueue` 线程

这说明消息栈不是：

- 用到的时候自然就活了

而是：

- 需要明确装配和启动的运行时系统

## 第 6 层：发送主线其实非常清晰：`send_to -> connect_to -> send_message`

Ceph 的发送路径看起来复杂，但骨架其实很清楚。

如果只取最核心的一条发送主线，大致就是：

```text
业务模块
  -> Messenger::send_to()
  -> connect_to()
  -> Connection::send_message()
  -> Protocol::send_message()
  -> write_event / write_message
  -> socket send
```

### 为什么这条链值得记住

因为它刚好对应了发送过程的分层：

### `Messenger::send_to()`

- 统一发送入口

### `connect_to()`

- 查找或新建到目标 peer 的逻辑连接

### `Connection::send_message()`

- 把消息挂到这条逻辑会话上

### `Protocol::send_message()`

- 准备协议层编码和排入发送流程

### `write_message()`

- 真正把编码结果写到 socket

也就是说，发送不是“一步发出去”，而是：

- 逐层下钻

## 为什么 `connect_to()` 很能体现 `Messenger` 的总线属性

这一点非常值得讲。

因为 `connect_to()` 说明：

- `Messenger` 自己持有 peer addr 到逻辑连接的全局视图

也就是说：

- 业务模块不用自己管理所有 peer 连接
- `Messenger` 会查已有连接，没有再建新的

这使得业务模块可以站在更高层：

- “我要给谁发消息”

而不必自己处理：

- 连接缓存
- 重用
- 新建时机

这正是总线层该干的事。

## 第 7 层：`ProtocolV1` 和 `ProtocolV2` 不是“另两套业务逻辑”，而是协议层实现

很多初学者看到：

- `ProtocolV1`
- `ProtocolV2`

会下意识觉得这是两套完全不同的消息系统。

更准确的理解是：

- 它们是同一套消息框架下的两种协议层实现

### 它们解决什么问题

- 消息如何编码
- frame / header 如何组织
- 如何读写 socket
- ack 如何处理
- read/write 状态机如何推进

### 它们不解决什么

- 它们不决定业务消息的语义
- 它们不决定消息该交给哪个业务模块

所以它们更像：

- “传输协议实现层”

而不是：

- “新的业务分发系统”

这条边界非常重要，否则很容易把协议细节和消息分发混为一谈。

## 第 8 层：发送时，协议层会把 `Message` 进一步变成真正的线上传输格式

这一点和上一篇客户端 IO 路径正好衔接。

上一篇我们讲到：

- `Objecter` 把请求做成 `MOSDOp`

但 `MOSDOp` 还不是最终 socket 字节流。

真正进一步把它变成线上格式的，是协议层里的动作，比如：

- `prepare_send_message()`
- `write_message()`
- `Message::encode()`

### 这意味着什么

意味着 Ceph 的消息发送至少还分两层：

### 业务消息对象层

- `MOSDOp`
- `MOSDOpReply`
- `MMonMap`
- 其他 `Message` 子类

### 协议帧层

- 具体如何组织 header、payload、frame、ack 等

这也就是为什么消息层值得单独一篇文章来讲。

## 第 9 层：接收主线不是“收完就交业务”，而是 decode、节流、快慢分流后才投递

发送路径明确，接收路径同样非常有层次。

大致主线可以压缩成：

```text
socket 可读
  -> protocol::read_event()
  -> 协议层读入并 decode Message
  -> 节流 / 统计 / stamp
  -> fast preprocess
  -> 能 fast dispatch 就直接走快路径
  -> 否则 enqueue 到 DispatchQueue
  -> DispatchQueue 线程再普通投递
```

### 为什么这条链非常关键

因为它说明 Ceph 不会把“收到消息”粗暴等同于“立刻交给业务模块”。

中间还会经过：

- 协议 decode
- 限流
- fast preprocess
- 快慢路径选择

这反映出 Ceph 消息层不是个轻薄壳，而是一个成熟的收发管线。

## 第 10 层：fast dispatch 为什么存在

这是很多人第一次读消息层时最容易好奇的点。

直觉上你可能会问：

- 既然已经有 `DispatchQueue`，为什么还要 fast dispatch？

答案很直接：

- 因为不是所有消息都值得先排进普通投递队列

有些消息如果已经满足条件，可以尽快进入业务快速路径，这样能减少：

- 多余排队
- 线程切换
- 延迟

### 这就是 fast dispatch 的设计动机

它允许某些 dispatcher 声明：

- 我能快速处理某些消息

然后消息层就在接收后先问：

- 这个消息能不能走 fast dispatch？

如果能，就直接快路径处理；否则再进普通 `DispatchQueue`。

所以 fast dispatch 的本质不是：

- 另一套系统

而是：

- 对普通投递路径的一次性能优化分流

## 第 11 层：`Dispatcher` 自己声明能不能走 fast path

这一点非常优雅，也非常值得记住。

在 Ceph 里，不是所有业务模块都无条件走 fast path，而是由 `Dispatcher` 接口自己表达：

- 我能不能 fast dispatch
- 哪些消息我能 fast dispatch

这意味着：

- 消息层不会替业务乱做决定
- 业务模块可以明确告诉消息层自己的处理能力

这是一种很成熟的契约设计：

- 通用框架负责流程
- 具体业务模块声明能力边界

## OSD 为什么是 fast dispatch 的典型代表

如果你想找一个最典型的 fast dispatch 使用者，那几乎一定是：

- `OSD`

原因也很好理解：

- OSD 承担了高频数据面消息
- 很多 OSD 相关消息走快路径更有意义

这也是为什么你在 OSD 代码里会看到：

- `ms_can_fast_dispatch2()`
- `ms_fast_dispatch()`

这些入口显得格外重要。

所以 fast dispatch 不只是一个抽象概念，它在 OSD 数据面里是真正有现实意义的优化通道。

## 第 12 层：普通 dispatch 依然是消息层的大本营

虽然 fast dispatch 很有名，但不能因此误解为：

- 普通 dispatch 就不重要了

恰恰相反，普通 dispatch 仍然是消息投递的大本营。

其核心主线大致是：

- `DispatchQueue::enqueue()`
- `DispatchQueue::entry()`
- `Messenger::ms_deliver_dispatch()`
- `Dispatcher::ms_dispatch2()`

### 为什么这一层很关键

因为这是真正体现消息层“总线到业务回调”那一刻的地方。

你可以把它理解成：

- `DispatchQueue` 负责“把消息按正确方式取出来”
- `Messenger` 负责“决定按 dispatcher 链交给谁”
- `Dispatcher` 负责“真正执行业务逻辑”

这就是 Ceph 经典消息栈最核心的一条普通投递链。

## 第 13 层：连接事件和普通消息是两条不同但平行的投递线

这是初学者特别容易忽略的一点。

Ceph 消息层里处理的不只是：

- 普通消息

还包括连接事件，比如：

- connect
- accept
- reset
- refused

### 为什么这点重要

因为这说明消息层不仅是“消息载体”，它还是：

- 节点间会话生命周期的发布系统

也就是说，一个业务模块实现 `Dispatcher` 之后，不只是会收到业务消息，还可能收到：

- 连接建立或重置等事件

这对 `OSD`、`MonClient`、`Objecter` 这类模块都非常关键，因为它们需要对连接状态变化作出反应。

所以你可以把 Ceph 消息层理解成：

- 既投递消息
- 也投递会话事件

## 第 14 层：为什么说 `Messenger` 和 `Dispatcher` 的关系像“总线”和“插件”

从架构风格上看，这个比喻非常贴切。

### `Messenger`

- 维护一组 dispatcher
- 统一分发消息和连接事件

### `Dispatcher`

- 通过实现接口，把自己接到消息总线上

这和很多插件式框架非常像：

- 总线负责广播和调度
- 插件负责定义自己如何响应事件

只不过在 Ceph 里，这套机制被用在了节点间分布式通信上。

这个视角很有帮助，因为它能让你在读源码时更自然地接受：

- 为什么 `OSD`、`Objecter`、`MonClient` 都会长得像“消息回调对象”

## 第 15 层：为什么这套分层对 Ceph 这么重要

讲到这里，可以回头看 Ceph 为什么需要把消息层拆成这么多角色。

根本原因就在于，Ceph 是一个：

- 高并发
- 多角色
- 多消息类型
- 高状态复杂度

的分布式系统。

如果不做这种分层，很快就会出现：

- 业务和协议纠缠
- 连接逻辑和业务逻辑纠缠
- 网络线程和业务线程互相阻塞
- 消息投递策略混乱

而 Ceph 的拆法非常清晰：

### `Messenger`

- 管全局

### `Connection`

- 管会话

### `Protocol`

- 管编码和 socket 收发

### `DispatchQueue`

- 管投递队列和线程

### `Dispatcher`

- 管业务回调

这套分层不是为了“抽象而抽象”，而是 Ceph 能长期维持复杂通信行为的工程前提。

## 用一句话重新概括这五个对象

如果你想记最压缩版本，可以记成下面这样：

### `Messenger`

- 消息总线和编排者

### `Connection`

- 到某个 peer 的逻辑会话

### `Protocol`

- 在线路上传输消息的协议实现

### `DispatchQueue`

- 消息和连接事件的投递层

### `Dispatcher`

- 业务处理接口

只要这五句话记住了，Ceph 经典消息栈的骨架就已经很清楚了。

## 把消息层压缩成一条最短主线

如果你只想记一条骨架，可以记成下面这样：

```text
业务模块实现 Dispatcher
  ->
Messenger 挂载这些 Dispatcher
  ->
发送时 Messenger 找/建 Connection
  ->
Connection 把 Message 交给 Protocol 编码并发送
  ->
接收时 Protocol 解码出 Message
  ->
先判断 fast dispatch 还是普通 dispatch
  ->
DispatchQueue 或 fast path 把消息交回 Messenger
  ->
Messenger 再把消息/连接事件分发给具体 Dispatcher
```

只要这条链不丢，你后面去读：

- `Objecter`
- `OSD`
- `MonClient`

里的消息处理代码就会容易得多。

## 初学者最容易混淆的 9 个点

### 1. 认为 `Messenger` 就是一条连接

不对。它更像消息总线和总控枢纽。

### 2. 认为 `Connection` 就是裸 socket

不对。它是逻辑会话，带状态、特性、重连和生命周期语义。

### 3. 认为 `Dispatcher` 和 `DispatchQueue` 是一回事

不对。一个是业务回调接口，一个是投递层。

### 4. 认为消息收到后就直接交给业务模块

不对。中间还有协议 decode、节流、快慢分流和投递层。

### 5. 认为 fast dispatch 是另一套消息系统

不对。它只是普通 dispatch 的优化分流。

### 6. 认为协议层决定业务语义

不对。协议层决定的是传输格式和收发状态机。

### 7. 认为所有业务模块自己管理连接

不对。连接查找/新建由 `Messenger` 统一组织。

### 8. 认为消息层只处理普通消息

不对。连接事件同样是这套系统的重要输入。

### 9. 认为只要会看 `ProtocolV1/V2` 就算看懂消息层

不对。真正完整理解还必须把 `Messenger`、`Connection`、`DispatchQueue`、`Dispatcher` 一起看。

## 这一篇最应该留下的 5 个直觉

### 直觉一：Ceph 的消息层是一套明确拆层的通信框架

不是若干 send/recv 工具函数的集合。

### 直觉二：`Messenger` 是总线，`Dispatcher` 是业务插件接口

这个比喻非常有帮助。

### 直觉三：`Connection` 表示逻辑会话，不是裸 socket

这是理解连接生命周期的关键。

### 直觉四：`DispatchQueue` 负责投递，不负责业务

这条边界必须建立起来。

### 直觉五：fast dispatch 是性能优化分流，不是另起炉灶

理解这一点后，OSD 的消息路径会清楚很多。

## 下一篇看什么

既然这一篇已经把：

- `Messenger`
- `Connection`
- `DispatchQueue`
- `Dispatcher`
- `ProtocolV1/V2`
- fast / normal dispatch

这套经典通信栈讲清楚了，下一步最自然的事情，就是把数据面真正最终落地的那一层拿出来单独讲：

**BlueStore 到底为什么出现，它又为什么能替代 FileStore 成为现代 Ceph 的核心后端？**

所以下一篇建议接：

**《BlueStore 设计与源码：Ceph 为什么放弃 FileStore》**
