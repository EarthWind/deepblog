# HTTP 服务层是如何搭起来的：SGLang 的 OpenAI 兼容接口实现

上一篇我们已经把 SGLang 的服务启动主链路走了一遍，也知道默认 HTTP 模式最终会进入 `python/sglang/srt/entrypoints/http_server.py`。

但走到这里，新的问题才刚刚开始：

SGLang 对外暴露的 OpenAI 兼容接口，到底是怎样搭起来的？一个 `POST /v1/chat/completions` 请求进入系统之后，是如何从 FastAPI 路由一路走到内部运行时的？

很多项目在讲“OpenAI-compatible API”时，给人的感觉像是做了一层协议转发：把外部 JSON 收进来，稍微改改字段，再发给内部服务。但如果你认真看 SGLang 的实现，会发现这里远不只是一个薄薄的 adapter。

SGLang 的 HTTP 服务层，实际上承担了四类职责：

- 初始化 FastAPI 应用和生命周期
- 组织 OpenAI、Ollama、Anthropic、SageMaker 等多套兼容路由
- 把外部请求协议转换成 SRT 运行时能理解的内部请求对象
- 把流式输出、结构化输出、工具调用、多模态输入这些能力，统一收口到同一层服务抽象里

换句话说，SGLang 的 OpenAI 兼容层不是“简单转发”，而是运行时能力对外暴露的统一出口。

## 文章定位

- 目标读者：已经理解服务启动链路，准备继续阅读 HTTP 层与接口层实现的工程师
- 阅读前提：知道默认 HTTP 模式会进入 `http_server.py`，但还不清楚 FastAPI、OpenAI schema 和内部运行时之间如何衔接
- 核心问题：SGLang 的 HTTP 服务层怎样组织应用、路由和协议适配，以及它为什么不是一个纯粹的代理层
- 阅读收益：读完后，你应该能把 `http_server.py`、`protocol.py`、`serving_*` 和 `TokenizerManager` 串成一条接口主链

## 重点代码路径

- `python/sglang/srt/entrypoints/http_server.py`
- `python/sglang/srt/entrypoints/openai/protocol.py`
- `python/sglang/srt/entrypoints/openai/serving_base.py`
- `python/sglang/srt/entrypoints/openai/serving_completions.py`
- `python/sglang/srt/entrypoints/openai/serving_chat.py`
- `python/sglang/srt/managers/io_struct.py`

## 先看一张总图

如果先不展开细节，SGLang 的 HTTP 接口主链路可以先记成下面这样：

```text
launch_server.py
-> http_server.launch_server()
-> Engine._launch_subprocesses()
-> _setup_and_run_http_server()
-> FastAPI app + lifespan
-> /v1/chat/completions 等路由
-> OpenAIServingChat / OpenAIServingCompletion 等 handler
-> _convert_to_internal_request(...)
-> GenerateReqInput / EmbeddingReqInput
-> TokenizerManager
-> Scheduler / ModelRunner
```

这张图里最重要的不是“有多少层”，而是每一层的职责非常明确：

- `http_server.py` 负责搭应用和挂路由
- `protocol.py` 负责定义外部接口契约
- `serving_*` 负责做协议适配和结果封装
- `TokenizerManager` 往后才是真正的 SRT 运行时

理解了这个分层，再看具体代码就会清楚得多。

## `http_server.py` 是一个真正的服务入口，不只是路由文件

很多 Web 项目的入口文件只做两件事：创建 app，然后挂一组路由。但 SGLang 的 `http_server.py` 明显承担了更多职责。

它至少做了三层事情：

1. 定义 FastAPI 应用和生命周期
2. 定义对外暴露的兼容接口
3. 在启动时把运行时对象注入到应用状态里

也就是说，这个文件并不是单纯的“路由表”，而是 HTTP 服务层的总装配点。

## `lifespan()`：应用真正开始工作的地方

在 `http_server.py` 里，最值得优先看的不是某个具体 `/v1/*` 路由，而是 `lifespan()`。

原因很简单：SGLang 的 handler 不是在模块 import 时静态构造好的，而是在应用生命周期启动时，结合当前运行时状态动态初始化的。

`lifespan()` 里大致做了这些事：

- 判断当前是单 tokenizer 模式还是 multi-tokenizer 模式
- 根据 `server_args` 初始化 metrics 和 tracing
- 构造各类 OpenAI serving handler
- 构造 Ollama 和 Anthropic 兼容 handler
- 在需要时初始化 tool server
- 尝试初始化 `/v1/responses` 对应的 handler
- 执行 warmup

这里最重要的一个动作是：把一系列 handler 放进 `fast_api_app.state`。

包括：

- `openai_serving_completion`
- `openai_serving_chat`
- `openai_serving_embedding`
- `openai_serving_classify`
- `openai_serving_score`
- `openai_serving_rerank`
- `openai_serving_tokenize`
- `openai_serving_detokenize`
- `openai_serving_transcription`
- 可选的 `openai_serving_responses`

这一层设计非常关键，因为它说明路由函数本身被刻意写得很薄，真正的处理逻辑都被转移到了专门的 handler 对象里。

换句话说，SGLang 把“Web 框架职责”和“协议适配职责”分开了。

## app 初始化很简单，但它背后的依赖并不简单

FastAPI 应用的创建本身很直接：

```python
app = FastAPI(
    lifespan=lifespan,
    openapi_url=None if get_bool_env_var("DISABLE_OPENAPI_DOC") else "/openapi.json",
)
```

后面再统一挂上 `CORSMiddleware`，并 `include_router(v1_loads_router)`。

表面看起来，这和普通 FastAPI 项目差别不大；但别被这层表象骗了。SGLang 的 HTTP 服务并不是一个“自给自足”的 web app，它高度依赖前面启动链路里已经初始化好的运行时对象。

前一篇我们讲过，`http_server.launch_server()` 在真正跑起 HTTP 服务前，会先调用：

- `Engine._launch_subprocesses(...)`

然后 `_setup_and_run_http_server(...)` 会把这些对象收进 `_GlobalState`，再注入到 app 可访问的全局上下文里。

也就是说，当前这个 FastAPI app 的底座不是数据库连接池、ORM session 或某个业务 service，而是：

- `TokenizerManager`
- `TemplateManager`
- `scheduler_info`

这件事直接决定了后面 OpenAI 兼容层的风格。它不是一个“代理到另一个 HTTP 服务”的接口层，而是直接站在 SRT 运行时上面。

## `_setup_and_run_http_server()`：把运行时和 Web 服务接起来

如果 `lifespan()` 是 HTTP 应用开始工作的地方，那么 `_setup_and_run_http_server()` 就是“运行时对象”和“FastAPI 应用”真正接上的地方。

这个函数主要做了几件事：

- 设置 `_GlobalState`
- 把 `subprocess_watchdog` 绑定到 `tokenizer_manager`
- 视配置添加 metrics middleware
- 按单 tokenizer 或多 tokenizer 模式准备 app 所需上下文
- 视配置挂载 API key 中间件
- 最后用 `uvicorn` 或 `granian` 启动服务

这里有两个特别值得注意的点。

### 1. 单 tokenizer 和多 tokenizer 是两套启动形态

如果 `tokenizer_worker_num == 1`：

- `app` 会直接持有 `server_args`
- 也会直接持有 warmup 所需参数
- API key 中间件也只在这个模式下直接加到 app 上

如果 `tokenizer_worker_num > 1`：

- 参数不会直接挂在 app 上
- 而是要写入共享内存，供多个 worker 读取
- `uvicorn.run(...)` 也会改成多 worker 形式

这说明 HTTP 层本身也并不是固定形态，而是会根据 tokenizer 并发模式切换启动方式。

### 2. HTTP/1.1、HTTP/2、SSL refresh 也在这里分流

SGLang 在这里会根据配置决定：

- 是否启用 `granian` 跑 HTTP/2
- 是否启用 `uvicorn` 的 SSL refresh 逻辑
- 是否直接走普通 `uvicorn.run(app, ...)`

所以，`http_server.py` 既是协议层入口，也是 HTTP 运行形态的选择点。

## `/v1/*` 路由为什么都写得很薄

真正读到路由定义时，你会发现 SGLang 的 OpenAI 路由函数几乎都很短。

比如：

- `/v1/completions`
- `/v1/chat/completions`
- `/v1/embeddings`
- `/v1/classify`
- `/v1/tokenize`
- `/v1/detokenize`
- `/v1/score`
- `/v1/rerank`

它们的共同模式几乎都是：

1. 让 FastAPI/Pydantic 先把请求解析成协议对象
2. 从 `raw_request.app.state` 里取出对应 handler
3. 调用 `handle_request(...)`

这是一种非常干净的组织方式。因为路由函数只做 HTTP 层最表面的事情：

- 接收请求
- 选择 handler
- 返回结果

而更复杂的逻辑，比如：

- 参数校验
- prompt 构造
- sampling 参数组装
- streaming 还是 non-streaming 的处理
- 结果格式回写成 OpenAI 风格

都被收敛到了 `serving_*` 模块里。

这让 HTTP 层保持了很好的可读性，也避免所有逻辑都堆在 `http_server.py` 里。

## `protocol.py`：OpenAI 兼容层的契约中心

路由函数之所以能写得这么薄，一个重要前提是：请求和响应 schema 已经被集中定义好了。

这个工作主要由 `python/sglang/srt/entrypoints/openai/protocol.py` 完成。

这里最值得注意的是，SGLang 并不是只定义了“最小可用”的几个字段，而是把 OpenAI 兼容层当成一套完整协议来维护。

### `CompletionRequest`

`CompletionRequest` 既包含 OpenAI 传统 completion 接口的常见字段，比如：

- `model`
- `prompt`
- `max_tokens`
- `temperature`
- `top_p`
- `stream`

也包含大量 SGLang 自身扩展字段，比如：

- `top_k`
- `min_p`
- `json_schema`
- `regex`
- `ebnf`
- `stop_token_ids`
- `lora_path`
- `bootstrap_host`
- `routed_dp_rank`

这非常重要，因为它说明 SGLang 的兼容层不是为了“强行模仿 OpenAI”，而是在保持兼容外壳的同时，把运行时特性也安全地暴露出来。

### `ChatCompletionRequest`

`ChatCompletionRequest` 更能体现这一点。

除了标准 chat completion 字段之外，它还支持：

- `tools`
- `tool_choice`
- `parallel_tool_calls`
- `reasoning_effort`
- `chat_template_kwargs`
- `continue_final_message`
- 多模态相关控制字段

也就是说，SGLang 并没有把 chat completion 理解成“字符串进、字符串出”的简单接口，而是把它视为一个能承接模板、工具调用、推理模式、多模态上下文的统一请求模型。

### schema 集中定义的收益

把这些请求和响应模型集中放在 `protocol.py` 里，有三个很直接的好处：

- HTTP 层拿到的是统一、强类型的请求对象
- handler 层可以专注做适配，不需要手写大量散乱字段解析
- SGLang 扩展能力可以在协议层被明确表达，而不是偷偷塞在某个未文档化的字段里

所以，`protocol.py` 不只是“Pydantic 模型文件”，而是整个兼容接口层的契约中心。

## `OpenAIServingBase`：兼容层真正的抽象起点

在具体的 `serving_chat.py`、`serving_completions.py` 之前，更值得先看的其实是 `serving_base.py`。

原因是：SGLang 并不是为每个接口各写一套完全独立的处理流程，而是先抽象出了一个共同处理骨架。

`OpenAIServingBase.handle_request()` 的基本流程很清楚：

1. 记录接收时间
2. 调用 `_validate_request(request)` 做校验
3. 记录原始 OpenAI 请求
4. 调用 `_convert_to_internal_request(...)` 转成内部请求对象
5. 根据 `request.stream` 分发到 streaming 或 non-streaming 路径
6. 捕获异常并统一返回 OpenAI 风格错误

这层抽象非常关键，因为它把 OpenAI 兼容接口的大部分共性流程都固定下来了。

于是，各具体 handler 只需要关心两件事：

- 这个请求怎样校验
- 这个请求怎样映射成内部 `GenerateReqInput` 或 `EmbeddingReqInput`

这样的结构既减少了重复，也让不同接口之间的行为更一致。

## 从 `/v1/completions` 看一条最典型的适配链

想理解兼容层如何衔接运行时，`serving_completions.py` 是一个很好的入口，因为它比 chat 路径简单，但已经足够完整。

`OpenAIServingCompletion._convert_to_internal_request()` 大致做了这些事：

- 处理 `prompt`
- 如果配置了 completion template，就先构造模板化 prompt
- 根据 `echo` 和 `logprobs` 推导日志概率相关参数
- 调用 `_build_sampling_params()` 组装采样参数
- 根据 `prompt` 类型决定走 `text` 还是 `input_ids`
- 从请求头提取自定义 labels 和路由信息
- 解析 LoRA adapter
- 最终构造 `GenerateReqInput`

这里最重要的认知是：

`/v1/completions` 并不是把请求“转发”给一个下游 HTTP 接口，而是直接把 OpenAI 请求翻译成 SRT 运行时原生能理解的对象。

真正的桥梁就是：

- `GenerateReqInput`

一旦到这一步，请求就已经脱离了“OpenAI 接口层”的语义，进入了内部运行时语义。

## 为什么说 completion handler 不是简单字段映射

只看 `_build_sampling_params()` 就能发现，completion handler 做的事情远比字段 rename 复杂。

它需要把外部请求解释成运行时真正关心的生成控制项，比如：

- `max_new_tokens`
- `stop`
- `stop_token_ids`
- `top_p`
- `top_k`
- `min_p`
- `presence_penalty`
- `frequency_penalty`
- `regex`
- `json_schema`
- `custom_params`
- `sampling_seed`

同时，它还要处理 `response_format` 这种更高层的约束，把 `json_object` 或 `json_schema` 映射成内部结构化生成约束。

这说明 SGLang 的兼容层其实承担着“协议解释器”的职责，而不是机械转发器。

## `/v1/chat/completions` 更能体现这层的复杂度

如果说 completion 是较简单的一条链，那么 chat completion 就真正体现了 SGLang OpenAI 兼容层的工程厚度。

`OpenAIServingChat` 在初始化时就会关心很多运行时上下文：

- 默认采样参数
- `tool_call_parser`
- `reasoning_parser`
- 当前模型是不是 `gpt_oss`
- 当前 tokenizer 是否自带 chat template

这意味着 chat handler 并不是一个纯协议层对象，它从一开始就和模型配置、模板能力、工具调用能力紧密耦合。

### chat handler 要解决的核心问题

相比 completion，chat completion 额外要处理的问题包括：

- `messages` 如何编码成最终 prompt
- 是否要应用 chat template
- `tools` 和 `tool_choice` 如何进入 prompt 构造过程
- `reasoning_effort` 如何影响模板参数
- `continue_final_message` 如何改变最后一轮消息处理
- 多模态消息里的 image/video/audio 如何并入请求

这也是为什么 chat 路径的代码明显更复杂。因为它本质上是在做“对话语义 -> 生成输入”的编译工作。

## 从 `messages` 到 `prompt_ids`：这一步才是 chat 的核心

在 `serving_chat.py` 里，最值得关注的不是某个返回格式函数，而是消息编码过程。

SGLang 会根据模型和模板情况决定：

- 使用哪种 chat encoding spec
- 是否调用 tokenizer 的 `apply_chat_template(...)`
- 是否需要额外拼接 assistant prefix
- 如何处理 tools
- 如何处理 reasoning 相关模板参数

最终目标不是直接返回字符串，而是产出：

- `prompt`
- `prompt_ids`
- 多模态数据
- `stop` 信息

然后这些结果再被封装进内部请求对象。

这一步非常能说明 SGLang 兼容层的本质：它不是在“搬运 OpenAI 字段”，而是在把一个外部会话协议编译成运行时能执行的输入表示。

## `GenerateReqInput` / `EmbeddingReqInput` 是 HTTP 层和运行时之间的桥

前面几次已经提到这两个对象，但在这一篇里可以把它们的重要性再强调一次。

对于 OpenAI 兼容接口来说，真正把“接口层”和“运行时层”接起来的，不是某个 JSON 字典，也不是某个 HTTP client，而是内部请求对象：

- `GenerateReqInput`
- `EmbeddingReqInput`

这些对象里承载的是运行时真正关心的信息：

- 输入文本或 token ids
- 采样参数
- 流式输出开关
- 路由信息
- LoRA / disaggregation / DP 相关信息
- 结构化生成和自定义 logit processor 等高级能力

一旦进入这个层次，请求就已经不再属于“OpenAI 兼容 API”这个概念，而属于 SRT 运行时本身。

这也是为什么说兼容层不是简单代理。代理通常把协议边界保留到最后，而 SGLang 是在 HTTP 层就把请求彻底翻译成内部语义。

## streaming 支持也说明这不是普通 CRUD 接口

`OpenAIServingBase.handle_request()` 在把请求转成内部对象之后，会根据 `request.stream` 决定进入：

- `_handle_streaming_request(...)`
- `_handle_non_streaming_request(...)`

这个分支看起来很普通，但它的意义其实很大。

因为对生成式系统来说，“流式返回”不是一个简单的传输细节，而是整个运行时推进过程的一部分。SGLang 在兼容层就把这件事纳入统一抽象，说明 HTTP 服务层本身已经深度理解生成系统的执行模型。

同理，`/v1/responses`、`/v1/realtime`、`/v1/audio/transcriptions` 这些接口也都不是普通 REST CRUD，而是在同一兼容层下暴露不同交互模式。

## 兼容路由远不止 OpenAI

如果继续往下看 `http_server.py`，还会发现它不只挂了 OpenAI 路由。

同一个 HTTP 服务层里，还组织了：

- Ollama 兼容接口
- Anthropic 兼容接口
- SageMaker 风格接口
- Vertex 风格接口

这进一步说明，SGLang 的 HTTP 服务层不是“某个 OpenAI demo server”，而是一个统一的协议适配层。

从工程收益看，这种组织方式至少有两个好处：

- 不同协议可以共享同一套底层运行时能力
- 服务观测、鉴权、中间件、warmup 等横切能力可以统一管理

也就是说，SGLang 是把“协议兼容”当成入口层问题来解决，而不是为每套外部协议分别造一套后端。

## 为什么说兼容层不是“简单转发”

现在可以回到这篇文章最开始的问题了：为什么说 SGLang 的兼容层不是简单转发？

至少有四个原因。

### 1. 它持有自己的协议模型

`protocol.py` 不是简单照抄外部文档，而是定义了一套强类型、可扩展、带 SGLang 扩展能力的请求/响应契约。

### 2. 它会做真实的请求解释和改写

handler 不只是把 JSON 原样送下去，而是要：

- 解析 messages
- 应用模板
- 解释工具调用
- 处理 reasoning 参数
- 组装采样参数
- 生成内部请求对象

### 3. 它直接连接运行时，而不是连接另一个 HTTP 后端

兼容层下游不是另一个“模型 HTTP 服务”，而是 `TokenizerManager` 及其背后的 `Scheduler`、`ModelRunner`。

### 4. 它承担流式输出和高级生成特性的统一出口

结构化输出、LoRA、路由、streaming、tool calling、多模态输入，都在这一层被统一纳入接口抽象。

所以，这层的本质更接近“运行时 API 门面”，而不是“协议中转站”。

## 把整条 HTTP 主链再串一次

看到这里，可以把第 4 篇最重要的链路重新整理成下面这张图：

```text
run_server(server_args)
-> http_server.launch_server()
-> Engine._launch_subprocesses()
-> _setup_and_run_http_server()
-> FastAPI app 启动
-> lifespan() 初始化各类 serving handler
-> /v1/chat/completions 等路由接收请求
-> OpenAIServingBase.handle_request()
-> _convert_to_internal_request(...)
-> GenerateReqInput / EmbeddingReqInput
-> TokenizerManager
-> Scheduler
-> ModelRunner
-> 返回 OpenAI 风格结果或流式输出
```

这条链路背后，其实对应着三个层面的分工：

- Web 应用层：FastAPI、middleware、路由、生命周期
- 协议适配层：OpenAI schema、handler、请求转换、响应封装
- 运行时执行层：TokenizerManager、Scheduler、ModelRunner

也正因为分层清晰，SGLang 才能同时做到：

- 对外保留熟悉的 OpenAI 风格接口
- 对内继续演进自己的高性能运行时

## 总结

SGLang 的 HTTP 服务层看起来是一个 FastAPI 应用，但它真正的价值远不止“把几个 `/v1/*` 路由跑起来”。

从代码结构上看，它做的是一套完整的接口层装配：

- `http_server.py` 负责搭建应用、生命周期和服务入口
- `protocol.py` 负责定义外部协议契约
- `serving_base.py` 提供统一 handler 骨架
- `serving_completions.py`、`serving_chat.py` 等模块负责把 OpenAI 请求转换成内部运行时请求
- `GenerateReqInput` / `EmbeddingReqInput` 则把接口层真正接到了 SRT 运行时上

所以，SGLang 的 OpenAI 兼容层并不是一个“简单转发”的薄代理，而是一个把外部协议、生成语义和内部运行时连接起来的统一出口。

理解了这一层，后面再去看 `TokenizerManager`、`Scheduler`、`ModelRunner` 的协作，视角就会更完整。因为你已经知道，用户发来的一个 OpenAI 风格请求，究竟是怎样被翻译成运行时内部任务的。

下一篇，就可以继续往更上游走：从 `lang/api.py` 出发，看看 SGLang 作为前端语言层时，到底提供了怎样的抽象边界。
