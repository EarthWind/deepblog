# SRT 核心协作机制：`TokenizerManager`、`Scheduler`、`ModelRunner` 怎样串起来

上一篇我们从 `lang/api.py` 往下看，理解了 SGLang 怎样把“生成式应用开发”抽象成一套前端语言。

但如果再继续顺着请求往下追，一个更偏运行时的问题就会出现：

一个已经被转换成内部请求对象的生成任务，进入 SRT 之后，究竟是怎样被分词、排队、组批、执行，再把结果一点点吐回上层接口的？

这个问题的答案，基本都藏在三类核心对象里：

- `TokenizerManager`
- `Scheduler`
- `ModelRunner`

它们分别站在请求生命周期的不同位置：

- `TokenizerManager` 负责接住上层请求，把原始输入变成运行时能消费的 tokenized request，并维护请求状态
- `Scheduler` 负责接住这些请求，决定谁先跑、怎样组 batch、什么时候 prefill、什么时候 decode
- `ModelRunner` 负责真正把 batch 送进模型做 forward，并完成 logits 处理与采样

如果只看类名，很容易把这三者理解成“分词器管理器、调度器、模型执行器”这样松散的组件列表。但 SGLang 的实现并不是几个模块平铺摆放，而是一条非常明确的执行通路。

更准确地说，SRT 的核心链路可以先概括成下面这样：

```text
Engine / HTTP handler
-> TokenizerManager.generate_request()
-> _tokenize_one_request()
-> _send_one_request()
-> Scheduler.handle_generate_request()
-> get_next_batch_to_run()
-> run_batch()
-> TpModelWorker.forward_batch_generation()
-> ModelRunner.forward()
-> ModelRunner.sample()
-> Scheduler.process_batch_result()
-> DetokenizerManager
-> TokenizerManager.handle_loop()
-> 上层流式/非流式返回
```

这篇文章就沿着这条链路，拆开看 SGLang Runtime 是怎样协作起来的。

## 文章定位

- 目标读者：已经理解服务层和前端语言层，准备深入 SRT 运行时实现的工程师
- 阅读前提：知道请求最终会进入 SRT，但不清楚主进程、调度进程、执行层之间如何分工
- 核心问题：`TokenizerManager`、`Scheduler`、`ModelRunner` 各自负责什么，它们之间怎样串起来形成完整请求生命周期
- 阅读收益：读完后，你应该能把一条请求从 `Engine.generate()` 一路串到 `ModelRunner.forward()`，再串回流式输出

## 重点代码路径

- `python/sglang/srt/entrypoints/engine.py`
- `python/sglang/srt/managers/tokenizer_manager.py`
- `python/sglang/srt/managers/scheduler.py`
- `python/sglang/srt/managers/tp_worker.py`
- `python/sglang/srt/model_executor/model_runner.py`

## 先看一张总图

如果先不展开实现细节，SRT 的核心协作关系可以先记成下面这张图：

```text
主进程
  Engine
  -> TokenizerManager

Scheduler 子进程
  接收 tokenized request
  -> 排队
  -> 组 batch
  -> 调 model worker

Detokenizer 子进程
  接收输出 token
  -> 反分词
  -> 回传字符串结果

主进程
  TokenizerManager.handle_loop()
  -> 更新请求状态
  -> 流式/非流式返回给 HTTP/API 调用方
```

这里最值得先记住的不是类名，而是进程边界。

在 `engine.py` 里，`Engine` 的注释已经把这个结构写得很清楚：

- `TokenizerManager` 运行在主进程
- `Scheduler` 运行在子进程
- `DetokenizerManager` 也运行在子进程
- 进程之间主要通过基于 ZMQ 的 IPC 通信

这意味着 SGLang 的运行时不是“一个大对象内部串几个函数”那么简单，而是一套明确拆分过职责和进程边界的执行系统。

## `Engine` 不是执行核心，但它是整条链路的总装配点

虽然这篇文章的主角是 `TokenizerManager`、`Scheduler`、`ModelRunner`，但如果不先看 `Engine`，很难理解它们是怎样被串起来的。

`Engine` 在 `engine.py` 里的定位非常直接：它是 inference engine 的入口对象。

它最重要的工作不是分词、调度或 forward，而是做两件事：

1. 把运行时需要的进程和对象都拉起来
2. 把上层 API 调用收口到统一请求入口

### 启动阶段：`Engine` 先把运行时骨架搭起来

`Engine.__init__()` 在构造时会调用 `_launch_subprocesses(...)`。这一层是理解 SRT 架构最重要的起点。

它会做一系列初始化动作，包括：

- 解析 `server_args`
- 分配 IPC 端口
- 拉起 scheduler 相关子进程
- 拉起 detokenizer 相关子进程
- 初始化 `TokenizerManager`
- 建立 ZMQ socket

所以从工程角色看，`Engine` 更像一个 runtime bootstrapper，而不是直接跑模型的执行器。

这一层设计的意义很大。

因为它把“系统装配”和“请求执行”分开了：

- 系统装配由 `Engine` 负责
- 单次请求的生命周期推进则交给 `TokenizerManager`、`Scheduler` 和后续执行层

### 请求入口：`Engine.generate()` 并不自己做生成

对上层调用方来说，最常见的入口通常是 `Engine.generate()` 或 `Engine.async_generate()`。

但如果沿着代码往下看，你会发现这两个函数真正做的事情很克制：

- 接收用户传进来的文本、采样参数、stream 开关等
- 构造 `GenerateReqInput`
- 调用 `self.tokenizer_manager.generate_request(obj, request)`

这说明一件非常重要的事：

`Engine` 并不负责执行请求，它负责把请求准确地转交给运行时主链路的第一个核心环节，也就是 `TokenizerManager`。

换句话说，`Engine` 是总入口，但不是执行主角。

## `TokenizerManager`：请求生命周期的上半场总管

如果说 `Engine` 只是把请求送进系统，那么真正开始“处理一条请求”的地方，是 `TokenizerManager.generate_request()`。

从职责上看，`TokenizerManager` 至少承担了五件事：

- 规范化请求参数
- 完成分词和多模态预处理
- 建立并维护请求状态
- 把 tokenized request 发给 scheduler
- 接收回包，并把内部输出重新组织成对外结果

也就是说，`TokenizerManager` 不是单纯的“tokenizer 包装器”，而是请求生命周期前半段和返回阶段的管理中心。

## `generate_request()`：真正的统一入口

`TokenizerManager.generate_request()` 是最值得优先阅读的函数之一。

它的大体流程很清楚：

1. 启动或确保 `handle_loop` 已经存在
2. 调 `normalize_batch_and_arguments()` 规范化请求
3. 设置默认优先级
4. 初始化请求状态 `rid_to_state`
5. 做 LoRA 等附加校验与解析
6. 对单请求走 `_tokenize_one_request()`
7. 通过 `_send_one_request()` 发送给 scheduler
8. 通过 `_wait_one_response()` 等待并返回结果

这一段代码很能体现 `TokenizerManager` 的边界感。

它做的是“请求进入执行系统前的最后整理”，包括：

- 输入是否合法
- 要不要附带路由信息
- 要不要带 LoRA
- prompt token ids 是否需要保留

但它还不会决定 batch 怎么组，也不会碰真正的模型执行。这些事都被留给了后面的 `Scheduler` 和 `ModelRunner`。

### `_init_req_state()`：为什么它不只是 tokenizer 封装

`TokenizerManager` 特别容易被低估的地方，是它内部维护着一整套请求状态。

当 `generate_request()` 调用 `_init_req_state()` 时，请求就会以 `rid` 为键被挂到内部状态表里。后面无论是流式输出、最终结果、logprob、hidden states，还是请求完成后的清理，都会围绕这个状态对象展开。

这件事非常关键。

因为一旦系统支持：

- streaming
- 批处理
- 多请求并发
- 部分结果分批返回

就必须有一层专门负责把“内部批输出”重新拆回“外部单请求状态”。

而这正是 `TokenizerManager` 的核心价值之一。

## `_tokenize_one_request()`：把外部输入翻译成运行时请求

`TokenizerManager` 的另一个关键职责，是把上层请求转成 scheduler 能理解的 tokenized request。

这部分主要发生在：

- `_tokenize_texts()`
- `_tokenize_one_request()`

前者负责更底层的 tokenization 细节，后者负责把 token ids、多模态输入、采样参数、请求元数据等封装成 `TokenizedGenerateReqInput` 或相关对象。

这一步最重要的意义，不在于“做了分词”，而在于它完成了一次语义切换：

- 进入 `TokenizerManager` 之前，请求还是“文本 + 参数”的上层语义
- 离开 `_tokenize_one_request()` 之后，请求已经变成“input ids + runtime metadata”的内部语义

一旦走到这一步，请求就不再属于 HTTP 或前端 DSL 的世界，而属于 SRT 运行时。

### 多模态、cross-encoder、LoRA 等复杂性都在这里开始收口

如果只把 `TokenizerManager` 理解成“纯文本转 token”，会错过很多实现重点。

它实际上还要负责兼容很多更复杂的输入形态，比如：

- 单条文本
- 文本 batch
- cross-encoder 成对输入
- 多模态输入
- 带 LoRA 或其他运行时附加参数的请求

所以它更像“输入归一化层”，而不是一个狭义 tokenizer 包装类。

## `_send_one_request()`：把请求真正交给 `Scheduler`

完成 tokenization 之后，`TokenizerManager` 会通过 `_send_one_request()` 或 batch 版本的发送逻辑，把请求发往 `Scheduler`。

这里值得强调的是：

这个动作不是一次普通函数调用，而是一次跨进程消息传递。

也就是说，从这一刻开始，请求离开主进程，进入 scheduler 所在的执行平面。

这也是为什么 SGLang 把 `TokenizerManager` 和 `Scheduler` 明确拆开后，系统边界会清晰很多：

- 主进程更适合接住外部请求、维护状态、管理异步返回
- scheduler 子进程更适合专注做高频调度、batch 决策和执行编排

## `Scheduler`：SRT 吞吐、批处理和状态推进的真正中枢

到了 `Scheduler` 这一层，SRT 才真正进入“高性能运行时”的核心区域。

如果说 `TokenizerManager` 解决的是“怎么把请求送进系统”，那么 `Scheduler` 解决的就是：

- 谁先跑
- 哪些请求能凑成一个 batch
- 当前应该做 prefill 还是 decode
- 当前显存和 KV cache 是否允许接更多请求
- forward 之后怎样推进每个请求的状态

从职责密度看，`Scheduler` 是这条链路里最重的那个对象。

它不是一个简单队列管理器，而是整个 SRT 执行平面的控制中心。

## `run_event_loop()`：SRT 核心节拍器

理解 `Scheduler`，最好先看 `run_event_loop()` 及其后续的几种 event loop。

在默认路径下，核心循环非常清楚：

```text
recv_requests()
-> process_input_requests(...)
-> get_next_batch_to_run()
-> run_batch(batch)
-> process_batch_result(batch, result)
```

这几步几乎就是整套运行时不断重复的心跳。

在 `event_loop_normal()` 里，这个循环是串行推进的。

在 `event_loop_overlap()` 里，SGLang 进一步做了 CPU 处理和 GPU 计算的 overlap，把上一批结果处理和下一批 forward 尽量并行起来。

即使暂时不深入 overlap 细节，这里也已经能看出 SGLang 的一个核心设计取向：

它不是“收到请求就立刻同步执行”，而是把请求先纳入统一调度循环，再在循环里做 admission、batching、forward 和结果推进。

## `handle_generate_request()`：`Scheduler` 接住请求的第一站

当来自 `TokenizerManager` 的 `TokenizedGenerateReqInput` 到达 scheduler 后，首先会经过 `handle_generate_request()`。

这个函数的主要职责是把 tokenized request 转成 scheduler 内部真正使用的 `Req` 对象。

它会补齐或处理很多运行时级信息，比如：

- `rid`
- `input_ids`
- `sampling_params`
- `stream`
- `return_logprob`
- LoRA 信息
- 多模态输入
- routed expert / hidden states / indexer 等高级返回项
- disaggregation 相关字段
- session 相关语义

这一步可以理解成 `Scheduler` 世界里的“建模入口”。

在此之前，请求虽然已经 tokenized，但还是偏向“跨组件传输对象”；到了这里，请求才真正变成调度器内部的运行单元。

### `_add_request_to_queue()`：调度真正从排队开始

构造完 `Req` 之后，`Scheduler` 会调用 `_add_request_to_queue()` 把请求放进等待队列或不同模式下的对应队列。

这一步虽然看起来普通，但它其实意味着请求状态发生了本质变化：

- 之前，请求只是“已进入系统”
- 现在，请求变成“等待被调度的执行单元”

从这一步开始，请求是否能立刻跑、需要等多久、会和谁拼成 batch，都不再由请求入口决定，而是由 `Scheduler` 的调度策略决定。

## `get_next_batch_to_run()`：真正决定“下一拍跑什么”

如果只选 `Scheduler` 里一个最能体现其核心价值的函数，`get_next_batch_to_run()` 基本是最有代表性的候选。

因为它负责回答每一轮事件循环里最关键的问题：

下一拍，到底应该跑哪一批请求？

这件事在推理系统里并不简单。它至少同时受下面几类因素影响：

- waiting queue 里有哪些请求
- running batch 当前处于什么状态
- 是不是有 chunked prefill
- 当前 KV cache / token pool / request pool 还能容纳多少
- 当前是不是应该优先 prefill
- 是否已经有 decode batch 在运行
- 是否有一些超时、abort、回收逻辑需要先处理

所以，`get_next_batch_to_run()` 的本质不是“取队头”，而是一次综合调度决策。

### prefill 和 decode 为什么要分开看

阅读这一段代码时，最值得建立的认知是：SGLang 并不是把所有请求都当成同一种 batch 处理。

它非常明确地区分：

- prefill 阶段
- decode 阶段

原因很好理解。

prefill 和 decode 的资源特征并不一样：

- prefill 更像把整段输入上下文灌进模型
- decode 更像让已经在运行中的请求继续一步步往前生成

这两类阶段对显存、KV cache、时延和吞吐的影响都不同，所以 scheduler 必须分别建模。

在 `get_next_batch_to_run()` 里，SGLang 的主逻辑大致是：

1. 先处理 pending abort、timeout、状态清理
2. 尝试把上一轮 prefill 结果并进 running batch
3. 尝试构造新的 prefill batch
4. 如果没有新的 prefill，再考虑推进 running decode batch

这正是 continuous batching 风格运行时的典型特征：系统不是一批跑完再接下一批，而是在持续地把新请求接进来，同时推进老请求往前走。

### `_get_new_batch_prefill_raw()`：吞吐优化真正发生的地方

如果想继续深入吞吐层面的实现，`_get_new_batch_prefill_raw()` 非常值得看。

这里会综合考虑很多约束来挑选能进入下一批 prefill 的请求，比如：

- 队列顺序与优先级
- 资源池容量
- chunked prefill
- 某些特性模式下的额外限制
- LoRA、层级缓存等运行时条件

也就是说，SGLang 的 batching 不是“凑满就上”，而是受一套相当细粒度的调度策略控制。

这也是为什么高性能推理系统的难点通常不只是模型 kernel，而是整个调度层设计。

## `run_batch()`：从调度层走向执行层的桥

当 `Scheduler` 决定好下一批之后，真正把这批请求送去执行的函数就是 `run_batch()`。

这个函数可以看成 scheduler 和 model worker 之间的桥梁。

它会根据当前 batch 的模式，走不同执行分支。但对最常见的生成场景来说，关键调用大致是：

```text
Scheduler.run_batch()
-> model_worker.forward_batch_generation(batch)
-> ModelRunner.forward(forward_batch)
-> ModelRunner.sample(...)
```

也就是说：

- `Scheduler` 决定“跑谁”
- `run_batch()` 决定“怎么把这批人送进去”
- 真正的模型 forward 则由更底层执行器完成

### `run_batch()` 本身也不只是简单转发

和前面的层一样，`run_batch()` 也不只是把 batch 原样传下去。

它还要处理很多执行期细节，比如：

- overlap 模式下的 stream 协调
- speculative decoding 相关分支
- future map 或 cache 更新
- embedding / generation 不同类型 batch 的分流
- 结果拷回 CPU 的时机

所以，这一层虽然已经靠近模型执行，但仍然保留着明显的“执行编排”属性。

## `TpModelWorker`：`Scheduler` 和 `ModelRunner` 之间的承接层

在继续往下看 `ModelRunner` 之前，还要先经过一个经常被忽略的中间层：`tp_worker.py` 里的 `TpModelWorker`。

对生成路径来说，典型入口是：

- `forward_batch_generation()`

它的作用可以粗略理解成：

- 把 scheduler 侧的 batch 整理成适合执行器使用的 `ForwardBatch`
- 调用 `ModelRunner.forward(...)`
- 在需要生成 token 时，进一步调用 `ModelRunner.sample(...)`
- 把结果整理成 `GenerationBatchResult`

这层的意义在于，它把“调度层 batch 语义”和“模型执行层 batch 语义”隔了一层。

于是：

- `Scheduler` 不需要知道太多模型内部 forward 细节
- `ModelRunner` 也不必直接理解调度器所有状态结构

## `ModelRunner`：真正落到模型 forward 的地方

终于到了这条链路里最贴近模型执行的核心对象：`ModelRunner`。

如果把 `Scheduler` 看成控制平面，那么 `ModelRunner` 基本可以看成执行平面里最核心的部分。

它负责的事情包括：

- 初始化模型执行环境
- 准备 distributed / attention backend / graph capture 等执行条件
- 根据 batch 模式选择 forward 路径
- 做 logits 后处理
- 完成采样

这也说明一件事：

`ModelRunner` 不是“一个简单的 `model(**inputs)` 包装器”，而是已经把很多高性能执行策略收进来了。

## `forward()`：统一的执行入口

从阅读顺序上看，`ModelRunner.forward()` 是最好的入口。

它做的事情可以概括成：

- 增加 forward pass 计数
- 建立 profiling / tracing / recorder 上下文
- 调用 `_forward_raw(...)`
- 在 forward 结束后处理 experts/indexer 等额外输出
- 返回 `ModelRunnerOutput`

真正的模式分流发生在 `_forward_raw()`。

在这里，SGLang 会根据 `forward_batch.forward_mode` 选择不同执行路径：

- decode
- split prefill
- extend
- idle

并且会先判断能不能走 cuda graph。

这一步很能体现 `ModelRunner` 的职责边界：

- 它不关心请求最初来自哪个 HTTP 接口
- 也不关心队列里还有多少 waiting request
- 它只关心当前这一个 batch 该用什么执行路径跑起来

也就是说，到了 `ModelRunner`，系统视角已经从“请求生命周期管理”切成了“单批执行优化”。

## `forward_decode()` 和 `forward_extend()`：执行路径为什么要细分

SGLang 在执行层继续区分 decode 和 extend，不是重复造概念，而是因为这两类 forward 本来就不一样。

在调度层，prefill / decode 的区分主要服务于 batch 管理和资源调度。

在执行层，这种区分会进一步影响：

- 输入张量组织方式
- attention backend 使用方式
- graph replay 或 eager 执行选择
- 返回 logits 的位置和后续采样逻辑

所以，`Scheduler` 和 `ModelRunner` 都区分这些模式，但它们的关注点不同：

- `Scheduler` 关注“该调哪一类 batch”
- `ModelRunner` 关注“该怎样跑这一类 batch”

## `sample()`：生成闭环的最后一跳

对生成任务来说，真正拿到 logits 还不算结束，还必须决定下一个 token 是什么。

这件事由 `ModelRunner.sample()` 完成。

它的大体流程是：

1. 先做 logits 预处理
2. 应用 regex / bias 等 sampling 约束
3. 调 sampler 产出 next token ids
4. 在需要时更新 ngram token table

这一步很重要，因为它说明：

在 SGLang 里，“模型 forward”和“生成一个 token”不是同一个概念。

forward 给出的是 logits；
sample 才把这些 logits 变成真正会推进请求状态的 next token ids。

所以从责任划分看，`ModelRunner` 不只负责算模型，还负责把模型输出接成生成系统所需的下一步动作。

## `Scheduler.process_batch_result()`：把执行结果重新接回调度循环

当 `run_batch()` 得到结果之后，请求还没有结束。

`Scheduler` 还要通过 `process_batch_result()` 去做后续状态推进，把执行结果重新纳入调度循环。

在这个阶段，scheduler 会关心的事情包括：

- 当前 batch 的 forward mode
- 哪些请求已经完成
- 哪些请求还能继续 decode
- 哪些输出需要送去 detokenizer
- 哪些状态要更新回运行中的 batch

换句话说，`ModelRunner` 给出的是“本轮执行结果”，而不是“最终对外响应”。

真正把结果继续往前推回系统其余部分的，仍然是 `Scheduler`。

这也再次说明，SGLang 的执行链路不是单向瀑布，而是一个由 scheduler 主导的循环系统。

## `TokenizerManager.handle_loop()`：结果回流的入口

很多人读完前面的链路后，会下意识以为结果会直接从 `Scheduler` 返回给用户。

但实际不是这样。

在 SGLang 的架构里，结果通常还会经过 detokenizer 侧，再回到主进程里的 `TokenizerManager.handle_loop()`。

`handle_loop()` 会持续从 `recv_from_detokenizer` 读取对象。

对于批量字符串输出、embedding 输出或 token id 输出，它会调用 `_handle_batch_output(...)`。

这一步的意义非常大，因为它完成的是：

- 从“批输出对象”回到“逐请求状态”
- 从“内部返回格式”回到“外部可见结果”

### `_handle_batch_output()`：把批结果拆回单请求状态

`_handle_batch_output()` 是 `TokenizerManager` 另一段非常值得仔细看的代码。

它会对 batch 里的每个 `rid` 做处理：

- 找到对应的 `ReqState`
- 构造 `meta_info`
- 累积输出文本或 token ids
- 处理 logprob、hidden states、routed experts 等附加结果
- 判断请求是否 finished
- 在流式模式下决定返回增量还是完整结果

这里最值得强调的是，`TokenizerManager` 在请求返回阶段的职责一点也不轻。

它并不是拿到一个字符串就直接 return，而是在做一层“结果状态机管理”。

这层状态机之所以必要，是因为内部执行天然是批量推进的，而外部用户看到的却是：

- 一个 HTTP streaming 响应
- 一个 SDK async generator
- 或一个最终完整结果

这两种视角之间必须有一层适配器，而 `TokenizerManager` 正是这层适配器。

### `_wait_one_response()`：把内部事件流包装成用户可消费接口

最后，对外暴露的流式或非流式体验，会在 `_wait_one_response()` 这类逻辑里被整理出来。

对于调用者来说，看到的是：

- 非流式时返回最终结果
- 流式时不断迭代拿到 chunk

但在内部，这背后其实是：

- scheduler 持续推进 batch
- detokenizer 持续产生批输出
- `TokenizerManager` 持续更新单请求状态

直到某个请求真正 finished，整条生命周期才算闭环。

## 把一条请求的完整生命周期串起来

现在可以把这篇文章最重要的主链路重新整理一次：

```text
上层 API / HTTP handler
-> Engine.generate() / async_generate()
-> TokenizerManager.generate_request()
-> _init_req_state()
-> _tokenize_one_request()
-> _send_one_request()
-> Scheduler.handle_generate_request()
-> _add_request_to_queue()
-> Scheduler event loop
-> get_next_batch_to_run()
-> run_batch()
-> TpModelWorker.forward_batch_generation()
-> ModelRunner.forward()
-> ModelRunner.sample()
-> Scheduler.process_batch_result()
-> DetokenizerManager
-> TokenizerManager.handle_loop()
-> _handle_batch_output()
-> _wait_one_response()
-> 返回最终结果或 streaming chunk
```

如果把这条链拆成三段来看，会更清楚：

### 第一段：请求进入系统

这一段主要由 `Engine` 和 `TokenizerManager` 完成。

核心目标是把用户请求变成内部 tokenized request，并建立请求状态。

### 第二段：请求被调度和执行

这一段主要由 `Scheduler`、`TpModelWorker` 和 `ModelRunner` 完成。

核心目标是把请求纳入持续 batch 调度，并真正跑出本轮 token 结果。

### 第三段：结果回流到用户接口

这一段主要由 `DetokenizerManager` 和 `TokenizerManager` 完成。

核心目标是把内部批执行结果重新拆回用户可感知的单请求输出。

## 为什么 SGLang 要拆成这三层，而不是做成一个大 executor

看到这里，一个自然的问题是：

为什么不把这些逻辑都揉进一个大对象里，而要拆成 `TokenizerManager`、`Scheduler`、`ModelRunner` 三层？

从代码组织看，至少有四个很现实的原因。

### 1. 让输入处理和高频调度解耦

输入规范化、tokenization、多模态预处理、本地状态管理，和高频 batch 调度、显存约束决策，本来就是两类不同问题。

把它们拆开后：

- 主进程可以更专注地对接外部请求
- scheduler 子进程可以更稳定地专注调度循环

### 2. 让调度策略和模型执行策略解耦

`Scheduler` 关心的是“谁该跑”，`ModelRunner` 关心的是“这批怎么跑最快”。

如果把两者揉在一起，调度逻辑会被模型执行细节淹没，执行层也会被请求状态管理污染。

### 3. 让结果回流和批执行推进解耦

内部执行是 batch-oriented 的，但对外返回通常是 request-oriented 的。

`TokenizerManager` 重新接管回包，正好把这两种视角隔开。

### 4. 为更多运行时特性留出接入点

从实际代码可以看出，SGLang 的运行时已经接入了很多高级特性，比如：

- overlap schedule
- speculative decoding
- disaggregation
- LoRA
- 多模态输入
- routed experts / hidden states 等扩展输出

如果没有分层，这些能力会很快把主链路搅成一个难以维护的大团。

## 这条链路最值得记住的几个判断

读完源码后，我觉得有几个判断特别值得记住。

### 1. `TokenizerManager` 不是“分词器包装器”

它本质上是请求生命周期前半段和返回阶段的状态管理中心。

### 2. `Scheduler` 才是运行时主循环的控制平面

吞吐、continuous batching、prefill/decode 推进，核心都落在这里。

### 3. `ModelRunner` 不是“简单调模型 forward”

它承接的是执行层优化，包括 graph、不同 forward mode、采样和附加执行逻辑。

### 4. SRT 的关键不是某一个类，而是三层之间的边界非常清楚

真正让系统既能扩展又能做性能优化的，不只是某个函数写得快，而是：

- 输入处理边界清楚
- 调度边界清楚
- 执行边界清楚
- 结果回流边界也清楚

## 总结

这一篇的核心目标，不是把 `TokenizerManager`、`Scheduler`、`ModelRunner` 各自逐行讲完，而是把它们放回同一条请求生命周期里看清楚。

从整条链路来看，SGLang SRT 的核心协作机制可以概括成一句话：

它把一条请求拆成了“进入系统、被调度执行、结果回流”三个阶段，并分别交给最合适的组件负责。

更具体一点说：

- `Engine` 负责装配运行时，并把请求送入主链路
- `TokenizerManager` 负责把请求变成内部输入、维护请求状态、接收结果回流
- `Scheduler` 负责统一调度循环、队列、batch 和状态推进
- `ModelRunner` 负责真正的 forward、执行路径选择和采样

因此，标题里的问题“`TokenizerManager`、`Scheduler`、`ModelRunner` 怎样串起来”，答案并不是“先后调用三个类”这么简单。

真正的答案是：

它们通过一条跨进程、可持续批处理、可流式返回的运行时主链路串起来，共同构成了 SGLang 高性能推理系统最核心的执行闭环。

下一篇，就可以继续从这条主链路里再往下钻一层：看看 SGLang 的高性能到底从哪里来，以及调度、缓存和并行策略是怎样在代码里真正落地的。
