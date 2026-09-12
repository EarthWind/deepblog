# 基于源码实现的 DAOS 博客系列清单

本文档面向准备系统性研究 `daos` 源码的开发者，给出一套由浅入深的博客选题。所有选题都尽量锚定仓库中的真实目录、入口文件和核心实现，便于后续继续扩写成正式博客。

## 一、先建立整体地图

在正式动笔前，建议先统一一个理解框架：

- 产品定位看仓库根目录 `README.md`。
- 内部架构总图看 `src/README.md`。
- 控制面看 `src/control/README.md`。
- 数据面看 `src/engine/README.md`。
- 通信协议看 `src/proto/README.md` 和 `src/cart/README.md`。
- 服务层重点看 `src/pool`、`src/container`、`src/object`、`src/rebuild`、`src/mgmt`。
- 存储内核重点看 `src/vos`、`src/bio`、`src/vea`、`src/rdb`、`src/rsvc`。

如果只想先抓主线，推荐先读下面这些入口：

- `src/control/cmd/daos_server/main.go`
- `src/control/cmd/dmg/main.go`
- `src/control/cmd/daos_agent/main.go`
- `src/engine/init.c`
- `src/engine/module.c`
- `src/client/api/init.c`
- `src/pool/README.md`
- `src/container/README.md`
- `src/object/README.md`
- `src/vos/README.md`

## 二、写作分层

为了做到“由浅入深”，整个系列建议分成四个阶段：

1. 认识 DAOS
2. 看懂进程、协议与启动链路
3. 进入服务层和存储层
4. 回到客户端、生态接口与工程体系

下面给出推荐的 14 篇博客清单。

---

## 第一阶段：认识 DAOS

### 01. DAOS 是什么：从产品定位到仓库结构

**适合读者**

- 第一次接触 DAOS 的工程师
- 想从仓库结构快速建立全局认知的读者

**这篇要回答的问题**

- DAOS 解决的核心问题是什么？
- 为什么它强调分布式、异步、对象存储、SCM/NVMe？
- 整个仓库为什么会同时出现 Go、C、Python、Proto、MkDocs？

**建议文章结构**

1. 从 `README.md` 提炼 DAOS 的目标能力。
2. 结合 `src/README.md` 解释代码库的分层。
3. 说明 `docs/`、`src/`、`ci/`、`utils/`、`src/include/` 的职责。
4. 给出一张“先读什么、后读什么”的源码地图。

**关键代码与文档入口**

- `README.md`
- `src/README.md`
- `mkdocs.yml`
- `SConstruct`

**写作重点**

- 这篇不要一上来钻实现细节，而是建立“产品能力 -> 架构分层 -> 仓库目录”的映射关系。

---

### 02. 控制面与数据面：DAOS 为什么是双平面架构

**适合读者**

- 已知道 DAOS 是分布式存储，但不清楚控制面和数据面边界的人

**这篇要回答的问题**

- `daos_server` 和 `daos_engine` 分别负责什么？
- 为什么控制面用 Go，而数据面主要用 C？
- 管理流量和数据流量为什么要分层处理？

**建议文章结构**

1. 从 `src/README.md` 的组件说明切入。
2. 讲清楚 `daos_server` 的编排、配置、设备管理职责。
3. 讲清楚 `daos_engine` 的 I/O、服务模块、线程模型职责。
4. 解释双平面架构对扩展性、性能和可运维性的价值。

**关键代码与文档入口**

- `src/README.md`
- `src/control/README.md`
- `src/engine/README.md`
- `src/control/cmd/daos_server/main.go`
- `src/engine/init.c`

**写作重点**

- 把“管理控制”和“高性能数据通路”拆开讲，这是后续理解协议栈和服务层的基础。

---

### 03. 从源码目录看 DAOS：第一次阅读应该按什么顺序

**适合读者**

- 准备正式阅读代码，但面对 `src/` 不知从哪下手的人

**这篇要回答的问题**

- `src/` 下面哪些目录是基础设施，哪些是业务服务？
- 客户端、控制面、数据面、公共库之间的依赖关系是什么？
- 阅读顺序应该按“功能模块”还是“请求链路”组织？

**建议文章结构**

1. 先解释 `src/include`、`src/common`、`src/gurt` 的公共性质。
2. 再解释 `src/control`、`src/engine`、`src/client` 的入口角色。
3. 最后介绍 `src/pool`、`src/container`、`src/object`、`src/vos` 等核心模块。
4. 给出一个适合博客持续更新的阅读路线图。

**关键代码与文档入口**

- `src/README.md`
- `src/include`
- `src/common`
- `src/gurt`
- `src/client`
- `src/control`
- `src/engine`

**写作重点**

- 强调 DAOS 源码不是单体程序，而是“公共库 + 服务模块 + 控制面应用 + 客户端接口”的组合。

---

## 第二阶段：看懂进程、协议与启动链路

### 04. `daos_server` 启动时做了什么：控制面主进程剖析

**适合读者**

- 已经知道控制面存在，但没看过启动代码的读者

**这篇要回答的问题**

- `daos_server` 从 `main()` 开始做了什么？
- 配置、预检查、网络初始化、NVMe/SCM 初始化是怎样串起来的？
- 控制面为什么需要特权 helper？

**建议文章结构**

1. 从 `src/control/cmd/daos_server/main.go` 的 `main()` 和 `parseOpts()` 进入。
2. 说明命令行解析、日志初始化、helper 检查等前置步骤。
3. 解释控制面如何准备网络、存储和 engine 的启动条件。
4. 结合 `src/control/server/README.md` 讲清服务端内部组织。

**关键代码与文档入口**

- `src/control/cmd/daos_server/main.go`
- `src/control/README.md`
- `src/control/server/README.md`
- `src/control/pbin`

**建议点名的实现符号**

- `main()`
- `parseOpts()`
- `pbin.CheckHelper()`

**写作重点**

- 这篇可以把“一个 Go 控制面进程如何编排本机数据面生命周期”讲清楚。

---

### 05. `daos_engine` 如何启动并装载模块：数据面初始化主线

**适合读者**

- 想知道 DAOS 数据面不是单一服务，而是模块化组合的人

**这篇要回答的问题**

- `daos_engine` 为什么按模块装载？
- `vos`、`rdb`、`pool`、`cont`、`obj`、`rebuild` 这些模块怎么被注册？
- RPC handler、dRPC handler、TLS key 是如何接入数据面的？

**建议文章结构**

1. 从 `src/engine/init.c` 讲数据面全局状态与模块列表。
2. 从 `modules_load()` 转到 `src/engine/module.c`。
3. 解释 `dss_module_load()`、`dss_module_init_one()` 的作用。
4. 说明模块接口为后续服务拆分带来了什么好处。

**关键代码与文档入口**

- `src/engine/init.c`
- `src/engine/module.c`
- `src/engine/README.md`

**建议点名的实现符号**

- `modules_load()`
- `dss_module_load()`
- `dss_module_init_one()`
- `dss_register_key()`

**写作重点**

- 这篇是从“架构图”进入“真实模块装载代码”的关键一篇，适合作为后续服务层文章的总入口。

---

### 06. gRPC、dRPC 与 CART：DAOS 为什么同时维护三条通信通路

**适合读者**

- 对 DAOS 通信模型感到困惑的读者

**这篇要回答的问题**

- 为什么控制面管理走 gRPC？
- 为什么本地进程间通信走 dRPC？
- 为什么客户端与数据面、数据面之间走 CART？

**建议文章结构**

1. 用一张图先画出三条通信路径。
2. 讲 `src/proto` 中协议定义如何服务于 gRPC 和 dRPC。
3. 讲 `src/engine/README.md` 里的 dRPC server 机制。
4. 讲 `src/cart` 在数据通路中的定位。
5. 总结三者在性能、部署边界、调用语义上的差异。

**关键代码与文档入口**

- `src/proto/README.md`
- `src/engine/README.md`
- `src/cart/README.md`
- `src/control/drpc`

**写作重点**

- 重点不是“列协议名词”，而是回答“为什么不是一个协议走到底”。

---

## 第三阶段：进入服务层和存储层

### 07. Pool 服务：集群级资源与元数据是如何组织的

**适合读者**

- 想知道 Pool 在 DAOS 里到底只是“存储池概念”，还是“服务模块”的读者

**这篇要回答的问题**

- Pool 为什么既是资源边界，又是服务边界？
- Pool Service 和 Pool Target Service 各自负责什么？
- Pool 元数据为什么要依赖 Raft 和复制服务框架？

**建议文章结构**

1. 从 `src/pool/README.md` 的概念和元数据布局讲起。
2. 解释 pool create、connect、disconnect 的服务端流程。
3. 结合 `src/rsvc`、`src/rdb` 说明强领导复制元数据模型。
4. 说明 pool map 对对象布局、容器访问和故障恢复的影响。

**关键代码与文档入口**

- `src/pool/README.md`
- `src/pool/srv.c`
- `src/pool/rpc.h`
- `src/rsvc/README.md`
- `src/rdb/README.md`

**建议写透的链路**

- `daos_pool_connect` 对应的服务端连接流程

**写作重点**

- Pool 是理解系统资源组织方式的入口，也是理解对象布局与故障处理的前置知识。

---

### 08. Container 服务：事务、快照和句柄状态为什么落在这一层

**适合读者**

- 只把 container 理解为“目录”或“命名空间”的读者

**这篇要回答的问题**

- Container 为什么不只是逻辑隔离，而是事务和快照的核心承载层？
- container handle、epoch state、snapshot 分别由谁管理？
- OID allocator 为什么挂在 container 层？

**建议文章结构**

1. 从 `src/container/README.md` 的 metadata layout 讲起。
2. 解释 container create/open/close/destroy 的服务语义。
3. 重点讲 epoch protocol 与 snapshot。
4. 解释 container 层与 VOS、object 层之间的关系。

**关键代码与文档入口**

- `src/container/README.md`
- `src/container/srv.c`
- `src/container/rpc.h`
- `src/vos/README.md`

**写作重点**

- 这篇非常适合讲“容器是 DAOS 的事务与版本视图边界”。

---

### 09. Object 服务：从 dkey/akey 到对象类与数据保护

**适合读者**

- 需要理解 DAOS 对象模型的读者

**这篇要回答的问题**

- dkey/akey 这种二级 key 模型解决了什么问题？
- object class 如何决定分布、复制和纠删码？
- checksum、replication、EC 在 object 层如何协同？

**建议文章结构**

1. 从 `src/object/README.md` 讲对象的逻辑模型。
2. 解释 object type、object class、sharding 的设计含义。
3. 结合 checksum、replication、EC 说明 object I/O 栈的职责。
4. 补充 object 与 placement、VOS、DTX 的关系。

**关键代码与文档入口**

- `src/object/README.md`
- `src/object/cli_obj.c`
- `src/object/srv_obj.c`
- `src/object/srv_ec.c`
- `src/object/srv_csum.c`
- `src/placement/README.md`

**建议写透的概念**

- `dkey` 的放置含义
- object class 命名规则
- `daos_obj_generate_oid()` 的设计意义

**写作重点**

- 这篇是从“用户视角的数据模型”切入源码实现，非常适合承上启下。

---

### 10. RDB 与 RSVC：DAOS 的强领导复制元数据框架

**适合读者**

- 想弄清楚 pool/container 元数据高可用机制的读者

**这篇要回答的问题**

- RDB 和 RSVC 分别解决什么问题？
- 为什么服务端请求必须先变成可复制的状态更新？
- 为什么只有 leader 能处理更新 RPC？

**建议文章结构**

1. 从 `src/rdb/README.md` 讲 replicated log 和 deterministic update。
2. 再讲 `src/rsvc` 作为 replicated service framework 的位置。
3. 结合 pool/container service 说明这些基础设施如何被业务服务复用。
4. 总结“元数据高可用”和“数据副本一致性”在 DAOS 中是两条不同主线。

**关键代码与文档入口**

- `src/rdb/README.md`
- `src/rsvc/README.md`
- `src/pool/README.md`
- `src/container/README.md`

**写作重点**

- 很多读者会把 Raft 和对象复制混在一起，这篇文章要主动把两类一致性问题拆开。

---

### 11. VOS：DAOS 真正的本地存储内核

**适合读者**

- 准备进入底层存储实现的读者

**这篇要回答的问题**

- VOS 为什么被称为 Versioning Object Store？
- 近 epoch 读取、版本历史、discard、aggregate 是如何成立的？
- DTX、MVCC、timestamp cache 为什么要落到这一层？

**建议文章结构**

1. 从 `src/vos/README.md` 的设计目标切入。
2. 解释 VOS 的层级结构：container -> object -> dkey -> akey -> value。
3. 分别讲 Single Value 索引、Array Value EV-tree、MVCC、discard、aggregate。
4. 再讲 checksums 与 DTX 如何嵌入 VOS。

**关键代码与文档入口**

- `src/vos/README.md`
- `src/vos/vos_io.c`
- `src/vos/vos_obj.c`
- `src/vos/vos_container.c`
- `src/vos/vos_dtx.c`
- `src/vos/vos_aggregate.c`

**建议单独成节的主题**

- near-epoch read
- EV-tree 与 array extent
- `vos_aggregate()` 和 `vos_discard()`

**写作重点**

- 这篇建议写成长文，因为它决定了前面 object/container 文章到底“落盘到哪里”。

---

### 12. BIO、VEA 与 NVMe：数据为什么能高效地下沉到块设备

**适合读者**

- 想继续从 VOS 向下追到 NVMe/SPDK 的读者

**这篇要回答的问题**

- BIO 在 SCM 和 NVMe 双层介质之间扮演什么角色？
- SMD、DMA buffer、device owner xstream 分别解决什么问题？
- VEA 为什么是 VOS 之下的重要空间分配器？

**建议文章结构**

1. 从 `src/bio/README.md` 解释 BIO 的职责。
2. 讲 SPDK blobstore、SMD、健康监控、热插拔等机制。
3. 再介绍 `src/vea` 的空间分配角色。
4. 总结底层介质管理如何与上层对象服务衔接。

**关键代码与文档入口**

- `src/bio/README.md`
- `src/bio/bio_device.c`
- `src/bio/bio_context.c`
- `src/bio/smd/smd_device.c`
- `src/vea/README.md`
- `src/vea/vea_alloc.c`

**写作重点**

- 这篇是“VOS 之下还有什么”的延伸篇，适合作为存储内核阶段的收束。

---

## 第四阶段：回到客户端、生态接口与工程体系

### 13. `libdaos`、`libdfs` 与客户端初始化：用户请求如何进入 DAOS

**适合读者**

- 更关心客户端接口和应用接入方式的读者

**这篇要回答的问题**

- `daos_init()` 到底初始化了哪些客户端基础设施？
- `libdaos`、`libdfs`、KV、Array API 各处在什么层次？
- agent 为什么是客户端链路中不可忽视的一环？

**建议文章结构**

1. 从 `src/client/api/init.c` 的 `daos_init()` 切入。
2. 讲 task API 表、agent 初始化、job 初始化、attach info 缓存。
3. 解释 `src/client/api`、`src/client/dfs`、`src/client/kv`、`src/client/dfuse` 的分层。
4. 给出一条“应用调用 -> 客户端库 -> 网络 -> 服务端”的总链路。

**关键代码与文档入口**

- `src/client/api/init.c`
- `src/client/README.md`
- `src/client/api`
- `src/client/dfs`
- `src/control/cmd/daos_agent`

**建议点名的实现符号**

- `daos_init()`
- `dc_agent_init()`
- `dc_mgmt_cache_attach_info()`

**写作重点**

- 这篇能把“服务端世界”重新和“应用开发者视角”接起来。

---

### 14. 从构建到测试：SCons、Proto、CI 如何把 DAOS 组织起来

**适合读者**

- 准备参与开发、修 bug、跑测试或提交补丁的读者

**这篇要回答的问题**

- DAOS 为什么选择 SCons？
- `src/proto`、生成代码、Go/C 双语言构建是怎么协同的？
- 单元测试、功能测试、CI 流水线分别在哪里？

**建议文章结构**

1. 从 `SConstruct` 看整体构建入口。
2. 解释 `src/proto` 如何生成 Go 的 `.pb.go` 和 C 的 `.pb-c.[ch]`。
3. 介绍 `ci/`、`src/tests/`、`utils/run_utest.py`、`.github/workflows/ci2.yml`。
4. 给出一个适合新贡献者的上手路径。

**关键代码与文档入口**

- `SConstruct`
- `src/proto/README.md`
- `src/proto/Makefile`
- `src/tests`
- `ci`
- `.github/workflows/ci2.yml`
- `utils/run_utest.py`

**写作重点**

- 这篇不是讲业务逻辑，而是帮助读者真正进入“可构建、可测试、可贡献”的状态。

---

## 三、建议的正式发布顺序

如果你准备真的开始写博客，而不是只做选题归档，推荐按下面顺序发布：

1. `01. DAOS 是什么：从产品定位到仓库结构`
2. `02. 控制面与数据面：DAOS 为什么是双平面架构`
3. `03. 从源码目录看 DAOS：第一次阅读应该按什么顺序`
4. `04. daos_server 启动时做了什么：控制面主进程剖析`
5. `05. daos_engine 如何启动并装载模块：数据面初始化主线`
6. `06. gRPC、dRPC 与 CART：DAOS 为什么同时维护三条通信通路`
7. `07. Pool 服务：集群级资源与元数据是如何组织的`
8. `08. Container 服务：事务、快照和句柄状态为什么落在这一层`
9. `09. Object 服务：从 dkey/akey 到对象类与数据保护`
10. `10. RDB 与 RSVC：DAOS 的强领导复制元数据框架`
11. `11. VOS：DAOS 真正的本地存储内核`
12. `12. BIO、VEA 与 NVMe：数据为什么能高效地下沉到块设备`
13. `13. libdaos、libdfs 与客户端初始化：用户请求如何进入 DAOS`
14. `14. 从构建到测试：SCons、Proto、CI 如何把 DAOS 组织起来`

## 四、如果要继续扩写成正式博客，推荐统一模板

后续每篇文章都可以复用同一套模板：

1. 问题背景
2. 模块职责
3. 关键结构或关键进程
4. 入口函数与调用链
5. 核心数据结构或状态机
6. 故障、边界条件与一致性设计
7. 小结与下一篇衔接

这样做的好处是，整套文章会更像一个连续的源码解读系列，而不是彼此割裂的知识点摘录。
