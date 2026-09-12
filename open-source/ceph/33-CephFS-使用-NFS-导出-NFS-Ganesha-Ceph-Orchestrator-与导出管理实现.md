# CephFS 使用 NFS 导出：NFS-Ganesha、Ceph Orchestrator 与导出管理实现

## 这篇文章要解决什么问题

前面几篇我们一直站在 Ceph 原生客户端视角理解 CephFS：

- 用户态 `ceph-fuse`
- Linux kernel client
- `src/client` 里的通用客户端

这些路径有一个共同前提：

- 客户端本身要理解 CephFS 协议

但现实里很多业务系统并不想直接接 CephFS 原生客户端，它们更希望看到的是：

- 一个普通的 NFS 导出

也就是说，业务机器不一定愿意：

- 安装 Ceph 客户端
- 管理 CephX
- 直接理解 MDS / caps / session

它们更希望：

- `mount -t nfs ...`

就能把文件系统挂上来。

这时就出现了一个很关键的问题：

**CephFS 是怎么通过 NFS 对外提供访问的？**

更具体一点就是：

- `NFS-Ganesha` 在这条链里到底扮演什么角色？
- Ceph 为什么又要有 `mgr/nfs` 模块？
- `ceph orch apply nfs`、`ceph nfs export create cephfs ...` 这些命令背后到底是谁在管？
- export 配置为什么不是直接改一份本地 `ganesha.conf`？
- Ceph 的 orchestrator 和 cephadm 又是怎样把 NFS 服务真正部署起来的？

如果只允许用一句话先给结论，那就是：

**CephFS over NFS 并不是“Ceph 自己实现了一个 NFS 服务器”，而是把 `NFS-Ganesha` 作为用户态 NFS 服务器，把 `mgr/nfs` 作为导出配置与集群管理控制面，把 orchestrator/cephadm 作为部署编排层；最终客户端看到的是 NFS，Ganesha 看到的是 `FSAL_CEPH`，而真正的数据访问仍然回到 CephFS。**

这一篇的目标，就是把这条链真正讲清楚。

## 先建立第一条边界：Ceph 没有自己发明一个 NFS 协议栈

这是理解整篇文章的第一步。

很多人第一次听到：

- CephFS 支持 NFS 导出

很容易下意识理解成：

- Ceph 里有一个自己的 NFS server

这个理解不准确。

更准确地说：

- Ceph 采用的是 `NFS-Ganesha`

也就是一个独立的、成熟的用户态 NFS 服务器项目。

### 这意味着什么

意味着 Ceph 在这件事上真正做的，不是：

- 自己从头造一套 NFS 协议栈

而是：

- 把 Ganesha 接到 CephFS 上
- 再把导出配置、部署、生命周期管理纳入 Ceph 自己的控制面

这点一定要先立住。

## 第 1 层：先把整条链路的三层结构画出来

如果整篇只记一张图，我建议先记下面这张：

```text
NFS client
  ->
NFS-Ganesha
  ->
FSAL_CEPH
  ->
CephFS
  ->
MDS + RADOS

而在控制面上:

ceph nfs / Dashboard
  ->
mgr/nfs
  ->
orchestrator
  ->
cephadm
  ->
部署/更新 Ganesha 服务
```

这张图最重要的意义在于：

- 它把数据面和控制面分开了

也就是：

## 数据面

- NFS client 请求到 Ganesha
- Ganesha 再通过 `FSAL_CEPH` 访问 CephFS

## 控制面

- Ceph 的 `mgr/nfs` 管导出和 NFS cluster
- orchestrator/cephadm 管服务部署与运行

这两层不要混。

## 第 2 层：为什么 `NFS-Ganesha` 是这条链里的真正 NFS 服务器

这是整篇最重要的基础概念。

从业务客户端视角看，它只知道：

- 自己在和一个 NFS server 说话

这时真正响应：

- NFSv3 / NFSv4
- export
- mount
- file handle
- 客户端兼容语义

这些事情的，不是 Ceph monitor、不是 MDS、也不是 `mgr`，而是：

- `NFS-Ganesha`

### 所以 Ganesha 的定位最准确应该是

- 用户态 NFS 服务器

而 Ceph 的角色则更偏：

- 给 Ganesha 提供后端存储能力
- 给 Ganesha 提供配置和编排控制面

这点必须清楚。

## 第 3 层：为什么 `FSAL_CEPH` 才是 Ganesha 接入 CephFS 的关键桥

很多人理解到 Ganesha 这一层后，下一步又会问：

- 那 Ganesha 怎么访问 CephFS？

关键桥梁就是：

- `FSAL_CEPH`

### 它的意义是什么

`FSAL` 可以理解为：

- File System Abstraction Layer

也就是说，Ganesha 并不是直接“懂所有后端文件系统”，而是通过不同 FSAL 插件接不同后端。

对 CephFS 来说，最关键的就是：

- `FSAL_CEPH`

### 这意味着什么

意味着 Ganesha 面向上层说的是：

- NFS 协议

面向下层说的是：

- 通过 `FSAL_CEPH` 去访问 CephFS

所以这条链更准确地说是：

- **NFS 协议语义在 Ganesha，CephFS 存储语义在 FSAL_CEPH 后面。**

## 第 4 层：为什么 Ceph 还要单独做一个 `mgr/nfs` 模块

讲到这里，很多人会接着问：

- 既然 Ganesha 已经能做 NFS 服务器了，为什么 Ceph 还要搞 `mgr/nfs`？

这是个特别关键的问题。

因为如果没有 `mgr/nfs`，你当然也可以：

- 手工写 `ganesha.conf`
- 手工部署 Ganesha
- 手工重载服务

但这样会立刻产生几个工程问题：

- export 配置如何集中管理？
- 多个 Ganesha 实例如何保持一致？
- 集群创建、删除和 HA 谁来管？
- Dashboard 和 CLI 怎么统一？
- 配置状态放在哪里才不依赖单机本地文件？

### 所以 `mgr/nfs` 的真实定位是什么

- 它不是 NFS server
- 它是 Ceph 里的 NFS 管理控制面模块

这点非常关键。

## 第 5 层：`mgr/nfs` 到底在管什么

最准确的理解方式是，`mgr/nfs` 主要管理两类对象：

## NFS cluster

- 某个逻辑 NFS 服务集群
- 对应一个 cluster id
- 可能有一个或多个 Ganesha 实例
- 可能再叠加 ingress / VIP / HA

## export

- 某个被导出的后端资源
- 比如一个 CephFS path
- 带有 pseudo path、访问控制、squash、sectype 等导出属性

### 这说明什么

说明 `mgr/nfs` 关心的不是“某个 NFS 请求怎么转发”，而是：

- 哪些后端被导出
- 导出长什么样
- 哪些 Ganesha 实例组成这个服务
- 这些配置如何一致地下发

这就把它和 Ganesha 的职责边界明确分开了。

## 第 6 层：为什么 `ceph nfs export create cephfs ...` 是理解整条控制面链路的最佳入口

如果要从源码视角抓主线，我会优先抓这个动作：

- `ceph nfs export create cephfs ...`

因为这个命令几乎把整条管理链都串起来了。

它大致会经历这样的逻辑：

```text
CLI
  ->
mgr/nfs module.py
  ->
ExportMgr
  ->
构造 CephFS export 对象
  ->
写入 RADOS 中的 NFS 配置对象
  ->
必要时通知/重启 Ganesha 服务
```

### 为什么这条线很关键

因为它说明：

- Ceph 的 export 管理不是临时拼字符串
- 而是先在 `mgr/nfs` 里维护一个结构化的 export 模型

这点很重要。

## 第 7 层：为什么 export 最终不是“某台机器上的本地配置文件”

这是 Ceph NFS 管理里非常关键的工程设计点。

很多传统思维会默认：

- NFS 配置不就是一份 `ganesha.conf` 吗？

但 Ceph 这里刻意没有把核心状态寄托在某台机器的本地文件上。

更准确地说：

- export 和公共配置会被放进 RADOS

也就是：

- 配置本身被存成集群对象

### 这意味着什么

意味着 NFS 配置的权威状态不依赖：

- 某个容器本地目录
- 某台机器的磁盘
- 某个手工同步脚本

而依赖：

- Ceph 集群自己维护的共享配置对象

这正是 Ceph 风格很强的一种设计。

## 第 8 层：为什么 `.nfs` pool 和 `%url` 这类配置组织方式值得特别讲

进一步往下看，你会发现 `mgr/nfs` 的状态不是“随便存几段文本”，而是有明确组织方式。

最关键的直觉是：

- 公共配置对象会引用各个 export 对象
- export 对象本身也都是 RADOS 里的独立对象

从结果看，更像：

- 一个共享的、结构化的 Ganesha 配置仓库

### 为什么这样设计很有价值

因为它天然适合：

- 多实例共享配置
- export 独立增删改
- 声明式 apply
- 集群级一致视图

所以这条设计真正解决的是：

- **NFS 配置怎样脱离单机文件，变成集群状态。**

## 第 9 层：为什么 `ganesha_conf.py` 这层特别重要

如果从源码导读角度再往下走，最值得注意的一层是：

- `ganesha_conf.py`

### 为什么重要

因为它体现了 Ceph 的一个关键设计选择：

- Ceph 不是把 Ganesha 配置当成纯文本胡乱拼接

而是：

- 先解析成结构化对象模型
- 在内部按字段维护 export / FSAL / block
- 再序列化回 Ganesha 配置块

### 这说明什么

说明 `mgr/nfs` 的管理风格不是：

- shell 脚本式改配置文件

而更像：

- 维护一个领域对象模型，再生成配置

这也是它能支持：

- JSON apply
- Ganesha export block apply
- CLI / Dashboard 统一复用

的重要原因。

## 第 10 层：为什么 cluster create 和 export create 是两条不同主线

很多人第一次使用 NFS 管理命令时，会把这两件事混在一起：

- 创建 NFS cluster
- 创建 export

其实这两件事解决的是完全不同的问题。

## `cluster create`

- 解决“谁来提供 NFS 服务”
- 也就是 Ganesha 服务实例、VIP、ingress、HA 怎么建立

## `export create`

- 解决“到底导出什么”
- 也就是 CephFS 哪个路径、什么 pseudo path、什么访问控制

### 所以最准确的理解方式是

- `cluster` 是服务承载层
- `export` 是导出资源层

这两个概念分开，整条链就清楚很多。

## 第 11 层：为什么 `orchestrator` 是 `mgr/nfs` 和 cephadm 之间的总线

讲到 cluster 这一层，就必须引入：

- orchestrator

这是很多人第一次看 Ceph NFS 管理时最迷糊的一层。

### 它到底在干什么

最简单的理解是：

- `mgr/nfs` 知道“我要一个 NFS 服务”
- 但它不自己直接启动容器

它会通过：

- orchestrator 抽象接口

去请求当前选定的后端编排器执行部署。

### 所以 orchestrator 的定位最准确应该是

- Ceph mgr 内部统一的服务编排总线

这意味着：

- `mgr/nfs` 不需要死绑某个具体部署器实现
- 它只需要说“请帮我 apply nfs service”

这层抽象非常关键。

## 第 12 层：为什么 cephadm 是“真正把 Ganesha 跑起来”的那一层

如果 orchestrator 是抽象总线，那在常见部署里真正执行落地动作的就是：

- cephadm

### 这意味着什么

意味着当你执行：

- `ceph orch apply nfs ...`

最终真正负责：

- 生成服务 spec
- 准备 keyring
- 生成 `ganesha.conf`
- 生成 idmap 配置
- 启动容器
- 管理 ingress / VIP / HAProxy / keepalived

这些事情的，是：

- cephadm 的 NFS service 实现

也就是说：

- `mgr/nfs` 管逻辑对象
- cephadm 管运行时实例

这两者不要混。

## 第 13 层：为什么 `NFSServiceSpec` 是整个部署链里的关键中间对象

一旦你从 `mgr/nfs` 往 cephadm 走，就会看到一个很关键的对象：

- `NFSServiceSpec`

### 它的意义是什么

它把下面这些部署信息结构化了：

- cluster id
- port
- monitoring port
- virtual ip
- ingress 相关选项
- idmap 配置
- 是否启用某些 NFS 语义特性

### 这说明什么

说明 `mgr/nfs` 和 cephadm 的协作不是：

- 互相传几段字符串

而是：

- 通过统一 service spec 交换部署意图

这和 Ceph 其他服务的部署模式是一致的。

## 第 14 层：为什么 Dashboard 也能管理 NFS，但并没有自己实现一套 NFS 逻辑

这是理解 Ceph 控制面复用方式的一个很好例子。

很多人看到 Dashboard 能管理 NFS cluster/export，会以为：

- Dashboard 里有另一套 NFS 管理逻辑

其实不是。

更准确地说：

- Dashboard 只是通过 `mgr.remote('nfs', ...)` 去复用 `mgr/nfs` 模块

### 这意味着什么

意味着：

- CLI
- Dashboard

虽然入口不同，但最终都回到：

- 同一套 `mgr/nfs` 业务逻辑

这就是 Ceph 控制面的典型设计：

- 业务逻辑集中在 mgr 模块
- Web UI 只是另一种调用方式

## 第 15 层：为什么 CephFS over NFS 的 HA 不能只理解成“多起几个 Ganesha”

当进入生产部署时，另一个容易被低估的问题是：

- 高可用到底怎么做？

很多人会本能觉得：

- 多起几个 Ganesha 不就行了

但这只解决了一部分问题。

### 真正还要回答什么

- 客户端连哪个地址？
- 故障切换时入口怎么漂移？
- VIP 谁来接管？
- HAProxy / keepalived 在哪一层？
- ingress 是不是要和 NFS cluster 一起管理？

### 所以 Ceph 这里的思路更准确地说是

- Ganesha 实例只是后端服务节点
- ingress/VIP 才是接入层稳定入口

这就是为什么 `mgr/nfs` 和 cephadm 文档里会把：

- cluster
- ingress
- virtual_ip

这些概念放到一起讲。

## 第 16 层：为什么手工部署仍然存在，但控制面能力会明显缩水

Ceph 官方文档也明确保留了：

- 手工部署 Ganesha

这说明：

- Ceph NFS 并不是只能靠 cephadm 才存在

但必须同时理解：

- 一旦没有 orchestrator/cephadm 这套管理面
- `mgr/nfs` 的某些 cluster 生命周期能力就会明显受限

### 这意味着什么

你当然可以：

- 手工跑 Ganesha
- 手工挂 CephFS FSAL

但你失去的通常是：

- 集群级统一部署
- 一体化 HA 管理
- 更顺滑的服务生命周期控制

所以手工部署更像：

- 兼容路径

而不是 Ceph 现在最强调的主路径。

## 第 17 层：把整条 CephFS over NFS 主线压缩成一张图

如果这一篇只记一张图，我建议记下面这张：

```text
NFS 客户端
  ->
NFS-Ganesha
  ->
FSAL_CEPH
  ->
CephFS

控制面:

ceph nfs export create cephfs ...
  ->
mgr/nfs
  ->
ExportMgr / NFSCluster
  ->
RADOS 中的 NFS 配置对象
  ->
orchestrator
  ->
cephadm NFS service
  ->
生成 ganesha.conf / keyring / ingress
  ->
运行 Ganesha
```

这张图里最关键的一句话是：

- **数据访问通过 Ganesha 回到 CephFS，导出和部署管理则由 Ceph 控制面接管。**

## 第 18 层：为什么这套设计很“Ceph”

如果从架构风格角度总结，这套设计非常符合 Ceph 的习惯：

### 不把关键状态绑死在单机本地文件

- export 配置放进 RADOS

### 不让业务模块直接操心具体容器部署

- `mgr/nfs` 通过 orchestrator 抽象后端

### 不重复造已经成熟的协议服务

- NFS server 直接采用 Ganesha

### 把 CLI 和 Dashboard 收敛到同一套业务逻辑

- 都回到 `mgr/nfs`

### 所以这条线真正体现的是什么

- **Ceph 更擅长做“分布式控制面 + 集群状态管理 + 编排集成”，而不是重新发明已有协议服务本身。**

这就是它的工程味道。

## 用一句话重新概括这篇

如果把这篇全部内容压缩成一句尽量准确的话，我会这样说：

**CephFS 使用 NFS 导出的本质，是让 `NFS-Ganesha` 充当真正的 NFS 服务器，让 `FSAL_CEPH` 作为访问 CephFS 的桥，让 `mgr/nfs` 维护 NFS cluster 与 export 这两类管理对象，并把配置持久化到 RADOS，再通过 orchestrator/cephadm 把 Ganesha 服务、配置、keyring 和 ingress 真正部署起来；因此这条链既不是“Ceph 自己实现 NFS 协议栈”，也不是“手工写个 ganesha.conf 就完了”，而是一套完整的集群级导出控制面。**

## 把整篇压缩成一条最短骨架

如果你只想记一条骨架，可以记成下面这样：

```text
NFS 客户端访问的是 Ganesha
  ->
Ganesha 通过 FSAL_CEPH 访问 CephFS
  ->
mgr/nfs 管 NFS cluster 和 export
  ->
export/config 持久化到 RADOS
  ->
orchestrator 把部署请求转给 cephadm
  ->
cephadm 生成 Ganesha 运行配置并拉起服务
```

只要这条骨架记住了，CephFS over NFS 的总体结构就不会乱。

## 初学者最容易混淆的 10 个点

### 1. 认为 Ceph 自己实现了 NFS 协议栈

不对。真正的 NFS server 是 `NFS-Ganesha`。

### 2. 认为 CephFS over NFS 就是挂个网关而已

不对。它背后还有完整的导出与集群控制面。

### 3. 认为 `mgr/nfs` 就是 NFS server

不对。它是管理模块，不处理实际 NFS 协议请求。

### 4. 认为 Ganesha 直接“懂 CephFS”

更准确地说，它通过 `FSAL_CEPH` 接 CephFS。

### 5. 认为 cluster create 和 export create 是一回事

不对。一个管服务承载层，一个管导出资源层。

### 6. 认为 export 最终只是一份本地 `ganesha.conf`

不对。权威配置状态会落到 RADOS。

### 7. 认为 orchestrator 是可有可无的装饰层

不对。它是 `mgr/nfs` 和具体部署器之间的总线。

### 8. 认为 cephadm 只是“帮忙起个容器”

不对。它还负责配置生成、keyring、ingress 和服务生命周期。

### 9. 认为 Dashboard 有自己独立的一套 NFS 逻辑

不对。它主要复用 `mgr/nfs`。

### 10. 认为多起几个 Ganesha 就自然高可用

不对。还要考虑 ingress、VIP 和接入层稳定入口。

## 这一篇最应该留下的 5 个直觉

### 直觉一：Ganesha 是真正的 NFS 协议服务端

这是第一原则。

### 直觉二：`mgr/nfs` 是导出和集群管理控制面

它不是数据面。

### 直觉三：RADOS 承载了 NFS 配置状态

这是很关键的工程设计点。

### 直觉四：orchestrator/cephadm 负责把管理意图落到运行时实例

这层不能漏。

### 直觉五：CephFS over NFS 仍然最终回到 CephFS 本体

NFS 只是外部接入协议，不是新的存储底座。

## 下一篇看什么

既然这一篇已经把：

- `NFS-Ganesha`
- `FSAL_CEPH`
- `mgr/nfs`
- orchestrator
- cephadm
- export/config 持久化

这条 CephFS over NFS 主线讲清楚了，下一步最自然的事情，就是从文件系统世界切到对象网关世界：

**如果说 CephFS 是文件接口，那 Ceph 的对象接口 RGW 又是怎样把 S3/Swift 建立在 RADOS 之上的？**

所以下一篇建议接：

**《RGW 架构总览：S3/Swift 网关如何建立在 RADOS 之上》**
