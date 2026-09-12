# Rook 架构与实现：Kubernetes 中如何编排和管理 Ceph 集群

## 这篇文章要解决什么问题

上一篇我们站在裸机/虚机环境里，看了一次最小可用 Ceph 集群是如何通过 `cephadm` 被拉起来的。那条主线很清晰：

- 先 bootstrap
- 再纳管主机
- 再部署 `OSD`
- 再按需启用 `MDS`、`RGW`
- 再接上 Dashboard、Prometheus、Grafana

但一旦你把运行环境切换到 Kubernetes，很多东西表面上都变了：

- 机器不再是你直接 SSH 管理的主机，而是 Kubernetes Node
- 服务不再只是容器进程，而是 Pod、Deployment、DaemonSet、Job
- 集群配置不再主要靠命令式操作，而是大量通过 CRD 声明出来
- “运维动作”不再只是执行命令，而是让 controller 持续 reconcile 期望状态

这时最常见的困惑会变成：

- Rook 到底是 Ceph 的一部分，还是 Kubernetes 的一部分？
- `Rook Operator` 和 `ceph-mgr` 的 `rook` 模块是什么关系？
- `CephCluster`、`CephFilesystem`、`CephObjectStore` 这些 CRD 分别对应什么角色？
- Rook 和 `cephadm` 都在“编排 Ceph”，它们到底谁替代谁，谁调用谁？

这一篇的目标，就是把这些关系彻底讲清楚。

如果只允许用一句话先给结论，那就是：

**Rook 是 Kubernetes 世界里的 Ceph 编排器，它用 Operator + CRD + 控制循环来管理 Ceph；而 Ceph 本身则通过 `ceph-mgr` 的 orchestrator 框架与 Rook 对接。**

## 先建立第一层认知：Rook 不是“另一个 Ceph”，而是 Ceph 的 Kubernetes 编排层

理解 Rook 的第一步，就是不要把它想成“Ceph 的替代品”。

Rook 本身不去重新实现：

- monitor 仲裁
- OSD 数据写入
- CRUSH 放置
- BlueStore 持久化
- MDS 元数据语义
- RGW 对象网关语义

这些仍然是 Ceph 自己的职责。

Rook 做的事情是另一类问题：

- 如何在 Kubernetes 里声明“我要一个 Ceph 集群”
- 如何把这种声明转换成 Pod、Deployment、Job、Config、Secret 等资源
- 如何在节点变化、Pod 重建、配置变化时持续让系统回到期望状态
- 如何用 Kubernetes 方式管理 Ceph 的生命周期

所以可以先把关系记成：

```text
Ceph = 存储系统本体
Rook = Kubernetes 中的 Ceph 编排与生命周期管理层
```

这个边界非常重要。后面一旦混淆了“谁负责存储语义，谁负责编排语义”，你就会在阅读 Rook 和 Ceph 源码时不断串线。

## 为什么 Ceph 进了 Kubernetes，就需要 Rook 这样的东西

上一篇里我们讲 `cephadm` 的时候，有一个核心思想是：

- 通过统一的 orchestrator 视角管理主机和服务
- 把“每台机器手工养护”变成“按 service spec 持续管理”

进入 Kubernetes 后，管理模型又进一步变化了。

在 Kubernetes 世界里，理想的控制方式不是：

- “登录进机器执行脚本”
- “我手工把这个服务起起来”
- “我把某个进程重启一下”

而是：

- “我声明一个期望状态”
- “controller 持续观察实际状态”
- “如果实际状态偏离期望状态，就自动 reconcile 回去”

这正是 Operator 模式的核心逻辑。

所以，当 Ceph 要在 Kubernetes 中成为一个“像云原生系统一样被管理的存储集群”时，它就需要一个能理解：

- Ceph 领域模型
- Kubernetes 资源模型
- 持续调谐控制循环

的中间层。Rook 就是这个中间层。

## 第二张总图：把 Rook 放进 Kubernetes 世界

先看一张最重要的全景图：

```text
用户 / 平台工程师
      |
      v
kubectl / Helm / GitOps / YAML
      |
      v
CephCluster / CephFilesystem / CephObjectStore / ...
      |
      v
Rook Operator
      |
      +--> 创建/更新 Kubernetes 资源
      |    Deployment / DaemonSet / Job / Secret / ConfigMap / Service
      |
      +--> 驱动 Ceph 守护进程运行
             mon / mgr / osd / mds / rgw
      |
      v
Ceph 集群本体
      |
      v
RADOS / MON / MGR / OSD / MDS / RGW
```

这张图里最关键的一点是：

**Rook 不是在 Ceph 外面包一层“静态部署脚本”，而是在 Kubernetes API 之上，持续维护 Ceph 集群期望状态。**

也就是说，Rook 不是一次性安装器，而是常驻控制器。

## `Rook Operator` 是什么

Rook 的核心角色是 `Operator`。

从 Kubernetes 视角看，Operator 本质上就是一个理解某个领域模型的 controller。Rook Operator 理解的领域模型，就是分布式存储，尤其是 Ceph。

它的典型职责包括：

- 监听 CRD 对象变化
- 根据 `CephCluster` 等对象的期望状态，创建或调整底层 Kubernetes 资源
- 驱动 Ceph 守护进程的部署、扩缩容、升级和部分生命周期动作
- 观察当前状态并在偏差出现时做 reconcile

所以你可以把 `Rook Operator` 理解成：

**Kubernetes 世界里“懂 Ceph 业务语义”的控制器。**

这句话有两个层次：

- 它首先是 Kubernetes 控制器
- 但它控制的对象，不是普通 Web 服务，而是 Ceph 这种有复杂角色分工和状态要求的分布式存储系统

## CRD 是 Rook 的“声明式接口”

如果 Operator 是“控制器大脑”，那么 CRD 就是它的“声明式输入面”。

Rook 之所以看起来很“云原生”，核心就在于：你不是主要通过一条条 imperative 命令告诉它“现在去起一个 mon”“现在加一个 rgw”，而是通过 CRD 描述你希望整个系统长成什么样。

最典型的几个对象包括：

- `CephCluster`
- `CephFilesystem`
- `CephObjectStore`
- 以及其他与 NFS、RBD mirror 等相关的对象

这几个 CRD 的名字，本身已经揭示了 Rook 的设计思想：

- 集群级声明
- 文件系统级声明
- 对象网关级声明

也就是说，Rook 并不是只管“Pod 数量”，而是尽量以 Ceph 的业务概念作为建模边界。

## `CephCluster`：Rook 里的集群根对象

在 Rook 体系里，`CephCluster` 是最关键的入口对象。

你可以把它理解成：

- Kubernetes 世界中对一个 Ceph 集群整体期望状态的描述
- 后续 mon、mgr、osd、网络、存储、健康、升级等一系列行为的总入口

从开发文档 `doc/dev/kubernetes.rst` 里也能看到这种定位，它直接提到 Rook 的 `CephCluster` CR 会引用 Ceph 镜像。

这点非常重要，因为它说明：

- Rook 管的是“Ceph 集群对象”
- Ceph 集群对象最终要落成一组真实运行的 Ceph 守护进程

也就是说，`CephCluster` 不是一个抽象标签，它最终会映射到真实的 `mon`、`mgr`、`osd` 等服务组合。

## `CephFilesystem`：把 CephFS 变成声明式对象

当你需要在 Kubernetes 里启用 CephFS 时，Rook 并不会要求你先离开 Kubernetes 世界，再回到一堆主机命令里手工组织 MDS 拓扑。

更自然的方式是：

- 通过 `CephFilesystem` 这种 CRD 声明“我要一个文件系统”
- 由 Rook Operator 把这个声明翻译成相应的 CephFS 资源和守护进程布局

这和我们上一篇里讲的 `ceph fs volume create` 是同一种目标，只是控制方式不同：

- 在 `cephadm` 语境下，常见入口是 Ceph CLI + orchestrator
- 在 Rook 语境下，常见入口是 Kubernetes CRD + Operator reconcile

但无论哪种入口，背后的 Ceph 事实并没有变：

- CephFS 需要文件系统对象
- 需要 MDS
- 文件数据最终仍在 `RADOS` / `OSD`

## `CephObjectStore`：把对象网关也变成声明式对象

同理，如果你要在 Kubernetes 中启用对象网关，Rook 会通过 `CephObjectStore` 这样的对象来描述：

- 对象存储服务实例
- 网关部署数量
- 网关配置
- 相关服务形态

这也是为什么在 `src/pybind/mgr/rook/rook_cluster.py` 里，你能直接看到 `CephObjectStore` 这样的 CRD 类型被使用。

这说明两件事：

- Rook 的建模对象确实就是 Ceph 的上层服务概念
- Ceph 自己的 `rook` orchestrator 模块知道如何把 `rgw` 这类服务映射到 Rook 的 CRD 资源

## 关键关系：Ceph 里的 `rook` 模块到底是什么

到这里，很多人会出现第二层困惑：

“既然已经有 Rook Operator 了，Ceph 为什么在 `ceph-mgr` 里还要有一个 `rook` 模块？”

这是全篇最值得讲清楚的一点。

## 先看官方文档给出的定义

`doc/mgr/rook.rst` 里对 `rook` 模块的定义很直接：

- 它提供了 Ceph orchestrator framework 和 Rook 之间的集成
- 其他像 `dashboard` 这样的模块可以通过这个框架控制集群服务

这句话其实已经把问题说穿了：

**Ceph MGR 里的 `rook` 模块，不是 Rook Operator 本身，而是 Ceph 管理平面里连接 Rook 的适配器。**

换句话说，它的职责不是在 Kubernetes 里“重新做一个 Operator”，而是：

- 让 Ceph 的 orchestrator 抽象能调用 Rook
- 让 Dashboard、Orchestrator CLI 等 Ceph 管理功能可以把 Rook 当作一个后端

## Ceph MGR orchestrator 框架：统一抽象层

这一点要结合 `doc/mgr/orchestrator.rst` 一起看。

Ceph 的 orchestrator CLI 文档明确说明：

- orchestrator modules 是 `ceph-mgr` 插件
- 它们用来对接外部编排服务
- 你可以通过 `ceph orch set backend <module>` 选择后端

这意味着，Ceph 自己先定义了一套统一的编排抽象，例如：

- host
- service type
- service
- daemon
- apply mds
- apply rgw
- apply osd

然后不同后端来实现这些抽象。

在这个模型里：

- `cephadm` 是一个 orchestrator backend
- `rook` 也是一个 orchestrator backend

这就是它们之间最本质的共同点。

## Rook 和 cephadm 的关系：不是上下级，而是两种 backend

这也是本篇必须建立的结论：

**Rook 和 `cephadm` 都可以作为 Ceph orchestrator 的后端，但它们面向的是两种完全不同的运行环境。**

你可以这样对比理解：

### `cephadm`

- 面向裸机/虚机主机环境
- 通过 SSH、容器运行时、主机标签、service spec 管理 Ceph
- 重点是主机纳管和服务编排

### Rook

- 面向 Kubernetes 环境
- 通过 Operator、CRD、Pod、Deployment、Kubernetes API 管理 Ceph
- 重点是声明式对象和控制循环

也就是说，它们解决的问题相似：

- “如何编排和管理 Ceph”

但所在世界完全不同：

- 一个是主机世界
- 一个是 Kubernetes 世界

## 为什么说 `rook` 模块是“桥”，不是“王”

如果继续把系统拆分清楚，可以得到下面这张更细的关系图：

```text
Ceph Dashboard / ceph orch CLI / 其他 mgr 模块
                    |
                    v
          ceph-mgr orchestrator framework
                    |
        +-----------+-----------+
        |                       |
        v                       v
      cephadm backend         rook backend
        |                       |
        v                       v
   主机 / SSH / 容器          Kubernetes API / Rook CRD / Operator
```

在这张图里，`rook` backend 的作用是：

- 把 Ceph 管理平面的抽象动作翻译成 Rook/Kubernetes 世界能理解的资源变更

它不是整个系统唯一的“大脑”，也不是 Rook Operator 的上级控制器。

更准确地说，它是：

- Ceph 视角下的 backend
- Rook 视角下的集成桥接层

## 从源码看：`rook` 模块为什么能“懂 Kubernetes”

如果看 [module.py](file:///e:/projects/ceph/src/pybind/mgr/rook/module.py)，可以很直观地看到这个模块的世界观。

### 1. 它直接使用 Kubernetes Python Client

源码里明确导入了：

- `kubernetes.client`
- `config`
- `CoreV1Api`
- `CustomObjectsApi`
- `AppsV1Api`

这说明它不是通过 shell 调 `kubectl` 拼接命令，而是直接把 Kubernetes API 当成一等公民。

### 2. 它会判断自己是否运行在 Rook 集群里

在 [module.py](file:///e:/projects/ceph/src/pybind/mgr/rook/module.py#L85-L101) 的 `RookEnv` 里，可以看到它会读取：

- `POD_NAMESPACE`
- `ROOK_CEPH_CLUSTER_CRD_NAME`
- `ROOK_OPERATOR_NAMESPACE`
- `ROOK_CEPH_CLUSTER_CRD_VERSION`

这说明 `ceph-mgr` 的 `rook` 模块不是一个“完全脱离环境的通用插件”，而是明确假设：

- 自己运行在一个 Rook 管理的 Kubernetes 环境中
- 并通过环境变量识别当前 cluster namespace、operator namespace、CRD 版本

### 3. 它支持 in-cluster config

在 [serve](file:///e:/projects/ceph/src/pybind/mgr/rook/module.py#L210-L253) 里，可以看到：

- 如果有 `POD_NAMESPACE`，就 `load_incluster_config()`
- 否则才回退到开发模式下的本地 kubeconfig

这又进一步说明：

- 正常部署场景里，这个模块就是作为 Kubernetes 集群内组件运行的
- 它天然依赖 Kubernetes API 访问能力

## 从源码看：`rook_cluster.py` 真正在做什么

相比 `module.py` 更像“入口和胶水层”，[rook_cluster.py](file:///e:/projects/ceph/src/pybind/mgr/rook/rook_cluster.py) 更像真正的“Rook CRD 操作执行层”。

它文件开头的注释就已经写得很清楚：

- 它封装 Rook + Kubernetes API
- 用来实现 orchestrator 模块所需调用
- orchestrator 暴露异步抽象，但这里主要做阻塞式 API 调用

这说明它的职责就是：

**把 Ceph orchestrator 的抽象动作，落实为对 Kubernetes / Rook 资源的具体操作。**

### 它操作的不是抽象字符串，而是真实 CRD 类型

在文件开头可以直接看到导入：

- `cephfilesystem`
- `cephnfs`
- `cephobjectstore`
- `cephcluster`
- `cephrbdmirror`

这说明 `rook_cluster.py` 的工作方式非常“声明式”：

- 不是说“起一个 mds 进程”
- 而是说“创建或修改一个 `CephFilesystem` CRD”

同样地：

- 不是说“起一个 rgw 进程”
- 而是说“创建或修改一个 `CephObjectStore` CRD”

这就是 Kubernetes Operator 世界和传统主机编排世界最大的差别。

## 一个最关键的转变：从“创建守护进程”到“修改期望状态对象”

这也是理解 Rook 的真正门槛。

在 `cephadm` 世界里，你虽然也会说“apply service”，但你的直觉仍然很容易停留在：

- 去哪个主机上
- 起几个 daemon
- 用什么 placement

在 Rook 世界里，你更应该把动作理解成：

- 创建/更新某个 Ceph 领域对象
- 让 Rook Operator 根据该对象去调谐真实 Pod 和相关资源

源码里的几个实现非常能说明这个问题。

### `apply mds` 映射到 `CephFilesystem`

在 [rook_cluster.py](file:///e:/projects/ceph/src/pybind/mgr/rook/rook_cluster.py#L651-L709) 里，可以看到针对文件系统的创建/更新逻辑：

- 更新时会修改 `metadataServer.activeCount`
- 还会修改 placement
- 创建时直接构造 `CephFilesystem`

这意味着在 Rook 里，“部署 MDS”这个动作，本质上不是直接起进程，而是修改文件系统对象的期望状态。

### `apply rgw` 映射到 `CephObjectStore`

类似地，在 [rook_cluster.py](file:///e:/projects/ceph/src/pybind/mgr/rook/rook_cluster.py#L747-L812) 里，可以看到对象网关的创建/更新逻辑：

- 创建 `CephObjectStore`
- 设置 gateway instances、端口、secure port 等
- 更新时修改 gateway 配置

这说明 Rook 对 RGW 的控制方式，同样是基于 CRD 期望状态，而不是“直接在某台主机起几个 radosgw”。

### mon 数量修改映射到 `CephCluster`

在 [rook_cluster.py](file:///e:/projects/ceph/src/pybind/mgr/rook/rook_cluster.py#L881-L890) 里，还能看到对 `CephCluster` 中 mon count 的 patch。

这正好说明：

- 集群级角色配置通过 `CephCluster` 这种根对象表达
- 上层动作会被翻译成对这个根对象的 patch

从架构上看，这非常符合 Kubernetes 哲学。

## Rook 的设备与 OSD 视角：为什么它和 `cephadm` 看起来不一样

上一篇讲 `cephadm` 时，我们看到 OSD 部署非常强调：

- 主机上的块设备
- `ceph orch device ls`
- 本地 LVM / 设备可用性

而在 Rook 里，你看到的视角会更偏向：

- Kubernetes Node
- PersistentVolume / StorageClass
- rook-discover 发现到的设备信息
- 通过 CRD/配置驱动 OSD 编排

这一点在 [rook_cluster.py](file:///e:/projects/ceph/src/pybind/mgr/rook/rook_cluster.py) 里也能看到：它会从 Kubernetes 资源、ConfigMap、PV、StorageClass 这些对象里收集设备与节点信息。

所以你必须明确：

- `cephadm` 看到的是“主机与块设备”
- Rook 看到的是“Node、PV、StorageClass，以及 Operator 发现出的设备视图”

Ceph 数据面并没有变，变的是编排观察世界的方式。

## 为什么说 Rook 的核心是“控制循环”，不是“安装脚本”

如果你只在 quickstart 里看过 YAML，很容易以为 Rook 只是“帮你把一堆 Ceph Pod 起起来”。

这种理解太浅了。

Rook 真正关键的地方在于：

- 它持续 watch CRD 与底层资源状态
- 它不是一次部署后就退出
- 它会不断尝试把系统拉回期望状态

这就是 Operator 模式最本质的优势：

- 把运维动作从“人工一次性操作”变成“系统持续对齐”

对于 Ceph 这种组件多、角色多、生命周期复杂的系统，这种模式的价值尤其大。

## 那 Rook 会不会取代 Ceph 自己的管理面

不会，至少不应该这样理解。

更准确地说，Rook 和 Ceph 管理面是协作关系。

### Ceph 仍然负责什么

- 存储语义
- monitor / osd / mds / rgw 本体逻辑
- cluster maps
- 数据路径
- `ceph-mgr` 模块体系

### Rook 主要负责什么

- Kubernetes 里的部署与生命周期编排
- CRD 到底层资源的映射
- 节点、Pod、资源对象层面的控制循环

### `ceph-mgr` 的 `rook` 模块负责什么

- 让 Ceph 的 orchestrator 框架把 Rook 当作 backend 使用
- 让 Dashboard / CLI 等 Ceph 管理入口能和 Rook 对接

所以真正准确的关系是三层：

- Ceph 是存储系统本体
- Rook Operator 是 Kubernetes 编排控制器
- Ceph MGR rook 模块是二者在管理平面上的桥接层

## Rook 和 cephadm，该怎么选

这是用户最实际的问题之一。

先直接给答案：

### 如果你的 Ceph 主要运行在裸机/虚机环境

优先考虑 `cephadm`。

因为它天然面向：

- SSH 纳管主机
- 主机标签
- 宿主机块设备
- 容器化守护进程部署

### 如果你的 Ceph 本身就是 Kubernetes 环境中的核心存储系统

优先考虑 Rook。

因为它天然面向：

- Kubernetes API
- CRD
- Operator 控制循环
- Pod / Deployment / StorageClass 体系

### 不要把二者理解成“哪个更新、更高级”

更准确地说，它们只是：

- 面向不同基础设施运行环境的两种编排世界观

所以，选型核心不是“谁功能更多”，而是：

- 你的 Ceph 运行在哪个世界里

## 初学者最容易混淆的 7 个边界

### 1. Rook 就是 Ceph

不是。Rook 是 Kubernetes 中的 Ceph 编排层，不是 Ceph 存储本体。

### 2. Rook Operator 和 Ceph MGR rook 模块是同一个东西

不是。一个是 Kubernetes Operator，一个是 Ceph 管理平面中的 backend 适配模块。

### 3. `CephCluster` 只是一个安装模板

不对。它是集群级声明对象，是 Operator 持续 reconcile 的目标之一。

### 4. Rook 部署 Ceph 就不需要理解 `MON`、`OSD`、`MDS`、`RGW`

不对。Rook 改变的是编排方式，不是 Ceph 组件本体职责。

### 5. Rook 和 `cephadm` 可以简单叠加成同一种控制面

不应该这样理解。它们都是 orchestrator backend，但面向不同环境与管理模型。

### 6. 用了 CRD 就意味着不需要 Ceph CLI

也不对。声明式管理很重要，但 Ceph 本体的观察、诊断、某些操作仍然离不开 Ceph 管理工具和认知。

### 7. Rook 的本质是起一堆 Pod

太浅了。Rook 的关键价值在于基于声明式对象和控制循环持续管理集群生命周期。

## 如果你从源码入口开始读，应该先看哪里

这一篇如果要继续往源码深挖，我建议先记住这几个入口：

- [rook.rst](file:///e:/projects/ceph/doc/mgr/rook.rst)
- [orchestrator.rst](file:///e:/projects/ceph/doc/mgr/orchestrator.rst)
- [module.py](file:///e:/projects/ceph/src/pybind/mgr/rook/module.py)
- [rook_cluster.py](file:///e:/projects/ceph/src/pybind/mgr/rook/rook_cluster.py)
- [kubernetes.rst](file:///e:/projects/ceph/doc/dev/kubernetes.rst)

它们分别解决不同层次的问题：

- `rook.rst` 负责告诉你“Ceph 如何把 Rook 当作 orchestrator backend”
- `orchestrator.rst` 负责告诉你“Ceph 管理平面的统一抽象是什么”
- `module.py` 负责告诉你“Ceph MGR rook backend 如何接上 Kubernetes API”
- `rook_cluster.py` 负责告诉你“抽象动作如何落到具体 CRD 变更”
- `kubernetes.rst` 负责告诉你“开发者如何在 Kubernetes / Rook 环境里工作”

## 这一篇最应该留下的 5 个直觉

### 直觉一：Rook 是 Kubernetes 中的 Ceph 编排层，不是 Ceph 本体

这是一切理解的起点。

### 直觉二：Rook 的控制接口是 CRD，而不是一组零散运维命令

`CephCluster`、`CephFilesystem`、`CephObjectStore` 都是声明式对象。

### 直觉三：Ceph MGR rook 模块是 bridge，不是 Operator 本身

它是 Ceph orchestrator framework 和 Rook 之间的适配层。

### 直觉四：Rook 和 `cephadm` 是两种 backend，不是简单上下级

它们都服务于“如何管理 Ceph”，但所在世界不同。

### 直觉五：进入 Kubernetes 后，最重要的变化不是 Ceph 组件职责变了，而是管理模型变了

角色还是那些角色，变的是编排、声明、调谐和生命周期管理方式。

## 下一篇看什么

到这里，我们已经把：

- Ceph 是什么
- 各核心角色做什么
- `cephadm` 如何在主机世界里部署 Ceph
- Rook 如何在 Kubernetes 世界里编排 Ceph

这四层都串起来了。

下一步最自然的事情，就是回到 Ceph 仓库本身，回答一个所有想深入源码的人都会遇到的问题：

**这么大的仓库，到底从哪里开始读，哪些目录最值得先认识？**

所以下一篇建议接：

**《Ceph 源码仓库怎么读：目录结构、构建系统与第一批核心模块》**
