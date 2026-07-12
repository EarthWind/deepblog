# 进阶特性专题：Speculative Decoding、PD Disaggregation、LoRA 如何接入运行时

上一篇我们把 SGLang 的高性能主线拆成了三部分：

- 调度
- 缓存
- 并行与执行

但如果继续往代码里走，很快就会遇到一个新的问题：

像 `Speculative Decoding`、`PD Disaggregation`、`LoRA` 这种高级特性，究竟是“外挂模块”，还是已经长进了运行时主链路？

答案显然是后者。

如果只看目录，确实会觉得它们分散在不同地方：

- `python/sglang/srt/speculative/`
- `python/sglang/srt/disaggregation/`
- `python/sglang/srt/lora/`

但只要顺着请求链路走一遍，就会发现这三类特性虽然目标不同，接入方式却有明显共性：

- 它们都不会停留在单一目录里
- 它们都会穿过 `TokenizerManager -> Scheduler -> Worker -> ModelRunner`
- 它们都会反过来影响调度策略、缓存组织或执行路径

所以这篇文章不打算把三个目录平铺介绍，而是回答一个更工程化的问题：

SGLang 里的高级特性，到底是怎样被接到运行时里的？

## 文章定位

- 目标读者：已经理解 SRT 主链路，并读过前一篇性能文章的工程师
- 阅读前提：知道 `TokenizerManager`、`Scheduler`、`TpModelWorker`、`ModelRunner` 的基本分工
- 核心问题：`Speculative Decoding`、`PD/EPD Disaggregation`、`LoRA` 分别插进运行时的哪一层，为什么会影响调度和执行器设计
- 阅读收益：读完后，你应该能把“目录里的高级特性”重新看成“主链路上的横切能力”

## 重点代码路径

- `python/sglang/srt/speculative/`
- `python/sglang/srt/disaggregation/`
- `python/sglang/srt/lora/`
- `python/sglang/srt/managers/tokenizer_manager.py`
- `python/sglang/srt/managers/scheduler.py`
- `python/sglang/srt/managers/tp_worker.py`
- `python/sglang/srt/model_executor/model_runner.py`
- `docs/advanced_features/speculative_decoding.md`
- `docs/advanced_features/pd_disaggregation.md`
- `docs/advanced_features/epd_disaggregation.md`

## 先看统一视角：高级特性并不是外挂，而是插在四层主链路上

在前面的文章里，我们已经把普通请求的主链路概括成：

```text
TokenizerManager
-> Scheduler
-> TpModelWorker
-> ModelRunner
```

这篇文章里最重要的一个判断是：

高级特性并不会绕开这条主链路，而是会在不同层做不同类型的“插桩”。

大致可以先这样理解：

- `TokenizerManager` 负责解析用户侧附加语义，把高级特性所需的元数据挂到请求上
- `Scheduler` 负责把这些特性纳入批调度、队列管理和资源分配
- `Worker` 负责把“调度侧的策略”变成“具体的执行体或传输体”
- `ModelRunner` 负责让这些特性真正进入 forward、cache、graph 或 batch metadata

所以，SGLang 里的高级特性并不是“加一个目录”就结束了，而是必须穿过这四层才能落地。

如果再把三类特性放在一起看，会发现它们分别代表三种很典型的运行时扩展方式：

- `Speculative Decoding` 更像“改执行路径”
- `PD/EPD Disaggregation` 更像“改请求生命周期与数据流”
- `LoRA` 更像“改租户解析、批调度和模型模块注入”

这也正是为什么这三类特性很适合作为第 8 篇。

它们不是边角功能，而是最能说明 SGLang 运行时可扩展性的三个样本。

## 为什么高级特性一定会改到多层，而不是只改执行器

很多人第一次看 speculative、LoRA 或 disaggregation，会直觉觉得这些事情应该主要发生在模型执行层。

但 SGLang 的代码恰恰说明，真正的高级特性很少只改执行器。

原因很简单：

- 如果特性会改变请求形态，就一定会触及 `TokenizerManager`
- 如果特性会改变谁先跑、怎么组 batch、要不要分流，就一定会触及 `Scheduler`
- 如果特性会改变 worker 拓扑或模型实例形态，就一定会触及 `TpModelWorker` 或特化 worker
- 如果特性最终影响 forward、KV、graph 或模块权重，就一定会触及 `ModelRunner`

所以理解这些高级特性时，最重要的不是记住目录名，而是记住下面这个判断：

真正进入运行时的特性，最终一定会改动“请求元数据、调度状态、执行体形态、forward 语义”中的至少两层，往往是三层甚至四层。

下面就分别来看。

## 专题一：Speculative Decoding 如何长进运行时

`Speculative Decoding` 很容易被理解成“再加一个 draft model 提前猜 token”。

这个理解方向没错，但如果只停在这里，就会低估 SGLang 的实现复杂度。

因为在 SGLang 里，speculative 不只是一个算法选项，而是一套会重新塑造 worker 形态、调度状态和 forward 模式的运行时分支。

## 文档先给的是算法谱系，代码接住的是统一抽象

从 `docs/advanced_features/speculative_decoding.md` 看，SGLang 支持的 speculative 变体并不少：

- `EAGLE`
- `EAGLE3`
- `STANDALONE`
- `NGRAM`
- `FROZEN_KV_MTP`
- 以及和 overlap scheduler 结合的 `SpecV2`

如果只是文档视角，你会把它理解成一组选项。

但代码里最关键的收口点其实是 `spec_info.py` 里的 `SpeculativeAlgorithm`。

它的价值不只在于枚举算法名，而在于把一系列运行时行为统一成了一套接口，例如：

- 如何判断当前算法属于哪一类
- 是否携带 draft hidden states
- 是否需要 top-k
- 如何构造 `FutureMap`
- 如何创建具体 worker
- 如何根据算法改写 `server_args`

这说明 SGLang 并不是“在各处 `if algorithm == xxx`”，而是先把 speculative 算法抽象成统一运行时协议。

也正因为有了这层抽象，`Scheduler` 和 `ModelRunner` 才能比较稳定地接入不同 speculative 变体。

## `Scheduler` 是 speculative 的第一个核心汇合点

虽然 speculative 目录里有很多 worker 和算法代码，但把这些东西真正接进系统的关键位置，是 `scheduler.py`。

这里最值得看的第一段是 `maybe_init_draft_worker()`。

它做的事情非常直接：

1. 根据 `spec_algorithm` 判断是否启用 speculative
2. 组装 draft worker 所需的上下文参数
3. 通过 `spec_algorithm.create_worker(server_args)` 选出具体 worker 类
4. 初始化 `draft_worker`

这一层意义非常大。

因为它说明在 SGLang 里，speculative 不是给现有 worker 打个补丁，而是会显式地创建第二执行体。

普通模式下，`Scheduler` 的 `model_worker` 直接指向 `tp_worker`。

而 speculative 模式下，它会让：

- `tp_worker` 继续作为 target worker
- `draft_worker` 成为新的主调度执行体入口

这说明 speculative 改动的第一层不是 kernel，而是 worker 拓扑。

## `init_model_worker()` 暗示了一件事：speculative 会影响内存池和 backend 初始化

如果继续看 `Scheduler.init_model_worker()`，可以看到 speculative 不是“创建 worker 就完事”。

后面还会顺着同一条初始化链继续走：

- 为 target worker 分配内存池
- 必要时为 draft worker 共享或建立 KV cache pool
- 初始化 attention backend
- 做 CUDA graph capture

这件事很关键。

因为这说明 speculative 并不是逻辑层面“多一个 draft 分支”，而是会直接改变：

- 内存池的构造方式
- KV cache 的拥有者关系
- attention backend 初始化路径
- graph warmup 的方式

所以 speculative 接入的深度，远比“多跑一个小模型”要深。

## `FutureMap` 和 overlap 说明 speculative 不只是双 worker，还会改异步调度形态

继续看 `Scheduler.init_overlap()`，会看到 speculative 还影响了 overlap 调度相关的状态初始化。

这里 `future_map` 的创建不是固定逻辑，而是走：

`self.spec_algorithm.create_future_map(...)`

这说明 speculative 算法本身已经参与到了 scheduler 的异步状态组织里。

也就是说，speculative 不是只在 forward 阶段工作，它还会影响：

- 输入 relay
- 序列长度的维护方式
- overlap 调度下的状态传递

这也是为什么文档里会专门提到 `SpecV2`、DP attention 兼容性以及不同算法约束。

本质上，speculative 一旦和 overlap scheduler 结合，就不再只是一个局部算法，而是会重写调度循环里的部分假设。

## `EagleDraftWorker`：最能看出 speculative 怎样变成“新执行体”

如果要选一个最能说明 speculative 接入方式的实现，`eagle_worker_v2.py` 里的 `EagleDraftWorker` 很有代表性。

它最值得注意的一点是：

这个 worker 内部会再构造一个 `TpModelWorker(is_draft_worker=True)`。

也就是说，EAGLE 并不是在原有 target worker 上附加一个函数，而是明确拥有一套 draft worker。

它还会继续做几件重要的事情：

- 根据 `speculative_eagle_topk`、`num_steps`、`num_draft_tokens` 配置内部形态
- 初始化 draft worker 的内存池
- 初始化自己的 attention backend
- 按需初始化 prefill CUDA graph
- 在某些路径下复用 target 的 embedding 或 lm head

这会带来一个非常清楚的认识：

在 SGLang 里，speculative 的核心不是“多一个模型”，而是“多一个被运行时正式管理的执行体”。

一旦是正式执行体，它就必须像普通 worker 一样被统一纳入：

- 内存池管理
- backend 初始化
- graph 管理
- 调度状态推进

这也解释了为什么 speculative 目录会看起来比很多人预期得大。

## `ModelRunner` 里 speculative 不是外挂，而是内建模式

speculative 最终要落到执行层，关键还是 `ModelRunner`。

虽然这一篇不展开所有细节，但从初始化路径和 forward metadata 的组织方式已经能看出，它在 `ModelRunner` 里不是独立外挂，而是内建能力。

一个典型信号是：

- `ModelRunner` 会感知 `is_draft_worker`
- `ForwardBatch` 里会携带 `spec_algorithm`、`spec_info`

这意味着当 batch 进入执行器时，speculative 不再是外部上下文，而已经变成 forward 输入语义的一部分。

换句话说，执行器在处理 speculative batch 时，不是“顺便带个参数”，而是在跑另一种前向模式。

因此，从架构角度看，speculative 在 SGLang 里的本质可以概括为：

- 先通过统一算法抽象把不同 spec 变体收口
- 再通过 scheduler 创建 draft worker、组织 overlap 状态
- 最后让 worker 和 model runner 共同把 speculative 变成正式执行路径

## 专题二：PD Disaggregation 为什么本质上是在重写请求生命周期

如果说 speculative 主要是在增加执行体和 forward 模式，那么 `PD Disaggregation` 的重点就完全不同。

它真正改写的是：

- 请求在哪个阶段被哪个服务处理
- KV cache 和附加 metadata 怎样在不同阶段之间转移
- scheduler 怎样分别管理 prefill 和 decode 的资源与等待状态

所以，PD 的本质不是“多开两个服务”，而是重写请求生命周期。

## 文档先给问题定义：prefill 和 decode 放在一起为什么不好

`docs/advanced_features/pd_disaggregation.md` 先回答了一个很基础但很重要的问题：

为什么要做 PD disaggregation？

文档给出的理由非常清楚：

- `Prefill` 更偏计算密集
- `Decode` 更偏内存和 KV cache 密集

如果把两者塞在统一引擎里，很容易带来两类问题：

- prefill 打断 decode，导致 decode latency 变差
- 在 DP attention 场景下，不同 worker 同时跑不同阶段，负载不平衡

所以 PD 做的事情不是简单拆服务，而是把两个资源特征完全不同的阶段，从统一调度里剥离开。

这和上一篇我们讲的“prefill/decode 应该被分别建模”是完全一致的，只不过这次不是在单个调度器里区分，而是直接升级成跨服务的运行时架构。

## `DisaggregationMode`：先从一个小枚举看清楚系统角色

代码里最先要看的，是 `disaggregation/utils.py` 里的 `DisaggregationMode`。

它只有三个值：

- `NULL`
- `PREFILL`
- `DECODE`

这个枚举看起来简单，但它代表的是运行时角色切换。

一旦 `server_args.disaggregation_mode` 被设成 `prefill` 或 `decode`，系统从这个时刻起就不再是一个统一引擎，而是一个有明确职责边界的阶段性引擎。

所以 PD 接入的第一步，不是传输 KV，而是先让整个运行时接受“我是 prefill 节点”或者“我是 decode 节点”这件事。

## `TokenizerManager.init_disaggregation()`：主进程先承认“请求要跨阶段流转”

在 `TokenizerManager` 里，`init_disaggregation()` 是 PD 接入主链路的第一个明显入口。

它至少做了三件事：

- 解析并保存 `disaggregation_mode`
- 启动 bootstrap service
- 在 language-only 场景下初始化 encoder 相关 receiver

这一层特别值得注意，因为它说明：

PD 不是 scheduler 子进程里才开始生效的。

从主进程开始，请求管理侧就已经要知道：

- 当前服务是什么角色
- 请求后面会不会跨阶段流转
- 是否需要 bootstrap room 这类额外元数据

也就是说，PD 一进入系统，请求状态模型就已经变化了。

## `Scheduler.init_disaggregation()`：PD 的真正核心在这里

如果说 speculative 的核心接入点在 `maybe_init_draft_worker()`，那么 PD 的核心接入点毫无疑问是 `Scheduler.init_disaggregation()`。

这段代码最值得关注的地方，不在于它建了多少对象，而在于它按 `prefill` 和 `decode` 走了两套完全不同的队列和 buffer 初始化逻辑。

### decode 模式：重点是等待 KV 和 metadata 落地

在 `DisaggregationMode.DECODE` 下，scheduler 会初始化：

- `ReqToMetadataIdxAllocator`
- `MetadataBuffers`
- `DecodeTransferQueue`
- `DecodePreallocQueue`

这套对象的组合已经非常能说明 decode 节点的职责：

- 先为请求分配 metadata buffer 索引
- 等待或轮询来自 prefill 侧的 KV 和 metadata
- 在真正执行 decode 之前完成预分配和接收

换句话说，decode 节点不再负责完整请求初始化，它负责“接住已经被 prefill 侧做过前半场工作”的请求。

### prefill 模式：重点是 bootstrap 和发送中的 inflight 请求

在 `DisaggregationMode.PREFILL` 下，scheduler 会转而初始化：

- `ReqToMetadataIdxAllocator`
- `MetadataBuffers`
- `PrefillBootstrapQueue`
- `disagg_prefill_inflight_queue`

这说明 prefill 节点的重点不是等待，而是：

- 给请求准备好 bootstrap 信息
- 组织 KV 和 metadata 发送
- 管理正在传输中的请求

所以 prefill 和 decode 在 PD 架构里并不是对称分工，而是发送端和接收端两种不同角色。

这也再次印证了 PD 不是“统一引擎拆成两半”，而是把同一条请求生命周期切成了两段不同职责。

## `MetadataBuffers`：PD 真正传的不只是 KV，还有请求推进所需的附加状态

很多人会把 PD 先理解成“把 KV cache 传过去”。

这当然是重点，但并不完整。

`disaggregation/utils.py` 里的 `MetadataBuffers` 能说明得更清楚。

它里面会维护很多与首个输出 token 和状态推进相关的数据，比如：

- `output_ids`
- `cached_tokens`
- `output_token_logprobs_*`
- `output_top_logprobs_*`
- `output_topk_*`
- `output_hidden_states`
- `bootstrap_room`

这说明 PD 在 SGLang 里传输的不是一块裸 KV，而是一组足够让 decode 侧继续推进请求的运行时状态。

从工程角度看，这一点很重要。

因为运行时分阶段之后，真正要传的从来不只是“模型内部缓存”，而是“让下一阶段能够接着工作的全部必要上下文”。

## speculative 和 disaggregation 已经交叉了

读 `Scheduler.init_disaggregation()` 时，还有一个特别值得注意的细节：

在构造 `MetadataBuffers` 时，hidden size 和 hidden states dtype 会根据：

`self.spec_algorithm.carries_draft_hidden_states()`

来决定。

这件事非常能说明 SGLang 的高级特性已经不是彼此隔离的。

它意味着：

- 如果当前 speculative 算法需要把 draft hidden states 一起带到 decode 侧
- 那么 disaggregation 的 metadata layout 也必须跟着变化

这就是一个很典型的运行时级耦合点。

也正因为有这种耦合，我们才不能把 speculative、PD、LoRA 简单看成互不相关的插件。

它们会共同作用于：

- metadata 结构
- buffer 大小
- 调度判断
- 执行路径

这也是高级特性会让运行时代码迅速复杂起来的根本原因。

## EPD 是把“拆两段”进一步变成“拆三段”

如果说 PD 是把请求拆成 `Prefill -> Decode` 两段，那么 `EPD` 做的事情就更进一步：

它把 Vision-Language Model 的执行拆成：

- `Encoder`
- `Prefill`
- `Decode`

`docs/advanced_features/epd_disaggregation.md` 讲得很清楚，这样做的动机是：

- vision encoder 也是高计算密集阶段
- 它和 language prefill 继续绑在一起，仍然会造成资源耦合

所以 EPD 的本质，是在 PD 的基础上再把 encoder 单独剥离出来。

这也解释了为什么在 `TokenizerManager.init_disaggregation()` 里，你会看到：

- `language_only`
- `encoder_urls`
- `EncoderBootstrapServer`
- `create_mm_receiver(...)`

这些和多模态编码流转有关的对象。

也就是说，EPD 并不是另起一个完全新系统，而是在原有 disaggregation 机制上继续把请求生命周期细分。

## 专题三：LoRA 为什么同时改了入口解析、批调度和模型层

和 speculative、PD 相比，`LoRA` 看起来像是另一类问题。

它不像 speculative 那样引入第二执行体，也不像 PD 那样拆服务阶段。

但这并不意味着它更简单。

恰恰相反，LoRA 很能体现一种典型的 serving 难点：

如何在不重启服务的情况下，支持多租户 adapter 动态装载、请求级选择，并在 batch 内高效混跑。

这就决定了 LoRA 必然会同时改到：

- 请求入口解析
- 调度阶段的 adapter 装载约束
- 模型层的权重注入和 batch metadata 准备

## `LoRARegistry`：为什么 LoRA 的单一真相源放在 `TokenizerManager`

LoRA 这条线最值得先看的，不是 `LoRAManager`，而是 `lora_registry.py` 里的 `LoRARegistry`。

它的注释已经把定位写得很清楚：

它驻留在 tokenizer manager 进程里，作为 LoRA adapter 的单一真相源。

这件事很关键，因为 LoRA serving 的第一个问题不是怎样 forward，而是：

- 某个 adapter 现在是否可用
- 用户传的是 `lora_name` 还是 `lora_path`
- 它对应哪个内部 `lora_id`
- 当前有没有请求正在使用这个 adapter

这些问题天然更适合放在请求管理侧，而不是放到 scheduler 或 model runner 里。

所以，LoRA 在 SGLang 里的第一落点是主进程，而不是执行器。

## `TokenizerManager.init_lora()`：请求入口先拥有 LoRA 解析能力

在 `TokenizerManager` 里，`init_lora()` 会初始化三样关键状态：

- `LoRARegistry`
- `lora_update_lock`
- `lora_ref_cache`

这三样东西合在一起，基本就把 LoRA 请求入口层的职责说明白了：

- registry 负责当前可用 adapter 的真相源
- update lock 负责序列化动态 load/unload
- ref cache 负责支持被驱逐 adapter 的重新加载

这里最值得强调的是：

LoRA 在 SGLang 里不是“只支持启动时预加载”，而是显式支持运行时动态加载和回收。

这也是为什么它必须有自己的 registry、引用缓存和使用计数。

## `_validate_and_resolve_lora()`：LoRA 的真正入口不是 forward，而是请求解析

继续往下看 `TokenizerManager._validate_and_resolve_lora()` 和 `_resolve_lora_path()`，会发现一条非常清楚的链路：

1. 检查当前服务是否启用了 LoRA
2. 规范化请求里的 `lora_path`
3. 检查单请求涉及的 adapter 数量是否超过上限
4. 如果有被动态驱逐的 adapter，尝试隐式重新加载
5. 通过 registry 把用户侧 LoRA 名称映射成内部 `lora_id`
6. 在请求状态里传播 `lora_id`

这说明 LoRA 在请求进入执行系统之前，就已经完成了从“用户语义”到“内部标识”的转换。

一旦做完这一步，后面的 scheduler 和 model runner 就不需要再理解用户传的是路径还是名字，它们只需要处理内部 `lora_id`。

这就是一个非常标准的分层设计：

- 用户接口的复杂性在入口层消化
- 执行层只处理稳定内部标识

## `Scheduler` 里的 LoRA 重点不是解析，而是调度公平和装载约束

LoRA 进入 scheduler 之后，关注点就完全变了。

这时候调度器不再关心 adapter 名称，而是关心：

- 这个 batch 里会不会混进太多不同 LoRA
- 某个 adapter 是否已经在运行中 batch 里
- 是否允许边算边加载新的 adapter
- 如何避免 pinned adapter 或热门 adapter 让其他请求饿死

所以 LoRA 在 `Scheduler` 里的主线，其实是一条“多租户 batch 调度”主线。

## `init_lora_drainer()` 和 `init_lora_overlap_loader()`：LoRA 调度不是附加判断，而是专门的调度组件

从 `scheduler.py` 看，LoRA 相关初始化甚至有单独组件：

- `LoRADrainer`
- `LoRAOverlapLoader`

这意味着 SGLang 并没有把 LoRA 调度压缩成一个简单布尔判断，而是把它正式当成调度子系统的一部分。

这两个组件分别代表两种很现实的运行时诉求：

- drainer：控制不同 LoRA 请求之间的 draining 和公平性
- overlap loader：在可能时把 LoRA 权重加载和计算重叠起来

也就是说，LoRA 在 scheduler 里不只是“能不能跑”，而是“怎样在多租户场景下跑得更公平、更平滑”。

## `_can_schedule_lora_req()`：LoRA 批调度的核心判断

如果要从 scheduler 里挑一个最关键的 LoRA 函数，`_can_schedule_lora_req()` 很有代表性。

它的判断顺序非常清楚：

1. 如果 drainer 不允许，直接不调度
2. 如果该 `lora_id` 已经在 running 集合里，可以继续混入
3. 如果启用了 overlap loading，就尝试边跑边加载
4. 否则调用 `lora_manager.validate_lora_batch(...)` 判断这一批 adapter 是否还能装得下

这里最值得记住的是，LoRA 批调度的核心不是“看 batch size”，而是“看 batch 内 adapter 集合”。

这和普通生成请求的调度约束完全不同。

所以从系统角度看，LoRA 给 scheduler 带来的变化，不是附加一个请求字段，而是增加了一维新的资源约束：

adapter 也是运行时资源。

## `TpModelWorker`：LoRA 在 worker 层很薄，因为重头戏不在这里

和 speculative 不同，LoRA 在 worker 层的存在感反而比较低。

`tp_worker.py` 里能看到的主要是几个 RPC 透传入口：

- `load_lora_adapter`
- `unload_lora_adapter`
- `load_lora_adapter_from_tensors`

这其实很合理。

因为 LoRA 的 worker 层职责不是创造新执行体，而是把 scheduler 或外部控制面的 load/unload 请求传给真正执行层。

所以这里的“薄”恰恰说明了分层是清楚的：

- 解析在 tokenizer manager
- 调度约束在 scheduler
- 真正装载和前向注入在 model runner / lora manager

## `ModelRunner.init_lora_manager()`：LoRA 最终在执行层变成正式能力

LoRA 真正生效的地方，还是 `ModelRunner`。

`init_lora_manager()` 会创建一个 `LoRAManager`，把下面这些关键配置收进来：

- `max_loras_per_batch`
- `lora_backend`
- `max_lora_rank`
- `lora_target_modules`
- 初始 `lora_paths`

这意味着 LoRA 到了执行层以后，就不再是请求级元数据，而是一套正式的执行资源：

- 有自己的 backend
- 有自己的 memory pool
- 有自己的 batch metadata
- 有自己的模块替换逻辑

## `LoRAManager`：LoRA 接入最完整的一层抽象

如果说 `SpeculativeAlgorithm` 是 speculative 的统一抽象，那么 `LoRAManager` 基本就是 LoRA 的统一抽象。

它把 LoRA serving 里最关键的几类事情都包了起来：

- adapter 的加载、校验、卸载
- LoRA memory pool 管理
- batch 内不同 `lora_id` 的 buffer 映射
- 模块替换或注入
- CUDA graph 相关 batch info 初始化

这说明 SGLang 对 LoRA 的处理并不是“在 linear 层前后加个 adapter”，而是把它真正产品化成运行时子系统。

### `validate_new_adapter()`：LoRA 能不能加载，先看运行时约束

在 `LoRAManager` 里，`validate_new_adapter()` 很能体现 SGLang 的工程态度。

它会检查很多现实约束，比如：

- 是否引入新增 token
- 是否是当前不支持的 DoRA
- 名称是否重复
- rank 是否和当前 memory pool 配置兼容
- pinned adapter 会不会把可用槽位占满

这说明 LoRA 的接入不是“模型能读到 adapter 文件就算成功”，而是要先满足当前 serving 运行时的资源和兼容性约束。

### `prepare_lora_batch()`：LoRA 到了执行器里会变成 batch metadata

再往下看 `prepare_lora_batch()`，就能看到 LoRA 是怎样真正进入 forward 的。

它会根据当前 `forward_batch.lora_ids` 去计算：

- 每个请求对应哪个 weight buffer
- 每个 buffer 对应的 LoRA rank
- 每个 adapter 的 scaling

然后把这些信息交给 backend 去准备批级元数据。

这件事特别关键，因为它说明：

执行器并不是在 forward 时再去查询某个 adapter 文件，而是把 LoRA 使用关系先编译成 batch metadata，再交给下层 kernel 或模块逻辑。

于是 LoRA 在执行层里的形态就变成了：

- 请求级 `lora_id`
- 批级 buffer index
- 层级权重映射

这才是高性能多租户 LoRA serving 真正可行的关键。

## `ModelRunner` 里的 LoRA 已经进入 CUDA Graph 相关初始化

LoRA 还有一个容易被忽略但很重要的点：

它甚至影响到了 CUDA graph 相关准备。

`ModelRunner` 里会有专门的 LoRA CUDA graph buffer 初始化逻辑，而 `LoRAManager` 也有：

- `init_cuda_graph_moe_buffers()`
- `init_cuda_graph_batch_info()`

这说明 LoRA 不是“启用后自动失去 graph 优化”的简单情况，而是执行器会尽量把它纳入已有 graph 体系中。

当然，这也意味着 LoRA 会增加更多兼容性和资源管理复杂度。

但从架构上看，这一步非常重要：

它让 LoRA 不只是功能正确，而是尽可能融入高性能执行路径。

## 把三类特性并排看，会更容易理解它们为什么都算“运行时高级特性”

现在可以把三条线重新并排整理一下。

### `Speculative Decoding`

它最核心的变化是：

- 增加 draft worker
- 增加新的 forward 语义和 worker 协作关系
- 影响 overlap 调度、memory pool、backend 初始化和 metadata

所以它主要是在改：

- worker 形态
- 调度状态
- 执行路径

### `PD/EPD Disaggregation`

它最核心的变化是：

- 重写请求生命周期
- 把 prefill/decode，甚至 encoder/prefill/decode 拆成不同角色
- 引入 KV 和 metadata 的跨阶段传输

所以它主要是在改：

- 请求流转路径
- scheduler 队列和状态机
- metadata buffer 和跨节点协同

### `LoRA`

它最核心的变化是：

- 增加请求级 adapter 解析
- 把 adapter 装载约束纳入 batch 调度
- 在执行层完成模块替换和批级 LoRA metadata 构造

所以它主要是在改：

- 请求解析
- 调度资源约束
- 模型层权重注入

## 真正值得记住的，是它们分别改了哪一层最多

如果要再压缩一下，我会用下面这张“重心图”来记。

```text
Speculative
  重心：Scheduler / Worker / ModelRunner
  本质：多执行体 + 新 forward 模式

PD / EPD Disaggregation
  重心：TokenizerManager / Scheduler
  本质：请求生命周期与 KV/metadata 传输重构

LoRA
  重心：TokenizerManager / Scheduler / ModelRunner
  本质：租户解析 + 批级 adapter 约束 + 模块注入
```

这张图的好处是，它能帮你快速建立读代码顺序。

比如：

- 想看 speculative，优先盯 `spec_info.py`、`scheduler.py`、`eagle_worker_v2.py`
- 想看 PD，优先盯 `tokenizer_manager.py`、`scheduler.py`、`disaggregation/utils.py`
- 想看 LoRA，优先盯 `lora_registry.py`、`tokenizer_manager.py`、`scheduler.py`、`lora_manager.py`

## 文档和代码是怎样互相印证的

这一篇还有一个很值得强调的点：

这三个专题都非常适合“文档和代码对着读”。

### speculative

文档主要告诉你：

- 支持哪些算法
- 参数怎么配
- 哪些组合有兼容性限制

代码真正回答的是：

- 算法如何统一抽象
- draft worker 怎样被创建
- speculative 怎样进入 overlap、memory pool 和 forward batch

### PD / EPD

文档主要告诉你：

- 为什么要拆阶段
- prefill/decode/encoder 分开部署后有什么收益
- transfer backend 和部署方式怎么配

代码真正回答的是：

- mode 怎样落到进程角色
- scheduler 怎样构造不同队列和 metadata buffers
- metadata 除了 KV 之外还包含哪些请求推进状态

### LoRA

文档主要告诉你：

- 服务端怎样开启 LoRA
- 客户端怎样选择 adapter
- 有哪些参数需要配置

代码真正回答的是：

- adapter 名称怎样被解析成内部 `lora_id`
- 多租户 batch 怎样被 scheduler 限制和组织
- 模型层怎样把 `lora_id` 变成真正的批级权重注入

这也说明一个很实用的源码阅读方法：

高级特性不要只看文档，也不要一上来只啃代码。

最好的顺序通常是：

1. 先用文档确认功能目标和用户侧接口
2. 再回到 tokenizer/scheduler/worker/model runner 四层找接入点
3. 最后再读特性目录里的具体实现细节

## 这篇文章最值得记住的几个判断

如果读完源码后只记住几句话，我觉得下面几句最重要。

### 1. 高级特性不是目录概念，而是主链路上的横切能力

看目录只能看到文件归属，看主链路才能看清它们怎样真正进入运行时。

### 2. speculative 的本质不是“加个小模型”，而是“增加正式执行体和新 forward 语义”

它会改 worker 拓扑、内存池、overlap 状态和执行路径。

### 3. PD 的本质不是“拆服务”，而是“重写请求生命周期与数据流”

它真正带来的变化，是 scheduler 阶段和 KV/metadata 传输模式都变了。

### 4. LoRA 的难点不是“加载 adapter”，而是“多租户请求怎样高效混跑”

所以它必须同时改入口解析、批调度和模型层模块注入。

### 5. 三类特性并不是彼此隔离的

speculative 会影响 disaggregation 的 metadata，LoRA 会影响 batch 和 graph 准备，高级特性之间会在运行时层面发生真实耦合。

## 总结

这一篇真正想回答的问题是：

`Speculative Decoding`、`PD Disaggregation`、`LoRA` 是怎样接入 SGLang 运行时的？

答案并不是“各有一个目录和一份文档”。

真正的答案是：

它们都不是外挂，而是顺着同一条运行时主链路进入系统，并分别改写了不同层的职责。

更具体地说：

- `Speculative Decoding` 通过统一算法抽象、draft worker 和新 forward 语义进入系统
- `PD/EPD Disaggregation` 通过 mode、队列、metadata buffer 和跨阶段传输重写请求生命周期
- `LoRA` 通过 registry、调度约束和模型层注入，把多租户 adapter serving 变成正式运行时能力

所以如果把这一篇再压缩成一句话，可以概括成：

SGLang 的高级特性之所以“高级”，不是因为名字复杂，而是因为它们都已经不是局部补丁，而是会穿透 `TokenizerManager`、`Scheduler`、`Worker`、`ModelRunner` 的系统级扩展。

下一篇，就可以继续把视角从 LLM 主运行时拉开，去看 SGLang 为什么会长出 diffusion，以及这套架构复用到底能走多远。
