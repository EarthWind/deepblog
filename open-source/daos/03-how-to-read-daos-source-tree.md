# 03. 从源码目录看 DAOS：第一次阅读应该按什么顺序

## 这一篇为什么重要

前两篇我们已经回答了两个基础问题：

- DAOS 是什么，它在产品层面解决什么问题？
- DAOS 为什么采用控制面和数据面的双平面架构？

但对真正准备读源码的人来说，接下来会遇到一个更现实的问题：

**面对 `src/` 下面几十个目录，第一次到底该按什么顺序读？**

这是一个比“读哪个函数”更重要的问题。因为如果顺序错了，你很容易出现下面几种情况：

- 一上来钻进 `vos/`，结果只看到大量底层细节，不知道它在整张图里的位置。
- 一上来读 `pool/` 或 `object/`，结果看得见服务实现，却看不清它们依赖哪些公共层。
- 先打开 `engine/init.c`，知道数据面会装模块，但不知道这些模块在系统里各自扮演什么角色。

所以这一篇不打算罗列目录清单，而是给出一条更适合第一次阅读 DAOS 的路线：

1. 先分清哪些目录属于“公共地基”。
2. 再分清哪些目录属于“系统入口层”。
3. 最后再进入真正的“核心服务层”和“存储内核层”。

如果你能按这个顺序读，后面再去追具体调用链，迷路的概率会低很多。

## 先给结论：不要按字母顺序读 `src/`

`src/README.md` 对源码结构的描述其实已经给了一个很重要的提示：

- 每个基础设施库和服务都有独立目录。
- 服务通常同时包含 client side 和 server side 组件。
- client 侧函数通常以 `dc_` 开头，server 侧函数通常以 `ds_` 开头。
- 控制面的 Go 代码集中在 `src/control`。
- 对外 API 头文件在 `src/include`。

这说明 `src/` 不是“一个大程序被拆成很多子目录”，而是一个由多层能力共同组成的系统：

- 最底下是公共库和基础设施层。
- 中间是控制面、数据面和客户端这些入口层。
- 再往上是 pool、container、object 这些服务层。
- 更往下潜还有 VOS、BIO、VEA、RDB、RSVC 这些存储与高可用基础设施。

因此，第一次阅读最忌讳的就是从你碰巧最感兴趣的目录直接开始。更好的方法是先看“层”，再看“点”。

## 第一层：先认识公共地基

第一次读 DAOS，最容易被忽略的，恰恰是最该先建立认知的部分：公共层。

如果没有这层认识，后面看到大量 `dc_`、`ds_`、task、event、dRPC、日志、校验、公共数据结构时，就会感觉所有模块都在重复发明轮子。其实不是，很多能力都已经沉到了公共层。

### `src/include/`：先看系统公开了什么边界

`src/README.md` 明确说，面向最终用户的正式 DAOS API 头文件在 `src/include/`，并且：

- `src/include/daos` 放公共库和客户端可见接口
- `src/include/daos_srv` 放服务端接口

这使得 `src/include/` 很适合作为“总索引”来读。它的价值不在于实现细节，而在于帮你先回答两个问题：

- DAOS 对外暴露了哪些核心概念？
- 哪些能力是客户端边界，哪些能力是服务端内部边界？

比如只看目录名，你就能快速得到一个概念地图：

- `daos_pool.h`
- `daos_cont.h`
- `daos_obj.h`
- `daos_kv.h`
- `daos_array.h`
- `daos_event.h`
- `daos_mgmt.h`

它们几乎就是后续阅读时最重要的名词入口。

所以如果你完全没读过 DAOS，我会建议先不要急着看某个 `.c` 文件，而是先扫一遍 `src/include/`，用它建立术语感。

### `src/gurt/`：最基础的通用运行时能力

虽然 `src/gurt/` 没有单独 README，但从 `src/README.md` 和目录内容可以看出，它承担的是非常基础的公共能力，比如：

- 调试和日志
- 错误码
- 哈希与堆结构
- telemetry
- 故障注入

你可以把 `gurt` 理解成 DAOS 的基础运行时工具箱。它不是“业务逻辑的一部分”，却几乎会出现在所有关键路径里。

第一次阅读时，没必要深究每个实现文件，但应该建立这样一个认识：

- 看到 `gurt/` 相关接口时，不要把它误以为某个具体服务逻辑。
- 它更像整个项目共用的最底层基础设施。

### `src/common/`：比 `gurt` 更贴近 DAOS 语义的公共层

如果说 `gurt` 更偏通用基础能力，那么 `src/common/` 就更像“已经带有 DAOS 语义的公共库”。

`src/common/README.md` 提到这里承载的能力包括：

- hash 和 checksum
- event / event queue
- logging / debugging
- locking primitives
- network transport

更重要的是，这里还有一个第一次读源码时非常值得留意的概念：**TSE**。

README 里明确说：

- TSE 是一个通用任务调度引擎。
- 它支持任务、依赖图和非阻塞推进。
- `libdaos` 在其上构建更高层的 task API。

这意味着什么？

意味着如果你后面在客户端 API 或某些服务逻辑里看到 task、scheduler、event，不要以为每个模块都自己实现了一套异步框架。很多异步执行基础实际上已经在 `common/` 层准备好了。

所以第一次阅读 `src/common/` 的目标，不是读完每个实现文件，而是先建立一个判断：

- 哪些能力是所有模块共享的公共机制？
- 哪些调用和结构是“DAOS 通用语义”，而不是某个服务自己的私货？

## 第二层：再认识系统入口层

在公共地基之上，DAOS 有三个最值得先建立全局认知的入口层：

- `src/control`
- `src/engine`
- `src/client`

理解它们的价值，在于你会知道系统是从哪里“跑起来”的，而不是只知道模块“存在于哪里”。

### `src/control/`：控制面入口

这一层我们在上一篇已经讲过重点。它的核心作用是：

- 管理配置
- 编排节点资源
- 启动和监控数据面
- 暴露管理接口

第一次阅读 `src/control/`，重点不是扫所有 Go 包，而是先抓住几个入口：

- `src/control/README.md`
- `src/control/cmd/daos_server/main.go`
- `src/control/cmd/dmg/main.go`
- `src/control/cmd/daos_agent/main.go`

为什么这几个入口重要？

因为它们能帮你先看明白：

- 管理命令从哪里进入系统
- 控制面如何组织自己的命令和服务
- agent 在客户端链路里处于什么位置

换句话说，`src/control/` 是理解“系统怎么被管理”的入口。

### `src/engine/`：数据面入口

如果 `src/control/` 是“管理入口”，那 `src/engine/` 就是“执行入口”。

`src/engine/README.md` 已经很清楚地描述了：

- `daos_engine` 是多线程、基于 Argobots 的非阻塞进程
- 它支持模块化装载
- 模块可以注册 CART RPC handler 和 dRPC handler
- 每个 xstream 都有自己的 TLS 空间

因此第一次阅读 `src/engine/` 的重点也不是读完每个 C 文件，而是先抓住：

- `src/engine/README.md`
- `src/engine/init.c`
- `src/engine/module.c`

你真正想搞清楚的是：

- 数据面线程和执行模型大致怎么组织
- 模块是怎么装进去的
- 服务能力为什么会集中挂在 `daos_engine` 下面

只有把这层读清楚，后面去看 `pool/`、`object/` 才不会把它们误以为独立程序。

### `src/client/`：客户端入口

很多第一次读 DAOS 的人只盯着 server 端，这是不够的。

`src/client/README.md` 很短，但其实已经把客户端层次概括出来了：

- `libdaos`
- Python bindings
- Go bindings
- `libdfs`
- `libds3`

这说明 `src/client/` 不是一个“小配角”，而是连接应用与 DAOS 系统的重要入口层。

第一次阅读时，建议优先看：

- `src/client/README.md`
- `src/client/api/README.md`
- `src/client/api/init.c`
- `src/client/dfs/README.md`

为什么要这么早看客户端？

因为这会帮你建立一个非常关键的认知：

- DAOS 的很多名词不是从 server 端开始定义的，而是从“客户端如何使用这些能力”开始变得清晰。
- `pool`、`container`、`object` 不只是服务端模块名，它们也是客户端 API 视角下真正可操作的对象。

因此，`src/client/` 是理解“请求如何进入系统”的入口。

## 第三层：再进入核心服务层

当你已经分清公共层和入口层之后，才适合真正进入 DAOS 的服务层。

这一层最值得优先读的通常是：

- `src/pool`
- `src/container`
- `src/object`
- `src/rebuild`
- `src/mgmt`

### 为什么先看 `pool`

`pool` 是资源边界、成员信息、目标分布和很多上层语义的前置条件。

如果没理解 `pool`，后面看对象布局、容器状态和故障恢复时，就容易缺背景板。

所以第一次进入服务层时，`src/pool/README.md` 往往比 `object` 更适合作为第一站。

### 为什么 `container` 紧跟其后

`container` 在 DAOS 里不只是“命名空间”，它还和事务、snapshot、handle 状态、对象分配等语义紧密相关。

因此阅读顺序上，`container` 通常应该在 `object` 前面。因为你只有先理解 container 层的状态边界，后面再看 object 才不会只剩“对象怎么发 RPC”这一层理解。

### 为什么 `object` 放在服务层阅读的中后段

很多人会被 `object` 这个名字吸引，想早点看它。但实际上，`object` 层对很多基础概念有依赖：

- pool map
- container state
- placement
- checksum
- replication / EC

所以第一次阅读时，如果你把 `object` 放得太靠前，读起来通常会非常碎。

更合理的顺序是：

1. 先看 `pool`
2. 再看 `container`
3. 再看 `object`

这样你会感觉对象层是在前两层语义之上展开，而不是一组凭空出现的 I/O 逻辑。

### `rebuild` 和 `mgmt` 应该怎么看

`rebuild` 和 `mgmt` 也很重要，但它们更像“跨层协作模块”：

- `mgmt` 更贴近系统管理和服务端管理逻辑
- `rebuild` 则把 pool、container、object 等模块串起来处理故障恢复

因此，第一次读源码时，不建议把它们放在最前面，而是放在你已经理解主要对象模型之后再看。这样更容易看明白它们到底在协调什么。

## 第四层：最后再下潜到存储内核和基础设施深处

真正的“硬骨头”通常在这一层：

- `src/vos`
- `src/bio`
- `src/vea`
- `src/rdb`
- `src/rsvc`
- `src/placement`
- `src/cart`

这些目录非常重要，但并不适合完全零背景时就直接读。

### `src/vos/`：本地存储内核

VOS 是 DAOS 最核心的本地存储实现之一。你几乎可以把它看成“上层对象和容器语义最终落盘到哪里”的关键答案。

但也正因为它太核心，所以如果你在完全没建立对象模型和服务层认知之前就直接钻进去，通常会很痛苦。

因此 VOS 更适合放在：

- 你已经理解 pool/container/object 的基本关系之后
- 你已经知道数据面是如何装载服务模块之后

这时再读 `src/vos/README.md`，你会更容易理解它为什么叫 Versioning Object Store。

### `src/bio/` 与 `src/vea/`：向介质层下潜

`src/README.md` 已经说明：

- BIO 负责 blob I/O
- VEA 负责 NVMe 空间分配

这两层是典型的“再往下一层”的阅读对象。它们很适合在你已经理解 VOS 之后继续往下追，而不适合作为第一次阅读 `src/` 的入口。

### `src/rdb/` 与 `src/rsvc/`：元数据高可用基础设施

第一次读源码时，很多人容易把对象复制和 RDB/RSVC 混在一起。

其实 `src/README.md` 说得很清楚：

- RSVC 是 replicated service framework
- RDB 是 replicated key-value store over Raft

它们主要服务于 pool、container、management 这些元数据相关服务，而不是直接等价于对象数据复制。

因此这层更适合在你理解服务层之后，再作为“服务高可用基础设施”来读。

### `src/placement/` 与 `src/cart/`：一个管布局，一个管通信

这两个目录也很重要：

- `placement` 决定对象如何在 targets 上布局
- `cart` 承担高性能通信

但它们都不是最适合第一次阅读的起点。原因很简单：

- 如果你还没理解对象和 target 的关系，看 placement 很容易只剩算法名词。
- 如果你还没理解控制面/数据面和客户端/服务端关系，看 cart 也容易只剩通信细节。

所以它们更适合在第二轮、第三轮阅读时作为专题深入。

## 一条更实用的阅读顺序

如果让我给第一次阅读 DAOS 的人安排一条顺序，我会建议按下面的路线走。

### 第一阶段：先建立地图

先读：

- `README.md`
- `docs/overview/architecture.md`
- `docs/overview/storage.md`
- `src/README.md`

目标：

- 知道 DAOS 的产品目标
- 知道控制面、数据面、客户端三类角色
- 知道 `src/` 里大概有哪些层

### 第二阶段：先认识公共层和边界

再读：

- `src/include`
- `src/common/README.md`
- `src/control/drpc/README.md`

目标：

- 建立 API 术语感
- 理解 task / event / dRPC 这些公共机制
- 先知道系统有哪些共用基础设施

### 第三阶段：抓住三个入口

接着读：

- `src/control/README.md`
- `src/control/cmd/daos_server/main.go`
- `src/engine/README.md`
- `src/engine/init.c`
- `src/engine/module.c`
- `src/client/README.md`
- `src/client/api/init.c`

目标：

- 知道系统怎么启动
- 知道数据面怎么装载模块
- 知道客户端请求从哪里进入 DAOS

### 第四阶段：进入核心服务层

然后读：

- `src/pool/README.md`
- `src/container/README.md`
- `src/object/README.md`
- `src/rebuild/README.md`

目标：

- 理解资源边界
- 理解事务与容器状态边界
- 理解对象模型和数据保护
- 理解故障恢复主线

### 第五阶段：最后下潜到底层

最后读：

- `src/vos/README.md`
- `src/bio/README.md`
- `src/vea/README.md`
- `src/rdb/README.md`
- `src/rsvc/README.md`
- `src/placement/README.md`
- `src/cart/README.md`

目标：

- 追到底层存储落盘机制
- 理解元数据复制框架
- 理解布局和通信基础设施

## 一个很实用的判断法：读目录时先问“它属于哪一层”

第一次阅读 `src/` 时，最实用的技巧不是背目录，而是每打开一个目录都先问一句：

**这个目录属于哪一层？**

你大致可以这样判断：

- 如果它提供日志、任务、校验、通用结构，多半是公共层
- 如果它是 `control`、`engine`、`client`，多半是入口层
- 如果它是 `pool`、`container`、`object`，多半是核心服务层
- 如果它是 `vos`、`bio`、`vea`、`rdb`、`rsvc`，多半是存储内核或高可用基础设施层

只要先把层次归对，阅读难度就会下降很多。因为你不会再要求每个目录都单独解释整个系统，也不会误把基础设施目录当成功能入口。

## 小结

第一次读 DAOS，最重要的不是“先看哪个最核心”，而是“先建立怎样的阅读顺序”。

更准确地说，DAOS 并不存在唯一正确的阅读入口，但存在一条明显更省力的路线：

**先看公共地基，再看入口层，再看服务层，最后再下潜到底层存储与高可用基础设施。**

如果把这条路线压缩成一句话，那就是：

**先看系统边界，再看进程入口，再看服务模型，最后看落盘与复制实现。**

一旦按这个顺序建立起整体地图，后面不管你是继续追 `daos_server` 启动链路，还是进入 `pool/container/object`，又或者最终深挖 `vos`，都会知道自己现在站在整张图的哪一层。

## 下一篇看什么

理解完源码目录阅读顺序之后，下一步就可以正式进入真实启动链路了。

最自然的下一篇是：

**`daos_server` 启动时做了什么：控制面主进程剖析**

因为从这里开始，我们就不再只是“认路”，而是要真正沿着控制面的 `main()` 往下走，看一个 DAOS 节点是如何被拉起来的。
