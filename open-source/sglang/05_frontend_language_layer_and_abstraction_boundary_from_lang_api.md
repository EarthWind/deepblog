# 前端语言层长什么样：从 `lang/api.py` 看 SGLang 的抽象边界

上一篇我们站在 HTTP 服务层，分析了 SGLang 怎样把 OpenAI 兼容接口接到 SRT 运行时上。

但如果继续顺着仓库往前追，就会发现 SGLang 还有另一条很重要的入口线：它不只是一个 serving engine，也不只是一个 OpenAI-compatible server，它还试图给开发者提供一套“写生成程序”的前端语言抽象。

这套抽象最直接的入口，就是 `python/sglang/lang/api.py`。

很多人第一次看到这个文件，可能会觉得它内容不多，无非是定义了几个 `gen()`、`select()`、`system()`、`user()` 之类的辅助函数。但如果把它放回整个 `lang` 子目录来看，你会发现这里其实是 SGLang 前端语言层的门面：

- 它决定了用户能写出哪些 DSL 形态
- 它决定了哪些概念属于“前端语言”，哪些概念已经越过边界进入执行层
- 它把 `function -> IR -> interpreter -> backend` 这一整条链路的起点收口到了一个极小的 API 面上

换句话说，`lang/api.py` 虽然薄，但它正好能帮助我们看清 SGLang 的抽象边界。

## 文章定位

- 目标读者：已经理解服务层和运行时概念，想继续看 SGLang“编程接口”是如何组织的工程师
- 阅读前提：知道 SGLang 可以 `import sglang as sgl` 写程序，但还不清楚这些 API 背后对应什么内部对象
- 核心问题：`lang/api.py` 暴露了哪些核心抽象，它们如何落到 IR、解释器和 backend 上，以及前端语言层的边界到底在哪里
- 阅读收益：读完后，你应该能把 `@sgl.function`、`sgl.gen()`、`ProgramState`、`SglExpr`、`BaseBackend` 串成一条完整链路

## 重点代码路径

- `python/sglang/lang/api.py`
- `python/sglang/lang/ir.py`
- `python/sglang/lang/interpreter.py`
- `python/sglang/lang/tracer.py`
- `python/sglang/lang/backend/base_backend.py`
- `python/sglang/lang/backend/runtime_endpoint.py`
- `python/sglang/__init__.py`

## 先看一张总图

如果先不陷进细节，SGLang 前端语言层可以先记成下面这样：

```text
import sglang as sgl
-> sgl.function / sgl.gen / sgl.user / sgl.assistant
-> lang/api.py
-> SglFunction + SglExpr family
-> 用户函数执行时往 ProgramState 里追加 IR
-> interpreter.run_program()
-> StreamExecutor._execute(...)
-> backend.generate() / backend.select() / backend.generate_stream()
-> RuntimeEndpoint 或其他 backend
-> SRT / OpenAI / Anthropic / Vertex 等真实模型能力
```

这条链路里最重要的认知是：

- `api.py` 负责暴露“怎么写程序”
- `ir.py` 负责定义“程序由哪些语义节点组成”
- `interpreter.py` 负责定义“这些节点如何被执行”
- `base_backend.py` 负责定义“执行层可以向后端要求什么能力”

所以，SGLang 的前端语言并不是一个独立小语法糖，而是一层明确站在执行系统前面的编程抽象。

## `__init__.py` 先告诉你：前端语言是对外 API 的一等公民

在看 `lang/api.py` 之前，先看一下包根目录的 `python/sglang/__init__.py`，会更容易理解 SGLang 的设计取向。

这个文件直接把下面这些对象重新导出了：

- `function`
- `gen`
- `gen_int`
- `gen_string`
- `select`
- `system` / `user` / `assistant`
- `image` / `video`
- `Runtime`
- `set_default_backend`

这意味着用户通常不是写：

```python
from sglang.lang.api import gen
```

而是直接写：

```python
import sglang as sgl
```

然后把这套前端语言当成整个包的主入口来用。

这件事很重要，因为它说明在 SGLang 的产品视角里，“前端语言”不是藏在 SDK 角落里的高级特性，而是公共 API 的核心部分。

## `lang/api.py` 很薄，但它定义了用户看到的世界

`lang/api.py` 的代码量并不大，但它把用户视角下最重要的几类对象都集中收口了。

大致可以分成五组。

### 1. 程序定义入口：`function()`

最核心的入口就是：

```python
@sgl.function
def program(s, question):
    s += "Q: " + question + "\n"
    s += "A:" + sgl.gen("answer", stop="\n")
```

`api.py` 里的 `function()` 本质上做的事非常直接：把一个普通 Python 函数包成 `SglFunction`。

这一步看起来简单，但它带来了两个关键效果：

1. 用户写的仍然是 Python 函数，而不是单独学一门新语法
2. SGLang 可以在 `SglFunction` 上继续挂 `run()`、`run_batch()`、`trace()`、`cache()` 这些运行和分析能力

也就是说，SGLang 的 DSL 并不是自己发明一套 parser 去解析文本，而是借用了 Python 作为宿主语言，把“生成程序”封装成了可执行、可追踪、可批量运行的对象。

### 2. 执行入口：`Runtime()` 和 `Engine()`

`api.py` 还暴露了 `Runtime()` 和 `Engine()` 两个工厂函数，而且都故意写成了延迟导入。

这背后有一个非常明确的设计意图：前端语言层尽量不要在 import 阶段就把整套 SRT 依赖拉进来。

也就是说：

- 你可以先使用语言层抽象
- 真正需要连接本地运行时或启动引擎时，再去导入更重的模块

这个小细节很好地体现了边界意识。前端语言层不应该无条件依赖后端运行时的全部实现，否则 `lang` 就会和 `srt` 紧紧耦合，失去“前端抽象层”应有的独立性。

### 3. backend 管理入口：`set_default_backend()`、`flush_cache()`、`get_server_info()`

这几个 API 很容易被忽略，但它们恰好告诉我们：SGLang 的前端程序不是直接绑定某一种后端，而是通过 `BaseBackend` 抽象来运行的。

这里最重要的是 `set_default_backend()`。

它把 backend 写入全局配置，后面 `SglFunction.run()` 如果没有显式传 backend，就会默认拿这个全局 backend 来执行。

这意味着用户写的程序：

- 不必知道底层到底是本地 Runtime、远程 RuntimeEndpoint，还是 OpenAI/Anthropic/Vertex 之类后端
- 只需要依赖一组统一的生成原语

这是前端抽象成立的前提。如果 DSL 一开始就把具体后端写死，它就只能算某个服务的 helper，而不能算一层真正的语言接口。

### 4. 生成原语：`gen()`、`gen_int()`、`gen_string()`、`select()`

这一组是 `api.py` 最核心的内容。

表面看，`gen()` 就像是在“调用模型生成文本”。但如果看它的返回值，就会发现它根本没有真的去请求模型，而是返回一个 `SglGen` 或 `SglSelect` 对象。

这件事非常关键。

因为它说明：

- `api.py` 这一层负责的是“声明要做什么”
- 真正“什么时候执行、怎么执行、向谁执行”，是在后面的解释器和 backend 层解决的

`gen()` 做的几件事情也很能体现这一点：

- 接收采样参数，比如 `max_tokens`、`temperature`、`top_p`
- 在 `choices` 存在时直接转成 `SglSelect`
- 对 `regex` 做最基本的合法性检查
- 最终把这些信息封装成 `SglGen`

这其实是在构造一段 IR，而不是在做一次推理调用。

`gen_int()` 和 `gen_string()` 也是同样的思路。它们不是新的执行逻辑，而是对 `SglGen(dtype=int/str)` 的更友好封装。

### 5. 结构原语：角色、多模态、推理分离

`api.py` 还暴露了几组非常有代表性的结构化原语：

- `system()` / `user()` / `assistant()`
- `system_begin()` / `system_end()` 等 begin/end 形式
- `image()` / `video()`
- `separate_reasoning()`

这些 API 的共同点是：它们表达的不是“采样参数”，而是“程序结构”。

比如角色 API 对应的是聊天消息边界，多模态 API 对应的是把图像和视频并入上下文，`separate_reasoning()` 对应的是把 reasoning 内容和最终回答拆开处理。

换句话说，SGLang 前端语言层不是只提供一个 `generate(prompt)` 函数，而是在提供一套可组合的生成程序构件。

## 用户写的不是 prompt 字符串，而是在构造一棵 IR

要真正理解这层抽象，最重要的一步就是接受一个事实：

在 SGLang 里，用户写程序时虽然看起来一直在做字符串拼接，但内部其实是在不断构造 IR 节点。

先看一个很典型的测试用例：

```python
@sgl.function
def answer_mt_bench(s, question_1, question_2):
    s += sgl.system("You are a helpful assistant.")
    s += sgl.user(question_1)
    s += sgl.assistant(sgl.gen("answer_1"))
    with s.user():
        s += question_2
    with s.assistant():
        s += sgl.gen("answer_2")
```

这段代码读起来非常接近“自然的 prompt 编程”。

但如果从 `ir.py` 的角度看，里面实际对应的是：

- 常量文本节点 `SglConstantText`
- 生成节点 `SglGen`
- 选择节点 `SglSelect`
- 角色边界节点 `SglRoleBegin` / `SglRoleEnd`
- 节点序列 `SglExprList`
- 变量引用 `SglVariable`

也就是说，`s += ...` 并不是把字符串直接塞进某个 prompt buffer，而是在给解释器提交一串 `SglExpr`。

## `ir.py`：前端语言真正的语义中心

如果说 `api.py` 是门面，那 `ir.py` 才是真正定义语言语义的地方。

这里最值得优先关注的是三层对象。

### `SglSamplingParams`：生成控制参数的统一表示

`SglSamplingParams` 把采样控制项集中成一个 dataclass，包括：

- `max_new_tokens`
- `temperature`
- `top_p`
- `top_k`
- `min_p`
- `stop`
- `stop_token_ids`
- `regex`
- `json_schema`

更关键的是，它提供了多套转换方法：

- `to_openai_kwargs()`
- `to_vertexai_kwargs()`
- `to_anthropic_kwargs()`
- `to_litellm_kwargs()`
- `to_srt_kwargs()`

这一下就把抽象边界说得很清楚了：

- 前端语言层只维护一套统一的采样语义
- 不同后端各自负责把这套语义翻译成目标接口需要的参数形式

这就是典型的“前端统一语义，后端各自适配”。

### `SglFunction`：DSL 程序的载体

`SglFunction` 不只是一个 decorator 包装结果，它几乎就是 SGLang 前端程序的主对象。

它负责：

- 解析用户函数签名
- 保存绑定参数 `bind_arguments`
- 提供 `run()`、`run_batch()`、`trace()`、`cache()`
- 在 `__call__()` 里根据当前是否处于 tracing scope 决定走执行还是走追踪

这说明 `@sgl.function` 的真正效果不是“做点语法糖”，而是把一段 Python 代码提升成一个可以被解释器和 tracer 理解的程序对象。

### `SglExpr` 家族：语言层和执行层之间的中间表示

`SglExpr` 及其子类定义了 SGLang 前端语言能表达的所有基本动作。

比如：

- `SglGen` 表示一次生成请求
- `SglSelect` 表示有限候选选择
- `SglImage` / `SglVideo` 表示多模态输入
- `SglSeparateReasoning` 表示推理内容后处理
- `SglVarScopeBegin` / `SglVarScopeEnd` 表示变量作用域截取
- `SglConcateAndAppend`、`SglCommitLazy` 则已经开始接近执行优化语义

这里最值得注意的一点是：IR 里既有“用户能直观看懂的语义”，也开始出现“为执行器准备的控制语义”。

这就意味着 SGLang 的 IR 不是一个只为了展示的 AST，而是一个会直接进入执行流程的中间层。

## `ProgramState` 让 DSL 看起来像在“写对话”

前端语言好不好用，不只取决于有没有 `gen()`，还取决于用户写程序时的手感。

这部分主要由 `interpreter.py` 里的 `ProgramState` 提供。

用户在函数里拿到的第一个参数 `s`，本质上就是 `ProgramState`。

它提供了几类非常关键的操作：

- `__iadd__`：支持 `s += expr`
- `system()` / `user()` / `assistant()`：支持角色作用域
- `var_scope()`：支持捕获一段生成片段
- `fork()` / `copy()`：支持分支执行
- `text()` / `messages()`：读取最终文本或消息
- `__getitem__`：支持 `s["answer"]` 取变量

这也是为什么 SGLang 的 DSL 看起来不像在“堆 JSON 请求”，而像在写一个小型生成程序。

从抽象角度看，`ProgramState` 做了一层非常聪明的封装：

- 对用户，它暴露的是一种接近 prompt 编排的编程体验
- 对内部，它只是不断把表达式提交给 `StreamExecutor`

所以，`ProgramState` 其实是语言层语法体验和执行层机制之间的一层适配器。

## `interpreter.py`：真正把 IR 变成执行过程

如果说 `api.py` 解决的是“怎么写”，`ir.py` 解决的是“写出来是什么”，那么 `interpreter.py` 解决的就是“这些东西怎么跑”。

### `run_program()`：从 `SglFunction` 进入执行

`SglFunction.run()` 最终会进入 `run_program()`。

这一步主要做了三件事：

1. 确定 backend
2. 创建 `StreamExecutor`
3. 创建 `ProgramState`，然后执行用户函数

也就是说，SGLang 并不是先把整棵 IR 完整构造出来再统一解释，而是在用户函数运行的过程中，边构造表达式、边提交给执行器。

这是一种“宿主语言驱动 + 内部解释执行”的模型。

### `StreamExecutor`：真正的执行核心

`StreamExecutor` 是 `interpreter.py` 里最关键的对象。

它内部维护了很多运行状态：

- `text_`：当前累计文本
- `messages_`：按聊天格式组织的消息
- `variables`：命名生成结果
- `meta_info`：logprob 等附加信息
- `images_`：多模态上下文
- `stream_text_event` / `stream_var_event`：流式事件

而且它的 `_execute()` 明确按 IR 节点类型分发：

- `SglGen` -> `_execute_gen()`
- `SglSelect` -> `_execute_select()`
- `SglRoleBegin` / `SglRoleEnd` -> 角色边界处理
- `SglImage` / `SglVideo` -> 多模态编码处理
- `SglSeparateReasoning` -> 推理内容拆分

换句话说，解释器这一层真正定义了每种语言构件的运行语义。

### 解释器掌握的是程序状态，不是模型细节

这里有个非常值得注意的边界点：

`StreamExecutor` 知道如何维护文本、消息、变量和流式状态，但它并不知道模型怎么推理、KV cache 怎么管理、调度器怎么批处理。

当它真的需要模型能力时，只会调用 backend 抽象方法，比如：

- `backend.generate(...)`
- `backend.generate_stream(...)`
- `backend.select(...)`
- `backend.concatenate_and_append(...)`

这就是这篇文章最想强调的抽象边界：

- 前端语言层和解释器层负责“程序语义”
- backend 层负责“能力实现”

解释器不会直接操作 SRT 的 scheduler，也不会直接碰 HTTP schema。它只依赖一组很小的 backend 接口。

## `tracer.py`：为什么前端语言层还能做追踪和前缀缓存

只看 `api.py` 和 `interpreter.py`，你会觉得 SGLang 的 DSL 已经很完整了。但 `tracer.py` 又进一步说明：这套前端语言不是只能执行，还可以被“分析”。

`SglFunction.trace()` 会进入 `trace_program()`，而 tracer 使用的是 `TracerProgramState`。

这个对象和真正执行时的 `ProgramState` 很像，但目标不同：

- 它不去真正请求模型
- 它记录节点依赖关系
- 它维护变量引用关系
- 它可以把节点展平，提取 prefix

这里最典型的例子是 `extract_prefix_by_tracing()`。

它会用 dummy arguments 执行一次 tracing，然后一直收集前缀里的 `SglConstantText`，直到碰到第一个无法静态确定的动态节点为止。

接着，`interpreter.py` 里的 `cache_program()` 就能利用这个结果，让 backend 预缓存公共前缀。

这件事说明什么？

说明 SGLang 的前端语言层不是“执行前的装饰语法”，而是一种可以被静态分析一部分结构的程序表示。

这也是它比“单纯 prompt 模板库”更进一步的地方。

## `BaseBackend` 才是语言层真正对后的边界

前面几层串起来之后，就可以来看最核心的边界定义了：`python/sglang/lang/backend/base_backend.py`。

这个文件几乎可以看成“前端语言层对后端世界的接口契约”。

它要求后端实现的核心能力并不多：

- `generate()`
- `generate_stream()`
- `select()`
- `concatenate_and_append()`
- `fork_program()`
- `commit_lazy_operations()`
- `flush_cache()`
- `get_server_info()`

这组接口很有代表性，因为它告诉我们，前端语言层并不关心后端到底是什么形态。它只关心后端能不能完成下面这些抽象动作：

- 根据当前程序状态继续生成
- 做流式生成
- 在多个候选里做决策
- 支持缓存、拼接、分支等执行优化

这就是边界真正“干净”的地方。

语言层既没有把自己绑死在 HTTP 上，也没有把自己绑死在某个本地 executor 上。它只依赖 `BaseBackend`。

## `RuntimeEndpoint`：边界后面可以是 HTTP 运行时

有了 `BaseBackend`，再看 `runtime_endpoint.py` 就很容易理解了。

`RuntimeEndpoint` 是 `BaseBackend` 的一个具体实现。它做的事情并不神秘：

- 初始化时访问 `/get_model_info`
- `generate()` 时调用 `/generate`
- `generate_stream()` 时消费流式 `/generate`
- `flush_cache()` 时调用 `/flush_cache`
- `get_server_info()` 时调用 `/server_info`

也就是说，从前端语言层的角度看，本地程序最终完全可以落到一个 HTTP server 上执行。

更进一步，`Runtime` 这个包装类甚至可以在 Python 进程里直接拉起本地 HTTP server，然后把它包成一个 backend 暴露给前端语言层使用。

这一步的意义非常大：

- 对用户来说，前端程序调用方式几乎不变
- 对 SGLang 来说，语言层与运行时之间通过 backend 契约解耦了

因此，`lang/api.py` 代表的前端语言边界，并不是“止步于 Python 内存对象”，而是可以自然跨过进程、跨过 HTTP，接到真正的 SRT 系统上。

## 所以，`lang/api.py` 的抽象边界到底在哪里

看到这里，可以把这个问题回答得更具体一些。

`lang/api.py` 的边界，不在于“它是不是薄文件”，而在于它把用户能直接操作的概念限定在了下面这些层面：

- 程序定义：`function`
- 生成原语：`gen`、`select`
- 结构原语：角色、多模态、推理分离
- backend 选择：`Runtime`、`Engine`、`set_default_backend`

一旦越过这层，内部就会进入另一套世界：

- `SglFunction` 和 `SglExpr` 表示程序
- `ProgramState` 和 `StreamExecutor` 解释程序
- `BaseBackend` 定义后端契约
- `RuntimeEndpoint`、OpenAI、Anthropic、Vertex 等实现具体能力

所以，`api.py` 可以被看成一条非常清晰的“前后端分界线”：

- 它之前，是开发者写生成程序的世界
- 它之后，是 SGLang 把程序编译成执行动作、再把动作交给后端的世界

这也是为什么这一层值得单独拿出来看。因为理解了它，你就会知道 SGLang 的核心价值并不只是“跑得快”，还包括“把生成任务组织成程序”的能力。

## 把整条前端语言链再串一次

现在可以把这篇文章最重要的链路重新整理成下面这样：

```text
用户写 Python 函数
-> @sgl.function 包装成 SglFunction
-> 函数体里调用 sgl.gen / sgl.select / sgl.user / sgl.assistant
-> 这些 API 生成 SglExpr 节点
-> ProgramState 通过 s += ... 把节点提交给 StreamExecutor
-> StreamExecutor 按节点类型解释执行
-> 需要模型能力时调用 BaseBackend 接口
-> RuntimeEndpoint 等具体 backend 把请求翻译到 HTTP / OpenAI / Anthropic / Vertex / SRT
-> 返回结果再写回 text、messages、variables、meta_info
```

这条链路背后，对应着三层清晰分工：

- 前端语言层：提供可组合的编程抽象
- 中间表示与解释层：把抽象变成可执行语义
- backend 与运行时层：把语义落到真实模型能力

## 总结

`python/sglang/lang/api.py` 表面上只是一个公共 API 文件，但它其实是理解 SGLang 前端语言层最好的切口。

从这个文件往下看，可以清楚看到 SGLang 的设计不是“给推理服务补几段 Python helper”，而是认真做了一层生成程序抽象：

- `function()` 把 Python 函数提升成可执行程序对象
- `gen()`、`select()`、角色原语和多模态原语把用户意图表达成 IR
- `ProgramState` 让 DSL 保持接近自然的编程体验
- `interpreter.py` 定义这些原语真正如何执行
- `tracer.py` 让程序还能被分析和做前缀缓存
- `BaseBackend` 则把语言层与具体运行时隔开

所以，从 `lang/api.py` 往下看，SGLang 的抽象边界可以概括成一句话：

它把“生成式应用开发”抽象成了一套前端语言，把“模型推理执行”抽象成了一套后端契约，中间通过 IR 和解释器衔接起来。

理解了这条边界，后面再去看 `TokenizerManager`、`Scheduler`、`ModelRunner`，视角就会更完整。因为你已经知道，在运行时真正开始调度之前，SGLang 先把用户写的 Python 程序变成了一套可执行的生成语义。
