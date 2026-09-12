# RBD 读写路径源码：一次块设备 IO 如何落到 Ceph 集群

## 这篇文章要解决什么问题

上一篇我们已经把 `RBD` 的整体架构讲清楚了：

- `RBD` 不是底层新存储，而是建立在 `RADOS` 之上的块语义层
- 一个 image 会拆成多个对象
- `librbd` 和 `krbd` 是两条不同的客户端接入路径
- QEMU 通常直接走 `librbd`

但如果你继续往实现里追，马上就会碰到一个更关键的问题：

**一次块设备 IO，到底是怎样从“块偏移读写”变成“Ceph 对象请求”的？**

也就是：

- `librbd` 里一次 `aio_read/aio_write` 到底走了哪些核心层？
- image 级请求是怎样被拆成 object 级请求的？
- `librbd` 最终是怎样把这些 object 请求交给 `RADOS` 的？
- `krbd` 这条内核态路径又和 `librbd` 有什么本质不同？

如果只允许用一句话先给结论，那就是：

**RBD IO 路径的本质，是把“image 偏移上的块请求”拆成“一组 object 偏移上的对象请求”，再通过用户态 `librbd` 或内核态 `krbd` 接入 Ceph；其中 `librbd` 的核心主线是 `ImageDispatch -> ImageRequest -> ObjectDispatch -> ObjectRequest -> RADOS execute`。**

这一篇的目标，就是把这条链彻底讲清楚。

## 先建立第一条边界：RBD IO 路径不是“块请求直接发给 OSD”

初学者第一次想象 RBD 读写时，最容易脑补成这样：

```text
块设备 read/write
  -> 立刻发往 Ceph
  -> OSD 收到
```

这当然不是完全错，但中间省掉了最关键的一层：

- **块地址空间到对象地址空间的翻译**

这一步正是 RBD 存在的核心价值。

所以更准确的主线应该是：

```text
块请求
  -> image 级请求
  -> object 级拆分
  -> RADOS 对象请求
  -> OSD
```

这也是为什么你真正读源码时，会发现 `librbd` 里最关键的东西不是“如何发网络包”，而是：

- 如何在 image 层组织请求
- 如何把 image extents 映射到 object extents
- 如何把一个块请求拆成多个并发对象请求

## 第二张图：RBD IO 路径的全景图

先建立一张最重要的总图：

```text
上层应用 / QEMU / 文件系统
  |
  +-- 用户态路径
  |     |
  |     v
  |   librbd API
  |     |
  |     v
  |   ImageDispatchSpec / ImageDispatcher
  |     |
  |     v
  |   ImageRequest
  |     |
  |     v
  |   ObjectDispatchSpec / ObjectDispatcher
  |     |
  |     v
  |   ObjectRequest
  |     |
  |     v
  |   RADOS execute
  |     |
  |     v
  |   OSD
  |
  +-- 内核态路径
        |
        v
      krbd / Linux rbd 驱动
        |
        v
      内核 Ceph 客户端
        |
        v
      OSD
```

这张图最重要的结论有两个：

### 1. 用户态 `librbd` 路径有非常清晰的“两级拆分”

- image 级
- object 级

### 2. `krbd` 不是 `librbd` 的简单薄壳

- 它是另一条内核态接入路径

所以这篇最好的写法，不是强行把两条路径揉成一条，而是：

- 先把 `librbd` 主线讲透
- 再讲 `krbd` 的边界和接入方式

## 第 1 层：为什么 `librbd` 路径最值得先讲

如果你的目标是理解：

- 块请求如何一步步变成对象请求

那最值得先抓住的路径几乎一定是：

- `librbd`

### 为什么

因为在 Ceph 仓库里，`librbd` 的整条调用链是完整可见的：

- API 入口
- image 级调度
- object 级拆分
- RADOS 提交

而 `krbd` 这条路径，在 Ceph 仓库里能看到的是：

- 用户态映射桥接层

真正的内核块请求处理主线并不在这个仓库里，而在 Linux 内核源码树中。

所以如果你想在 Ceph 仓库内部把一条“完整且可追踪”的 IO 路径讲清楚，最佳主线一定是：

- `librbd`

## 第 2 层：`librbd::api::Io` 是用户态块请求的 API 入口

如果从最上层开始看，`librbd` 数据面最自然的入口之一就是：

- `librbd::api::Io`

这里能直接看到类似：

- `aio_read`
- `aio_write`

### 为什么这个入口重要

因为它代表了：

- 用户态应用把“块设备语义请求”正式送进 `librbd`

从这一层开始，请求还保留的是非常明显的块设备视角：

- 读哪个 image
- 偏移是多少
- 长度是多少

但很快，`librbd` 就会把它包装成更正式的内部请求对象。

## 第 3 层：`ImageDispatchSpec` 是 image 级 IO 请求信封

在现代 `librbd` 里，一个非常值得抓住的核心对象是：

- `ImageDispatchSpec`

### 为什么这个对象特别重要

因为它代表：

- 一次 image 级 IO 请求的正式封装

也就是说，请求到了这里，已经不再只是：

- “某个 API 被调用”

而是被包装成一个带完整上下文的 image 级请求信封。

这里面通常会带上：

- `AioCompletion`
- image extents
- 请求类型
- 读写缓冲相关信息

### 这说明什么

说明 `librbd` 的第一步不是直接拆对象，而是：

- 先把“针对 image 的请求”组织好

所以 `ImageDispatchSpec` 可以理解为：

- **块请求进入 image 级调度系统的门票**

## 第 4 层：`ImageDispatcher` 为什么是理解 `librbd` 的关键责任链

请求一旦进入 `ImageDispatchSpec`，下一步通常就是：

- `ImageDispatcher::send_dispatch`

这一层特别值得讲，因为它能体现现代 `librbd` 的架构风格。

### 它不是简单转发器

`ImageDispatcher` 更像：

- image 级责任链调度器

这里会依次经过不同 dispatch layer，比如：

- core
- queue
- qos
- refresh
- write_block

### 为什么要有这一层

因为 `librbd` 的 image 级请求在真正拆成对象请求之前，往往还要经过：

- 请求裁剪
- 只读检查
- 队列调度
- QoS 限制
- 刷新相关控制

所以 `ImageDispatcher` 的意义不是：

- 多一层架子

而是：

- 在 image 级把不同横切逻辑组织成统一责任链

## 第 5 层：`ImageDispatch` 是“开始真正进入 image IO 语义”的地方

如果说 `ImageDispatchSpec` 是信封，`ImageDispatcher` 是责任链，那么：

- `ImageDispatch`

就是请求真正开始进入 image IO 语义处理的地方。

这里最值得记住的入口包括：

- `ImageDispatch::read`
- `ImageDispatch::write`

### 它们做的关键事情是什么

不是立即提交 RADOS，而是把请求继续交给：

- `ImageRequest::aio_read`
- `ImageRequest::aio_write`

这说明 `ImageDispatch` 更像：

- image 层调度框架和具体 image 请求实现之间的桥

所以如果你画调用链，这一层是很好的中间锚点。

## 第 6 层：`ImageRequest` 才是真正把块请求拆成对象请求的核心

如果这一篇只允许记住一个最重要的类，我会选：

- `ImageRequest`

因为它正是整条 RBD IO 路径的核心转折点。

### 为什么这么说

因为在这之前，请求主要还是：

- image 偏移和长度

到了这里，系统才真正开始做最关键的事情：

- 把 image extents 映射成 object extents

这一步是 RBD 成立的灵魂。

## `ImageRequest` 在做什么

可以把它的核心工作概括成三件事：

### 1. 接住 image 级读写请求

也就是：

- 这次访问的是 image 哪段逻辑地址空间

### 2. 把 image 区间拆成 object 区间

也就是：

- 命中了哪些对象
- 每个对象内偏移是多少
- 每个对象需要读写多少数据

### 3. 为每个对象构造 object 级请求

也就是：

- 后续交给 `ObjectDispatchSpec`

### 这一步为什么是整条路径里最关键的地方

因为 RBD 的本质问题，正是在这里被解决的：

- 块语义如何翻译成对象语义

## 第 7 层：为什么 image -> object 的映射是整条路径最值得建立直觉的部分

这是 RBD 读写源码里最关键的“认知门槛”。

如果你没建立这个直觉，后面看什么都会觉得只是：

- 一堆请求对象互相转来转去

但一旦建立起来，整条链就会非常清晰。

### 一个块请求为什么会命中多个对象

因为 image 是按 striping 和 object layout 被切开的。

所以上层一次：

- 从偏移 X 读 Y 字节

在底层可能变成：

- 命中对象 A 的一段
- 命中对象 B 的一段
- 命中对象 C 的一段

### 这意味着什么

意味着一次块 IO，在 `librbd` 内部可能天然就是：

- 多个对象 IO 的并发聚合

这也是为什么 `AioCompletion` 在 `librbd` 里那么重要。

## 第 8 层：`AioCompletion` 为什么是这条路径的总完成对象

一旦 image 请求被拆成多个 object 请求，就必然会遇到一个问题：

- 谁来代表这次上层 IO 的“整体完成”？

答案就是：

- `AioCompletion`

### 为什么它重要

因为对上层来说，请求仍然是：

- 一次 image 读
- 或一次 image 写

而不是：

- 5 个对象读
- 7 个对象写

所以 `librbd` 需要一个总完成对象来：

- 聚合多个 object 级请求的完成状态
- 在全部必要子请求结束后，再向上报告 image 请求完成

这也是为什么现代 `librbd` 的结构既有层次又能保持对上层接口简洁。

## 第 9 层：`ObjectDispatchSpec` 和 `ObjectDispatcher` 是 object 级责任链入口

当 `ImageRequest` 完成 image -> object 拆分之后，接下来会发生的事情是：

- 为每个对象创建 `ObjectDispatchSpec`
- 再交给 `ObjectDispatcher`

### 这一层的定位怎么理解

非常类似于 image 级那套结构：

- `ImageDispatchSpec` 对应 image 级请求信封
- `ObjectDispatchSpec` 对应 object 级请求信封

### 为什么还要再来一套 object 级 dispatch

因为 object 级请求同样可能需要经过一些统一处理逻辑，比如：

- 读写策略
- copyup
- object-map
- parent 读取
- 各类对象级状态处理

这说明现代 `librbd` 的架构很一致：

- image 级有一套调度责任链
- object 级也有一套调度责任链

## 第 10 层：`ObjectDispatch` 是 object 级读写的统一落点

接下来最值得抓的对象就是：

- `ObjectDispatch`

这里典型的入口包括：

- `ObjectDispatch::read`
- `ObjectDispatch::write`

### 这层在干什么

它把 object 级请求继续交给真正的 object request 实现，也就是：

- `ObjectReadRequest`
- `ObjectWriteRequest`
- 以及更抽象的 `AbstractObjectWriteRequest`

所以你可以把 `ObjectDispatch` 理解成：

- object 级责任链和真正对象请求执行体之间的桥

## 第 11 层：`ObjectRequest` 才是单对象请求真正提交到 RADOS 的地方

如果 `ImageRequest` 是块语义转对象语义的核心，那么：

- `ObjectRequest`

就是对象语义真正开始变成 RADOS 请求的核心。

### 为什么这层特别重要

因为到了这里，请求已经缩小成：

- 针对单个对象的一次读或写

于是系统终于可以做最接近底层对象世界的动作：

- 构造 `ReadOp`
- 构造 `WriteOp`
- 调 `image_ctx->rados_api.execute(...)`

这一步就是整条 `librbd` 路径最关键的“出用户态块语义、入 RADOS 对象语义”的边界。

## 第 12 层：为什么现在更应该说 `librbd` 最终落到 “RADOS 异步提交层”

这里有一个很值得澄清的细节。

很多人讲旧版 `librbd` 路径时会直接说：

- 最终调用 `librados::IoCtx::aio_read/aio_write`

从架构理解上，这种说法不算错得很离谱，因为它抓住了：

- 最终是异步提交到 RADOS

但从当前代码实现看，更准确的说法是：

- `librbd` 当前数据面主路径更多通过 `ImageCtx::rados_api`
- 也就是 `neorados::RADOS` 的 `execute(...)`

### 为什么要澄清这点

因为这样你才能同时兼顾：

- 现代实现的准确性
- 对旧资料和经典 `librados AIO` 心智模型的兼容

所以最稳妥的博客表述应该是：

- `librbd` 当前对象 IO 最终落到 RADOS 异步提交层
- 如果从经典 `librados` 角度理解，其语义可类比 `IoCtxImpl::aio_read/aio_write/aio_operate`

## 第 13 层：把 `librbd` 读路径压缩成一条主线

如果只看读路径，可以先记成下面这样：

```text
Io::aio_read
  ->
ImageDispatchSpec::create_read(...)->send()
  ->
ImageDispatcher::send_dispatch
  ->
ImageDispatch::read
  ->
ImageRequest::aio_read
  ->
ImageReadRequest::send_request
  ->
image extents -> object extents
  ->
ObjectDispatchSpec::create_read
  ->
ObjectDispatch::read
  ->
ObjectReadRequest::read_object
  ->
RADOS execute
```

这条链里最重要的三个节点是：

- `ImageDispatchSpec`
- `ImageRequest`
- `ObjectRequest`

## 第 14 层：把 `librbd` 写路径压缩成一条主线

写路径的骨架也类似，只是通常会多出更多写相关逻辑。

可以先记成：

```text
Io::aio_write
  ->
ImageDispatchSpec::create_write(...)->send()
  ->
ImageDispatcher::send_dispatch
  ->
ImageDispatch::write
  ->
ImageRequest::aio_write
  ->
AbstractImageWriteRequest::send_request
  ->
image extents -> object extents
  ->
ObjectDispatchSpec::create_write
  ->
ObjectDispatch::write
  ->
ObjectWriteRequest::write_object
  ->
RADOS execute
```

### 为什么写路径比读路径更值得下篇继续深挖

因为写路径还会更自然地牵出很多高级语义，比如：

- journal
- object-map
- copyup
- parent/child 关系
- snapshot 相关写语义

所以这一篇先把骨架立住，下一篇高级特性篇再进一步展开是最合适的。

## 第 15 层：为什么说 `librbd` 的核心复杂度不在“怎么发请求”，而在“怎么拆请求”

这是整篇最想让读者留下的直觉之一。

因为如果你只盯着：

- 最后是 `execute(...)`

你会以为 `librbd` 的核心只是某种 RADOS 包装器。

其实不是。

真正困难的地方在于：

- image 偏移如何映射到底层对象布局
- 一个块请求如何拆成多个对象请求
- 多个对象请求如何并发又如何聚合
- 对象级特性逻辑如何重新组合成块设备语义

所以 `librbd` 的难点主要在：

- **语义翻译和调度分层**

而不是简单的网络发包。

## 第 16 层：`krbd` 为什么要单独讲，而且不能和 `librbd` 强行揉成一条

到这里，自然要切到另一条路径：

- `krbd`

很多人会下意识想：

- 既然也是 RBD，那它是不是只是 `librbd` 的内核版？

这个理解太粗糙了。

更准确地说：

- `krbd` 是内核态 RBD 客户端接入路径

但在 Ceph 仓库里，你主要能看到的是：

- 用户态工具如何把一个 image 映射进 Linux 内核

而不是 Linux 内核内部怎样处理后续 BIO。

所以这条路径讲解时，必须明确边界。

## 第 17 层：在 Ceph 仓库里，`krbd` 路径最关键的入口在哪里

如果从 Ceph 仓库本身看，`krbd` 路径最值得抓的几个入口是：

- `rbd device map`
- `Kernel.cc`
- `krbd.h`
- `krbd.cc`

### 这条链在做什么

它并不是在用户态自己执行所有块 IO，而是在做：

- 收集 monitor 地址
- 准备认证信息
- 拼接映射参数
- 调 `krbd_map`
- 通过 `/sys/bus/rbd/add` 把映射请求交给 Linux 内核 rbd 驱动

也就是说，Ceph 仓库里你能看到的关键动作是：

- **把 image 注册给内核**

而不是：

- **内核里后续每个块请求怎样拆成对象请求**

## 第 18 层：为什么 `krbd` 真正的数据面后半段不在这个仓库里

这一点必须说清楚，否则读者会以为仓库里缺了半截。

事实是：

- 真正的 Linux 内核 `rbd` 驱动实现不在 Ceph 仓库里
- 它在 Linux 内核源码树中

所以从 Ceph 仓库视角，我们最多能清楚讲到：

```text
rbd device map
  ->
Kernel.cc
  ->
krbd_map
  ->
/sys/bus/rbd/add
  ->
Linux 内核 rbd 驱动接管
```

接下来真正的：

- block layer
- BIO 处理
- 内核 Ceph messenger
- 内核 OSD client

都已经进入 Linux 内核实现范围。

### 为什么这一点反而是很好的博客内容

因为它能帮助读者建立一条非常清晰的边界：

- `librbd` 路径在 Ceph 仓库里基本可完整追
- `krbd` 路径在 Ceph 仓库里只能追到“进入内核”为止

这比硬把两者讲成一样清楚要诚实得多，也更容易理解。

## 第 19 层：所以 `krbd` 路径最准确的讲法是什么

最稳妥的说法是：

- `krbd` 不是 Ceph 仓库里另一套完整块 IO 执行器
- 它是“Ceph 用户态工具 + Linux 内核 rbd 驱动”共同组成的内核态接入路径

从架构图上可以把它写成：

```text
应用 / 文件系统
  ->
/dev/rbdX
  ->
Linux block layer
  ->
Linux rbd 驱动
  ->
内核 Ceph 客户端
  ->
Ceph 集群
```

而 Ceph 仓库这里负责的是：

- 让这条映射关系建立起来

## 第 20 层：把双路径真正放在一起看

现在可以把 `librbd` 和 `krbd` 真正放在一起对照了。

### 用户态路径

```text
QEMU / 应用
  ->
librbd
  ->
image 级调度
  ->
object 级调度
  ->
RADOS execute
  ->
Ceph 集群
```

### 内核态路径

```text
文件系统 / 块层应用
  ->
/dev/rbdX
  ->
krbd / Linux rbd 驱动
  ->
内核 Ceph 客户端
  ->
Ceph 集群
```

### 两条路径最大的共同点

- 最终都要把块语义翻译成 Ceph 对象访问

### 两条路径最大的差别

- `librbd` 把大部分逻辑放在用户态库里
- `krbd` 把后半段逻辑交给 Linux 内核

这就是理解 RBD IO 路径时最值得留下的总体对比。

## 用一句话重新概括第 19 篇的主线

如果把这篇全部内容压缩成一句尽量准确的话，我会这样说：

**一次 RBD 块设备 IO 的关键，不是“直接发给 Ceph”，而是先在客户端侧完成从 image 地址空间到 object 地址空间的映射；在用户态路径里，这条主线由 `ImageDispatch -> ImageRequest -> ObjectDispatch -> ObjectRequest -> RADOS execute` 串起，在内核态路径里则由 `krbd` 把映射建立后交给 Linux 内核 rbd 驱动继续执行。**

## 把整篇压缩成一条最短骨架

如果你只想记一条骨架，可以记成下面这样：

```text
块请求先命中 image 地址空间
  ->
librbd 把 image 区间拆成 object 区间
  ->
每个对象请求分别提交到 RADOS
  ->
RADOS 再下发到 OSD

或

krbd 先把 image 映射进 Linux 内核
  ->
后续块请求由内核 rbd 驱动接管并进入 Ceph
```

这条链里最重要的直觉只有一个：

- **RBD IO 的本质永远是“块到对象”的翻译。**

## 初学者最容易混淆的 10 个点

### 1. 认为块请求会直接发给 OSD

不对。中间一定要经过 image -> object 映射。

### 2. 认为 `librbd` 只是对 `librados` 的简单薄封装

不对。它的核心复杂度在 image/object 两级调度和语义翻译。

### 3. 认为 `ImageDispatcher` 只是转发层

不对。它是 image 级责任链。

### 4. 认为 `ImageRequest` 只是个普通请求对象

不对。它正是块地址空间拆成对象地址空间的核心。

### 5. 认为 `ObjectRequest` 只是网络层细节

不对。它是单对象请求真正进入 RADOS 的关键边界。

### 6. 认为当前 `librbd` 仍然只是直接调 `IoCtx::aio_read/aio_write`

不够准确。现代主路径更接近 `rados_api.execute(...)`。

### 7. 认为 `krbd` 能在 Ceph 仓库里完整追到每个块请求的执行细节

不对。真正的内核数据面后半段在 Linux 内核源码里。

### 8. 认为 `krbd` 和 `librbd` 是简单一一对应的“内核版/用户态版”

不对。它们是两条接入方式不同、边界不同的路径。

### 9. 认为 `AioCompletion` 只代表某个对象请求完成

不对。它经常代表整个 image 请求的聚合完成。

### 10. 认为理解 RBD IO 路径只需要看最后 RADOS 提交

不对。真正最值得理解的是前面的拆分与调度。

## 这一篇最应该留下的 5 个直觉

### 直觉一：RBD IO 路径的灵魂是 image -> object 映射

这是第一核心。

### 直觉二：`ImageRequest` 是用户态路径最关键的转折点

它把块请求真正拆成对象请求。

### 直觉三：`ObjectRequest` 是单对象请求进入 RADOS 的边界

这一步是第二核心。

### 直觉四：`librbd` 走完整用户态链路，`krbd` 进入内核态链路

两条路径不要混着讲。

### 直觉五：Ceph 仓库里能完整讲清 `librbd`，但 `krbd` 只能讲到“如何交给内核”

这条边界本身就是理解的一部分。

## 下一篇看什么

既然这一篇已经把：

- `librbd` 的 image/object 两级调度
- `ImageRequest`
- `ObjectRequest`
- `RADOS execute`
- `krbd` 的映射边界

这条 RBD IO 主线讲清楚了，下一步最自然的事情，就是把 RBD 那些真正决定生产能力上限的高级机制拆开讲：

**为什么 `layering`、`object map`、`exclusive lock`、`journaling` 这些特性能让 RBD 从“可用”变成“好用”？**

所以下一篇建议接：

**《RBD 高级特性详解：快照、克隆、特性位与镜像元数据设计》**
