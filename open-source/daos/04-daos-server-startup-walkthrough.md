# 04. `daos_server` 启动时做了什么：控制面主进程剖析

## 为什么要先看 `daos_server`

前面几篇我们已经建立了整体地图：

- DAOS 是一套围绕分布式新型存储介质设计的对象存储软件栈。
- 它采用控制面和数据面的双平面架构。
- 第一次读源码，应该先认公共层、入口层，再进入服务层和底层存储层。

现在就该真正进入第一条启动主线了。

而在所有启动入口里，`daos_server` 是最值得先看的那个。原因很简单：

- 它是控制面主进程。
- 它负责解析配置、准备环境、管理本机数据面生命周期。
- 它是管理命令、节点编排、gRPC 服务和 engine 启动链路的汇合点。

如果把 DAOS 节点看成一套会动起来的系统，那 `daos_server` 基本就是这套系统的“总开关”。

所以这一篇的目标不是解释某个局部函数，而是沿着真实调用顺序回答一个问题：

**从 `main()` 开始，`daos_server` 到底做了什么，才把一个控制面节点带起来？**

## 先看结论：`daos_server` 启动不是“直接拉起 engine”

很多人第一次接触 DAOS，会以为 `daos_server` 的启动逻辑大概就是：

1. 读配置
2. 启动 `daos_engine`
3. 开始服务

但实际并不是这么简单。

从 `src/control/cmd/daos_server/main.go`、`start.go` 以及 `src/control/server/server.go` 可以看出，真正的主线更接近下面这个顺序：

1. 建立命令行和日志环境。
2. 解析子命令和全局参数。
3. 对非纯信息类命令执行 helper 预检查。
4. 初始化网络/SCM/NVMe 等命令上下文。
5. 加载配置文件，并叠加 CLI 覆盖项。
6. 执行 `start` 子命令。
7. 在 `start` 中配置日志并调用 `server.Start()`。
8. 在 `server.Start()` 里完成 fabric 扫描、配置加工、server 对象创建、gRPC 与 dRPC 准备、engine harness 组装。
9. 最终由 harness 启动并管理一个或多个 `daos_engine` 实例。

也就是说，`daos_server` 的启动核心不是“直接 fork 一个 C 进程”，而是先把控制面自己搭成一个可管理、可通信、可编排的环境，然后才把数据面纳入这个环境。

## 第一站：`main()` 做的事其实很克制

### `main()` 不是业务大总管

`src/control/cmd/daos_server/main.go` 里的 `main()` 很短，但这并不意味着它不重要。恰恰相反，它把控制面启动风格表达得非常清楚：

- 创建命令行日志器。
- 组装 `mainOpts`。
- 注入一组 pre-exec tests。
- 注入网络、NVMe、SCM 初始化 helper。
- 把实际解析和执行交给 `parseOpts()`。

这说明 `main()` 本身并不试图承载所有业务逻辑，而是更像一个启动编排入口。它负责把“要用哪些能力”准备好，然后把执行交给统一命令框架。

### `mainOpts` 已经暴露了控制面的职责范围

只看 `mainOpts` 里定义的子命令，其实就能看出 `daos_server` 的职责边界：

- `scm`
- `nvme`
- `start`
- `network`
- `version`
- `ms`
- `dump-topology`
- `support`
- `config`

这几个子命令背后的关键词非常典型：

- 存储
- 网络
- 管理服务
- 支持和拓扑
- 启动

也就是说，`daos_server` 不是一个只负责“start”的窄进程，而是一个承载控制面管理能力的总入口。`start` 只是其中最重要的一条路径。

## 第二站：`parseOpts()` 是真正的启动分发器

如果说 `main()` 只是入口，那么 `parseOpts()` 才是 `daos_server` 启动流程真正的分发中心。

它做的事情可以分成几层。

### 1. 命令行解析和 JSON/日志模式准备

`parseOpts()` 先创建 `go-flags` parser，然后在 `CommandHandler` 里处理被选中的具体子命令。

最开始的一批工作主要是：

- 拒绝意外的位置参数。
- 如果用户要求 JSON 输出，则对支持 JSON 的命令启用 JSON 输出模式。
- 根据 `--debug`、`--json-logging`、`--syslog` 等参数调整日志行为。

这一步的意义在于：控制面在真正开始干活之前，先统一了“输出长什么样”和“日志怎么记”。

### 2. 对大多数命令做 pre-exec tests

`parseOpts()` 最值得注意的一段，是它并不会对所有命令一视同仁。

例如 `version` 这类命令，会直接执行，不需要额外准备。

而其他命令则会先跑 `opts.preExecTests`。

在 `main()` 中，这组测试当前至少包含一件很关键的事：

- `pbin.CheckHelper(log, pbin.DaosPrivHelperName)`

也就是说，控制面在真正执行大多数命令前，会先确认特权 helper 是否已经正确安装、可调用、版本匹配。

### 3. 做命令级初始化注入

`parseOpts()` 会检查被执行的命令是否实现了对应的初始化接口，例如：

- `initWith(initNetworkCmdFn)`
- `initWith(initScmCmdFn)`
- `initWith(initNvmeCmdFn)`

这说明命令初始化不是散落在每个子命令里的，而是由统一入口在执行前注入依赖。

从架构角度看，这一步很漂亮，因为它让：

- 命令定义
- 初始化能力
- 配置加载

这几件事在框架层被串起来了，而不是耦死在 `start` 子命令本身。

### 4. 加载配置并叠加 CLI 覆盖项

这是 `parseOpts()` 最关键的一步之一。

它会判断命令是否实现了 `cfgLoader`，如果实现了，就执行：

- `loadConfig(log)`
- 如有需要，执行 `setCLIOverrides()`

这意味着控制面的真实执行顺序不是“只看配置文件”，也不是“只看命令行参数”，而是：

1. 先构造默认配置对象。
2. 再找配置文件并加载。
3. 最后把 CLI 显式指定的内容覆盖到配置上。

这一点非常符合控制面软件的工程风格：以配置文件为主，以命令行为覆盖层。

## 第三站：为什么启动前一定要检查 helper

这一步非常值得单独拎出来讲，因为它体现了 `daos_server` 和普通业务服务最不一样的地方。

### `CheckHelper()` 实际在验证什么

`src/control/pbin/pbin.go` 里的 `CheckHelper()` 做的事情并不复杂，但很有代表性：

1. 创建一个指向 helper 的 forwarder。
2. 发送 `Ping` 请求。
3. 根据返回结果区分：
   - fault
   - 文件不存在或权限不足
   - 其他请求失败
4. 最后检查 helper 返回的版本是否和当前 DAOS 版本一致。

换句话说，它不是只判断“二进制在不在”，而是在判断：

- helper 能不能被真正调用
- 权限是否正确
- 请求链路是否通
- 版本是否兼容

### 为什么这一步要前置

`pbin.go` 里的注释写得很直接：这一步是为了主动识别安装或配置问题，避免控制台后面刷出一串重复错误。

这其实很符合控制面角色。因为 `daos_server` 后续要做的很多事都不是纯用户态逻辑，它要和系统资源、设备和特权操作打交道。与其等到启动半截才报出一堆底层错误，不如一开始先做一次明确的可用性探测。

从读源码的角度，这一步也给我们一个重要认识：

**`daos_server` 的启动不是单纯的应用启动，而是带有系统管理前置校验的启动。**

## 第四站：配置是怎么被加载进来的

`src/control/cmd/daos_server/config.go` 把配置加载这件事拆得很清楚。

### 默认配置对象先被创建

`cfgCmd.loadConfig()` 里先调用 `config.DefaultServer()` 创建默认配置对象。

这说明 DAOS 控制面不是“必须全量写配置文件才能启动”的模型，而是有一套默认配置结构作为基底。

### 然后查找配置文件

如果命令行没有显式指定 `-o/--config`，它会通过 `build.FindConfigFilePath(defaultConfigFile)` 去查找默认配置文件，也就是 `daos_server.yml`。

如果：

- 配置是可选的，就允许找不到；
- 配置不是可选的，就直接报错。

这个机制让同一套配置加载基础设施既能服务“必须有配置”的启动命令，也能服务“配置可选”的扫描类命令。

### 最后真正执行配置加载

配置路径确定后，会执行：

- `c.config.SetPath(c.ConfigPath)`
- `c.config.Load(log)`

到这一步，YAML 里的 server 配置才真正进入内存对象。

### CLI 覆盖项如何生效

`start.go` 中的 `setCLIOverrides()` 又在配置文件之上叠加了一层显式覆盖，例如：

- `--insecure`
- `--port`
- `--storage`
- `--group`
- `--socket_dir`
- `--modules`
- `--targets`
- `--xshelpernr`
- `--firstcore`

并且这些覆盖不仅作用于控制面本身，也会下沉到各个 engine 配置上，例如 target 数量、helper xstream 数量、service thread core 等。

因此，真正的启动配置来源可以总结成：

**默认值 -> 配置文件 -> CLI 覆盖**

这是理解 `daos_server` 启动行为最重要的一条规则之一。

## 第五站：`startCmd.Execute()` 做了什么

等到 `parseOpts()` 完成预检、初始化和配置加载之后，才会轮到 `startCmd.Execute()`。

它的逻辑很短，但含义非常明确。

### 1. 确认启动实现

如果 `cmd.start` 为空，就默认指向 `server.Start`。

这说明 `startCmd` 本身更像一个命令壳，它把真正的启动行为委托给 `server` 包。

### 2. 配置日志

`configureLogging()` 会先检查每个 engine 是否指定了日志文件，然后按控制面配置设置 logger：

- `ControlLogFile`
- `ControlLogMask`
- `ControlLogJSON`

这一步的重要性在于：控制面在进入真正的 server 启动前，先把自己的日志体系切到配置定义的目标状态。

### 3. 处理 `AutoFormat`

`Execute()` 会把 `--auto-format` 显式写回 `cmd.config.AutoFormat`。

这说明 `AutoFormat` 虽然来自 CLI，但最终仍然作为 server 配置的一部分往下传递。

### 4. 调用 `server.Start()`

最后，`startCmd.Execute()` 调用：

- `cmd.start(cmd.Logger, cmd.config)`

从这里开始，逻辑就正式从命令行层切到控制面 server 层。

## 第六站：`server.Start()` 才是真正的启动主线

如果说前面的部分是在“准备启动”，那么 `src/control/server/server.go` 里的 `Start()` 才是真正把控制面节点带起来的主线。

它大致可以拆成下面几步。

### 1. 防止重复进程

`Start()` 一上来先调用 `common.CheckDupeProcess()`。

这一步很好理解：控制面主进程不应该在同一环境里无意间重复拉起。

### 2. 建立根上下文

随后创建 root context，并在退出时统一 `shutdown()`。

这意味着后面的：

- fabric 等待
- gRPC 服务
- dRPC 服务
- harness 启动

都会被纳入同一个生命周期控制框架。

### 3. 等待 fabric 准备好并做网络扫描

接着 `Start()` 会：

- 读取配置中的 fabric providers
- `waitFabricReady()`
- 调用 `network.DefaultFabricScanner(log).Scan(ctx, providers...)`

这一步说明 `daos_server` 的启动不是“配置读完就直接干”，而是要先确认数据路径依赖的网络环境已经准备好。

从系统角度看，这一步非常关键，因为数据面最终依赖的就是 fabric 通信能力。

### 4. 读取系统内存信息并加工配置

然后它会：

- 获取系统内存信息 `common.GetSysMemInfo()`
- 调用 `processConfig(...)`

这一步的意义是把“原始配置”加工成“真正适合当前节点资源和 fabric 结果的运行配置”。

也就是说，配置文件不是最终态，中间还要经过一次结合本机环境的信息补全和校验。

### 5. 计算 fault domain 并创建 server 对象

接下来 `Start()` 会：

- `getFaultDomain(cfg)`
- `newServer(log, cfg, faultDomain)`

从这里开始，一个真正的控制面 server 实例被构造出来。

这个时刻非常重要，因为后续的：

- 网络初始化
- 服务创建
- engine 实例装配
- gRPC/dRPC 服务

都要挂到这个 server 对象上。

### 6. 设置 core dump filter，识别 fabric 网卡类别

再往后它会继续做一些节点级准备工作：

- `srv.setCoreDumpFilter()`
- `getFabricNetDevClass(cfg, fis)`

这些动作虽然不像 `Start()`、`Scan()` 那样显眼，但它们正体现了控制面的工程职责：不仅要启动服务，还要把节点运行环境调到适合 DAOS 的状态。

### 7. 创建控制面服务、初始化网络、组装 engine

这是 `Start()` 中最关键的一段：

- `srv.createServices(ctx)`
- `srv.initNetwork()`
- `srv.addEngines(ctx, smi)`

这三步分别意味着：

- 控制面自身的服务组件被建立起来。
- 节点级网络相关初始化被完成。
- 数据面实例被加入 harness 管理体系。

注意这里仍然不是“马上运行 engine”，而是先把管理这些 engine 的框架搭好。

### 8. 建立 gRPC 服务与事件机制

然后 `Start()` 会执行：

- `srv.setupGrpc()`
- `srv.registerEvents()`

这说明在数据面真正开始工作前，控制面先把自己的对外通信能力和事件机制准备好。

也就是说，`daos_server` 不是“先跑 engine，再补控制接口”，而是要先把控制面作为控制面立起来。

### 9. 进入 `srv.start()`

最后 `Start()` 通过：

- `return srv.start(ctx)`

把控制权交给下一段真正开始监听和拉起 harness 的逻辑。

## 第七站：`srv.start()` 如何让控制面真正“活起来”

`srv.start()` 是启动链路里最像“点火”动作的一段。

### 1. gRPC server 开始监听

它先在 goroutine 中执行：

- `srv.grpcServer.Serve(srv.listener)`

这意味着控制面主进程正式开始在 gRPC 地址上接受管理请求。

### 2. 启动 pprof 和输出启动日志

接着会：

- 启动调试相关能力 `control.StartPProf(srv.log)`
- 输出监听地址和版本信息

这些动作说明此时控制面已经进入一个“可以被连接、可以被观察”的状态。

### 3. 建立单一 dRPC server

然后是特别关键的一步：

- 构造 `drpcServerSetupReq`
- 调用 `drpcServerSetup(ctx, drpcSetupReq)`

代码里的注释写得很明确：

**Single daos_server dRPC server to handle all engine requests**

这意味着：

- 本机上的 engine 并不是各自拥有完全独立的控制面入口。
- 控制面会建立一个统一的 dRPC server 来处理来自 engine 的请求与交互。

这正好对应了前面我们讲的双平面协作模型：控制面与数据面之间通过 Unix Domain Socket 上的 dRPC 做本机通信。

### 4. 启动管理服务异步循环

`srv.mgmtSvc.startAsyncLoops(ctx)` 会把管理服务相关的后台异步循环跑起来。

这说明控制面不仅是“接收 RPC 请求”，它自身还带有持续运行的管理逻辑。

### 5. 如有需要，先做 AutoFormat

如果配置里启用了 `AutoFormat`，`srv.start()` 会直接调用：

- `srv.ctlSvc.StorageFormat(ctx, &ctlpb.StorageFormatReq{})`

这一步很有代表性，因为它再次说明：

- 控制面并不只是进程外壳。
- 它本身就具备节点级存储准备和格式化能力。

换句话说，控制面启动链路里已经把“存储是否可进入 engine 启动阶段”这件事考虑进来了。

### 6. 最后交给 harness 启动 engine 实例

最终，`srv.start()` 会执行：

- `srv.harness.Start(ctx, srv.sysdb, srv.cfg)`

这一步就是整个启动流程里真正把数据面拉起来的地方。

注意这里的主语不是 `server` 直接去粗暴 fork 某个进程，而是：

- 通过 engine harness 来统一启动和管理 engine 实例

这和 `src/control/server/README.md` 的描述完全对得上：

- engine instances 由 `daos_server` fork
- engine harness 负责管理和监控这些实例

所以从架构上看，`daos_server` 启动的最终落点不是“启动一个子进程”这么简单，而是“启动一整套受控制面治理的 engine 运行时”。

## 把整条链路串起来

如果把前面的步骤压缩成一条真正可记住的主线，可以写成这样：

1. `main()` 创建日志器和顶层选项，把初始化 helper 与 pre-exec tests 注入进去。
2. `parseOpts()` 解析命令，处理 JSON/日志模式，执行 helper 检查，完成配置加载和 CLI 覆盖。
3. `startCmd.Execute()` 配置控制面日志，处理 `AutoFormat`，调用 `server.Start()`。
4. `server.Start()` 等待 fabric ready、扫描网络、读取系统资源、加工配置、创建 server 对象、初始化服务和 engine harness。
5. `srv.start()` 启动 gRPC server、建立统一 dRPC server、启动管理循环、必要时执行格式化，然后由 harness 拉起并管理 `daos_engine` 实例。

这条链路非常适合记忆，因为它揭示了控制面启动的三个层次：

- 命令层：解析和预检查
- server 层：构建控制面运行环境
- harness 层：把数据面纳入控制面治理

## 这条启动链路最值得记住的三个点

### 1. 启动的核心不是“执行 start 命令”，而是“构建控制面环境”

很多系统把启动理解成“启动服务线程”或者“拉起工作进程”，但 `daos_server` 更像是在先搭平台，再让数据面上台。

### 2. helper、配置、fabric、gRPC、dRPC 都是启动前置条件

这些步骤并不是边角料，而是控制面存在的理由本身：

- helper 保证特权操作路径可用
- 配置决定控制面和数据面如何组织
- fabric 决定数据路径网络前提
- gRPC 提供对外管理入口
- dRPC 提供本机控制面和数据面协作入口

### 3. `daos_engine` 是被 harness 管理起来的，不是随手拉起的子进程

这点特别重要。因为它决定了后面你去读控制面 server 包时，应该关注的是：

- engine instance abstraction
- engine harness
- dRPC server
- membership 和 management service

而不是把 `daos_engine` 仅仅看成 `exec()` 出去的一个 C 程序。

## 小结

`daos_server` 的启动链路，本质上是在完成这样一件事：

**先把控制面自己建设成一个具备配置能力、预检能力、网络感知能力、gRPC 管理能力和 dRPC 协作能力的节点管理中心，再由它去启动和托管数据面。**

所以这条链路最有价值的地方，不是它“调用了哪些函数”，而是它把 DAOS 控制面的角色说明白了：

- 它不是只会转发命令的薄壳。
- 它不是只会拉起子进程的 supervisor。
- 它是节点级控制、服务注册、资源准备和数据面托管的统一入口。

## 下一篇看什么

理解了 `daos_server` 的启动主线后，下一步最自然的衔接就是进入数据面：

**`daos_engine` 如何启动并装载模块：数据面初始化主线**

因为现在我们已经知道控制面是怎么把舞台搭起来的，接下来就该看数据面是如何真正登场并把 `vos`、`pool`、`cont`、`obj`、`rebuild` 这些模块装进去的。
