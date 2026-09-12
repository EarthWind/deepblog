# SGLang 的高性能从哪里来：围绕调度、缓存与并行策略的代码阅读

上一篇我们已经把 SRT 的主执行通路拆开了，也知道一条请求会经过 `TokenizerManager`、`Scheduler`、`ModelRunner` 这几个关键环节。

但理解“链路能跑起来”和理解“为什么它跑得快”，其实是两回事。

很多人第一次接触推理框架时，容易把性能问题理解成：

- attention kernel 够不够快
- 某个 fused op 有没有打开
- CUDA Graph 有没有捕获成功

这些当然重要，但如果认真看 SGLang 的代码，你会发现它的性能并不是建立在某一个单点技巧上，而是建立在三层协同上：

- 调度层决定“什么时候跑谁”
- 缓存层决定“哪些 token 和 KV 不必重算”
- 并行与执行层决定“这一批怎样跑得最快”

换句话说，SGLang 的高性能更像是一种系统设计结果，而不是某几个 kernel 优化的偶然叠加。

这篇文章就沿着这三条主线，去看 SGLang 的性能到底从哪里来。

## 文章定位

- 目标读者：已经理解 SRT 主链路，准备进一步理解性能设计的工程师
- 阅读前提：知道 `Scheduler`、`ModelRunner` 和缓存层大致存在，但还不清楚这些能力怎样共同影响吞吐和时延
- 核心问题：SGLang 的高性能为什么不是单点优化，而是调度、缓存和并行策略协同的结果
- 阅读收益：读完后，你应该能把 continuous batching、prefix cache、HiCache、CUDA Graph、TP/DP/PP/EP 放进同一张性能地图里

## 重点代码路径

- `python/sglang/srt/managers/scheduler.py`
- `python/sglang/srt/managers/schedule_policy.py`
- `python/sglang/srt/server_args.py`
- `python/sglang/srt/mem_cache/unified_radix_cache.py`
- `python/sglang/srt/runtime_context.py`
- `python/sglang/srt/layers/dp_attention.py`
- `python/sglang/srt/model_executor/model_runner.py`
- `python/sglang/srt/models/qwen3.py`

## 先给结论：SGLang 的性能不是“算得快”，而是“少算 + 算对 + 算满”

如果把这篇文章先压缩成一句话，我会这样概括：

SGLang 的高性能，来自三件事同时成立：

1. 通过调度让 GPU 尽量别空着
2. 通过缓存让已经算过的前缀和 KV 尽量别重算
3. 通过并行、后端选择和执行优化，让当前 batch 尽量以最合适的方式跑起来

这三件事分别对应：

- `Scheduler`
- `UnifiedRadixCache` 及其相关缓存体系
- `ModelRunner`、`layers`、`models` 和 `server_args`

也正因为是系统协同，SGLang 的很多“性能参数”看起来不像传统业务系统里的普通配置，而像一个运行时策略面板。

## 为什么性能问题首先落在 `Scheduler`

很多人会下意识以为，性能优化首先应该看模型执行器。

但在 SGLang 里，最应该先看的其实是 `Scheduler`。

原因很简单：

在生成式推理系统里，真正决定吞吐上限的，往往不是单次 forward 的理论速度，而是系统能不能把：

- 新请求不断接进来
- 老请求持续推进下去
- prefill 和 decode 交替安排好
- 显存、KV cache 和 batch 空间利用起来

这类问题，都先落在调度层。

所以，SGLang 的性能故事首先是一篇调度设计故事。

## `run_event_loop()`：性能不是一次调用，而是持续心跳

`scheduler.py` 里最值得优先看的，是 `run_event_loop()` 以及它分发到的 `event_loop_normal()`、`event_loop_overlap()`。

因为这里直接揭示了 SGLang 的运行方式：

```text
recv_requests()
-> process_input_requests(...)
-> get_next_batch_to_run()
-> run_batch(batch)
-> process_batch_result(batch, result)
```

这个循环很重要，因为它说明 SGLang 并不是“请求到来就同步执行一次”的 RPC 模型，而是一个持续跳动的调度系统。

这会带来两个直接收益：

- 系统可以不断把新请求并入正在运行的工作集
- 系统可以把 CPU 侧 bookkeeping 和 GPU 侧计算解耦，做更激进的 pipeline 化

尤其在 `event_loop_overlap()` 里，这种设计更明显：

- 当前 batch 可以继续往前跑
- 上一轮 batch 的结果处理可以和下一轮准备工作尽量重叠

这意味着 SGLang 的性能优化，从一开始就不是“单步最优”，而是“整条事件循环吞吐最优”。

## continuous batching 为什么是性能主线，而不是一句口号

很多框架都会说自己支持 continuous batching，但代码里是否真的把它作为核心设计，差别很大。

在 SGLang 里，continuous batching 的落地几乎都集中在 `Scheduler.get_next_batch_to_run()` 这一段逻辑里。

它做的不是简单地从队列里取一批请求，而是每一轮都要重新回答：

- 要不要先处理 timeout 和 abort
- 上一轮 prefill 结果要不要并入 running batch
- 当前有没有新的 prefill batch 值得先跑
- 如果没有新的 prefill，是不是该继续推进 decode

这几个判断组合起来，才构成真正的 continuous batching。

所以它的本质不是“支持边进边出”，而是：

系统始终把“新请求接入”和“老请求推进”放在同一个调度决策里。

这件事会直接影响两个关键指标：

- TTFT，也就是首 token 时延
- 吞吐，也就是单位时间内系统能持续推进多少请求

如果没有这层统一调度，后面的 kernel 再快，也很容易被空转和碎片化调度吞掉。

## prefill 和 decode 被拆开建模，本身就是性能设计

继续读 `get_next_batch_to_run()` 和 `_get_new_batch_prefill_raw()`，会发现 SGLang 对 prefill 和 decode 的区分非常明确。

这不是代码组织上的偏好，而是性能层面的硬需求。

因为这两个阶段的资源特征本来就不同：

- prefill 更像一次把长上下文灌进模型，激活和计算负担重
- decode 更像持续小步推进，每一步虽然短，但频率高、对调度敏感

如果把它们混成一种统一 batch 语义，调度器就很难同时优化 TTFT 和吞吐。

所以在 SGLang 里：

- `Scheduler` 会先判断新的 prefill 能不能安全且值得加入
- 然后再决定是否继续推进 running decode batch

这让它可以更灵活地做两件事：

- 尽快接住能带来缓存收益或优先级更高的新请求
- 同时不让已经进入 decode 的请求长期饿死

换句话说，SGLang 的 continuous batching 真正成立的前提，就是它先把 prefill 和 decode 当成两类不同资源问题来处理。

## 调度策略本身就是 cache-aware 的

如果只看 `scheduler.py`，你会觉得调度已经很复杂了；但再看 `schedule_policy.py`，会发现 SGLang 还把“缓存命中潜力”直接纳入了调度策略。

这里最值得注意的是两类 policy：

- cache-agnostic：比如 `fcfs`、`lof`、`random`
- cache-aware：比如 `lpm`、`dfs-weight`

这件事非常关键，因为它说明 SGLang 并没有把 prefix cache 当成“执行时顺手命中一下”的附加收益，而是把“谁更可能命中缓存”直接纳入排队顺序。

### `match_prefix_for_req()`：调度前先算前缀命中

在 cache-aware policy 下，请求在真正进入调度决策前，会先通过 `match_prefix_for_req()` 去树形缓存里做 prefix match。

然后调度器才会根据：

- `num_matched_prefix_tokens`
- `last_node`
- 甚至 waiting queue 内部的共享前缀情况

来决定谁该先跑。

这背后对应的是一个很朴素但非常有效的性能思路：

同样两个请求，如果其中一个和已有 cache 的重合前缀更长，它的实际执行代价就更低，也更值得优先推进。

所以，SGLang 的调度不是只看“谁先来”，而是在某些策略下会看“谁更便宜、更能放大缓存收益”。

### in-batch prefix caching：不只是命中已有缓存，还会避免队内重复

`schedule_policy.py` 里还有一段很有意思的逻辑，就是 waiting queue 内部的 simulated radix tree。

它表达的意思是：

- 不只是要看请求是否命中已有缓存
- 还要看 waiting queue 里多个请求之间是不是共享同一段短前缀

如果很多请求都带着相同短前缀，SGLang 会倾向于不要把它们同时都拉进来，而是优先让其中一个先跑，等它把这段前缀变成真正 cache 后，再让后续请求享受命中收益。

这就把“缓存”从一个被动命中机制，变成了主动影响调度顺序的收益放大器。

## prefix cache 为什么不是一个普通 map，而是 `UnifiedRadixCache`

当我们说“prefix caching”时，脑子里很容易浮现出一个字典：

- key 是 token 序列
- value 是某段 KV cache

但 SGLang 的实现明显比这个复杂得多。

它的主实现叫 `UnifiedRadixCache`，从名字就能看出两个关键词：

- `Unified`
- `Radix`

也就是说，它不是简单地存一堆前缀，而是要把不同缓存组件、不同层次的存储和不同并行场景收口到同一棵树结构之下。

## `match_prefix()`：为什么要先 page align 再匹配

在 `unified_radix_cache.py` 里，`match_prefix()` 非常值得看。

它在真正匹配之前，会先做几件事：

- 处理 streaming session 的特殊路径
- 把 key 转成适合当前模式的视图
- 做 page alignment

这件事背后的信号非常明确：

SGLang 的 prefix cache 不是纯粹的逻辑缓存，它和底层 paged KV 组织方式是绑定的。

这意味着缓存命中不是“字符串前缀相同”这么简单，而是要落到实际内存布局可复用的粒度上。

所以这里的高性能不是抽象层面的“少算”，而是已经把“少算”对齐到了真实的 KV 存储单元。

## `cache_finished_req()`：缓存写回本身也是性能策略的一部分

缓存系统另一个很重要的入口是 `cache_finished_req()`。

这一段代码告诉我们，缓存不是请求结束时简单把整段 KV 塞进去，而是会综合考虑：

- `cache_protected_len`
- page alignment
- priority
- 各组件返回的有效缓存长度

如果尾部没有对齐，或者某些部分不值得保留，SGLang 会直接释放对应 KV，而不是机械缓存全部内容。

这说明缓存层同样在做“收益和成本权衡”。

缓存越多不一定越好，真正重要的是：

- 哪些前缀值得保留
- 哪些缓存页之后还可能被复用
- 哪些部分留下来只会占显存和拖累淘汰策略

这也是为什么 prefix cache 在 SGLang 里不是孤立特性，而是和调度、内存池、淘汰策略绑在一起看。

## HiCache：SGLang 的缓存不只在 GPU 里

如果继续看 `UnifiedRadixCache.init_hicache()` 和 `server_args.py` 里 `_handle_hicache()` 相关逻辑，会发现 SGLang 的缓存体系甚至不只停留在 GPU 设备侧。

它还显式支持 hierarchical cache，也就是通常所说的 HiCache。

这层设计的意义很大：

- GPU 上保留最热、最直接影响当前吞吐的 KV
- Host 或外部存储侧承接更大规模的缓存容量
- 调度器根据 HiCache 状态决定能不能更积极地复用历史前缀

所以 SGLang 的缓存体系并不是一个“显存里的前缀树”，而是一个分层缓存系统。

而这也正是为什么在 `server_args` 里，HiCache 不是一个边角参数，而是有专门的 layout、io backend、storage backend 和 prefetch policy 配置。

从工程视角看，这说明 SGLang 已经把缓存看成正式的运行时子系统，而不是附加优化。

## `server_args.py`：性能配置层其实是在定义运行时策略空间

很多人看 `server_args.py` 会觉得它只是参数堆积。

但如果把这一篇的视角带进去，再看这些字段，会发现它其实在做一件很重要的事：

把调度、缓存、并行和执行器优化统一收敛成一套可组合的策略空间。

比如和调度直接相关的参数就包括：

- `max_running_requests`
- `max_total_tokens`
- `chunked_prefill_size`
- `schedule_policy`
- `enable_priority_scheduling`
- `page_size`

这些参数不是平铺罗列的选项，而是在共同决定：

- 单机允许多大工作集
- prefill 是激进还是保守
- 是否要通过 chunked prefill 换更稳定的显存行为
- 调度是更偏公平还是更偏缓存命中收益

也就是说，`server_args.py` 其实就是 SGLang 对“性能行为”做产品化封装的地方。

## 为什么 CUDA Graph 相关逻辑会写得这么重

如果继续往下看 `server_args._handle_cuda_graph_config()`，会发现这部分代码比很多人预期得重得多。

它不是简单解析几个布尔开关，而是：

- 先解析显式配置、便利参数和 legacy 参数
- 再应用 compatibility rule
- 最后做配置校验

更关键的是，它会根据当前运行时条件自动禁用不兼容的 prefill CUDA graph。

这些条件包括：

- DP attention
- pipeline parallelism
- MoE A2A
- LoRA
- multimodal
- hierarchical cache
- deterministic inference
- disaggregation
- context parallel

这件事特别能体现 SGLang 的性能观：

它不是“尽量把所有优化都打开”，而是把不同优化视为有约束关系的策略组合。

换句话说，CUDA Graph 在这里不是一个总是正收益的开关，而是一种只有在当前系统条件允许时才成立的执行优化。

这也是成熟运行时和 demo 级优化最大的差别之一。

## 自动启发式：配置不是让用户猜，而是让系统先估一个合理值

`server_args.py` 里还有一类很有意思的逻辑，就是根据 GPU 容量自动推导：

- `chunked_prefill_size`
- decode graph 的 `max_bs`
- `mem_fraction_static`

它的思路非常直接：

- GPU 显存越大，一般可承载的 chunked prefill 和 graph batch size 也越大
- 但激活和 graph buffer 也会占内存，所以 KV 池不能贪满

于是 SGLang 会根据显存档位先做一个经验型估计。

这件事的工程收益很实际：

- 普通用户不必从零手调每个性能参数
- 系统可以先给出一个相对合理的初始解
- 高级用户再在此基础上针对具体模型和硬件微调

这也是“运行时策略面板”成熟化的一部分。

## 并行不是几种 feature 平铺，而是统一拓扑抽象

接下来要看第三条主线：并行。

SGLang 的并行策略并不只是“支持 TP/DP/PP/EP”，真正值得注意的是它怎样把这些能力统一抽象起来。

`runtime_context.py` 里的 `ParallelContext` 很能说明这一点。

它把下面这些信息统一收口成一个结构化命名空间：

- `tp_size / tp_rank`
- `pp_size / pp_rank`
- `moe_ep_size / moe_ep_rank`
- `attn_tp_size / attn_tp_rank`
- `attn_cp_size / attn_cp_rank`
- `attn_dp_size / attn_dp_rank`

这说明在 SGLang 里，并行不是“某几个模块各自拿一点分布式状态”，而是整个运行时共享同一张拓扑视图。

这件事非常重要。

因为一旦性能优化同时跨越调度层、缓存层和模型层，如果并行拓扑没有统一抽象，代码很快就会变得非常混乱。

## DP Attention：并行策略甚至细到“padding 该怎么做”

再看 `dp_attention.py`，会发现 SGLang 对并行的处理远不只是“做 all-reduce”。

例如 `DpPaddingMode` 就已经在回答一个非常具体但又非常性能敏感的问题：

在 attention DP 场景下，跨 rank 聚合 token 时，到底该按：

- `MAX_LEN`
- 还是 `SUM_LEN`

来组织通信？

SGLang 的判断标准是：

- 如果是 extend in batch 且多 DP rank，优先用 `SUM_LEN`，减少不均匀分布下的 padding 浪费
- 其他情况下再按通信代价估算选择

这件事特别值得拿出来讲，因为它说明并行策略在 SGLang 里不是“有没有开某个分布式模式”，而是已经深入到了数据排布和通信成本建模。

这种细粒度设计，才是高性能运行时真正拉开差距的地方。

## `ModelRunner`：执行层的高性能来自“后端选择”和“路径选择”

虽然这一篇重点放在系统层，但最终性能还是要落到执行层。

这里最关键的对象仍然是 `ModelRunner`。

在上一篇我们已经看过它的主职责，这一篇更值得关注的是它怎样把不同执行优化组合起来。

其中最有代表性的两类入口是：

- `init_attention_backend()`
- `init_decode_cuda_graph()` / `init_prefill_cuda_graph()`

## attention backend 可插拔，本身就是一层性能架构

在 `ModelRunner.init_attention_backend()` 里，SGLang 并不是固定绑定某一个 attention kernel。

它会根据配置解析出：

- prefill backend
- decode backend

如果两者不同，还会构造 hybrid attention backend。

这件事非常关键，因为它说明 SGLang 已经接受了一个事实：

prefill 最优 backend 和 decode 最优 backend，未必是同一个。

所以它不是强行让一个 backend 统治所有阶段，而是允许：

- prefill 用更适合长上下文吞吐的路径
- decode 用更适合小步高频执行的路径

这和调度层区分 prefill/decode 的思路是完全一致的。

也就是说，SGLang 的高性能不是局部灵机一动，而是上下层在使用同一套系统假设：

prefill 和 decode 是两类不同问题，应该用不同策略处理。

## CUDA Graph 在 `ModelRunner` 里不是一刀切，而是分 decode 和 prefill

继续看 `init_decode_cuda_graph()` 和 `init_prefill_cuda_graph()`，这种思路就更明显了。

SGLang 并没有把 graph capture 写成单一开关，而是显式拆成：

- decode graph
- prefill piecewise graph

这里的意义在于：

- decode 更适合重复、小步、形态稳定的 capture
- prefill 更容易受 batch size、模型结构和特性开关影响，需要更细粒度的 piecewise 判断

所以 graph 这件事在 SGLang 里也不是“能抓就抓”，而是结合模型类型、capture size、运行时兼容性，动态决定是否启用。

这也是为什么 `server_args` 那么重，`ModelRunner` 初始化也那么重。

因为执行层优化不是单独的局部技巧，而要和整个运行时状态对齐。

## 以 `Qwen3` 为例：模型层如何尽量少知道细节，却最大化复用高性能基础设施

如果要选一个模型实现来观察“运行时基础设施是怎样被模型复用的”，`qwen3.py` 是一个很好的例子。

它有几个特别值得注意的点。

### 1. 线性层默认就是并行友好的

在 `Qwen3Attention` 里，Q/K/V projection 直接用的是：

- `QKVParallelLinear`
- `RowParallelLinear`

这说明 TP 并不是模型外面再包一层，而是模型层本身就按并行友好的算子组织。

也就是说，模型实现从一开始就在配合运行时的并行策略。

### 2. attention 直接接的是 `RadixAttention`

这件事特别关键。

因为它说明模型层并不是自己管理一套 KV cache 逻辑，而是直接接到运行时的 attention 抽象上。

这样带来的收益是：

- paged KV
- radix/prefix cache 相关语义
- 不同 attention backend 的切换

都能在更底层统一实现，而不需要每个模型各写一套。

这正是“模型最少知道 runtime 细节，却最大化复用高性能基础设施”的典型例子。

### 3. fused 路径直接把执行优化下沉到模型热路径

`Qwen3Attention.forward_prepare_aiter_fused_mrope()` 这一类逻辑展示了另一面：

当某些硬件和后端条件满足时，SGLang 也会把：

- QK norm
- mRoPE
- KV cache write

这样的步骤进一步融合进热路径里。

这说明 SGLang 的模型实现不是“纯粹保持干净抽象”，而是在合适的位置愿意吸收后端优化，只是这种优化仍然走统一 runtime 接口，而不是在每个模型里到处散落特殊分支。

### 4. `LayerCommunicator` 说明并行和通信逻辑也被抽象出来了

在 `Qwen3DecoderLayer` 里，attention 和 MLP 前后不是直接硬写残差和通信逻辑，而是通过：

- `prepare_attn()`
- `prepare_mlp()`
- `postprocess_layer()`

这样的接口完成。

这进一步说明，模型层虽然感知运行时，但感知的是一组稳定抽象，而不是一堆具体分布式细节。

这会让模型接入层更可维护，也让性能优化更容易统一演进。

## 所以，SGLang 的性能地图到底长什么样

看到这里，可以把 SGLang 的性能来源重新整理成一张更清楚的图：

```text
请求进入系统
-> Scheduler 决定接多少、先跑谁、prefill 还是 decode
-> SchedulePolicy 根据 prefix 命中潜力调整顺序
-> UnifiedRadixCache / HiCache 决定哪些前缀和 KV 可以复用
-> ModelRunner 根据当前模式选择 attention backend、graph 路径和执行方式
-> 模型层通过并行友好的线性层、RadixAttention、LayerCommunicator 复用这些能力
-> 结果回到调度循环，继续推进下一拍
```

这张图里最重要的不是“组件很多”，而是每一层都在解决不同维度的性能问题：

- 调度层解决工作集组织问题
- 缓存层解决重复计算问题
- 并行和执行层解决单批执行效率问题

这三层叠加起来，才构成了 SGLang 的高性能。

## 这篇文章最值得记住的几个判断

如果读完源码后只记住几句话，我觉得下面这几句最有价值。

### 1. SGLang 的性能核心先在 `Scheduler`，不先在 kernel

因为推理系统的瓶颈首先是工作负载如何被组织，而不是单步 forward 有多快。

### 2. cache 在 SGLang 里不是附加收益，而是调度策略的一部分

调度会主动利用 prefix match 结果来决定先跑谁，这比“顺便命中缓存”更进一步。

### 3. 并行不是几种模式并列支持，而是统一拓扑抽象

`TP/DP/PP/EP/attn-*` 被统一建模，才能让调度、缓存和模型层共享同一套视图。

### 4. 执行优化不是全开，而是受系统约束控制

CUDA Graph、hybrid backend、HiCache、LoRA、多模态这些特性之间存在真实兼容性边界，SGLang 的代码是按“组合约束”来管理它们的。

### 5. 模型层的价值不只是“把架构跑出来”，还要最大化复用运行时基础设施

像 `Qwen3` 这样的实现，正是在用最少的模型专属逻辑去接上最多的运行时能力。

## 总结

这一篇真正想回答的问题是：

SGLang 的高性能从哪里来？

答案并不是某个单独模块，而是三层协同：

- `Scheduler` 让系统知道什么时候该跑谁
- `UnifiedRadixCache` 和 HiCache 让系统知道哪些内容不必重算
- `ModelRunner`、attention backend、并行抽象和模型层实现，决定当前 batch 怎样跑得最快

所以，如果把 SGLang 的性能观再压缩成一句话，可以概括成：

它不是靠一个更快的 kernel 赢，而是靠“更聪明地组织请求、更系统地复用缓存、更统一地管理并行和执行路径”赢。

这也是为什么第 7 篇要放在第 6 篇之后来读。

因为只有先理解 `TokenizerManager`、`Scheduler`、`ModelRunner` 怎样串起来，你才能真正看见，SGLang 的性能不是零散优化点，而是整条运行时主链路共同塑造出来的结果。

下一篇，就可以继续往高级特性层走：看看 speculative decoding、PD disaggregation、LoRA 这些能力，为什么会反过来影响调度、缓存和执行器设计。
