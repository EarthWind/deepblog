# 详解 vLLM：由浅入深的博客选题清单

本文档基于本仓库当前代码实现整理，目标不是重复官网文档目录，而是给出一条适合写技术博客的源码解读路线。整套路线按“先建立整体认知，再进入请求主链路，随后拆性能关键路径，最后再讲扩展能力和底层实现”来组织。

## 适用读者

- 想系统理解 vLLM，而不是只会使用 `vllm serve`
- 想从源码角度理解 V1 架构、调度器、KV Cache 和执行层
- 想输出一套连续的技术博客，而不是零散的笔记

## 写作原则

- 先讲问题，再讲实现，不要一上来按目录背源码
- 以“请求生命周期”作为主叙事线索
- 每篇只解决一个核心问题，避免把多个大主题揉进一篇
- 每篇都给出必读源码入口，方便继续深挖

## 建议总主线

可以把整个系列概括为一句话：

> 一个请求进入 vLLM 之后，会先经过服务入口和配置收敛，再进入 Engine Core 做统一调度，随后分配 KV Cache、派发到 Worker 执行、经过采样和输出处理返回结果，而 LoRA、多模态、分布式与插件系统则横切在整条链路之上。

---

## 第一阶段：先建立全局认知

### 1. 《vLLM 到底在解决什么问题：为什么推理系统会复杂到今天这样》

**写作目标**

解释 vLLM 不是简单的“模型推理封装”，而是围绕吞吐、延迟、显存利用率、并发调度和 API 兼容性构建的一整套推理运行时。

**建议重点**

- 什么是 continuous batching
- 什么是 prefix caching
- 为什么要多进程拆分 API Server、Engine Core、Worker
- 为什么 V1 要重构调度器、KV Cache、Worker、Sampler、API Server

**必读材料**

- `docs/README.md`
- `docs/design/arch_overview.md`
- `docs/usage/v1_guide.md`

**建议产出**

- 一张“vLLM 能力地图”
- 一张“V0 到 V1 的变化摘要图”

### 2. 《一张图看懂 vLLM V1：API Server、Engine Core、GPU Worker 如何协作》

**写作目标**

先把 V1 多进程架构讲清楚，让读者知道后面的调度、执行、缓存分别发生在哪一层。

**建议重点**

- API Server 负责 HTTP、输入处理、流式返回
- Engine Core 负责调度、KV Cache 管理、执行协调
- GPU Worker 一卡一进程，负责模型执行和设备资源管理
- 数据并行场景下还会引入 DP Coordinator

**必读材料**

- `docs/design/arch_overview.md`
- `vllm/v1/engine/core.py`
- `vllm/v1/executor/multiproc_executor.py`
- `vllm/v1/worker/gpu_worker.py`

**建议产出**

- 一张“单机 TP=4”进程图
- 一张“DP + TP”进程图

---

## 第二阶段：从入口走一遍主链路

### 3. 《一条 vllm serve 命令，如何拉起整套推理系统》

**写作目标**

解释用户最熟悉的一条命令，是如何一步步变成完整服务进程树的。

**建议重点**

- `serve` 子命令如何处理 headless、DP、external LB、hybrid LB
- `api_server_count` 是如何推导出来的
- 单进程、multi API server、headless 三种启动分支有什么不同

**必读源码**

- `vllm/entrypoints/cli/serve.py`
- `vllm/engine/arg_utils.py`
- `vllm/config/vllm.py`

**建议产出**

- 一张启动分支决策图
- 一张 CLI 参数收敛到 `VllmConfig` 的流程图

### 4. 《OpenAI 兼容接口只是表面：vLLM 服务层真正做了哪些事》

**写作目标**

说明 API Server 并不只是转发 HTTP，而是承担了输入处理、模型选择、流式协议拼装等一系列职责。

**建议重点**

- FastAPI 应用如何装配
- 请求如何映射到内部 engine client
- 流式输出、鉴权、metrics、中间件分别在哪里接入
- 为什么服务层仍然需要理解 LoRA、多模态和 renderer

**必读源码**

- `vllm/entrypoints/openai/api_server.py`
- `vllm/entrypoints/openai/engine/serving.py`

**建议产出**

- 一张 HTTP 请求进入服务层后的调用图

### 5. 《Engine Core：vLLM V1 真正的控制中枢》

**写作目标**

从初始化逻辑切入，解释为什么 Engine Core 是 V1 中最值得优先读的模块。

**建议重点**

- 为什么先建 executor，再初始化 KV Cache，再构建 scheduler
- 为什么 structured output manager 在这里初始化
- 为什么非因果 attention 会影响 chunked prefill 和 prefix caching
- 为什么 KV connector 的握手信息要在这一层汇总

**必读源码**

- `vllm/v1/engine/core.py`

**建议产出**

- 一张 Engine Core 初始化流程图
- 一张 Engine Core 对外部能力的依赖关系图

---

## 第三阶段：理解 vLLM 为什么快

### 6. 《continuous batching 在 vLLM 里到底是什么：统一调度模型详解》

**写作目标**

把“continuous batching”从口号变成可以落到代码里的调度机制。

**建议重点**

- V1 为什么把 prompt token 和 output token 放到统一预算里
- `waiting`、`skipped_waiting`、`running` 三类请求队列分别是什么
- FCFS 和 priority 调度如何切换
- speculative decoding、encoder budget、多模态预算是怎样接入同一个调度器的

**必读源码**

- `docs/usage/v1_guide.md`
- `vllm/v1/core/sched/scheduler.py`
- `vllm/v1/request.py`

**建议产出**

- 一张“单步调度决策图”
- 一个“请求状态变化时间线”

### 7. 《从 BlockPool 到 Prefix Cache：vLLM 如何把 KV Cache 做成分页系统》

**写作目标**

把 KV Cache 从“一个显存缓存”讲成“带块管理、哈希命中、回收与复用策略的内存系统”。

**建议重点**

- `KVCacheManager` 是外观层，真正块管理在 coordinator 和 block pool
- `get_computed_blocks()` 如何寻找最长 cache hit
- 为什么 cache hit 仍然可能重算最后一个 token
- `allocate_slots()` 如何同时处理新 token、已命中 token、外部 connector token、lookahead token
- `BlockPool` 如何维护 free queue 和 block hash 到 block 的映射

**必读源码**

- `vllm/v1/core/kv_cache_manager.py`
- `vllm/v1/core/block_pool.py`
- `docs/design/prefix_caching.md`
- `docs/design/paged_attention.md`

**建议产出**

- 一张 KV block 布局图
- 一张 prefix cache 命中与补算流程图

### 8. 《Sampler 不是最后的小步骤：vLLM 如何定义输出语义》

**写作目标**

解释采样逻辑为什么会影响用户可见语义，尤其是 logprobs 与采样处理顺序。

**建议重点**

- 采样前后有哪些 logits processor
- `raw_logprobs` 与 `processed_logprobs` 的区别
- 为什么 V1 的 logprobs 语义和 V0 不一样
- greedy、temperature、top-k/top-p 在代码里的实际顺序

**必读源码**

- `vllm/v1/sample/sampler.py`
- `docs/usage/v1_guide.md`

**建议产出**

- 一张采样流水线图
- 一个“同一组 logits 在不同模式下的输出差异”示意表

---

## 第四阶段：执行层与并行运行时

### 9. 《为什么 vLLM 要一张 GPU 对应一个 Worker 进程》

**写作目标**

从执行层和资源隔离的角度解释“一卡一进程”的设计动机。

**建议重点**

- world size 与 TP、PP、PCP 的关系
- worker 进程如何创建、就绪同步、建立消息队列
- 为什么要把控制面和执行面拆开
- 为什么 multiprocess 架构能更稳定地承载复杂并行模式

**必读源码**

- `vllm/v1/executor/multiproc_executor.py`
- `vllm/v1/worker/gpu_worker.py`
- `docs/design/multiprocessing.md`

**建议产出**

- 一张 executor 与 worker 的消息流图

### 10. 《真正跑模型的地方：GPUModelRunner 应该怎么读》

**写作目标**

告诉读者源码阅读时该怎样接近 vLLM 最大、最复杂的执行层文件。

**建议重点**

- 为什么 ModelRunner 是多个性能特性的汇合点
- attention backend、LoRA、多模态、sampling、CUDA graph 为何都在这里汇合
- 哪些逻辑值得单独拆读，哪些逻辑只需建立索引

**必读源码**

- `vllm/v1/worker/gpu_model_runner.py`
- `docs/design/cuda_graphs.md`
- `docs/design/attention_backends.md`

**建议产出**

- 一份“GPUModelRunner 阅读地图”
- 一张执行前准备张量与执行后收集结果的流程图

### 11. 《vLLM 如何接管并行运行时：TP、PP、DP 与更多并行形态》

**写作目标**

从运行时组织的角度，解释 vLLM 如何在 PyTorch distributed 之上构建自己的并行抽象。

**建议重点**

- `parallel_config` 如何贯穿全局
- data parallel、tensor parallel、pipeline parallel 的角色分工
- decode context parallel、prefill context parallel 为什么会影响调度和 KV
- 为什么并行配置会反过来影响 API server 和 engine core 个数

**必读源码**

- `vllm/distributed/parallel_state.py`
- `vllm/config/parallel.py`
- `docs/serving/parallelism_scaling.md`
- `docs/design/arch_overview.md`

**建议产出**

- 一张并行维度总览图

---

## 第五阶段：高级能力如何嵌入主链路

### 12. 《LoRA 在 vLLM 里为什么不是外挂，而是一等能力》

**写作目标**

说明 LoRA 并不是请求到来时简单替换权重，而是深入到了模型管理、slot 管理和执行层封装。

**建议重点**

- `LoRAModelManager` 负责什么
- adapter 的注册、激活和容量控制怎么做
- 多 LoRA 与 MoE、multimodal 的关系
- 为什么服务层也要参与 LoRA 解析

**必读源码**

- `vllm/lora/model_manager.py`
- `vllm/lora/worker_manager.py`
- `vllm/entrypoints/openai/engine/serving.py`
- `docs/features/lora.md`

**建议产出**

- 一张 LoRA 从请求到执行生效的路径图

### 13. 《多模态在 vLLM 里不是独立分支，而是从调度阶段就开始参与预算》

**写作目标**

解释多模态支持为什么不只是“多几个输入字段”，而是深入到了输入处理、预算管理和执行层。

**建议重点**

- `MultiModalRegistry` 如何按模型注册 processor
- 什么条件下模型虽然是 multimodal，但会退化为 text-only mode
- scheduler 为什么需要 encoder cache 和 multimodal budget
- 多模态处理如何影响请求生命周期

**必读源码**

- `vllm/multimodal/registry.py`
- `vllm/multimodal/inputs.py`
- `vllm/v1/core/sched/scheduler.py`
- `docs/design/mm_processing.md`

**建议产出**

- 一张多模态输入从原始数据到模型输入的转换图

### 14. 《结构化输出、推理增强与请求状态机》

**写作目标**

用较小篇幅解释结构化输出为什么要进 Engine Core，而不是只在 API 层做后处理。

**建议重点**

- request 创建时如何附着 structured output 信息
- 为什么结构化输出需要进入调度与输出处理链路
- 它与 sampling、tool use、parser 的关系是什么

**必读源码**

- `vllm/v1/request.py`
- `vllm/v1/engine/core.py`
- `vllm/parser/`

**建议产出**

- 一张请求对象字段拆解图

---

## 第六阶段：扩展性与底层性能栈

### 15. 《vLLM 为什么能支持这么多模型和平台：插件系统视角》

**写作目标**

解释 vLLM 的扩展能力不是靠主仓无限膨胀，而是依赖插件化边界。

**建议重点**

- general plugins 在何时加载
- 插件系统如何扩展模型、平台和处理器
- 为什么官方能把部分能力放到独立插件仓库

**必读材料**

- `docs/design/plugin_system.md`
- `vllm/plugins/__init__.py`
- `vllm/v1/engine/core.py`

**建议产出**

- 一张插件加载时机图

### 16. 《Python 外壳下的真性能：vLLM 的 kernel 栈怎么分层》

**写作目标**

在系列收尾时把读者从 Python 控制层带到真正的性能实现层。

**建议重点**

- Python 侧 kernel 封装与 `csrc` 自定义算子的关系
- attention、quantization、all-reduce、CPU kernel 在仓库中的位置
- 为什么阅读底层实现前，必须先理解调度和缓存

**必读材料**

- `vllm/kernels/`
- `csrc/`
- `docs/design/custom_op.md`
- `docs/design/fusions.md`

**建议产出**

- 一张“控制层 / 执行层 / kernel 层”三层结构图

---

## 推荐发布顺序

如果要真的按由浅入深连载，建议按下面顺序发布：

1. vLLM 到底在解决什么问题
2. 一张图看懂 vLLM V1 多进程架构
3. 一条 `vllm serve` 命令如何启动整套系统
4. OpenAI 兼容服务层的真实职责
5. Engine Core 为什么是 V1 的控制中枢
6. continuous batching 的统一调度模型
7. KV Cache、BlockPool 与 Prefix Cache
8. Sampler 如何定义输出语义
9. 为什么是一卡一 Worker
10. GPUModelRunner 的阅读方法
11. 并行运行时与 distributed 抽象
12. LoRA 如何接入主链路
13. 多模态如何进入预算与调度
14. 结构化输出与请求状态机
15. 插件系统与可扩展架构
16. 底层 kernel 与性能实现层

---

## 每篇文章都可以复用的固定模板

为保证系列风格一致，建议每篇都按下面结构写：

1. 这篇要回答什么问题
2. 如果不了解这个模块，会在哪些地方读不下去
3. 先给一张全景图
4. 再按关键类和关键函数往下钻
5. 最后用一次请求生命周期回到全局

---

## 一句话总结

如果只保留一个写作原则，那就是：

**不要按目录介绍 vLLM，要按请求生命周期介绍 vLLM。**

这样写出来的博客会更像“讲清系统为什么这样设计”，而不是“带读源码目录”。
