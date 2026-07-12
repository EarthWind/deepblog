# SGLang 服务启动主链路：从 `launch_server.py` 到不同后端分流

上一篇我们从 `sglang serve` 和 `sglang generate` 进入了 SGLang 的 CLI 世界，也知道了 `serve` 并不会直接“启动一个固定的 HTTP 服务”，而是会先识别模型类型，再把 LLM 场景交给 `prepare_server_args()` 和 `run_server()`。

这篇文章就接着往下走，专门回答一个很多人第一次读 SGLang 时都会关心的问题：

当我们执行 `sglang serve --model-path ...` 之后，服务到底是怎样一路启动起来的？它又是怎样根据参数切到 HTTP、gRPC、Ray、encoder-only 等不同模式的？

如果只看 `python/sglang/launch_server.py`，你会觉得这个文件短得有点过分，似乎只是几个 `if/elif`。但真正有意思的地方恰恰在这里：SGLang 把“服务启动”分成了两层。

- 第一层是入口分流：决定当前进程要走哪一种服务形态
- 第二层是运行时装配：决定 HTTP 主路径背后要拉起哪些进程、哪些管理器，以及选择哪些内核后端

也就是说，`launch_server.py` 不是一个大而全的启动脚本，而是整个服务启动主链路里的第一个总分发器。

## 文章定位

- 目标读者：已经读过 CLI 入口，准备顺着服务启动主链路继续往下看的工程师
- 阅读前提：知道 `sglang serve` 会进入 `prepare_server_args()` 和 `run_server()`，但还不清楚后续分支如何展开
- 核心问题：SGLang 服务启动时，协议层、部署层和运行时后端层分别在哪些位置发生分流
- 阅读收益：读完后，你应该能把 `launch_server.py`、`http_server.py`、`Engine`、`ServerArgs` 放进同一条启动链里

## 重点代码路径

- `python/sglang/launch_server.py`
- `python/sglang/srt/server_args.py`
- `python/sglang/srt/entrypoints/http_server.py`
- `python/sglang/srt/entrypoints/engine.py`
- `python/sglang/srt/entrypoints/grpc_server.py`
- `python/sglang/srt/ray/http_server.py`
- `python/sglang/srt/model_executor/model_runner.py`
- `python/sglang/srt/layers/sampler.py`
- `python/sglang/srt/constrained/grammar_manager.py`

## 先把主链路记住

如果先不展开细节，SGLang 在 LLM 服务场景下的主链路可以先记成下面这张图：

```text
sglang serve
-> cli.serve:serve()
-> srt.server_args:prepare_server_args()
-> launch_server:run_server()
-> HTTP / gRPC / Ray / encoder-only

默认 HTTP 主路径继续展开为：

run_server()
-> http_server.launch_server()
-> Engine._launch_subprocesses()
-> TokenizerManager + Scheduler + DetokenizerManager
-> _setup_and_run_http_server()
-> FastAPI / uvicorn(or granian)
```

但这条图里还藏着一个更深的分流层：

```text
prepare_server_args()
-> ServerArgs.from_cli_args(...)
-> __post_init__ / check / compatibility handlers
-> sampling backend / attention backend / grammar backend 的最终选择
```

所以，SGLang 的“后端分流”至少包含三种不同含义：

- 服务入口走哪种协议或部署形态
- HTTP 主路径里拉起哪些运行时子进程
- 模型执行阶段最终选用哪些 attention、sampling、grammar backend

理解这一点之后，再看 `launch_server.py` 就不会误以为它只是在做表面分支。

## `launch_server.py`：短小，但它是第一层总分发器

`python/sglang/launch_server.py` 的核心只有一个函数：`run_server(server_args)`。

它的逻辑可以概括成下面这个伪代码：

```python
if encoder_only:
    if grpc_mode:
        serve_grpc_encoder(server_args)
    else:
        encode_server.launch_server(server_args)
elif grpc_mode:
    serve_grpc(server_args)
elif use_ray:
    ray.http_server.launch_server(server_args)
else:
    http_server.launch_server(server_args)
```

这段代码很值得仔细看，因为它说明 SGLang 在入口层首先回答的不是“怎样把 HTTP 服务跑起来”，而是“当前到底要启动哪一种服务形态”。

### 分支一：`encoder_only`

当 `server_args.encoder_only` 为真时，系统不会进入标准文本生成服务主路径，而是切到 encoder disaggregation 相关实现。

这里又分成两种情况：

- `encoder_only + grpc_mode`：走 `srt.disaggregation.encode_grpc_server`
- `encoder_only + 非 grpc_mode`：走 `srt.disaggregation.encode_server`

这条分支说明，SGLang 从入口层就把“编码器服务”视为独立部署形态，而不是标准生成服务里的一个小功能开关。

### 分支二：`grpc_mode`

如果不是 encoder-only，但显式开启了 `grpc_mode`，系统会进入 `srt.entrypoints.grpc_server`。

当前这个路径在代码注释里已经被标注为相对“legacy”的独立路径，因为默认主路径里未来会把原生 Rust gRPC 能力和 HTTP 一起启动。但就当前实现而言，这仍然是一条明确独立的服务模式。

它的重要意义在于：SGLang 并没有把服务协议绑死在 HTTP/FastAPI 上，gRPC 依然是一等入口。

### 分支三：`use_ray`

如果开启 `use_ray`，则不会走普通 HTTP 启动逻辑，而是进入 `srt.ray.http_server`。

要注意，这条分支并不是“换了一个 API 层”，而是“HTTP 层基本保持一致，但调度和进程拉起方式换成了 Ray 风格”。

也就是说，Ray 分支主要改变的是运行时部署拓扑，而不是用户看到的服务协议。

### 分支四：默认 HTTP

如果前面几个条件都不满足，系统才进入最常见的默认路径：

- `srt.entrypoints.http_server.launch_server(server_args)`

这条路径是理解 SGLang 服务启动主干最重要的一条。后面你会看到，真正的 `TokenizerManager -> Scheduler -> DetokenizerManager` 协作，就是从这里开始装配起来的。

## 为什么 `prepare_server_args()` 是启动链路里的关键节点

在上一篇里我们已经见过 `prepare_server_args(argv)`，但到了这一篇，需要把它的重要性再提高一个层级。

很多项目的参数解析函数只负责把命令行参数塞进一个对象里，而 SGLang 的 `prepare_server_args()` 明显做得更多。

它大致分成四步：

1. 创建 `argparse.ArgumentParser(prog="sglang serve")`
2. 通过 `ServerArgs.add_cli_args(parser)` 注册完整服务参数
3. 如果传入了 `--config`，就先把配置文件和命令行参数合并
4. 调用 `ServerArgs.from_cli_args(raw_args)` 得到真正的 `server_args`

最关键的是最后一步。因为 `ServerArgs.from_cli_args(...)` 并不是简单构造数据类，它会进一步触发一系列兼容性处理、默认值填充和后端选择逻辑。

换句话说，命令行里的“原始参数”到这里才第一次被整理成“可以真实驱动运行时”的最终配置。

## `ServerArgs` 不只是参数容器，它还负责第一次后端决策

如果你翻 `python/sglang/srt/server_args.py`，会看到在参数对象初始化之后，SGLang 会依次执行一串处理函数。里面最值得关注的是这几项：

- `_handle_sampling_backend()`
- `_handle_deterministic_inference()`
- `_handle_attention_backend_compatibility()`
- `_handle_grammar_backend()`

这意味着什么？

意味着“后端分流”并不完全发生在 `launch_server.py` 那几个分支里。更深一层的运行时后端选择，其实已经在 `ServerArgs` 阶段开始了。

### `sampling backend`：先决定采样走哪套实现

`_handle_sampling_backend()` 的逻辑很直接：

- 如果用户没有显式指定 `sampling_backend`
- 那么优先使用 `flashinfer`
- 如果 `flashinfer` 不可用，则回退到 `pytorch`

后面在 `srt/layers/sampler.py` 中，`create_sampler()` 会根据这个结果决定最终采样实现。

这说明采样后端并不是模型执行时才临时猜出来的，而是在服务启动阶段就完成了第一次选择。

### `grammar backend`：结构化约束也有独立后端

`_handle_grammar_backend()` 同样很清晰：

- 如果用户没有显式指定，就默认使用 `xgrammar`

而调度器启动后，`GrammarManager` 会继续根据这个字段创建真正的 grammar backend，比如：

- `xgrammar`
- `outlines`
- `llguidance`
- `none`

这对理解 SGLang 很重要，因为它说明“结构化输出约束”并不是散落在某个 API 层的小功能，而是被当成运行时后端能力来管理。

### `attention backend`：最复杂的自动决策发生在这里

相比之下，`attention backend` 的决策要复杂得多。

`_handle_attention_backend_compatibility()` 会结合模型结构、平台能力和若干特性开关，自动决定最终应该使用什么 attention backend。这里面至少会考虑：

- 当前硬件平台是不是 Hopper、Blackwell、HIP、MPS
- 模型是不是 MLA 架构
- 是否启用了 speculative decoding
- `flashinfer` 是否可用
- 是否存在 attention sinks

最终候选后端可能包括：

- `fa3`
- `trtllm_mha`
- `flashinfer`
- `aiter`
- `torch_native`
- `triton`

也就是说，服务刚启动时看起来只是解析了一堆参数，但实际上系统已经在为后续 `ModelRunner` 选择注意力内核实现了。

## 所以，`run_server()` 之前已经决定了一半事情

把前面两节放在一起看，会发现启动链路里有一个很重要的认知转折：

- `prepare_server_args()` 决定的是“运行时该怎样被配置”
- `run_server()` 决定的是“当前进程该进入哪一种服务模式”

这两个步骤一前一后，刚好把服务启动拆成了：

1. 配置归一化与后端预决策
2. 入口级运行模式分流

这样的分层非常合理。因为如果先不把参数归一化，后面很多分支都没法可靠判断；而如果没有入口级分流，所有模式都塞进一个启动函数里，代码很快就会失控。

## 默认 HTTP 路径为什么是理解主干的最好入口

虽然 SGLang 支持多种服务模式，但如果你的目标是理解“标准推理服务到底怎样跑起来”，默认 HTTP 路径仍然是最值得优先阅读的主干。

原因很简单：

- 它是最常见的部署方式
- 它完整暴露了 SRT 运行时的核心协作关系
- Ray 分支本质上复用了它的大部分 HTTP 逻辑
- 很多后续高级特性最终也都是挂在这条主干上

`http_server.launch_server()` 的文档字符串把这件事讲得非常清楚：SRT Server 由两部分组成。

- HTTP server：负责对外暴露 FastAPI 接口
- SRT engine：负责真正的请求处理与模型执行

而 engine 又由三个核心组件组成：

1. `TokenizerManager`
2. `Scheduler`
3. `DetokenizerManager`

这段说明几乎可以当作整条主路径的总纲。

## `http_server.launch_server()`：HTTP 层和 Engine 在这里接上

`http_server.launch_server()` 本身做的事情其实不算多，但每一步都很关键。

它首先调用：

- `Engine._launch_subprocesses(...)`

拿回下面这些对象：

- `tokenizer_manager`
- `template_manager`
- `port_args`
- `scheduler_init_result`
- `subprocess_watchdog`

然后再调用：

- `_setup_and_run_http_server(...)`

把 FastAPI 服务真正跑起来。

这个结构非常有意思，因为它明确把“运行时装配”和“HTTP 对外服务”拆开了。

换句话说，HTTP 层并不是直接在请求处理函数里自己管理所有模型执行逻辑，而是先把引擎搭好，再把请求路由到引擎上。

这也是为什么 SGLang 的服务层不会显得像一个“FastAPI 包模型”的轻量 demo。真正复杂的部分被收敛到了 `Engine` 及其子进程体系里。

## `Engine._launch_subprocesses()`：运行时主链路真正开始的地方

如果说 `launch_server.py` 是服务入口的总分发器，那么 `Engine._launch_subprocesses()` 才是默认 HTTP 路径真正进入运行时内部的起点。

这个函数做的事情，可以按顺序概括成下面几步：

1. 配置日志、环境变量、插件和 GC
2. 检查 `server_args`
3. 分配 IPC 和通信端口
4. 拉起 scheduler 相关进程
5. 根据需要拉起 detokenizer 进程或 router
6. 初始化 tokenizer manager 或 multi-tokenizer router
7. 等待模型加载完成
8. 启动子进程 watchdog

这里最值得展开说的是其中几层分流。

### 调度器分流：`dp_size == 1` 还是 `dp_size > 1`

在 `_launch_scheduler_processes()` 里，SGLang 会先判断数据并行规模。

如果 `dp_size == 1`：

- 系统会按 tensor parallel / pipeline parallel 维度直接拉起 scheduler 子进程

如果 `dp_size > 1`：

- 系统不会直接把所有 scheduler 平铺启动
- 而是先拉起 `data parallel controller`

这说明在 SGLang 里，“调度器启动”本身也是有拓扑差异的。并不是所有部署场景都共享同一套进程组织方式。

### 多节点分流：`node_rank >= 1`

如果当前是多节点部署中的非 0 号节点，`Engine._launch_subprocesses()` 会走一个很特别的分支：

- 不再初始化本地 tokenizer 和 detokenizer
- 只等待 scheduler 就绪
- 然后阻塞等待调度相关进程完成

这背后的设计很清晰：在多节点场景下，并不是每个节点都需要承担完整的“对外服务 + 文本处理 + 调度”职责。

也就是说，SGLang 的运行时拓扑从一开始就允许“有些节点主要负责执行，有些组件只在特定节点存在”。

### detokenizer 分流：单进程还是多 worker

detokenizer 侧同样不是固定结构。

如果 `detokenizer_worker_num <= 1`：

- 直接拉起一个 detokenizer 进程

如果 `detokenizer_worker_num > 1`：

- 每个 detokenizer worker 都拥有自己的 IPC
- 再额外拉起一个 `MultiDetokenizerRouter`

这说明 SGLang 在文本输出这层也做了可扩展设计，而不是默认假设 detokenization 永远足够轻量。

### tokenizer 分流：单 tokenizer 还是 router

tokenizer 侧也有类似处理。

如果 `tokenizer_worker_num == 1`：

- 直接初始化 `TokenizerManager`

如果 `tokenizer_worker_num > 1`：

- 改用 `MultiTokenizerRouter`

这一步尤其重要，因为它说明 SGLang 的入口请求处理层本身就支持横向扩展，而不是等请求打进模型之后才考虑并发问题。

## 这三个组件为什么是 SRT 的主干

在 HTTP 默认路径下，SGLang 的核心协作关系可以简化为：

```text
HTTP 请求
-> TokenizerManager
-> Scheduler
-> DetokenizerManager
-> HTTP 响应
```

这三个组件的职责边界非常清楚。

### `TokenizerManager`

它负责接收来自上层请求的数据，做 tokenization，并把请求送给 scheduler。

### `Scheduler`

它是运行时真正的核心。批处理调度、执行推进、与模型执行器的衔接，都在这里发生。

### `DetokenizerManager`

它负责把模型输出的 token 重新转回文本，并把结果传回上层请求路径。

这个结构的意义在于：SGLang 的主干并不是“HTTP handler 直接调模型”，而是一条显式拆分过的运行时流水线。

这也正是它后续能承接 continuous batching、prefix caching、并行策略、结构化约束等复杂能力的基础。

## gRPC、Ray、encoder-only 到底分别改变了什么

现在可以回过头来，把 `run_server()` 的几个分支放在一起比较。

### 默认 HTTP：最完整的标准路径

默认 HTTP 模式的特点是：

- 外部接口层是 FastAPI
- 内部通过 `Engine._launch_subprocesses()` 组织 SRT 运行时
- 最终由 `uvicorn` 或 `granian` 启动 HTTP 服务

这是理解 SGLang 主干实现最重要的一条路径。

### gRPC：换协议，但不是简单删掉 HTTP 能力

`grpc_server.py` 里的实现很有意思。它并不是完全脱离 HTTP 生态，而是会配一个轻量 HTTP sidecar，用来暴露：

- `/metrics`
- `/start_profile`
- `/stop_profile`

这说明协议切换不等于把所有 HTTP 能力都扔掉。即使主请求通道走 gRPC，运维和观测层仍然可能保留 HTTP 端点。

从工程视角看，这是一种很实用的设计：把“业务请求协议”和“管理面/观测面接口”区分开。

### Ray：换的是调度和部署拓扑

`srt.ray.http_server.launch_server()` 的结构和普通 HTTP 版非常相似，最大的差异是它把：

- `Engine._launch_subprocesses()`

替换成了：

- `RayEngine._launch_subprocesses()`

这说明 Ray 模式的核心变化在于：

- scheduler 侧不再使用普通 `multiprocessing.Process` 作为唯一组织方式
- 而是让 Ray actor 参与运行时调度拓扑

也因此，Ray 模式更像是在“复用 HTTP 外壳的同时，更换内部执行部署方式”。

### encoder-only：服务对象已经不是标准生成链路

当系统走到 `encoder_only` 分支时，整个服务的职责已经从标准文本生成主路径切到了编码相关能力。

从代码组织上看，这条路径单独放在 `disaggregation/` 下面，本身就说明它并不是普通 HTTP 生成服务里的一个可选细节，而是围绕 EPD 场景单独组织出来的一类服务。

这也能解释为什么 `run_server()` 会优先检查 `encoder_only`。对启动入口来说，这已经不是“协议差异”，而是“服务角色差异”。

## “不同后端分流”不只发生在协议层

到这里，已经可以看出一个容易被忽略的事实：

很多人说“不同后端分流”，第一反应是 HTTP、gRPC、Ray 这些入口形态；但在 SGLang 里，真正影响执行路径的还有更深一层的运行时后端。

这层后端主要在 `ModelRunner`、`Sampler` 和 `GrammarManager` 里落地。

## `ModelRunner`：attention backend 的最终实例化点

前面我们说过，`ServerArgs` 已经会提前决定 attention backend 的候选结果。但真正把这个结果落成运行时对象的，是 `model_runner.py`。

在 `ModelRunner.init_attention_backend()` 里，SGLang 会根据最终配置决定：

- 使用普通单一 attention backend
- 使用 `HybridAttnBackend`
- 使用 two-batch-overlap 对应的 backend
- 在 `enable_pdmux` 场景下初始化一组 decode backend

这里最值得注意的是 `HybridAttnBackend` 逻辑。

如果 prefill 和 decode 的 backend 不同，SGLang 不会强迫两者统一，而是显式创建一个 hybrid backend，让两阶段分别使用不同实现。

这说明 attention backend 的“分流”已经细化到：

- prefill 走什么
- decode 走什么
- 两者是否需要组合

这远远不是一个简单的 `--attention-backend=xxx` 能概括的事情。

## `Sampler`：采样后端也不是纯粹的细枝末节

在 `srt/layers/sampler.py` 里，`create_sampler()` 会根据 `sampling_backend` 选择真正的采样实现。

默认情况下：

- `flashinfer` 可用时优先用它
- 否则回退到 `pytorch`

虽然这层不像 attention backend 那么复杂，但它依然说明 SGLang 的很多运行时能力都采用了“统一接口 + 多后端实现”的思路。

也正因为如此，`prepare_server_args()` 才必须在服务真正拉起之前就把这些后端决策做掉。

## `GrammarManager`：结构化输出约束也是运行时后端

如果只从 API 视角理解结构化输出，很容易把它想成“解析器”或者“协议层工具”。但从代码结构看，SGLang 明显不是这么设计的。

调度器初始化时会创建 `GrammarManager`，而 `GrammarManager` 会继续根据 `grammar_backend` 去创建真正的 grammar backend。

这层可以对应到不同实现，比如：

- `xgrammar`
- `outlines`
- `llguidance`
- `none`

这意味着结构化输出约束并不是贴在 OpenAI 兼容接口表面的一个能力，而是深入到了调度和执行主链路中。

## 把整条链再串一遍

到这里，可以把这一篇最重要的启动主链路重新整理成一条更完整的路径：

```text
sglang serve --model-path <model>
-> cli.serve:serve()
-> prepare_server_args(argv)
-> ServerArgs.from_cli_args(...)
-> 归一化配置并决定 sampling / attention / grammar backend
-> run_server(server_args)
-> 根据 encoder_only / grpc_mode / use_ray 分流

默认 HTTP 路径：
-> http_server.launch_server()
-> Engine._launch_subprocesses()
-> 启动 scheduler / detokenizer / tokenizer 相关组件
-> ModelRunner / Sampler / GrammarManager 各自实例化后端
-> _setup_and_run_http_server()
-> 对外提供服务
```

这条链路背后，其实对应着三个层次的问题：

- 服务入口层：当前要启动哪种服务形态
- 运行时装配层：当前需要拉起哪些组件和子进程
- 执行后端层：attention、sampling、grammar 最终分别用哪套实现

所以，更准确地说，SGLang 的服务启动并不是“一次分流”，而是一连串逐层收敛的决策过程。

## 为什么 SGLang 要把多入口并存当成默认设计

从工程视角看，这种设计至少带来了三点收益。

### 1. 入口稳定，内部可扩展

用户仍然可以从统一的 `sglang serve` 进入系统，但内部可以继续扩展协议、部署形态和运行时后端，而不需要频繁改动对外入口。

### 2. 不同变化被隔离在不同层

- HTTP、gRPC、Ray 的差异主要留在入口和部署层
- 调度器、tokenizer、detokenizer 的差异留在 Engine 装配层
- attention、sampling、grammar 的差异留在运行时后端层

这让系统的复杂度虽然高，但不是混在一起的。

### 3. 更适合真实生产环境

一个真实的推理系统，不会永远只有单一协议、单一部署方式、单一内核实现。SGLang 从启动链路一开始就把这些变化留出了空间，这也是它能逐步扩展到更复杂场景的基础。

## 总结

`launch_server.py` 表面上很短，但它在 SGLang 里的位置非常关键。它不是服务启动的全部，却是整个启动主链路里第一个明确的总分发器。

顺着这条链路往下看，可以得到一个很重要的理解框架：

- `prepare_server_args()` 先把命令行参数整理成真正可执行的运行时配置
- `run_server()` 再决定服务要走 HTTP、gRPC、Ray 还是 encoder-only
- 默认 HTTP 路径会进一步进入 `http_server.launch_server()`
- `Engine._launch_subprocesses()` 负责把 `TokenizerManager`、`Scheduler`、`DetokenizerManager` 真正装配起来
- 更深一层的 attention、sampling、grammar backend 则在运行时组件中继续实例化

所以，SGLang 的服务启动主链路，本质上是一套“入口分流 + 运行时装配 + 执行后端选择”的分层启动过程，而不是一个单一 server 函数把所有事情做完。

理解了这条主链路，后面再去看 HTTP 服务层、OpenAI 兼容接口、Scheduler 和 ModelRunner，就会容易得多。因为你已经知道它们分别处在整套系统的哪一层。

下一篇，就可以顺着默认 HTTP 主路径继续深入：看看 SGLang 的 OpenAI 兼容接口和 HTTP 服务层，到底是怎样搭起来的。
