# 一条 `vllm serve` 命令，如何拉起整套推理系统

## 这篇要回答什么问题

很多人使用 vLLM 时，最熟悉的一条命令就是：

```bash
vllm serve <model>
```

它看起来很简单，像是“启动一个 API 服务”这么直接。

但如果你已经读过前一篇关于 V1 多进程架构的文章，就会立刻意识到一个问题：

> 既然 vLLM V1 里至少有 API Server、Engine Core、GPU Worker，有时还会出现多个 API Server 和 `DP Coordinator`，那么这一条命令到底是怎么把这些进程一步步拉起来的？

这篇文章真正要解释的，不是“`vllm serve` 会启动服务”这种表层事实，而是下面这条启动链路：

1. CLI 参数先如何被解析
2. 哪些参数会决定启动分支
3. 这些参数如何被收敛成 `VllmConfig`
4. `VllmConfig` 又如何继续变成最终的服务进程树

如果把上一篇文章回答的问题概括成“**系统启动后谁在负责什么**”，那这篇文章回答的问题就是“**系统是怎么被启动成这个样子的**”。

## 如果不了解这个模块，后面会在哪些地方读不下去

如果不先把 `vllm serve` 的启动链路理顺，后面读服务层和运行时源码时，通常会在几个地方卡住：

- 看到 `serve.py` 里同时有 `run_server`、`run_headless`、`run_multi_api_server`、`run_dp_supervisor`，但不知道这些分支各自对应什么部署模式。
- 看到 `api_server_count` 被自动改写，会不知道它到底是用户显式指定的，还是系统根据 DP / LB 模式推导出来的。
- 看到 `AsyncEngineArgs.from_cli_args(args)` 和 `create_engine_config()`，会觉得它们只是“把参数搬一下”，看不出这里其实在做大量默认值推导和一致性修正。
- 看到 `launch_core_engines()`、`CoreEngineProcManager`、`DPCoordinator`，会不知道它们和 `serve` 命令的关系到底有多近。
- 看到 headless、internal LB、external LB、hybrid LB、multi-port external LB 这些模式时，会感觉概念很多，但脑子里没有一张统一的启动决策图。

这些困惑的根源其实是一样的：

**`vllm serve` 并不是“简单调用一下 API server main 函数”，而是一个完整的启动编排器。**

## 先给一张总图：从命令到进程树

先用一句话概括启动链路：

> `vllm serve` 先在 CLI 层根据 headless、DP、LB 模式选择启动分支，再把参数收敛成 `VllmConfig`，最后由 V1 运行时根据配置创建 Engine、Coordinator、API Server 等实际进程。

把整个链路画成一张图，大致是这样：

```mermaid
flowchart TB
    A[`vllm serve ...`] --> B[`serve.py` 解析与分支]
    B --> C[`AsyncEngineArgs.from_cli_args(args)`]
    C --> D[`create_engine_config()`]
    D --> E[`VllmConfig`]

    E --> F{启动分支}
    F -->|单 API Server| G[`run_server(args)`]
    F -->|多 API Server| H[`run_multi_api_server(args)`]
    F -->|headless| I[`run_headless(args)`]
    F -->|multi-port external LB| J[`run_dp_supervisor(args)`]

    H --> K[`launch_core_engines()`]
    I --> K
    G --> K
    J --> L[多个子 `run_server()` 进程]

    K --> M[`CoreEngineProcManager`]
    K --> N[`DPCoordinator` 可选]
    M --> O[Engine Core 进程]
    O --> P[GPU Worker 进程]
    H --> Q[多个 API Server 子进程]
    G --> R[当前进程内 API Server]
```

这张图最重要的不是函数名，而是三层递进关系：

- 第一层：`serve.py` 决定启动哪条路。
- 第二层：`arg_utils.py` 决定这些 CLI 参数最终变成什么配置。
- 第三层：`v1/engine/utils.py` 和相关管理器把配置落成真实进程。

也就是说，`vllm serve` 的本质不是“直接启动服务”，而是：

**先做模式判定，再做配置收敛，最后做进程编排。**

## 第一步：`serve.py` 先决定要走哪条启动分支

`vllm/entrypoints/cli/serve.py` 是整个启动链路的总入口。它做的第一件事，不是创建模型，也不是启动 HTTP 服务，而是判断这次启动到底属于哪一种模式。

### 1. 先处理一些最高优先级分支

`ServeSubcommand.cmd()` 里最先做的几件事，优先级其实很高：

- 如果命令行位置参数里给了 `model_tag`，它会覆盖 `args.model`
- 如果设置了 `--grpc`，就直接走 gRPC server 分支
- 如果设置了 `--headless`，则强制 `api_server_count = 0`

这一步的意义非常直接：

- gRPC 和 OpenAI API server 是两个不同入口，不应该混在一起走
- headless 模式意味着根本不启动 API Server，所以 `api_server_count` 必须被压成 0

也就是说，从这里开始，`serve.py` 已经不是一个“普通 main 函数”了，而是一个带模式意识的分发器。

### 2. 然后判断 DP 负载均衡模式

接下来是这一篇最值得注意的一段逻辑：`serve.py` 会识别当前是否处于以下几种 DP/LB 模式之一：

- `external LB`
- `hybrid LB`
- `multi-port external LB`

源码中的判断方式大致可以概括成：

- `external LB`：`--data-parallel-external-lb` 或显式给了 `--data-parallel-rank`
- `hybrid LB`：`--data-parallel-hybrid-lb`，或者在未显式指定时，通过 `--data-parallel-start-rank` 推断
- `multi-port external LB`：`--data-parallel-multi-port-external-lb`

并且三者互斥，不能同时出现。

这一段逻辑非常重要，因为后面 `api_server_count` 的默认值推导、是否需要 `DP Supervisor`、前后端是否 many-to-many 连接，都会被这里的模式判断影响。

### 3. `api_server_count` 的默认值就在这里被推出来

这也是很多人第一次读时最容易忽略，但实际上非常关键的一步。

如果用户没有显式指定 `--api-server-count`，`serve.py` 会根据模式自动填默认值：

- `multi-port external LB`：默认是 `1`
- Rust frontend：默认是 `1`
- `external LB`：默认是 `1`
- `hybrid LB`：默认是 `data_parallel_size_local`
- `internal LB`：默认是 `data_parallel_size`
- `headless`：前面已经强制成 `0`

可以把这段规则浓缩成一张表：

| 场景 | 默认 `api_server_count` | 背后的含义 |
| - | - | - |
| `headless` | `0` | 根本不启动 API Server |
| 单机普通服务 | `1` | 一个前端入口就够 |
| `internal LB` + DP | `data_parallel_size` | 前端和所有 DP rank 内部协作 |
| `hybrid LB` | `data_parallel_size_local` | 仅在本地 DP ranks 内做内部均衡 |
| `external LB` | `1` | 外部 LB 已经负责分流 |
| `multi-port external LB` | `1` | 当前 supervisor 只负责每个本地 rank 起一个子服务 |

这里最值得记住的不是具体数字，而是一个设计原则：

> `api_server_count` 不是“多起几个前端试试看”，而是服务拓扑的一部分。

它反映的是：这次启动准备让多少前端进程去连接多少后端 engine。

## 第二步：真正的启动分支只有四条

在 `ServeSubcommand.cmd()` 里，所有判断最后会收敛到四个主分支：

1. `run_dp_supervisor(args)`
2. `run_headless(args)`
3. `run_multi_api_server(args)`
4. `uvloop.run(run_server(args))`

也就是说，`vllm serve` 的启动世界，最终并不混乱，只有四条主路。

### 分支 1：`multi-port external LB` 走 `run_dp_supervisor`

如果用户开启了 `--data-parallel-multi-port-external-lb`，`serve.py` 不会直接自己起一个 API Server，而是转入 `dp_supervisor.py`。

这个模式的核心思路是：

- 当前进程扮演 supervisor
- 它会为每个本地 DP rank 派生一个子服务进程
- 每个子服务监听不同端口
- 每个子服务内部都是一个独立的 `run_server(child_args)`

也就是说，这不是“一个 API Server 连多个 engine”，而更像是：

**一个 supervisor 管多个本地 vLLM 子实例，每个实例各自占一组本地设备并暴露自己的端口。**

这个模式尤其适合把“对外负载均衡”彻底交给外部系统，比如：

- k8s service / ingress
- L4 / L7 负载均衡器
- 机房内部已有的统一流量入口

### 分支 2：`headless` 走 `run_headless`

headless 模式最容易理解：没有 API Server，只起后端引擎。

在 `run_headless()` 里，代码会：

1. 从 CLI 参数构造 `AsyncEngineArgs`
2. 调用 `create_engine_config(..., headless=True)`
3. 根据 `parallel_config` 决定本地需要拉起多少个 engine
4. 通过 `CoreEngineProcManager` 启动本地 Engine Core 进程

这个分支的关键特征是：

- 没有 HTTP 入口
- 没有 API Server 子进程
- 重点是把 Engine Core / Worker 这套后端运行时拉起来

所以 headless 模式不是“精简版 serve”，而是：

**只启动运行时后端，把服务入口留给别的系统。**

### 分支 3：多 API Server 走 `run_multi_api_server`

当 `api_server_count > 1`，或者启用了 Rust frontend，`serve.py` 会进入 `run_multi_api_server()`。

这个分支和单 API Server 最大的不同是：

- 当前进程不再直接自己跑 FastAPI server
- 当前进程变成一个 supervisor / manager
- 它会先拉起 engine，再起多个 API Server 子进程

在这个分支里，关键步骤大致是：

1. `setup_server(args)` 先准备监听地址和 socket
2. `AsyncEngineArgs.from_cli_args(args)` 把参数转为 engine args
3. `create_engine_config()` 生成 `VllmConfig`
4. `get_engine_zmq_addresses()` 分配前后端通信地址
5. `launch_core_engines()` 拉起 engine 和可选 coordinator
6. `APIServerProcessManager` 再拉起多个 API Server 子进程

这说明当前进程本质上在做一件事：

**替一组 API Server 和一组 Engine Core 做统一编排。**

### 分支 4：单 API Server 直接 `run_server(args)`

最后一种就是最常见的单服务入口模式。

当：

- 不是 `headless`
- 不是 `multi-port external LB`
- 不是多 API Server

那么 `serve.py` 就直接执行：

```python
uvloop.run(run_server(args))
```

这说明当前进程自己就成为那个唯一的 API Server。

这一点很容易和上一篇的“多进程架构”产生误解，好像既然 V1 是多进程，那为什么这里看起来只有一个 `run_server`？

答案是：

- “单 API Server” 只说明前端入口只有一个
- 不代表系统后面没有 Engine Core / GPU Worker
- 只是当前进程直接承担了 API Server 角色，而不是再额外 fork 一批 API 子进程

所以单 API Server 不是“单进程模式”，而是：

**单前端进程模式。**

## 第三步：CLI 参数不是直接拿来用，而是先收敛成 `AsyncEngineArgs`

读到这里，最自然的问题是：既然 `serve.py` 最终都要创建 engine，那它是不是直接拿 `argparse.Namespace` 去创建后端对象？

不是。

在 V1 启动链路里，CLI 参数先会被收敛到 `AsyncEngineArgs`。

### `from_cli_args()` 做的事看似简单，但意义很大

`AsyncEngineArgs.from_cli_args(args)` 的实现表面上很简单：它只是把 `Namespace` 中和 dataclass 字段同名的部分抽出来，构造成一个 `AsyncEngineArgs`。

但这一步的意义非常重要：

- CLI 层的参数命名方式
- engine 层真正关心的字段集合
- 后续配置收敛的入口对象

从这里开始，就被统一到了一个类型化对象里。

也就是说，从这一步开始，系统不再把“命令行参数”当作一堆零散 flag，而是开始把它们当成“待收敛的 engine 输入”。

### `create_model_config()` 是第一次真正进入模型语义

在 `arg_utils.py` 里，`create_model_config()` 会把大量模型相关 flag 组装成一个 `ModelConfig`。

这一步开始引入的已经不是 CLI 层概念，而是运行时概念，例如：

- 模型路径
- tokenizer
- dtype
- quantization
- max model len
- multimodal 处理相关设置
- LoRA / generation / renderer 等模型侧能力

也就是说，到了这里，系统开始从“命令参数”进入“模型运行语义”。

## 第四步：`create_engine_config()` 才是配置收敛的真正中心

如果说 `serve.py` 是启动分支决策中心，那么 `arg_utils.py` 里的 `create_engine_config()` 就是配置收敛中心。

它做的事，远远不只是“new 一个 `VllmConfig`”。

### 1. 它先创建 `ModelConfig`

`create_engine_config()` 里最先做的重要事情之一，就是调用 `create_model_config()`。

然后它会基于模型信息进一步决定：

- 默认 chunked prefill / prefix caching 参数
- 推理模式的默认值
- `kv_cache_dtype` 的实际解析结果
- 一些和模型结构相关的能力开关

也就是说，很多后续配置并不是纯粹靠 CLI 决定，而是：

**CLI 参数 + 模型属性 一起推导出来的。**

### 2. 它会把 DP / LB 相关参数修正成真正可运行的并行配置

这是第 3 篇最值得重点理解的一段逻辑。

`create_engine_config()` 里会处理很多和数据并行部署方式相关的推导，例如：

- `headless` 模式下禁止 `hybrid LB`
- `hybrid LB` 和 `external LB` 不能同时开启
- 多节点场景下如何推导 `data_parallel_rank`
- `data_parallel_size_local` 没给时如何默认
- `external LB` 下 `data_parallel_size_local` 必须收敛为 `1`
- `data_parallel_start_rank` 在非 headless 场景下可触发 `hybrid LB`

这一整段逻辑其实就是在做一件事：

**把用户输入的分布式意图，修正成一组内部自洽的并行配置。**

你也可以把它理解成：

- CLI 说的是“我想怎么部署”
- `create_engine_config()` 负责把它翻译成“系统实际上要怎么组织自己”

### 3. 它真正组装出 `ParallelConfig`

在所有推导做完后，`create_engine_config()` 会显式创建 `ParallelConfig`，把这些关键字段全部塞进去：

- `pipeline_parallel_size`
- `tensor_parallel_size`
- `data_parallel_size`
- `data_parallel_rank`
- `data_parallel_external_lb`
- `data_parallel_size_local`
- `data_parallel_hybrid_lb`
- `_api_process_count`
- `_api_process_rank`

这里最关键的一点是：

`api_server_count` 并不会只停留在 CLI 层，而是继续下沉进入并行配置语义。

这也解释了为什么前端进程数量不是一个“纯服务层参数”，而会影响后端运行时的拓扑认知。

### 4. 它继续组装出 `SchedulerConfig`、`CacheConfig`、`LoadConfig` 等等

除了 `ParallelConfig` 之外，`create_engine_config()` 还会依次构建：

- `CacheConfig`
- `SchedulerConfig`
- `LoadConfig`
- `AttentionConfig`
- `LoRAConfig`
- `ObservabilityConfig`
- `CompilationConfig`
- `OffloadConfig`

最后再统一塞进 `VllmConfig(...)`。

所以从工程角度看，`create_engine_config()` 的作用非常像一个总装工厂：

- 前面是分散的 CLI flag
- 中间是各类子配置对象
- 最后是一个统一的 `VllmConfig`

## 一张图看懂：CLI 参数如何收敛到 `VllmConfig`

这一段很适合用流程图来记忆：

```mermaid
flowchart TB
    A[`argparse.Namespace`] --> B[`AsyncEngineArgs.from_cli_args()`]
    B --> C[`create_model_config()`]
    B --> D[`create_engine_config()`]
    C --> D

    D --> E[`CacheConfig`]
    D --> F[`ParallelConfig`]
    D --> G[`SchedulerConfig`]
    D --> H[`LoadConfig`]
    D --> I[`AttentionConfig`]
    D --> J[`LoRAConfig`]
    D --> K[`CompilationConfig`]
    D --> L[`ObservabilityConfig`]

    E --> M[`VllmConfig`]
    F --> M
    G --> M
    H --> M
    I --> M
    J --> M
    K --> M
    L --> M
```

这张图的核心信息只有一句：

**`vllm serve` 不是“参数直接喂给 engine”，而是先收敛为统一配置对象，再由运行时读取。**

## 第五步：`VllmConfig` 还会再做一次自检和修正

很多人以为 `create_engine_config()` 返回 `VllmConfig` 之后，配置收敛就结束了。

其实还没有。

`VllmConfig.__post_init__()` 里还会继续做一轮“校验 + 修正”。

### 1. 它会校验配置之间是否彼此兼容

比如：

- `model_config` 和 `parallel_config` 是否匹配
- 某些能力是否和 PP / KV connector / speculative decoding 冲突
- 某些默认值是否需要被自动关闭或自动开启

这意味着 `VllmConfig` 并不是一个“被动的参数容器”，而是：

**一个带一致性约束的运行时配置对象。**

### 2. 它会决定是否需要 `DP Coordinator`

这也是理解后续进程树时非常关键的一步。

`VllmConfig` 上有一个 `needs_dp_coordinator` 属性，逻辑大致是：

- 当 `DP > 1` 且模型是 MoE 时，需要 coordinator
- 当 `DP > 1` 且不是 external LB 时，也需要 coordinator

换句话说：

- 对非 MoE 模型，coordinator 更多是为了内部 / hybrid LB 的统计与协调
- 对 MoE 模型，哪怕 external LB 场景下也可能还需要它来处理 wave coordination

这说明 `DP Coordinator` 不是由 `serve.py` 直接“拍脑袋决定起不起”，而是已经下沉成了配置层语义。

## 第六步：`launch_core_engines()` 才把配置真正变成进程

当代码进入 `run_multi_api_server()` 或单 API server 的后端拉起阶段后，真正把 `VllmConfig` 变成进程树的关键函数，就是：

`vllm/v1/engine/utils.py` 里的 `launch_core_engines()`

### 1. 它先决定要不要起 `DPCoordinator`

`launch_core_engines()` 首先会根据：

- `vllm_config.needs_dp_coordinator`
- 是否 online 模式
- 当前 `dp_rank` 是否为 0

来决定是否启动 `DPCoordinator`。

这一步和上一篇架构文里的结论是对上的：

- 协调器不是总有
- 它是 DP 模式下按需出现的协调进程

### 2. 它再决定要和哪些 Engine Core 做握手

这一步特别能体现 vLLM 在不同 DP/LB 模式下的启动差异。

`launch_core_engines()` 会根据：

- 是否 offline / headless
- 当前 `dp_rank`
- `data_parallel_size_local`
- `local_engines_only`

来决定本进程这次需要等待哪些 Engine Core 完成握手。

这背后的含义是：

- 不同 rank 负责的启动管理边界不同
- 有些场景下 rank 0 需要掌握全局引擎的握手
- 有些场景下非 0 rank 只需要管理本地 engine

所以 vLLM 的启动不是“无脑 fork 一堆进程”，而是带有明确控制边界的。

### 3. `CoreEngineProcManager` 负责真正拉起 Engine Core 子进程

在确定好本地应该管理多少 engine 后，`launch_core_engines()` 会通过 `CoreEngineProcManager` 去创建本地 Engine Core 进程。

这个 manager 的职责很清晰：

- 根据本地 DP rank 范围创建进程
- 启动这些进程
- 监控它们是否异常退出
- 在失败时统一 shutdown

所以从职责划分上说：

- `serve.py` 负责顶层模式选择
- `launch_core_engines()` 负责后端进程编排
- `CoreEngineProcManager` 负责具体 Engine Core 进程生命周期

### 4. Engine Core 再继续往下拉 Worker

一旦 Engine Core 进程自己启动起来，后面的事情就进入上一篇文章已经讲过的运行时层面：

- Engine Core 创建 executor
- executor（如 `MultiprocExecutor`）拉起 worker
- worker 绑定 GPU 并完成模型执行侧初始化

也就是说，从启动角度看：

`vllm serve` 并不会直接“创建 GPU Worker”，而是：

**先创建 Engine Core，再由 Engine Core 继续拉起执行层。**

## 单 API、Multi API、Headless：三种主路径到底差在哪

到这里，其实可以把最容易混淆的三种路径放到一张表里。

| 启动模式 | 当前进程扮演什么 | 是否启动 API Server | 是否启动 Engine Core | 典型使用场景 |
| - | - | - | - | - |
| 单 API Server | 当前进程自己跑 API Server | 是，1 个 | 是 | 普通单入口服务 |
| Multi API Server | 当前进程做 supervisor | 是，多个子进程 | 是 | DP/internal LB/hybrid LB 等前端多进程模式 |
| Headless | 当前进程做后端管理器 | 否 | 是 | 只起后端，不暴露 HTTP |

这里有两个特别容易混淆的点。

第一，单 API Server 不等于单进程。

它只是说 API 前端只有一个。后面仍然可以有 Engine Core 和 Worker。

第二，Multi API Server 不等于“后端更多”。

它只是说前端被复制成多个进程，真正后端有多少 engine，仍然主要由 DP / TP / PP 配置决定。

## 一张启动分支决策图

这篇文章最适合记住的，就是下面这张图：

```mermaid
flowchart TB
    A[`vllm serve`] --> B{`--grpc`?}
    B -->|是| G[gRPC 分支]
    B -->|否| C{`--headless`?}

    C -->|是| H[`api_server_count = 0`]
    H --> I[`run_headless()`]

    C -->|否| D{`--data-parallel-multi-port-external-lb`?}
    D -->|是| J[`run_dp_supervisor()`]
    D -->|否| E{`api_server_count > 1` 或 Rust frontend?}

    E -->|是| K[`run_multi_api_server()`]
    E -->|否| F[单 API Server]
    F --> L[`run_server(args)`]
```

如果你把这张图记住，后面再看 `serve.py` 就不会感觉“分支很多”，而会知道它其实只是：

- 先排除特殊模式
- 再决定是否有 API Server
- 最后决定是单前端、多个前端，还是 supervisor 管多个子实例

## 最后用一次完整启动过程回到全局

现在可以把“一条 `vllm serve` 命令”重新走一遍。

### 第 1 步：CLI 层先识别部署意图

用户输入：

```bash
vllm serve <model> [flags]
```

`serve.py` 首先识别：

- 这是不是 gRPC
- 这是不是 headless
- 是否启用了 DP
- DP 是 internal / external / hybrid / multi-port external LB 哪种模式
- `api_server_count` 是用户显式指定，还是系统要自动推导

### 第 2 步：参数进入 `AsyncEngineArgs`

接着 CLI 参数会被收敛进 `AsyncEngineArgs`，从“命令行 flag”变成“engine 侧输入对象”。

### 第 3 步：`create_engine_config()` 生成统一配置

这一层会：

- 创建 `ModelConfig`
- 推导 `ParallelConfig`
- 组装 `CacheConfig`、`SchedulerConfig`、`LoadConfig` 等子配置
- 最终得到 `VllmConfig`

从这里开始，系统已经不再关心“你是用什么参数名传进来的”，而只关心“最终配置是什么”。

### 第 4 步：`VllmConfig` 自检并推导运行时条件

这一步会继续确认：

- 配置是否自洽
- 某些能力是否兼容
- 是否需要 `DP Coordinator`
- 一些默认开关是否需要自动调整

### 第 5 步：运行时按配置拉起进程

最后，启动分支会进入：

- `run_server`
- `run_multi_api_server`
- `run_headless`
- 或 `run_dp_supervisor`

再由：

- `launch_core_engines()`
- `CoreEngineProcManager`
- `APIServerProcessManager`
- `DPCoordinator`

把 `VllmConfig` 落成最终的进程树。

### 第 6 步：Engine Core 再继续把系统补全

Engine Core 启动后，会继续创建 executor，并由 executor 拉起 GPU Worker。

于是，一开始那条看起来很简单的 `vllm serve` 命令，最终才真的变成：

- API Server
- Engine Core
- GPU Worker
- 以及按需出现的 `DP Coordinator`

这也就是为什么说，`vllm serve` 本质上不是一个“服务启动命令”，而是一套完整的系统启动器。

## 这篇文章之后，最值得继续读什么

如果你已经理解了“命令是怎么变成进程树的”，下一步最值得继续读的就是服务层本身：

1. `vllm/entrypoints/openai/api_server.py`
2. `vllm/entrypoints/openai/engine/serving.py`
3. `vllm/v1/engine/core.py`

按这个顺序读，会形成很自然的链路：

- 先看服务层真正干了什么
- 再看它如何把请求翻译给 engine client
- 最后回到 Engine Core 里看控制中枢如何接管请求

这样你对 vLLM 的理解就会从“怎么启动系统”，自然过渡到“系统启动后怎么处理请求”。

## 一句话总结

不要把 `vllm serve` 理解成一句普通的“启动 HTTP 服务”命令。

更准确地说，它在回答的是这样一个问题：

> 当部署模式可能是单机、DP、headless、external LB、hybrid LB、多前端进程甚至 multi-port supervisor 时，系统该如何把一堆 CLI 参数稳定地收敛成一套真实可运行的进程拓扑？

vLLM 给出的答案是：

- 用 `serve.py` 做模式分支
- 用 `arg_utils.py` 做配置收敛
- 用 `VllmConfig` 做统一运行时表达
- 再用 engine / API manager 把配置变成真实进程树

所以理解这条命令的关键，不是记住它调用了哪些函数，而是看清：

**`vllm serve` 是怎样把“用户输入的部署意图”，逐步翻译成“系统内部的运行时拓扑”的。**
