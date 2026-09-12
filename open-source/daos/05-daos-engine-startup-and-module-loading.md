# 05. `daos_engine` 如何启动并装载模块：数据面初始化主线

## 为什么这一篇很关键

上一篇我们已经沿着 `daos_server` 的 `main()` 看清了控制面是怎么把舞台搭起来的：

- 解析参数和配置
- 检查 helper
- 准备网络和存储环境
- 建立 gRPC / dRPC / harness
- 最终把 `daos_engine` 纳入控制面治理

但到这里，其实只解决了“谁来启动数据面”的问题，还没有解决另一个更核心的问题：

**`daos_engine` 自己启动后，到底是怎么变成一个可工作的 DAOS 数据面实例的？**

这个问题之所以重要，是因为 DAOS 数据面并不是一个把所有逻辑硬编码在一个二进制里的单体程序。它更像一个模块化运行时：

- `vos`
- `rdb`
- `rsvc`
- `security`
- `mgmt`
- `dtx`
- `pool`
- `cont`
- `obj`
- `rebuild`

这些名字你在 `src/` 里早就见过，但直到看清 `daos_engine` 的启动与模块装载链路，你才会真正理解：

- 它们为什么会同时存在于同一个数据面进程里
- 它们是怎么被装进去的
- RPC handler、dRPC handler、TLS key 又是怎么挂上去的

所以这一篇的目标，就是沿着 `src/engine/init.c` 和 `src/engine/module.c` 这两条主线，回答下面几个问题：

1. `daos_engine` 为什么是模块化装载，而不是直接静态写死？
2. `modules_load()`、`dss_module_load()`、`dss_module_init_one()` 分别做什么？
3. `sm_key`、RPC handler、dRPC handler 是什么时候接入数据面的？
4. `setup` 和 `init` 有什么区别？

## 先给结论：`daos_engine` 不是单体服务，而是模块运行时

`src/engine/README.md` 一开头就把事情说得很明确：

- I/O Engine 支持 module interface
- server-side code 可以按需加载
- 每个模块本质上都是通过 `dlopen` 动态加载进来的库
- 模块接口由 `dss_module` 结构定义

同时 README 也指出，一个模块至少会声明：

- 模块名
- 模块 ID
- feature bitmask
- init/fini 函数

并且还能可选声明：

- `setup/cleanup`
- CART RPC handlers
- dRPC handlers

这已经足够说明 `daos_engine` 的定位：

它不是“对象服务进程”或者“pool 服务进程”，而是一个负责把多个服务模块装起来、初始化起来并协同运行的执行引擎。

换句话说，如果把 `daos_server` 看成“控制面 orchestrator”，那 `daos_engine` 更像“数据面模块容器”。

## 先看 `init.c`：数据面启动主线长什么样

### 启动并不是一上来就装模块

`src/engine/init.c` 里很容易让人注意到 `modules_load()`，但如果只盯这一个函数，就会误以为模块装载是数据面启动的全部。

实际上，在模块装载之前，`daos_engine` 已经先做了不少基础设施准备工作，比如：

- dRPC 初始化
- dbtree class 注册
- Argobots 初始化
- 模块接口框架初始化 `dss_module_init()`
- 网络层初始化 `crt_init_opt(...)`
- handle hash table 初始化
- placement 初始化
- IV 初始化

这说明一个重要事实：

**模块不是在“空白进程”里直接被加载的，而是在一个已经具备并发、网络、公共结构和通信基础的引擎环境里被装进去的。**

这也是为什么 DAOS 能把各类服务模块统一挂在数据面之下。因为在它们登场之前，运行时地基已经铺好了。

### 一个特别值得注意的顺序

`init.c` 的主线里，模块相关流程大致是这样：

1. `modules_load()`
2. `bio_nvme_init(...)`
3. `dss_module_init_all(&dss_mod_facs)`
4. `dss_srv_init()`
5. `drpc_notify_ready(...)`
6. `server_init_state_wait(DSS_INIT_STATE_SET_UP)`

这条顺序非常值得记住，因为它说明模块相关并不是一个动作，而是三层动作：

- `load`: 把模块动态库装进来
- `init`: 真正执行模块初始化和 handler 注册
- `srv_init` / `set_up`: 让整个服务栈进入可工作状态

如果你不区分这几个阶段，就很容易把 `dlopen`、模块初始化、服务就绪混成一回事。

## 第一段：`modules_load()` 到底做了什么

### 默认模块列表从哪里来

`init.c` 顶部直接定义了默认模块列表：

- `vos`
- `rdb`
- `rsvc`
- `security`
- `mgmt`
- `dtx`
- `pool`
- `cont`
- `obj`
- `rebuild`

如果启用了 pipeline 构建，还会把 `pipeline` 加进去。

这条列表非常重要，因为它直接告诉你：

- `daos_engine` 的默认职责范围是什么
- 哪些服务模块是数据面原生运行时的一部分

换句话说，这不是一个“谁想加载谁就加载”的完全自由插件系统，而是一套围绕 DAOS 数据面主线组织的模块集合。

### `modules_load()` 本身做得很“克制”

`modules_load()` 的逻辑并不复杂：

- 先把模块列表字符串按逗号拆开
- 处理少数别名映射，比如 `object -> obj`、`container -> cont`
- 然后逐个调用 `dss_module_load(mod)`

也就是说，`modules_load()` 本身更像一个名单分发器。真正的模块装载细节不在这里，而在 `module.c` 里。

### 为什么 `load` 和 `init` 要分开

`init.c` 在 `modules_load()` 调用前后有一句很关键的注释：

> Split load and init so first call to dlopen() is from the engine

这个细节非常能说明工程设计意图。

它表示 DAOS 有意识地把：

- 动态库装载
- 模块初始化

拆成两个阶段，而不是 `dlopen` 完马上执行全部初始化逻辑。

这样做至少有两个好处：

- 第一，模块装载问题和模块初始化问题可以分层定位。
- 第二，数据面能先把模块库统一纳入进程，再在合适时机统一执行初始化。

从源码阅读角度，这一层分离也非常重要，因为它告诉你：

**`load` 不是“模块已经可用”，只是“模块已经被放进进程地址空间”。**

## 第二段：`dss_module_load()` 是怎么把模块装进来的

`src/engine/module.c` 才是真正的模块装载实现。

### 1. 模块在 DAOS 里本质上是动态库

`dss_module_load(const char *modname)` 的第一件事，就是拼出动态库名字：

- `lib%s.so`

然后调用：

- `dlopen(name, RTLD_LAZY | RTLD_GLOBAL)`

这说明 DAOS 的模块化不是通过源码级注册表静态展开的，而是明确借助动态库机制来完成装载。

所以当我们说 `vos`、`pool`、`obj` 是模块时，这里的“模块”不是抽象名词，而是真正对应可被 `dlopen` 的库。

### 2. 不是只加载库，还要找模块描述符

`dlopen()` 之后，`dss_module_load()` 并不会立刻调用业务逻辑，而是继续用 `dlsym()` 去找一个符号：

- `%s_module`

也就是例如：

- `obj_module`
- `mgmt_module`

这个符号对应的就是模块描述结构 `struct dss_module`。

这一步很关键，因为它说明动态库本身还不够，数据面真正需要的是：

**这个库对外暴露出来的一份模块元信息和回调表。**

### 3. 模块被装入后，会进入跟踪表

`dss_module_load()` 在拿到 `dss_module` 描述之后，会：

- 分配 `loaded_mod`
- 保存库 handle
- 保存 `dss_module` 指针
- 把它挂进 `loaded_mod_list`
- 同时写入按 `mod_id` 索引的 `dss_modules[]`

这意味着模块装载完成后，数据面已经拥有两套视角：

- 一套链表视角，方便遍历所有已加载模块
- 一套按 `mod_id` 访问的数组视角，方便快速查找

所以 `load` 阶段真正建立的是模块管理结构，而不是服务逻辑本身。

## 第三段：`dss_module_init_one()` 才是真正的模块接入点

如果说 `dss_module_load()` 只是把模块库和描述符搬进来，那么 `dss_module_init_one()` 才是“让模块真正接上数据面”的核心。

这一段最值得仔细看。

### 1. 先调用模块自己的 `sm_init()`

第一步非常直接：

- `rc = smod->sm_init()`

这意味着每个模块先有一次自己的初始化机会。

例如：

- `obj_module` 的 `sm_init` 是 `obj_mod_init`
- `mgmt_module` 的 `sm_init` 是 `ds_mgmt_init`

这一步通常对应模块自己的内部状态、资源、上下文准备。

### 2. 如果模块声明了 `sm_key`，就在这里注册 TLS key

`dss_module_init_one()` 紧接着会判断：

- `if (smod->sm_key != NULL) dss_register_key(smod->sm_key);`

这一步和 `src/engine/README.md` 的 TLS 说明正好对上：

- 每个 xstream 都有私有 TLS 存储
- 模块可以声明自己的 module key
- 引擎会为该 key 在各 xstream 上分配私有空间

这意味着模块自己的 per-xstream 私有上下文，并不是后面随便找地方挂上的，而是在模块初始化阶段正式注册进引擎 TLS 体系的。

例如 `obj_module` 就声明了：

- `.sm_key = &obj_module_key`

这正是对象服务模块把自己接入线程局部状态体系的方式。

### 3. 然后注册 CART RPC handlers

接下来，`dss_module_init_one()` 会遍历：

- `smod->sm_proto_fmt`
- `smod->sm_cli_count`
- `smod->sm_handlers`

并调用：

- `daos_rpc_register(...)`

这一层的含义是：

- 模块不仅要“存在”，还必须把自己能处理哪些 RPC 告诉引擎
- 引擎把这些协议格式和 handler 正式挂进 RPC 框架

这一步之后，模块才算真正对外暴露了自己的数据面 RPC 处理能力。

例如：

- `obj_module` 声明了两个 proto 版本和对应 handlers
- `mgmt_module` 也声明了自己的 proto 版本和 handlers

这说明模块并不是只有“初始化函数”，而是明确带着协议接入信息进入数据面的。

### 4. 再注册 dRPC handlers

之后，`dss_module_init_one()` 还会调用：

- `drpc_hdlr_register_all(smod->sm_drpc_handlers)`

这一步对应的是模块的本地进程间通信能力接入。

从 `src/engine/README.md` 可以知道，dRPC 主要用于控制面和数据面之间、以及某些本地进程间协作。

因此模块如果需要处理来自控制面的本机 dRPC 请求，就要在这里把自己的 dRPC handlers 注册进去。

例如：

- `mgmt_module` 就声明了 `.sm_drpc_handlers = mgmt_drpc_handlers`

这很有代表性，因为管理服务本来就是控制面和数据面之间衔接最强的模块之一。

### 5. 最后记录 feature bitmask，并标记模块已初始化

如果一切顺利，`dss_module_init_one()` 会：

- 把 `sm_facs` 记入 `mod_facs`
- `lmod->lm_init = true`

这意味着引擎不仅知道“哪些模块在”，还知道“当前这些模块具备哪些能力特征”。

## 第四段：`dss_module_init_all()` 如何统一完成模块初始化

`dss_module_init_all()` 做的事情并不花哨，但它是整个模块初始化阶段的总入口：

- 遍历 `loaded_mod_list`
- 逐个调用 `dss_module_init_one()`
- 汇总 feature bitmask

从数据面主线看，这一步是一个关键分水岭：

- 在它之前，模块只是“被加载”
- 在它之后，模块才真正拥有 init 状态、TLS key、RPC 和 dRPC handler

也就是说，如果你要找“数据面什么时候开始真正认识这些服务模块”，答案基本就在 `dss_module_init_all()` 这一层。

## 第五段：`init`、`setup`、`srv_init` 不是一回事

这也是第一次读 DAOS 数据面时最容易混淆的地方。

### `init`：模块自身接入引擎

`sm_init` 和 `dss_module_init_one()` 这一层，主要解决的是：

- 模块内部初始化
- TLS key 注册
- RPC/dRPC handler 注册

你可以把它理解成：

**模块已经正式插到引擎插槽里了。**

### `setup`：整体栈起来之后做额外准备

`src/engine/README.md` 明确说，模块还可以定义：

- `setup`
- `cleanup`

在 `module.c` 里，这对应：

- `dss_module_setup_all()`
- `dss_module_cleanup_all()`

也就是说，某些模块除了基本 init 之外，还需要在“整体运行环境已经基本起来”之后再做一轮 setup。

例如：

- `obj_module` 声明了 `.sm_setup = obj_mod_setup`
- `mgmt_module` 声明了 `.sm_setup = ds_mgmt_setup`
- `mgmt_module` 还声明了 `.sm_cleanup = ds_mgmt_cleanup`

这说明 `setup` 更像“进入运行状态前的额外就绪动作”，而不是最初级的模块注册。

### `dss_srv_init()`：不是某个模块的 init，而是整个服务栈初始化

在 `init.c` 里，`dss_module_init_all()` 之后紧接着就是：

- `dss_srv_init()`

然后数据面继续：

- 通知控制面 ready
- 等待进入 `DSS_INIT_STATE_SET_UP`
- 注册 event callback
- 打开 xstreams barrier

从整体节奏看，`dss_srv_init()` 更像把“已经接入引擎的模块集合”推进到整个服务栈层面的启动阶段，而不是单个模块的初始化重复。

所以第一次读数据面时，最好把这三层关系分清：

- `load`：把库装进来
- `init`：把模块接入引擎
- `setup/srv_init`：把整个服务栈推进到可工作状态

## 第六段：两个真实模块样本说明了什么

如果只看抽象接口，可能还是有点空。最好的办法是看两个真实模块定义。

### `obj_module`：典型业务服务模块

`src/object/srv_mod.c` 里的 `obj_module` 定义里能看到：

- `sm_name = "obj"`
- `sm_mod_id = DAOS_OBJ_MODULE`
- `sm_init = obj_mod_init`
- `sm_fini = obj_mod_fini`
- `sm_setup = obj_mod_setup`
- `sm_proto_count = 2`
- `sm_proto_fmt = {...}`
- `sm_handlers = {...}`
- `sm_key = &obj_module_key`

这几乎把“业务服务模块需要接入引擎的所有典型元素”都展示出来了：

- 自己的生命周期回调
- 自己的 RPC 协议
- 自己的 per-xstream TLS key

也就是说，对象服务不是一个“单独跑的 object server”，而是以 `obj_module` 的形式被并入 `daos_engine`。

### `mgmt_module`：同时带 dRPC handler 的管理模块

`src/mgmt/srv.c` 里的 `mgmt_module` 则更能说明本地控制面协作这一层：

- `sm_name = "mgmt"`
- `sm_mod_id = DAOS_MGMT_MODULE`
- `sm_init = ds_mgmt_init`
- `sm_fini = ds_mgmt_fini`
- `sm_setup = ds_mgmt_setup`
- `sm_cleanup = ds_mgmt_cleanup`
- `sm_proto_count = 2`
- `sm_handlers = {...}`
- `sm_drpc_handlers = mgmt_drpc_handlers`

这个定义特别能说明：

- 某些模块不仅参与普通数据面 RPC
- 还承担控制面与数据面之间的本地 dRPC 协作

所以当你在控制面文章里看到“必要时请求会经 dRPC 转发到数据面 mgmt 模块”时，这里就是数据面侧真正接住这件事的落点之一。

## 第七段：TLS、RPC、dRPC 为什么都放在模块初始化阶段注册

这个设计其实非常合理。

### 1. 让模块能力声明集中在 `dss_module` 描述里

如果 TLS key、RPC handlers、dRPC handlers 分散在各个模块的各种隐蔽初始化路径里，数据面会很难统一管理。

而现在 DAOS 的做法是：

- 模块在 `dss_module` 里声明自己有哪些接入点
- 引擎在统一的初始化阶段完成注册

这样引擎和模块之间的边界就很清晰：

- 模块声明
- 引擎注册

### 2. 让故障回滚更可控

`dss_module_init_one()` 的错误处理路径也很值得注意。

如果：

- RPC 注册失败
- dRPC 注册失败

它会按顺序回滚：

- 注销已注册 RPC
- 注销 key
- 调用 `sm_fini()`
- 从已加载链表中摘除并关闭动态库

这说明把注册动作集中在一个地方的另一个好处是：失败时可以统一收尾。

### 3. 让模块真正成为“插拔式能力单元”

从工程视角看，TLS、RPC、dRPC 都在模块初始化时完成注册，意味着：

- 引擎负责运行时框架
- 模块负责能力声明和自身逻辑

这使得新增模块时不需要到处补“额外注册逻辑”，而是围绕 `dss_module` 这一套约定接入即可。

## 第八段：从控制面视角回看这条链路

把这条数据面启动主线和上一篇 `daos_server` 的启动链路放在一起看，会更容易理解两者的协作关系。

控制面做的是：

- 建立 gRPC 服务
- 建立统一 dRPC server
- 组装 engine harness
- 拉起 `daos_engine`

数据面做的是：

- 初始化自己的并发、网络和公共运行时
- 按模块列表 `dlopen` 各模块
- 把模块的 TLS / RPC / dRPC 能力接入引擎
- 初始化服务栈并在 ready 后通知控制面

所以双平面的配合关系可以压缩成一句话：

**控制面负责把 engine 拉起来并管理它，数据面负责把多个服务模块组织成真正可运行的存储执行栈。**

## 把整条主线压缩成一张脑图

如果要记忆 `daos_engine` 的初始化主线，我建议记下面这条：

1. 初始化 dRPC、Argobots、模块接口、网络层和公共运行时。
2. 通过 `modules_load()` 和 `dss_module_load()` 把各服务模块动态库装进来。
3. 通过 `dss_module_init_all()` 和 `dss_module_init_one()` 调模块 `sm_init()`，并注册 TLS key、RPC handlers、dRPC handlers。
4. 继续执行 `dss_srv_init()`，并把服务栈推进到 ready 状态。
5. 通知控制面数据面已就绪，然后开放 xstreams，真正进入可服务状态。

这条链路里最容易记错的地方只有一个：

**`load` 不等于 `init`，`init` 也不等于 `setup/服务就绪`。**

只要把这三个阶段分清，`daos_engine` 的初始化脉络就会顺很多。

## 小结

`daos_engine` 的启动主线，真正揭示了 DAOS 数据面的组织方式：

- 它不是一个把所有逻辑直接塞进主程序的单体服务。
- 它是一套带有并发、网络、TLS、RPC、dRPC 运行时框架的执行引擎。
- 各类服务模块通过 `dss_module` 描述声明自己的生命周期、协议和局部状态需求，再由引擎统一接入。

因此，理解 `daos_engine` 最重要的不是背下模块列表，而是明白这条关系：

**动态库装载 -> 模块初始化 -> TLS/RPC/dRPC 注册 -> 服务栈就绪**

一旦把这条主线看清楚，后面无论你去读 `pool`、`cont`、`obj`、`mgmt` 还是 `rebuild`，都会更容易判断自己看到的代码到底属于：

- 模块声明
- 模块接入
- 服务逻辑
- 还是引擎运行时本身

## 下一篇看什么

理解完控制面的 `daos_server` 和数据面的 `daos_engine` 之后，最自然的下一步就是把双平面与协议层正式接起来：

**gRPC、dRPC 与 CART：DAOS 为什么同时维护三条通信通路**

因为到这一步，我们已经知道控制面和数据面分别怎么启动了，接下来就该回答它们之间、以及它们与客户端之间到底通过什么通信机制协作。
