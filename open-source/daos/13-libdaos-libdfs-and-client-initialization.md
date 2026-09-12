# 13. `libdaos`、`libdfs` 与客户端初始化：用户请求如何进入 DAOS

## 为什么写到这里要把视角切回客户端

前面几篇我们一路往服务端和存储内核下钻，已经看过了：

- `daos_server` 怎么启动
- `daos_engine` 怎么装载模块
- `pool/container/object` 服务如何分层
- `RDB/RSVC` 怎样支撑元数据高可用
- `VOS`、`BIO`、`VEA` 怎样让本地版本化数据真正落到介质

但如果只停在服务端视角，整条链路还缺最后一块拼图：

- 应用程序发起的 `daos_pool_connect()`、`daos_obj_fetch()`、`dfs_open()`，到底是怎么进入这套系统的？

这正是客户端这一层的价值。

因为站在应用开发者眼里，最先接触到的不是：

- `pool_svc`
- `ds_obj_rw_handler()`
- `VOS`

而是：

- `libdaos`
- `libdfs`
- `daos_init()`
- pool/container/object/array/kv 这些 API

所以这一篇的核心任务，就是把前面已经讲清的“服务端世界”，重新和“应用调用入口”接起来。

## 先给结论：客户端不是一个薄 RPC 壳，而是一套完整的初始化、调度、句柄和上层接口框架

如果第一次看 DAOS 客户端，很容易误以为：

- `libdaos` 不过是把 API 参数打包成 RPC 发给服务端

这其实低估了客户端层。

从 `src/client/README.md` 和 `src/client/api/init.c` 看，客户端至少承担了这些职责：

- 初始化网络与运行时上下文
- 初始化事件队列和任务调度器
- 管理 pool/container/object 等本地句柄
- 对接 `daos_agent` 获取 attach info 和凭证
- 把 blocking / non-blocking API 统一收敛到 task/event 执行模型
- 在 `libdfs`、KV、Array 等更高层接口里复用底层对象 API

所以更准确的理解应该是：

- **`libdaos` 是 DAOS 的客户端运行时与基础 API 层**
- **`libdfs`、KV、Array 等是在其上构建的更高层访问接口**

## `src/client` 这棵树到底怎么分层

### `src/client/README.md` 先给出最顶层地图

README 列出的几个库很清楚：

- `libdaos`
- Python bindings
- Go bindings
- `libdfs`
- `libds3`

这说明 `src/client` 不是只有一个 C API，而是整个客户端生态入口。

### `src/client/api` 是最底层公共客户端 API

`src/client/api/README.md` 把 API 分成了几块：

- Management API
- Pool API
- Container API
- Transaction API
- Object / Array / KV API
- Event / Event Queue / Task API
- Addons API
- DFS API
- DUNS API

其中真正的核心底座是：

- pool/container/object/tx/event/task 这几组 API

因为其他更高层东西，大多都会回到这里。

### `libdfs` 是构建在 `libdaos` 之上的 POSIX-like namespace

`src/client/dfs/README.md` 一开头就说：

- DFS 提供的是封装在单个 DAOS container 里的 POSIX-like namespace
- 文件和目录最终都是这个 container 里的对象

这意味着：

- `libdfs` 不是另一套独立存储系统
- 它是建立在 `libdaos` object / array / container API 之上的命名空间层

所以 `libdfs` 的价值主要是：

- 把应用更熟悉的文件/目录语义映射到 DAOS 对象模型

而不是绕开 `libdaos`。

### KV / Array 也不是“独立后端”，而是 object API 上的 addon

`src/client/api/README.md` 直接说：

- Addons API 是 built over the DAOS object API

再看 `src/client/kv/dc_kv.c` 和 `src/client/array/dc_array.c` 也很清楚：

- KV 的 put/get/list 最终调的是 `DAOS_OPC_OBJ_UPDATE`、`DAOS_OPC_OBJ_FETCH`、`DAOS_OPC_OBJ_LIST_DKEY`
- Array 的 open/read/write 最终也会创建 object open/fetch/update 任务

所以：

- KV/Array 是“更专用、更易用的对象访问包装层”
- 不是和 object 层平行的全新执行栈

## `daos_init()` 为什么是客户端真正的总入口

### `src/client/api/init.c` 的 `daos_init()` 非常值得直接顺着读一遍

如果要找“应用请求如何进入 DAOS”的源码起点，最自然的入口就是：

- `daos_init()`

因为它把客户端需要的运行时基础设施按顺序全立起来了。

### 第一步：保证初始化是进程内可重入、可引用计数的

`init.c` 一开始就有：

- `module_lock`
- `module_initialized`

并且 `daos_init()` / `daos_fini()` 都围绕它们做互斥与引用计数控制。

这说明 DAOS 客户端从一开始就考虑了：

- 同一进程里可能被多个 middleware / library 重复初始化

所以它不是“只能 init 一次”的脆弱模型，而是：

- 有 refcount 的库级运行时

### 第二步：初始化基础调试和本地句柄框架

`daos_init()` 先做：

- `daos_debug_init(NULL)`
- `daos_hhash_init_feats(D_HASH_FT_RWLOCK)`

这里的 `hhash` 非常重要，因为 pool/container/object 这些客户端句柄，本地都需要：

- 查找
- 引用计数
- 生命周期管理

这说明客户端 API 不是每次调用都“无状态地组 RPC”，它本地也维护着句柄世界。

## `dc_agent_init()` 为什么出现在初始化早期

### 它做的事情很小，但位置非常关键

`src/client/api/agent.c` 里的 `dc_agent_init()` 看起来很简单：

- 读取 `DAOS_AGENT_DRPC_DIR`
- 组装 agent 的 unix socket 路径
- 保存到 `dc_agent_sockpath`

这看上去不像“大初始化动作”，但它在客户端链路里非常关键，因为后面很多事情都依赖 agent。

### 客户端不是直接读 agent 配置文件

`src/control/cmd/daos_agent/README.md` 写得很明确：

- 客户端库不会读取配置文件
- 如果 agent socket 不在默认目录，必须通过环境变量 `DAOS_AGENT_DRPC_DIR` 告诉客户端

这意味着：

- 客户端和 agent 的耦合点不是共享配置文件
- 而是 dRPC socket 路径约定

这是个很典型的“本地进程协作”设计。

## agent 为什么是客户端链路里不可忽视的一环

### agent 不是“可选优化”，而是受信任的本地中介

`daos_agent/README.md` 开头就定义得很明确：

- agent 是运行在 compute node 上的后台进程
- 充当客户端应用和 DAOS 系统之间的 trusted intermediary
- 每个 compute node 都必须有 agent，客户端操作才能成功

这句话很重，说明 agent 在架构上不是边缘角色。

### agent 主要解决两类问题

README 列得很清楚：

- 获取 attach info
- 生成 signed client credential

这两件事恰好对应客户端进入 DAOS 前的两个必要前提：

- 我要知道用什么网络配置、去联系哪些 ranks
- 我要拿到可被服务端信任的用户身份凭证

### 为什么客户端不能自己做这两件事

README 对凭证部分说得很直白：

- client library 可能被篡改或替换
- 因而不能信任它自己生成凭证

所以：

- 用户身份确认和 credential 签发必须交给本机受信任 agent

而 attach info 这件事同样不适合塞进客户端静态配置，因为：

- PSR 列表
- fabric provider
- interface / domain / timeout
- NUMA 亲和设备选择

这些都更适合由本机 agent 结合控制面与本地 fabric 扫描动态决定。

## `dc_mgmt_cache_attach_info()`：客户端为什么要在初始化阶段先拿 attach info

### `daos_init()` 很早就调用了这一步

在 `init.c` 里，`dc_job_init()` 之后马上就是：

- `dc_mgmt_cache_attach_info(NULL)`

这说明 attach info 不是“第一次 RPC 时顺便再拿”的可选信息，而是客户端运行时早期就要准备好的基础数据。

### agent README 刚好解释了 attach info 里到底有什么

`daos_agent/README.md` 对 Get Attach Info 的描述非常完整：

- client 最开始并不知道 server ranks 的 URI
- 需要先拿到 Primary Service Ranks
- 需要拿到 fabric provider、interface、domain、timeout
- agent 会结合本地 NUMA 亲和和 fabric 扫描给出合适响应

这说明 attach info 的作用可以概括成一句话：

- **告诉客户端如何把后续 CaRT 通信真正建起来**

所以 `dc_mgmt_cache_attach_info()` 的重要性不在“缓存”两个字，而在：

- 它是客户端进入数据平面通信前的准备动作

## `dc_mgmt_net_cfg_init()` 和 `daos_eq_lib_init()`：客户端网络上下文真正从这里立起来

### `daos_init()` 的中段是一条非常清晰的网络初始化链

在 `init.c` 里，attach info 之后依次是：

- `dc_mgmt_net_cfg_init(NULL, crt_info)`
- `dc_tm_init(crt_info)`
- `daos_eq_lib_init(crt_info)`
- `dc_mgmt_net_cfg_check(NULL)`

如果压缩理解，这一段就是：

- 用 attach info 初始化 CaRT 配置
- 建 client telemetry
- 建 event queue / scheduler / network context
- 再检查最终网络配置是否有效

### `daos_eq_lib_init()` 真正做了什么

`src/client/api/event.c` 里可以直接看到：

- `crt_init_opt(...)`
- `crt_context_create(...)` 或 `crt_context_create_on_iface(...)`
- `tse_sched_init(&daos_sched_g, NULL, daos_eq_ctx)`

这三步非常关键，因为它们说明：

- 客户端不是抽象意义上的“有网络”
- 它是真的初始化了 CaRT runtime
- 创建了 client-side `crt_context`
- 再把这个网络 context 交给 TSE scheduler

所以从这里开始，DAOS 客户端 API 才真正具备：

- 发 RPC
- 跑 task
- 推进异步事件

这些能力。

## 为什么 Event / EQ / Task 是客户端 API 的中心，而不是边缘功能

### `api/README.md` 明确说 DAOS API 天生支持 blocking 和 non-blocking 两种模式

README 指出：

- 传 `NULL` event 就是 blocking
- 传合法 event 就是 non-blocking
- 真正执行结果由 event completion 决定

这说明异步不是“附赠功能”，而是 API 设计从一开始就内建的模式。

### EQ 的本质是“事件集合 + 独立调度器 + 独立网络 context”

README 进一步说：

- event queue 会为其下所有 DAOS tasks 建独立 scheduler
- 同时创建新的 network context

这点非常值得注意，因为它意味着：

- EQ 不只是一个完成队列
- 它还是 task 执行与网络推进的边界

所以应用和中间件如果自己管理很多并发 I/O，EQ 的设计直接关系到：

- 并发组织方式
- 网络上下文数量
- 资源开销

## `dc_task_create()` / `dc_task_schedule()`：为什么同步和异步 API 最终能走同一条主线

### `src/client/api/task.c` 把这个设计讲得很透

`dc_task_create(...)` 的逻辑是：

- 如果没给 scheduler，就从 event 推 scheduler
- 如果连 event 都没给，就取 private event
- 创建 `tse_task`
- 如果有 event，就注册 completion callback，在 task 完成时补全 event

这意味着 DAOS 客户端的 API 执行模型并不是：

- blocking 和 non-blocking 两套完全不同实现

而是：

- **同一套 task 执行框架，外面包不同 event 语义**

### `dc_task_schedule()` 更把这件事收口了

`dc_task_schedule(...)` 会：

- 若有关联 event，先 `daos_event_launch(ev)`
- 再 `tse_task_schedule(task, instant)`
- 如果是 private event，则内部等待完成并返回最终错误码

这正是整个客户端 API 风格统一的关键：

- 对 blocking 调用者来说，库内部帮你等待 private event 完成
- 对 non-blocking 调用者来说，库把完成状态挂在 event / EQ 上

所以从用户角度看是两种调用方式，但从库内部看，其实是：

- 同一任务引擎上的两种包装形式

## `dc_funcs[]`：为什么说 DAOS 客户端 API 本质上是一张任务分发表

### `init.c` 顶部那张表非常关键

`src/client/api/init.c` 里有一个很长的：

- `const struct daos_task_api dc_funcs[]`

它把很多 opcode 映射到对应 task function：

- `dc_pool_connect`
- `dc_cont_open`
- `dc_tx_commit`
- `dc_obj_fetch_task`
- `dc_obj_update_task`
- `dc_array_read`
- `dc_kv_put`

这张表其实就是一个很重要的架构信号：

- DAOS client API 在内部是 opcode -> task function 的分发表

### `daos_task_create()` 直接利用这张表

在 `task.c` 里：

- `daos_task_create(opc, sched, num_deps, dep_tasks, taskp)`

会从 `dc_funcs[opc]` 里拿到 `task_func`，然后创建对应 task。

所以不仅同步/异步 API 统一在 task 引擎里，连不同功能域的 API 也都被收口到：

- opcode + task function

这使得客户端库能把：

- pool/container/object/array/kv

这些不同能力都放进同一执行框架。

## Object / Pool / Container 公共 API 是怎么落到 task 的

### `src/client/api/*` 目录的写法很统一

例如：

- `pool.c` 里的 `daos_pool_connect()`
- `container.c` 里的 `daos_cont_open()`
- `object.c` 里的 `daos_obj_fetch()`

基本都遵循同一模式：

1. `dc_task_create(...)` 或对应 `*_task_create(...)`
2. 填充参数
3. `dc_task_schedule(task, true)`

这说明用户看到的 C API，大多只是 task-based internal API 的薄封装。

### 这也解释了为什么依赖关系能自然表达

`task.c` 的 `daos_task_create()` 支持：

- `num_deps`
- `dep_tasks`

而在 `array`、`kv`、`dfs` 这些更高层代码里，也可以看到大量：

- 先创建 open task
- 再创建 fetch/update/stat 子任务
- 最后按依赖顺序 schedule

这让客户端层不仅能发单个 RPC，也能在本地先组织小型任务图。

## `libdfs` 为什么能看成“把 POSIX-like namespace 翻译成 object/array 操作”

### `dfs/README.md` 其实把这一层说得很透

README 明确说：

- DFS namespace 位于一个 pool 和一个 container 内
- 目录和文件都是对象
- 文件读写通过 DAOS Array API 完成

这意味着 `libdfs` 的本质不是“文件系统设备驱动”，而是：

- **命名空间与 inode 风格元数据映射层**

### 目录和文件分别怎样落到底层

从 README 的映射表能看出：

- 目录对象的 dkey 是 entry name
- inode 类元数据落在 `DFS_INODE` 这样的 akey 里
- 文件自身是 DAOS array object

这就解释了为什么 `dfs_open()` 的控制流图里会出现：

- 先 `daos_obj_fetch()` 从父目录取元数据
- 再 `daos_array_open_with_attr()` 或 `daos_obj_open()`

也就是说，`libdfs` 对外提供的是文件/目录接口，但对内其实在做：

- 路径解析
- 目录项元数据抓取
- 对象/数组句柄打开

### `libdfs` 也没有绕开 task/event 模型

从 `src/client/dfs/io.c`、`obj.c` 可以看到很多：

- `dc_task_create(...)`
- `daos_task_create(...)`
- `tse_task_schedule(...)`

这说明 `libdfs` 也不是另起一套执行引擎，而是继续建在 `libdaos` 的 task/task-dependency 模型之上。

## KV 和 Array 为什么很适合用来理解“高层 API 复用底层对象 API”

### KV 的例子最直观

在 `src/client/kv/dc_kv.c` 里可以直接看到：

- `daos_task_create(DAOS_OPC_OBJ_OPEN, ...)`
- `daos_task_create(DAOS_OPC_OBJ_UPDATE, ...)`
- `daos_task_create(DAOS_OPC_OBJ_FETCH, ...)`
- `daos_task_create(DAOS_OPC_OBJ_LIST_DKEY, ...)`

这说明 KV 并没有一套独立“KV RPC”，而是：

- 用约定好的对象布局和 object API，把 KV 语义搭起来

### Array 也同样如此

在 `src/client/array/dc_array.c` 里可以看到：

- 先 object open
- 再 object fetch/update
- 以及围绕 array 元信息的附加逻辑

所以如果你想理解 DAOS 客户端 API 的层次感，KV 和 Array 是最好的例子：

- 它们更接近应用语义
- 但并不重造底层 transport 与执行框架

## agent 在请求链路里到底参与到什么程度

这点很容易写偏，所以要明确分界。

### agent 很关键，但不在每次对象 I/O 的热路径里

从 `daos_init()` 和 `daos_agent/README.md` 看，agent 最关键的作用在于：

- 初始 attach info 获取
- 凭证签发

也就是说，agent 主要参与的是：

- 客户端进入系统前的准备与认证链路

而不是：

- 每一次 `daos_obj_fetch()` / `daos_obj_update()` 都先转发给 agent

### 真正的数据 RPC 仍然是 client 直接经 CaRT 打到服务端

`daos_agent/README.md` 明确说：

- attach info 拿到之后，client communications over CaRT 就会初始化
- 之后会自动查询 PSR 并把 RPC 发到正确 rank

所以 agent 的位置更像：

- 本地控制与认证引导者

而不是：

- 数据平面的代理转发层

这个边界一定要记清。

## 一条完整总链路：从应用调用到服务端处理

把前面内容压成一条总链路，大概是这样。

### 1. 应用初始化客户端

应用或 middleware 调用：

- `daos_init()`

客户端完成：

- debug / handle hash 初始化
- agent socket 准备
- attach info 获取与缓存
- 网络配置初始化
- CaRT context 初始化
- 全局 scheduler / EQ library 初始化
- mgmt/pool/container/object 模块初始化

### 2. 应用发起某个高层 API

例如：

- `daos_pool_connect()`
- `daos_cont_open()`
- `daos_obj_fetch()`
- `dfs_open()`

### 3. 高层 API 落成 task

公共 API 封装层会：

- 创建 `tse_task`
- 绑定 event 或 private event
- 填充参数
- 必要时建立任务依赖

### 4. scheduler 推进 task，并通过 CaRT 发 RPC

此时客户端已经拥有：

- 可用网络配置
- client-side CRT context
- 事件/任务调度器

所以 task 可以进一步：

- 创建 RPC
- 提交到底层网络栈
- 在 event / EQ 或同步等待中返回结果

### 5. 服务端接手后进入前面几篇讲过的世界

请求到达服务端后，就会进入我们已经展开过的路径：

- `daos_server` / `daos_engine`
- pool/container/object handler
- `RDB/RSVC`
- `VOS`
- `BIO/VEA/NVMe`

这时客户端部分的主要职责就完成了。

## 为什么这一层对应用开发者和源码读者都很关键

### 对应用开发者

这一层回答的是：

- 为什么先 `daos_init()`
- 为什么 event/EQ 会影响异步并发模型
- 为什么 `libdfs` 是命名空间封装，不是独立后端
- 为什么 agent 没法简单省掉

### 对源码读者

这一层回答的是：

- 应该从哪里进入客户端代码
- 哪些目录是公共 API，哪些是 addon
- 为什么很多 API 最后都会落到 task/event
- 为什么 client 侧也有相当完整的运行时，而不只是轻量 stub

## 一个更实用的阅读顺序

如果你准备从客户端方向继续读源码，最推荐的顺序是：

1. 先读 `src/client/api/init.c`，看 `daos_init()` 把哪些模块拉起来。
2. 再读 `src/client/api/event.c` 和 `task.c`，理解 EQ / TSE / private event。
3. 接着读 `src/client/api/pool.c`、`container.c`、`object.c`，看公共 API 怎么包装成 task。
4. 然后读 `src/control/cmd/daos_agent/README.md`，把 attach info 和 credential 的角色补齐。
5. 最后再读 `src/client/dfs`、`array`、`kv`，看高层接口如何复用 object API。

这样读，主线会最清楚。

## 小结

`libdaos`、`libdfs` 和客户端初始化这一层，真正重要的不是“API 名字很多”，而是它把应用请求组织成了一条非常清晰的进入路径：

- `daos_init()` 建立客户端运行时
- `daos_agent` 提供 attach info 与可信凭证
- `event/EQ/task` 统一 blocking 与 non-blocking 执行模型
- pool/container/object 成为核心公共 API
- `libdfs`、KV、Array 则在其上构建更贴近应用的访问语义

如果把这一篇压缩成一句话，那就是：

**应用并不是直接“调用某个 RPC”，而是先进入 `libdaos` 的初始化、句柄、task/event 和 agent 协作框架，再由这套客户端运行时把请求送入前面几篇文章已经讲清的服务端世界。**

## 下一篇看什么

把客户端入口补齐以后，系列里最自然的下一篇就是回到工程体系本身：

**从构建到测试：SCons、Proto、CI 如何把 DAOS 组织起来**

因为到这里，架构主线已经基本闭环，下一步就很适合回答：

- 代码是怎么被编起来的
- Go/C/proto 为什么能混在一个仓库里协同
- 新贡献者应该从哪里开始 build、test、提 patch
