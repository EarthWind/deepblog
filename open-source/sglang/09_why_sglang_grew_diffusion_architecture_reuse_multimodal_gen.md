# SGLang 为什么会长出 Diffusion：从多模态生成子系统看架构复用

上一篇我们讨论了 SGLang 的高级特性怎样进入运行时主链路：

- `Speculative Decoding` 改执行路径
- `PD/EPD Disaggregation` 改请求生命周期
- `LoRA` 改入口解析、调度约束和模型注入

如果沿着这个方向继续往仓库里看，很快又会遇到一个更大的问题：

为什么一个最初主要被当作 LLM 推理框架来理解的项目，会在仓库里长出一整套 diffusion 子系统？

而且这套子系统还不是一个小插件，而是已经拥有：

- 独立文档
- 独立运行时目录
- 独立模型注册表
- 图像/视频生成接口
- OpenAI-compatible 服务层
- 多进程、多角色的调度与拆分能力

这篇文章想回答的，就是这个问题。

不过答案并不是“团队后来顺手又做了一个图像生成框架”这么简单。

如果只看目录，你会觉得这里像是又塞进了一个新项目：

- `docs/diffusion/`
- `python/sglang/multimodal_gen/`

但如果顺着入口、服务层和运行时层往下读，你会发现 diffusion 在 SGLang 里的成长方式，其实非常能说明这个仓库的架构特点：

它不是在 LLM runtime 旁边平行堆了一个完全无关的系统，而是在复用 SGLang 已经成熟的“入口分流、服务暴露、调度执行、运行时拆分”这套方法论，然后把模型语义从 token 生成扩展到了图像和视频生成。

所以这篇文章的重点不是介绍 diffusion 功能大全，而是回答一个更工程化的问题：

SGLang 为什么会长出 diffusion，以及它到底复用了什么、又新长出了什么？

## 文章定位

- 目标读者：已经理解 SGLang 主仓库结构，并且读过前面几篇运行时文章的工程师
- 核心问题：diffusion 子系统和 LLM 主系统之间，到底是“另起炉灶”还是“架构复用”
- 阅读收益：读完后，你应该能把 `multimodal_gen` 看成一套建立在 SGLang 系统骨架上的多模态生成运行时，而不是一个单独拼进来的目录

## 重点代码路径

- `docs/diffusion/index.md`
- `python/sglang/cli/generate.py`
- `python/sglang/cli/serve.py`
- `python/sglang/cli/utils.py`
- `python/sglang/multimodal_gen/__init__.py`
- `python/sglang/multimodal_gen/registry.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/cli/generate.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/cli/serve.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/diffusion_generator.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/http_server.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/openai/`
- `python/sglang/multimodal_gen/runtime/launch_server.py`
- `python/sglang/multimodal_gen/runtime/managers/scheduler.py`
- `python/sglang/multimodal_gen/runtime/disaggregation/roles.py`

## 先给结论：SGLang 长出 diffusion，不是因为它只想支持更多模型，而是因为它的系统骨架本来就可以复用

看完这批代码之后，我觉得最值得先记住的结论有两个。

### 第一，SGLang 里真正可复用的，不是“token 生成细节”，而是“生成系统骨架”

这个骨架至少包括：

- 顶层 CLI 入口
- 根据模型类型做入口分流
- 统一的参数解析和服务启动方式
- HTTP 服务层和 OpenAI-compatible 暴露方式
- scheduler/client/worker 的进程边界
- 动态 batching、warmup、监控、拆分部署这些运行时能力

这些东西本质上都不是 LLM 专属的。

它们属于更高一层的“生成系统工程”。

一旦这个骨架成立，SGLang 就不必把自己限制在“只会吐 token 的系统”里。

### 第二，diffusion 子系统真正新长出来的，是“模型语义”和“流水线阶段”，不是整套工程方法

也就是说，diffusion 确实引入了大量新组件，但这些新增大多集中在：

- pipeline registry
- pipeline config / sampling params
- 文本编码器、DiT、VAE、scheduler 等模型组件
- 图像/视频输入输出协议
- encoder / denoiser / decoder 这类更符合 diffusion 的角色拆分

而围绕这些组件运行的外层骨架，却大量延续了 SGLang 已经成熟的架构组织方式。

所以从架构角度看，diffusion 并不是“偏航”，反而是一次很典型的系统能力外延。

## 第一层复用：用户入口没有分裂，而是继续从 `sglang generate` 和 `sglang serve` 进入

理解 diffusion 为什么能长进来，最好的地方还是从你最熟悉的入口开始。

在第 2 篇我们已经看过，SGLang 的一个重要特点是用户入口很少。

最核心的两个命令就是：

- `sglang generate`
- `sglang serve`

到了 diffusion 这里，这件事依然成立。

也就是说，SGLang 并没有要求用户记住另一套完全不同的 CLI，而是在已有入口里做模型类型分流。

## `get_is_diffusion_model()` 暴露了一个很重要的设计思路：先统一入口，再延迟决定后端

`python/sglang/cli/utils.py` 里的 `get_is_diffusion_model()` 非常值得看。

它不是通过一个死板的显式开关判断，而是会综合多种信息来识别当前模型是不是 diffusion：

- overlay registry
- 已注册 diffusion 模型路径
- 本地目录里的 `model_index.json`
- Hugging Face / ModelScope 上的 diffusers 模型标记
- 一些已知的非 diffusers diffusion 模型模式

这段逻辑背后的意义不只在“自动识别模型”。

更重要的是，它说明 SGLang 的顶层入口是按“用户想跑哪个模型”组织的，而不是按“用户必须先选哪套系统”组织的。

这是一种很强的产品化设计：

- 用户先给出模型
- CLI 再推断该走 LLM 运行时还是 diffusion 运行时

所以 diffusion 进入 SGLang 的第一步，不是新开入口，而是接入已有的入口分流机制。

## `sglang generate` 在 diffusion 场景下的本质：离线单次任务，继续复用统一入口

顶层的 `python/sglang/cli/generate.py` 很短，但非常关键。

它做的事几乎可以一句话概括：

1. 从参数里找 `model_path`
2. 用 `get_is_diffusion_model()` 判断模型类型
3. 如果是 diffusion，就把控制权交给 `multimodal_gen.runtime.entrypoints.cli.generate`

这说明 diffusion 并没有绕开原有 CLI 框架，而是把自己嵌进了原有命令体系中。

继续看 `python/sglang/multimodal_gen/runtime/entrypoints/cli/generate.py`，会发现它也没有走“写个一次性脚本直接推理”的简单路线。

它做的关键事情是：

- 先用 `ServerArgs` 解析运行时参数
- 再按模型解析对应的 `SamplingParams`
- 最后通过 `DiffGenerator.from_pretrained(...)` 创建统一生成器

而 `DiffGenerator` 才是真正的收口点。

## `DiffGenerator` 说明 diffusion 沿用的不是“脚本式推理”，而是“服务化生成”

很多人第一次看 `sglang generate`，会默认把它想成：

- 读模型
- 读 prompt
- 直接 forward
- 保存输出

但 `DiffGenerator` 的实现说明，SGLang diffusion 不是这个思路。

`python/sglang/multimodal_gen/runtime/entrypoints/diffusion_generator.py` 里最重要的几个判断是：

- `DiffGenerator` 自己并不直接承担底层执行
- 它把自己定位成 scheduler service 的 client
- `local_mode=True` 时，它会在本地拉起 scheduler server
- 随后仍然通过 `prepare_request(...)` 和 scheduler client 把请求送进统一运行时

也就是说，在 diffusion 的离线生成场景里，SGLang 仍然优先复用“客户端 -> 调度端 -> worker”的服务化组织方式，而不是另写一条只给 CLI 用的执行链。

这件事非常重要。

因为它意味着：

- `sglang generate` 和 `sglang serve` 的后端不是两套逻辑
- 离线单次生成和在线服务，本质上共享同一套请求准备和调度执行机制

从工程收益看，这种设计的价值非常大：

- 减少两套运行时分叉
- 让离线和在线场景共享 warmup、调度、日志和 tracing 设施
- 让后续新增能力只需要接在统一运行时，而不是同时维护“脚本模式”和“服务模式”两条链

## `sglang serve` 在 diffusion 场景下的本质：顶层入口不变，后端切到 `multimodal_gen`

再看 `python/sglang/cli/serve.py`，会更清楚地感受到这种复用。

顶层 `serve()` 的逻辑仍然是：

- 先处理帮助信息
- 再判断模型类型
- 如果是 LLM，则走 `sglang.launch_server.run_server(...)`
- 如果是 diffusion，则走 `multimodal_gen.runtime.entrypoints.cli.serve`

这里最值得注意的，不只是“它能分流”。

更值得注意的是，顶层命令仍然把 diffusion 看成 SGLang 服务体系里的一个合法后端，而不是一个外部工具。

换句话说，SGLang 并没有把 diffusion 组织成：

- 一个额外安装包
- 一套额外命令
- 一份完全独立的使用心智

而是让它延续了：

`sglang serve --model-path xxx`

这个用户入口。

这是 diffusion 能真正“长在仓库里”而不是“挂在仓库边上”的第一层原因。

## 第二层复用：服务启动方式延续了 SGLang 一贯的“参数对象 + 启动分流 + 多进程 worker”结构

如果说顶层 CLI 解释了为什么用户入口没有分裂，那么 `multimodal_gen` 自己的 `serve` 入口，解释的就是 diffusion 为什么没有另起一套粗糙的服务骨架。

`python/sglang/multimodal_gen/runtime/entrypoints/cli/serve.py` 的主线很清楚：

- 先由 `ServerArgs.from_cli_args(...)` 生成统一参数对象
- 再调用 `dispatch_launch(server_args)`
- 如果需要，还能顺带拉起 WebUI

这种写法和我们前面看 LLM runtime 时已经很熟悉的风格非常像：

- 入口层做参数解析
- 真正的系统分叉交给启动层
- 参数对象作为运行时各层共享的真相源

这不是 diffusion 独有风格，而是 SGLang 一贯的系统写法。

## `dispatch_launch()` 继续复用了“同一入口下的多种部署形态”

真正值得看的，是 `python/sglang/multimodal_gen/runtime/launch_server.py`。

这里的 `dispatch_launch()` 会根据 `disagg_role` 把启动链路分成几类：

- `MONOLITHIC`
- `SERVER`
- `ENCODER`
- `DENOISER`
- `DECODER`

这段代码非常能说明 diffusion 不是“只加了几个模型文件”，而是已经成长为正式运行时。

但更重要的是，它说明 diffusion 在成长时复用的，是 SGLang 已经验证过的一个核心思想：

一个生成系统不应该只支持单一部署形态，而应该把“单体运行、头节点、拆分角色节点”都纳入统一启动框架。

LLM runtime 里我们已经见过类似思想，比如：

- 单体服务
- 不同 entrypoint
- disaggregation / gateway 协同

到了 diffusion，这种思想没有消失，只是换成了更符合 diffusion 流水线的角色切分。

## 第三层复用：HTTP 服务层依然是统一外部出口，而且继续保持 OpenAI-compatible 设计

如果说 CLI 和启动层解释的是“它怎样进入系统”，那么 HTTP 服务层解释的就是“它怎样对外暴露自己”。

这里最值得看的文件是：

- `python/sglang/multimodal_gen/runtime/entrypoints/http_server.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/openai/common_api.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/openai/image_api.py`
- `python/sglang/multimodal_gen/runtime/entrypoints/openai/video_api.py`

## `create_app()` 说明 diffusion 并没有放弃“统一服务出口”的思路

`http_server.py` 里的 `create_app(server_args)` 非常像我们熟悉的 serving 层组织方式。

它会创建 FastAPI app，并挂上多组 router：

- health router
- vertex router
- `common_api`
- `image_api`
- `video_api`
- realtime video API
- mesh API
- 权重更新和 post-training 相关接口

也就是说，diffusion 并不是“CLI 能跑，但服务化能力很弱”的状态。

它从一开始就沿用了成熟服务框架的写法：

- 生命周期管理
- warmup 完成前的请求阻塞
- 异步 scheduler client
- 健康检查、模型信息、服务信息接口

尤其是 `/server_info` 和 `/model_info` 的注释很值得注意。

代码里明确写到，这些字段是为了和 LLM engine 的同类接口保持兼容，便于 model gateway 发现 diffusion worker。

这说明 diffusion 子系统不是只考虑“自己能跑起来”，而是从一开始就考虑如何接入更大的系统基础设施。

## OpenAI-compatible API 在 diffusion 这里不是中断，而是延续

这篇文章题目里有一句很关键的话：

“从多模态生成子系统看架构复用”

而 OpenAI-compatible API 正是最清楚的一条复用线。

在 LLM 侧，我们已经看到 OpenAI-compatible 接口不是简单转发，而是 SGLang 的主要对外协议层。

到了 diffusion，这个选择没有变。

`common_api.py` 继续提供 `/v1/models` 等通用接口，只不过 model card 上扩展了 diffusion 特有信息，例如：

- `task_type`
- `num_gpus`
- `dit_precision`
- `vae_precision`
- `pipeline_name`
- `pipeline_class`

这说明 SGLang 的思路不是“图像/视频生成就完全抛弃现有接口规范”，而是在兼容外部生态的前提下，为 diffusion 扩展必要字段。

`image_api.py` 和 `video_api.py` 则把图像、视频请求转成内部 `SamplingParams`，再通过 `prepare_request(...)` 和 scheduler client 发往后端。

这件事的意义非常大：

- 外部接口仍然尽量统一
- 内部请求准备仍然走统一入口
- 变化主要落在协议字段和 sampling 语义，而不是服务骨架

所以 OpenAI-compatible API 在 diffusion 里并不是另起一页，而是 SGLang 服务层设计的一次自然外推。

## 第四层复用：真正进入运行时之后，diffusion 依然沿用了 scheduler / worker / client 的服务内核

到这里，我们已经能看到 diffusion 复用了：

- 顶层 CLI
- 服务启动方式
- HTTP 服务暴露方式

但最关键的一层其实还在下面：

进入执行系统之后，它到底是不是另一套完全不同的运行时？

答案是：不完全是。

模型语义确实不同，但运行时分层思想高度相似。

## `Scheduler` 的存在本身，就说明 diffusion 复用的是“调度系统”而不是“直接调用模型”

`python/sglang/multimodal_gen/runtime/managers/scheduler.py` 的 `Scheduler` 注释写得很直接：

它运行主事件循环，通过 ZMQ 接收外部请求，并和其他 worker 协作。

这句话和我们对 LLM runtime scheduler 的理解几乎处在同一个抽象层。

继续看它的初始化逻辑，会发现很多熟悉的元素：

- rank 0 绑定接收端 socket
- 根据平台创建 `CPUWorker` 或 `GPUWorker`
- 维护 `waiting_queue`
- 维护 request handler 映射
- 通过 `BatchAdmissionController` 做动态 batching 控制
- 在 event loop 里拉取请求、组 batch、下发执行

也就是说，diffusion 运行时并不是：

- HTTP 直接调 pipeline
- pipeline 直接调模型
- 模型直接吐图

而是仍然遵循 SGLang 很核心的一个系统原则：

请求先进入 scheduler，再由 scheduler 统一协调执行。

这和“把生成系统当 runtime 来做”密切相关。

## `DiffGenerator`、`prepare_request()`、scheduler client 共同构成了 diffusion 版的统一调用链

如果把 diffusion 的离线和在线路径放在一起看，会更清楚。

无论是：

- CLI 离线生成
- HTTP 图像生成
- HTTP 视频生成

它们最终都会收敛到相似的流程：

1. 把用户输入转成 `SamplingParams`
2. 用 `prepare_request(...)` 生成内部请求对象
3. 通过 scheduler client 发给后端 scheduler
4. 由 scheduler 再调 worker 执行

这条链路特别值得记住，因为它说明 diffusion 在 SGLang 里的接入方式不是“接口各写各的”。

相反，它是通过统一请求准备和统一调度执行来收口的。

这就是典型的架构复用。

## 第五层复用：多进程和拆分部署的思想也被保留下来了，只是角色切分换成了 diffusion 语义

如果只停在 monolithic server，你会觉得 diffusion 复用的主要是服务骨架。

但继续看 `launch_server.py` 和 `runtime/disaggregation/`，会发现它复用的其实还有更深一层的系统思想：

SGLang 并不把生成过程看成只能在一个进程、一个节点、一个统一角色里完成。

## LLM 里常见的是 prefill / decode，diffusion 里变成了 encoder / denoiser / decoder

这是我觉得这篇文章里最值得讲清楚的一点。

SGLang 并没有机械地把 LLM runtime 的角色划分复制到 diffusion 里。

它复用的是“按照流水线阶段拆运行时”这个思想，而不是复用具体阶段名字。

在 diffusion 子系统里，`runtime/disaggregation/roles.py` 定义了几类角色：

- `MONOLITHIC`
- `ENCODER`
- `DENOISER`
- `DECODER`
- `SERVER`

而且 `get_module_role()` 会按模块名把组件归类到这些角色上，比如：

- tokenizer、text encoder、image encoder 更偏 `ENCODER`
- transformer / DiT 更偏 `DENOISER`
- VAE / vocoder 更偏 `DECODER`

这特别能说明“复用”和“新增长”之间的边界。

复用的是：

- 系统支持分角色部署
- 头节点与工作节点分离
- 不同阶段之间通过传输和调度协作

新增长出来的是：

- 这些角色不再围绕 token 生成组织
- 而是围绕 diffusion pipeline 的真实计算阶段组织

所以 diffusion 子系统不是简单照搬 LLM runtime，而是把 SGLang 的拆分思想重新映射到多模态生成流水线上。

## `DiffusionServer` 说明 diffusion 甚至已经长出自己的阶段编排层

继续看 `runtime/disaggregation/orchestrator.py` 和 `launch_server.py`，会发现 diffusion 的 disaggregation 已经不是概念级支持，而是正式的编排机制。

例如：

- head node 会维护 frontend、work、result 多类 endpoint
- `DiffusionServer` 负责在 encoder、denoiser、decoder 之间转发阶段结果
- 请求状态会在不同角色之间流转

这意味着 diffusion 不是只在“模型内核”层做创新，而是在“跨角色流水线编排”层也正式接住了系统复杂度。

也正因为如此，SGLang 才不需要把 diffusion 写成一个独立项目。

因为它已经有足够成熟的运行时观念去容纳这种复杂度。

## 真正新长出来的部分一：模型注册表和 pipeline 体系

讲到这里，如果只强调“复用”，就会低估 diffusion 的新增工作量。

它当然复用了很多骨架，但 diffusion 绝不是小改一下参数就能接上的。

它真正新长出来的第一大块，就是 pipeline 体系本身。

## `registry.py` 不是简单模型清单，而是 diffusion 运行时的配置中枢

`python/sglang/multimodal_gen/registry.py` 很大，但非常值得看。

它至少说明了三件事：

### 1. diffusion 模型支持不是靠 if-else 临时堆出来的

注册表里明确维护了：

- pipeline config 类
- sampling params 类
- pipeline class

这说明 SGLang 并不是在请求进来时临时猜模型逻辑，而是把不同 diffusion 模型纳入正式的配置与管线系统。

### 2. pipeline discovery 是自动化的

`_discover_and_register_pipelines()` 会扫描 `runtime.pipelines` 包，寻找 `EntryClass` 并注册到全局 registry。

这表明 diffusion 不是单点写死，而是面向“继续扩展新模型、新 pipeline”设计的。

### 3. 新模型接入是先接注册体系，再接运行时

这和 LLM 世界里“先让模型适配统一执行框架”的思路是一致的。

换句话说，diffusion 新增长出来的核心，不只是多了几个模型目录，而是多了一层正式的模型和 pipeline 元数据管理体系。

## 真正新长出来的部分二：`SamplingParams` 和输入输出协议不再围绕 token，而是围绕多模态生成语义

在 LLM 里，sampling params 大多围绕：

- temperature
- top-p
- max tokens

但 diffusion 里完全不同。

从 `image_api.py`、`video_api.py` 和对应的 sampling config 可以看到，内部请求要处理的是：

- 宽高
- 帧数
- fps
- guidance scale
- negative prompt
- 输入图像
- upscaling
- frame interpolation
- output format

这些参数的存在说明一个事实：

SGLang 复用的是“请求抽象”和“参数对象”这套方法，但参数语义本身已经换成了多模态生成世界的语言。

这就是非常典型的“骨架复用，业务语义重建”。

## 真正新长出来的部分三：执行组件不再是 LLM block，而是 encoder / DiT / VAE / scheduler / bridge

如果继续往 `multimodal_gen/runtime/models/`、`configs/models/`、`runtime/pipelines/` 里看，会发现 diffusion 新增的内容其实非常厚。

它至少引入了整套新组件族：

- text/image encoders
- DiT 类 transformer
- VAE
- vocoder
- scheduler
- bridge / adapter
- 后处理与实时输出相关组件

这说明 diffusion 接入并不是“把 diffusers pipeline 套一层 HTTP”。

真正发生的事情是：

- 外层沿用 SGLang 的运行时方法论
- 内层替换成 diffusion 所需的模型组件和流水线阶段

所以 diffusion 在 SGLang 里的形态，更像“第二个生成域 runtime”，而不是“LLM runtime 的附属功能”。

## 为什么这仍然算架构复用，而不是另起炉灶

讲到这里，可能有人会问：

既然新增了这么多组件，为什么还说它是架构复用，而不是另起炉灶？

我觉得关键在于，要分清“复用的层级”。

## 复用的不是模型内部实现，而是系统设计约束

真正被复用下来的，是下面这些更高层的系统约束：

- 用户从统一 CLI 进入系统
- 模型类型在入口层自动分流
- 参数通过统一对象传递到运行时
- 离线和在线场景尽量共享同一条执行链
- 服务对外继续维持 OpenAI-compatible 思路
- 后端继续以 scheduler / worker / client 为主分层
- 多角色拆分、warmup、tracing、监控这些工程设施继续保留

这些约束一旦被保留下来，你就不能把 diffusion 看成一个完全独立的新项目。

因为它并没有逃出这套系统设计。

## 新增的是“领域内核”，不是“系统外壳”

反过来，真正新长出来的是：

- diffusion 模型注册与发现
- pipeline config / sampling config
- 图像和视频 API 协议
- encoder / denoiser / decoder 的阶段角色
- 各类模型组件和 pipeline stage
- 后处理、缓存加速、视频实时接口等多模态专属能力

所以最准确的说法不是：

“SGLang 附带了一个 diffusion 项目”

而是：

“SGLang 用自己已经验证过的生成系统外壳，承载了一个新的多模态生成领域内核”

这才是第 9 篇最想强调的判断。

## 再回到题目：SGLang 为什么会长出 diffusion

现在可以把题目里的“为什么”回答得更具体一点。

### 1. 因为 SGLang 的定位本来就不只是 LLM serving，而是生成系统基础设施

只要一个系统真正抽象出来的是：

- 入口
- 调度
- 服务层
- 部署形态
- 扩展机制

那么它天然就有机会从一种生成任务扩展到另一种生成任务。

SGLang diffusion 正是这个判断的结果。

### 2. 因为很多工程问题本来就跨模态共享

无论是文本生成，还是图像/视频生成，都会遇到类似问题：

- 怎样用统一 CLI 和统一服务对外暴露
- 怎样组织参数和请求对象
- 怎样用 scheduler 协调执行
- 怎样做多进程、多卡、多角色部署
- 怎样做 warmup、监控和 tracing

这些问题并不是 token 专属。

既然问题共享，架构也就有机会共享。

### 3. 因为 diffusion 足够复杂，必须进入正式运行时，而不能停留在脚本层

如果 diffusion 只是“跑一个模型 forward”，也许写几个 demo 脚本就够了。

但从 `multimodal_gen` 的目录规模和启动链路可以看到，它面对的是：

- 图片和视频生成
- 多模型族支持
- OpenAI-compatible server
- WebUI / ComfyUI 接入
- LoRA、量化、缓存加速
- disaggregation 和多角色编排

这些能力已经远远超过“样例代码”范围，必须进入正式运行时。

而 SGLang 恰好已经拥有承载这种复杂度的系统骨架。

所以 diffusion 会长进来，并不奇怪。

## 从阅读源码的角度，应该怎样理解 `multimodal_gen`

如果你后面准备继续读 diffusion 相关代码，我建议不要把它当作一整个陌生世界。

更好的顺序是：

### 先用旧地图找新系统

优先按下面这条熟悉的路径去读：

1. 顶层 CLI 怎么分流
2. serve / generate 怎么进入 diffusion 入口
3. HTTP 服务怎么组装 router
4. 请求怎样被准备并送入 scheduler
5. scheduler 怎样协调 worker

这样你会先看到复用下来的骨架。

### 再去理解 diffusion 特有的模型语义

等骨架看清楚以后，再进入：

- registry
- pipeline configs
- sampling params
- pipelines
- disaggregation roles

这样你会更容易理解哪些东西是新领域带来的，哪些东西只是系统外壳的延续。

## 这篇文章最值得记住的几个判断

如果要把整篇文章再压缩一下，我觉得最重要的是下面几句。

### 1. diffusion 在 SGLang 里不是边角功能，而是正式子系统

它有独立文档、独立运行时、独立注册表、独立 API 和独立拆分部署逻辑。

### 2. 它复用的不是 LLM token 语义，而是生成系统骨架

真正被复用的是入口分流、服务暴露、调度执行、多进程和拆分部署这些系统层能力。

### 3. `sglang generate` 和 `sglang serve` 在 diffusion 场景下仍然是统一入口

这说明 diffusion 是长在主产品心智里的，而不是挂在旁边的附属工具。

### 4. OpenAI-compatible API 在 diffusion 这里没有中断，而是继续扩展

它依然是服务层的主要外部出口，只不过承载了图像和视频生成语义。

### 5. diffusion 真正新长出来的，是 pipeline 语义和运行时角色划分

比如 registry、sampling params、encoder/denoiser/decoder、图像视频协议、模型组件族，这些才是新增的领域内核。

### 6. 所谓“架构复用”，本质上是系统外壳复用，领域内核替换

SGLang 不是把 LLM runtime 硬套给 diffusion，而是在保留系统骨架的同时，换上一套多模态生成内核。

## 总结

这篇文章真正想回答的问题是：

SGLang 为什么会长出 diffusion？

答案不是“它顺便支持了图像模型”，而是：

SGLang 从一开始抽象出来的就是一套生成系统骨架，这套骨架并不天然局限于 LLM，因此当项目需要支持图像和视频生成时，它完全可以把 diffusion 作为新的领域内核接到这套骨架之上。

更具体地说：

- 顶层继续复用 `sglang generate` 和 `sglang serve`
- 入口分流继续通过模型识别决定后端
- 服务层继续复用 FastAPI 和 OpenAI-compatible API
- 运行时继续复用 scheduler / worker / client 分层
- 部署层继续支持 monolithic 与 disaggregation 思想
- diffusion 自己则新增 registry、pipeline、sampling params 和 encoder/denoiser/decoder 等专属能力

所以如果把这一篇压缩成一句话，可以这样概括：

SGLang 之所以会长出 diffusion，不是因为它偏离了原本架构，而恰恰是因为它原本架构已经足够抽象，能够把“生成系统”的外壳稳定下来，再把具体生成语义从 LLM 扩展到多模态。

下一篇，就可以继续把视角再往外拉，去看 `sgl-kernel` 和 `sgl-model-gateway` 各自在这套更大系统里承担什么角色。
