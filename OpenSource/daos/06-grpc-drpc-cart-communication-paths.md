# 06. gRPC、dRPC 与 CART：DAOS 为什么同时维护三条通信通路

## 这一篇为什么必须写

写到这里，DAOS 的整体轮廓已经逐渐清楚了：

- 我们知道它是分布式异步对象存储。
- 我们知道它有控制面和数据面双平面。
- 我们知道 `daos_server` 如何启动控制面，`daos_engine` 如何装载模块。

但只要你继续往下读源码，很快就会遇到一个非常自然、也非常容易困惑的问题：

**为什么 DAOS 同时维护 gRPC、dRPC 和 CART 三条通信通路？**

第一次看 DAOS 仓库时，这种困惑几乎是必然的：

- `src/control` 到处是 gRPC 和 protobuf
- `src/control/drpc`、`src/common`、`src/engine` 又在讲 dRPC
- `src/cart` 则是另一套完全不同的高性能通信栈

如果不把这三条通路的边界和动机讲清楚，后面你去看：

- `dmg` 命令
- `daos_agent`
- `daos_server`
- `daos_engine`
- `libdaos`

就很容易把“谁通过什么和谁说话”完全混在一起。

所以这一篇的任务，就是用一个非常务实的角度回答这个问题：

1. gRPC、dRPC、CART 分别服务哪一类通信？
2. 它们各自位于系统的哪条边界上？
3. 为什么 DAOS 没有选择“一种协议走到底”？

## 先给答案：三条通路服务的是三种完全不同的边界

`src/README.md` 已经把答案说得很明确：

- gRPC 提供 DAOS 管理通道
- dRPC 是基于 Unix Domain Socket 的本地进程间通信
- CART 是面向数据面的低延迟高带宽通信库

如果把这句话翻成更直白的话，可以得到这样一张逻辑图：

- **gRPC**：给控制面管理世界用
- **dRPC**：给同机本地进程协作用
- **CART**：给客户端到数据面、以及数据面之间的高性能数据路径用

所以 DAOS 不是无缘无故地维护三种通信机制，而是因为它本来就有三条完全不同的通信边界：

1. 管理边界
2. 本机进程边界
3. 高性能数据边界

一旦按边界来理解，这三条通路就不再显得混乱了。

## 先看 gRPC：它服务的是控制面管理世界

### gRPC 在 DAOS 里的定位很明确

`src/README.md` 在 Network Transport and Communications 一节里直接说：

- gRPC provides a bi-directional secured channel for DAOS management
- relies on TLS/SSL to authenticate the administrator role and the servers

这意味着 gRPC 从一开始就不是为数据路径设计的，而是为**管理通道**设计的。

也就是说，在 DAOS 里，gRPC 的问题域主要是：

- 管理员如何和 `daos_server` 通信
- 控制面节点之间如何做控制类交互
- 管理服务 leader 如何承接系统级请求

### 哪些角色在用 gRPC

结合 `src/control/server/README.md`，gRPC 主要出现在下面这些角色之间：

- `dmg` 与 `daos_server`
- 控制面 client API 与 `daos_server`
- 一个 `daos_server` 到其他 `daos_server` 的 fan-out
- 非 access point 节点加入系统时对 MS leader 的控制面请求

也就是说，gRPC 承担的是“控制面之间”或“控制面与管理客户端之间”的远程管理通信。

这类请求通常包括：

- 系统管理
- 存储准备与格式化
- 成员管理
- 启停控制
- 状态查询

这些事情的共同特点是：

- 更强调可靠、可观测、安全、易集成
- 通常不是最高频的超低延迟数据 I/O
- 更适合成熟的 RPC 框架与 protobuf 协议表达

### `src/proto` 也印证了 gRPC 的边界

`src/proto/README.md` 对 proto 目录结构有一个非常有用的说明：

- `ctl/` 存放可以 multicast 到具备 gRPC server 能力的 DAOS Server 的消息
- `mgmt/` 存放必须由 management service leader 处理的消息
- `srv/` 则是 engine 到 server 的 dRPC 消息

这其实已经把协议边界画出来了：

- `ctl/` 和 `mgmt/` 更偏 gRPC 控制面消息
- `srv/` 更偏本机 dRPC 消息

换句话说，协议目录结构本身就体现了“不同通路服务不同边界”的设计。

### 为什么管理流量适合用 gRPC

从工程角度看，gRPC 非常适合控制面，因为它天然满足控制面的核心诉求：

- 有成熟的远程调用模型
- 和 protobuf 搭配好，消息定义清楚
- 安全能力成熟，适合 TLS/SSL
- 很适合 Go 控制面的工程组织方式

而控制面本来就主要用 Go 编写，这进一步降低了使用 gRPC 的成本。

所以 gRPC 在 DAOS 里的定位不是“通用通信底座”，而是“控制面远程管理通道”。

## 再看 dRPC：它服务的是本机进程边界

### dRPC 的核心特征不是“轻量”，而是“本地”

`src/control/drpc/README.md` 一开头就讲得非常清楚：

- dRPC is a means of communication between processes local to the same physical system
- via a Unix Domain Socket

这句话最关键的不是 protocol 本身，而是边界：

**同一台物理系统上的本地进程间通信。**

也就是说，dRPC 不是远程协议，也不是 fabric 上的高性能数据协议。它解决的是：

- 两个本地进程需要通信
- 但又不值得为此走一套远程网络协议

### 哪些角色在用 dRPC

`src/README.md` 已经给出两个最核心的场景：

- `daos_agent` 和 `libdaos` 之间
- `daos_server` 和 `daos_engine` 之间

这两个场景都非常有代表性。

#### 场景 1：`daos_agent` <-> `libdaos`

这里的通信本质是：

- 应用进程侧的客户端库
- 需要和本机上的受信任 agent 协作

这通常涉及：

- 应用进程认证
- 凭据签发
- 本地安全相关协作

这类通信显然不需要走远程 fabric，也不需要控制面级 gRPC。

#### 场景 2：`daos_server` <-> `daos_engine`

这里的通信本质是：

- 控制面进程
- 和同机上的数据面进程协作

上一篇我们在 `daos_server` 启动链路里已经看到：

- 控制面会建立统一 dRPC server
- 数据面在 ready 后会通知控制面

而 `src/control/server/README.md` 也明确指出：

- 当有必要时，请求会经 dRPC 转发到数据面 `mgmt` 模块

所以 dRPC 在这里承担的是**控制面和数据面之间的本机桥梁**。

### 为什么这里不用 gRPC

这是很多人第一个会问的问题。

答案其实很自然：因为这里根本不是远程管理边界，而是本机进程边界。

如果 `daos_server` 和 `daos_engine` 已经在同一台机器上：

- 没必要再走 TCP/IP 远程管理模型
- 没必要引入更重的远程连接语义
- Unix Domain Socket 更自然，也更符合“本地控制协作”的语义

所以 dRPC 在 DAOS 中的设计重点不是“比 gRPC 快一点”，而是：

**它更适合本机进程边界。**

### dRPC 依然用 protobuf，这一点很重要

`src/control/drpc/README.md` 和 `src/proto/README.md` 都强调：

- dRPC 的消息结构同样依赖 Protocol Buffers

这意味着 DAOS 并不是为了 dRPC 另造一套完全不同的消息描述体系，而是：

- 在通道层分开
- 在消息表达层尽量统一

这也是为什么你会看到：

- gRPC 和 dRPC 都和 `src/proto` 有关联
- 但它们服务的是不同边界

所以 DAOS 的思路并不是“每条通路都彻底独立”，而是“在合适层次共享、在关键边界分离”。

## 最后看 CART：它服务的是高性能数据边界

### CART 的定位和前两者完全不同

`src/README.md` 对 CART 的描述非常直接：

- userspace function shipping library
- low-latency high-bandwidth communications for the DAOS data plane
- supports RDMA and scalable collective operations
- used for all communications between `libdaos` and `daos_engine` instances

这里的信息量其实非常大。

因为它说明 CART 从一开始就不是为“管理请求”设计的，而是为：

- 数据面通信
- 高性能远程通信
- 低延迟、高带宽
- RDMA / fabric 场景

而设计的。

### 哪些角色在用 CART

最关键的一条是：

- `libdaos` 与 `daos_engine` 之间的通信使用 CART

但这还不是全部。由于 DAOS 是分布式数据面，所以数据面节点之间很多高性能通信也同样依赖 CART。

这意味着 CART 服务的是一类和前两者完全不同的请求：

- 数据访问
- 元数据 I/O 路径中的高性能请求
- 节点间数据面协作
- 集体通信和 target 级通信

这些请求的核心诉求不是“好集成、好管理”，而是：

- 延迟尽量低
- 吞吐尽量高
- 对 HPC/fabric 环境友好

### 为什么这里不用 gRPC

如果你把客户端数据请求也放到 gRPC 上，马上就会遇到两个问题：

- 管理语义和数据语义被混在一起
- 高性能数据路径会被不必要的远程服务框架抽象拖住

DAOS 的数据路径显然不想为了统一协议而牺牲高性能特征。

`src/cart/README.md` 也说明，CART 是一个面向 Big Data 和 Exascale HPC 的 RPC transport layer，支持：

- P2P RPC
- collective RPC

这正是 DAOS 数据面真正需要的方向。

### 为什么这里也不用 dRPC

原因更简单：

- dRPC 是本机 Unix Domain Socket 通信
- CART 是跨节点、跨 fabric 的高性能通信

两者根本不是同一个问题空间。

所以如果你看到：

- `daos_server` 和 `daos_engine` 用 dRPC
- `libdaos` 和 `daos_engine` 用 CART

千万不要把它理解成“为什么不统一一下”，而要理解成：

- 这两条边界压根不是同一种边界

## 把三条通路放到一张图里

如果只记一张图，我建议记下面这张逻辑图：

- **gRPC**
  - 角色：`dmg`、control API、`daos_server`、MS leader
  - 边界：远程控制面管理边界
  - 目标：管理、编排、安全、fan-out、成员管理

- **dRPC**
  - 角色：`daos_agent`、`libdaos`、`daos_server`、`daos_engine`
  - 边界：本机进程边界
  - 目标：同机进程间轻量协作、控制面与数据面本地桥接、认证协作

- **CART**
  - 角色：`libdaos`、`daos_engine`、engine 间数据面通信
  - 边界：高性能数据边界
  - 目标：低延迟、高带宽、RDMA、集体通信、数据面 RPC

一旦这样归位，DAOS 的通信模型就会变得非常清楚：

- 管理走管理通路
- 本地协作走本地通路
- 高性能数据走高性能通路

## 为什么 DAOS 不选择“一种协议走到底”

这是整篇文章最核心的问题。

答案可以直接说：

**因为三条边界的需求根本不一样。**

但为了更具体一点，可以拆成四个理由。

### 1. 管理语义和数据语义不同

管理请求通常包括：

- 配置
- 成员管理
- 存储准备
- 状态查询
- 启停和控制

而数据请求通常包括：

- 客户端 I/O
- 模块间服务 RPC
- 节点间高性能通信

这两类请求的目标函数完全不同：

- 管理通路更重视安全性、清晰性、易集成
- 数据通路更重视低延迟、高吞吐、协议开销和 fabric 适配

强行统一，通常只会让两边都不舒服。

### 2. 本机边界和远程边界不同

本机进程协作和远程节点通信，本来就不是一个问题。

如果本机通信也统一走远程协议：

- 语义更重
- 路径更绕
- 没有利用 Unix Domain Socket 这类天然适合本地 IPC 的机制

如果远程高性能通信也统一走本地 IPC 模型，那更不可能成立。

所以 dRPC 和 gRPC/CART 的分离，本质上是“本机边界”和“远程边界”的分离。

### 3. 控制面和数据面的技术诉求不同

这点和双平面架构完全呼应。

控制面更适合：

- gRPC
- protobuf
- Go 服务工程模式
- 面向管理的 RPC 语义

数据面更适合：

- CART
- 高性能 fabric 通信
- 低开销数据路径

控制面和数据面之间的本机桥接，则更适合：

- dRPC
- Unix Domain Socket

也就是说，通信通路的分层，本质上是双平面职责分层在通信层面的延伸。

### 4. 三条通路让系统边界更清晰

一旦三条通路各守自己的边界，系统很多事情都会更清楚：

- 看到 gRPC，就知道这是管理面问题
- 看到 dRPC，就知道这是本机进程协作问题
- 看到 CART，就知道这是数据面高性能路径问题

这不只是实现方便，更是理解和排障上的巨大好处。

## `src/proto` 为什么同样重要

这篇还有一个很容易被忽略的点：虽然三条通路不同，但 `src/proto/` 在其中扮演了一个“统一消息定义中心”的角色。

`src/proto/README.md` 已经说明：

- gRPC 和 dRPC 的消息格式都用 protobuf 定义
- Go 侧会生成 `.pb.go`
- C 侧会生成 `.pb-c.[ch]`

这意味着 DAOS 在通信设计上并不是完全散的：

- 通道层按边界分离
- 协议描述层尽量统一

这样做的好处是：

- 控制面 Go 代码和数据面 C 代码可以围绕同一份协议定义协作
- dRPC 与 gRPC 虽然不是同一通路，但可以共享 protobuf 生态和工作流

所以 `src/proto` 并不是一个单纯的“生成代码目录”，而是通信体系的共同入口之一。

## 一个更实用的阅读方法：看到通信相关代码先问“我现在在哪条边界上”

以后你再读 DAOS 通信相关代码时，最实用的技巧就是先不要问“这个协议叫啥”，而是先问：

**我现在处在哪条边界上？**

你可以这样判断：

- 如果是 `dmg`、`daos_server`、management service、fan-out、Join，优先想到 gRPC
- 如果是 `daos_server` 和 `daos_engine`，或者 `daos_agent` 和 `libdaos` 的本机协作，优先想到 dRPC
- 如果是 `libdaos` 到 engine，或者 engine 间高性能 RPC，优先想到 CART

只要先把边界判断对，后面的协议和目录就不会乱。

## 小结

DAOS 同时维护 gRPC、dRPC 和 CART，不是因为历史包袱太重，也不是因为架构没有统一起来。

恰恰相反，这是一种非常清醒的分层设计：

- **gRPC** 负责控制面远程管理
- **dRPC** 负责同机进程协作
- **CART** 负责数据面高性能通信

所以“为什么不是一个协议走到底”的真正答案是：

**因为 DAOS 面对的不是一种通信问题，而是三种本质不同的通信边界。**

一旦按这个视角理解，三条通路不但不显得复杂，反而会显得非常自然：

- 该安全和可管理的地方，用 gRPC
- 该本地轻量协作的地方，用 dRPC
- 该追求低延迟高带宽的地方，用 CART

这正是 DAOS 这种分布式高性能存储系统在通信层面的边界感。

## 下一篇看什么

理解完控制面、数据面和三条通信通路之后，下一步就可以正式进入服务层了。

最自然的下一篇是：

**Pool 服务：集群级资源与元数据是如何组织的**

因为从这里开始，我们就能沿着前面铺好的控制面、数据面和通信路径主线，真正进入 DAOS 里最核心的服务对象之一。
