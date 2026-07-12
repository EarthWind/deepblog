# 14. 从构建到测试：SCons、Proto、CI 如何把 DAOS 组织起来

## 为什么这一篇不是“工程边角料”

写到第 14 篇，前面的架构主线其实已经差不多闭环了：

- 控制面和数据面怎么拆
- 服务端如何启动
- pool/container/object/VOS/BIO 这些模块怎么衔接
- 客户端如何把请求送进系统

但如果你真的想进一步参与 DAOS 开发，只理解业务架构还不够。

你还得回答几个更“工程现实”的问题：

- 这么大的多语言仓库，到底从哪里开始构建？
- Go、C、Proto、Python 测试为什么能放在同一个仓库里协同？
- 单元测试、功能测试、Node Local Test、GitHub Actions 之间是什么关系？
- 新贡献者应该先跑什么，而不是一上来就被整套 CI 淹没？

这就是这一篇的意义。

它讲的不是服务逻辑，而是：

- **DAOS 这套复杂系统，是怎样被构建、生成、测试和持续验证起来的。**

## 先给结论：DAOS 不是“一个编译器命令 + 一套测试”，而是分层工程系统

如果把 DAOS 的工程组织压缩成一句话，可以这样记：

- **`SConstruct` 负责总装配**
- **`SConscript` 负责分目录构建**
- **`src/proto` 负责跨语言协议定义与生成代码**
- **`src/tests` 负责源码树内测试产物与测试程序**
- **`ci/` 和 `.github/workflows/` 负责把不同类型的验证流程组织成流水线**

也就是说，DAOS 的工程体系不是“一个 build 脚本 + 一个 test 脚本”的平面结构，而是分层的：

1. 顶层构建入口
2. 分模块构建脚本
3. 代码生成层
4. 测试产物层
5. CI 编排层

理解这五层之后，整个仓库会清楚很多。

## 第一层：为什么顶层入口是 `SConstruct`

### `SConstruct` 就是整个仓库的 build root

从仓库根目录的 [SConstruct](file:///e:/projects/daos/SConstruct) 开始看，最直观的感觉是：

- 它不是一个简单的“编译几个目录”的小脚本
- 而是整个 DAOS 构建系统的总入口

它负责的事情至少包括：

- 定义命令行选项
- 解析 build 配置
- 初始化环境
- 处理依赖构建策略
- 决定 build prefix
- 把后续构建委派给各级 `SConscript`

所以如果你想问“DAOS 从哪里开始 build”，答案就是：

- 先看 `SConstruct`

### 它并不是单纯替代 Make，而更像 Python 化的构建编排器

只看前面几百行就能感受到这一点。

例如它直接定义了很多 build 选项：

- `--build-deps`
- `--check-only`
- `--build-config`
- `--prepend-path`
- `--analyze-stack`
- `--no-rpath`

这说明它的职责远超“调用编译器”。

它还承担：

- 构建依赖管理
- 环境注入
- 变体构建配置
- 构建后分析

这些在大型 C/Go 混合仓库里很重要。

### 所以更准确的说法不是“DAOS 选择了 SCons”，而是“当前仓库形态非常适合 SCons”

从现有代码形态来看，SCons 在这里的优势很直观：

- 它本身就是 Python，可编程性强
- 很适合处理大量条件构建和平台差异
- 很适合组织分目录脚本和 variant build
- 很容易把依赖检查、环境配置和安装逻辑放进同一个入口

这也是为什么 DAOS 这种：

- 多模块
- 多语言
- 多平台
- 带大量可选依赖

的工程，会在当前形态下呈现出一个很“像 Python 项目在编排 C/Go 构建”的风格。

## `SConstruct` 真正做了哪些顶层装配动作

### 第一类：解析环境和 build config

`SConstruct` 里有：

- `parse_and_save_conf(env, opts_file)`

它会处理：

- `SCONS_ENV`
- `GO_BIN`
- `ENV_SCRIPT`

以及命令行传入的构建参数。

这说明 DAOS 的 build 并不是完全硬编码在仓库里，而是允许：

- 外部环境脚本
- 本地 build config
- 命令行选项

共同影响构建行为。

### 第二类：处理依赖与前置条件

从 `--build-deps`、`--skip-download`、`--check-only` 这些选项就能看出来：

- DAOS 把 prerequisite 管理当成构建系统的一部分

这很关键，因为 DAOS 依赖的不只是一个编译器，还包括：

- protobuf 工具链
- Go 工具链
- `protobuf-c`
- SPDK/ofi/mercury 等外部组件

所以顶层构建入口必须知道：

- 依赖是只检查、自动下载、自动构建，还是只构依赖不构主仓库

### 第三类：切出 build 变体目录

`SConstruct` 里最关键的两行之一是：

- `build_prefix = prereqs.get_src_build_dir()`
- `SConscript(os.path.join('src', 'SConscript'), variant_dir=path, duplicate=0)`

这说明源码目录和构建输出目录是被明确区分开的。

也就是说，顶层 build 的思路不是在源码目录里就地乱生成，而是：

- 用 `variant_dir` 把 `src/` 的构建输出放到对应 build 前缀下

这对大型仓库尤其重要，因为它有利于：

- 区分源文件和构建产物
- 支持不同 build type / prefix
- 让依赖构建与主仓库构建更好配合

### 第四类：把非源码产物一起安装

`build_misc()` 里还会处理：

- `utils/config`
- `utils/certs`

这说明在 DAOS 里，“构建”不是只关心二进制，还包括：

- 配置模板
- 证书相关文件
- 安装树布局

这也是大型系统软件常见的特征。

## 第二层：为什么 `SConscript` 是理解仓库构建结构的关键

### `SConstruct` 负责总控，`SConscript` 负责分目录自治

`SConstruct` 最后把真正的源码构建交给：

- `src/SConscript`

而每个重要子目录下面，又继续用自己的 `SConscript` 向下分解。

这种结构的核心好处是：

- 顶层保持总装配视角
- 子目录自己描述自己的构建规则

这和前面我们读源码时看到的模块化分层是同一个思路，只不过现在落在工程系统上。

### `src/tests/SConscript` 就是一个很好的例子

看 [SConscript](file:///e:/projects/daos/src/tests/SConscript) 很容易发现：

- 它先构建测试公用库 `dts`
- 再构建 `daos_racer`、`daos_perf`、`vos_perf`、`obj_ctl` 等测试/压测工具
- 然后继续递归到：
  - `suite/SConscript`
  - `drpc/SConscript`
  - `security/SConscript`
  - `ftest/SConscript`
  - `rpc/SConscript`

这说明测试并不是“仓库外部用脚本顺便跑一下”，而是：

- 测试产物本身就是被正式纳入构建系统的一部分

### 这也解释了为什么 `src/tests` 会同时出现很多不同风格的测试

从 `src/tests` 的结构能看到：

- C 测试程序
- `ftest` 下大量 Python + YAML 组合
- CART 相关网络测试
- dfuse、security、object、pool、rebuild 等功能测试

这不是混乱堆在一起，而是因为：

- 它们都属于“源码树内测试资产”
- 只是执行形态和目标层次不同

## 第三层：`src/proto` 为什么是 Go/C 双语言协同的关键中枢

### 先记住一点：Proto 在 DAOS 里不是装饰层，而是通信契约源文件

`src/proto/README.md` 一开头就说：

- 这里放的是 gRPC 和 dRPC 通信使用的 protobuf 定义

这句话非常关键。

因为它说明 `src/proto` 的地位不是：

- “有些工具代码顺手用了 protobuf”

而是：

- **控制面和本机进程间通信的消息契约都从这里出发。**

### README 还把目录职责拆得很清楚

`src/proto` 下主要有：

- `ctl`
- `mgmt`
- `security`
- `shared`
- `srv`

它们大致对应：

- 控制面可 multicast 的消息
- 必须由 management service leader 处理的消息
- 安全相关消息
- 共享消息
- engine 到 server 的 dRPC 消息

这和前面几篇讲过的：

- gRPC
- dRPC
- 控制面/数据面边界

是完全对上的。

### 所以 `src/proto` 是一个非常重要的“跨语言边界稳定层”

因为它站在：

- Go 控制面
- C 数据面

之间，承担的是：

- 统一消息格式源

## `src/proto/Makefile` 怎样把同一套 `.proto` 生成到 Go 和 C 两端

### 它的第一层逻辑很直接：`all: proto-go proto-c`

这就已经说明了 DAOS 对 protobuf 生成代码的态度：

- 同一套 proto，不是只生成一种语言
- 而是默认就维护 Go 和 C 两套产物

### Go 产物会落到控制面目录下

在 [Makefile](file:///e:/projects/daos/src/proto/Makefile) 里，`GO_CONTROL_FILES` 会生成到：

- `src/control/common/proto/...`
- `src/control/drpc/...`
- `src/control/security/auth/...`
- `src/control/cmd/hello_drpc/...`

并且使用：

- `protoc-gen-go`
- `protoc-gen-go-grpc`

这非常符合前面几篇讲到的事实：

- 控制面主语言是 Go
- gRPC/dRPC 相关控制侧消息自然要生成 `.pb.go`

### C 产物会落到数据面和其他 C 模块目录里

同一个 Makefile 里，`proto-c` 会生成：

- `src/common/drpc.pb-c.c`
- `src/include/daos/drpc.pb-c.h`
- `src/engine/*.pb-c.[ch]`
- `src/mgmt/*.pb-c.[ch]`
- `src/tests/drpc/*.pb-c.[ch]`

并依赖：

- `protobuf-c`
- `protoc-gen-c`

这说明在 DAOS 里，proto 生成不是“统一落到一个 gen 目录”，而是：

- **按消费方语言和模块，就近生成到对应源码树位置。**

这对开发体验其实很有帮助，因为消费代码和生成代码的距离更近。

### Makefile 里还有很多 one-off 规则，这恰好说明仓库很真实

例如：

- `drpc.proto`
- `shared/event.proto`
- `ctl/smd.proto`

这些文件的生成目标并不完全遵循一套统一目录模式，因此 Makefile 里会专门给它们单独写规则。

这也说明 DAOS 的 proto 生成系统不是教科书里的“完美规则引擎”，而是：

- 在真实仓库历史演进中整理出来的一套可维护工程规则

这类细节反而很能体现项目成熟度。

## 为什么改 `.proto` 不只是改一份文件

### README 已经明确提醒了这一点

`src/proto/README.md` 直接说：

- 修改 `.proto` 后，要更新所有 generated files
- 通常直接在 `src/proto` 目录下运行 `make`

这对贡献者非常重要，因为它意味着：

- 改协议定义 = 改契约
- 契约变化必须同步更新 Go/C 两侧生成代码

否则你提交的不是一个完整变更。

### 这也是为什么 proto 生成被单独抽成一个稳定入口

因为如果没有：

- README 说明
- `Makefile`
- 显式目标列表

那么多语言仓库里的协议变更会非常容易失控。

## 第四层：`src/tests` 不是一个目录，而是一整个测试资产树

### 从 `src/tests` 的体量就能看出 DAOS 的测试并不是附属品

只看目录结构就能看到很多层次：

- `drpc`
- `ftest`
- `cart`
- `dfuse`
- `object`
- `pool`
- `rebuild`
- `recovery`
- `security`
- `nvme`

这已经说明：

- DAOS 的测试是按功能域和子系统长期积累出来的
- 不是只有少量 smoke test

### `ftest` 尤其能体现“功能验证平台化”

`src/tests/ftest` 下是大量：

- `*.py`
- `*.yaml`

组合。

这种形态很典型，说明它不是简单把测试逻辑写死在单个脚本里，而是：

- Python 承载测试流程
- YAML 承载场景参数和部署配置

这类测试特别适合：

- 多节点
- 多配置
- 多场景矩阵

的系统软件。

### `src/tests/SConscript` 还说明很多测试工具本身也是 build artifact

例如：

- `daos_racer`
- `daos_perf`
- `vos_perf`
- `obj_ctl`

这些不是外部下载的测试工具，而是：

- 仓库自己构建出来、供验证体系复用的内部工具

这也是大型项目常见的成熟信号。

## 第五层：单元测试、本机测试、功能测试在 DAOS 里怎么分工

这是最容易让新读者混淆的一部分。

### 1. 单元测试

这一层最直接的入口是：

- `utils/run_utest.py`
- `ci/unit/test_main.sh`

`run_utest.py` 开头就直接说明：

- 这是运行 DAOS unit tests 的脚本

并且它还内建了：

- JUnit XML 结果输出
- Valgrind memcheck 支持
- cmocka 结果处理

这说明单元测试在 DAOS 里不只是“本地跑一下二进制”，而是已经被整理成：

- 可以进入 CI 报表体系
- 可以进入 memcheck 阶段
- 可以统一汇总结果

### 2. Node Local Test

[node_local_test.py](file:///e:/projects/daos/utils/node_local_test.py) 开头写得很直接：

- 它是在单节点上、基于 tmpfs 跑 DAOS 的 smoke/unit tests

并且还覆盖：

- DFuse
- 客户端压力
- 一些 fault injection 场景

这层测试的价值非常明显：

- 比纯 unit test 更接近真实运行时
- 又比多节点功能测试轻量很多

所以它正好填在：

- 单元测试
- 多节点功能测试

之间。

### 3. 多节点功能测试

这一层主要看：

- `src/tests/ftest`
- `ci/functional/test_main.sh`

后者会：

- 校验集群节点健康
- 做 NFS / 环境准备
- 在必要时重启集群重试
- 最后调用 `ftest.sh` 或远端节点脚本运行功能测试

这说明功能测试关心的已经不是单个 API 或单进程行为，而是：

- 集群环境
- 多节点状态
- 硬件条件
- 部署与恢复路径

### 所以最实用的区分是

- 单元测试：验证局部实现与边界条件
- NLT：验证单节点上较真实的运行闭环
- 功能测试：验证多节点、系统级行为和复杂场景

这样记最稳。

## `ci/` 目录其实是在讲“CI 编排逻辑”，不是只放几个 shell 脚本

### `ci/` 的结构已经把验证体系分层写在目录名里了

从 [ci](file:///e:/projects/daos/ci) 目录可以直接看到：

- `unit/`
- `functional/`
- `storage/`
- `rpm/`
- `provisioning/`
- `docker/`

这说明 `ci/` 真正承载的是：

- 不同验证阶段的环境准备与执行编排

而不只是“一个统一 test.sh”。

### `ci/unit/test_main.sh` 是个很典型的例子

这个脚本会：

- 清理旧日志和结果目录
- 根据阶段判断是否是 Bullseye / bdev 测试
- 复制构建树到目标节点
- 通过 `ssh` 调远端节点执行 `test_main_node.sh`

这说明即使是 unit test 阶段，在 DAOS 的 CI 里也不是绝对“本机原地执行”。

它仍然被放进：

- 专门节点
- 专门环境
- 专门结果收集流程

中。

### `ci/functional/test_main.sh` 更能体现“系统级测试编排”

它会做很多典型系统测试动作：

- 根据 `NODELIST` 选测试节点
- 预检查集群健康
- 必要时尝试 reboot cluster
- 收集硬件准备结果
- 组织 `ftest` 执行
- 整理 JUnit 结果目录

这说明 DAOS 的功能测试流程，并不是“运行 Python 用例这么简单”，而是：

- **测试代码 + 集群准备 + 环境恢复 + 结果收集** 的组合流程

## GitHub Actions 里的 `ci2.yml` 实际在验证什么

### 这份工作流的名字很简单，但内容不简单

在 [.github/workflows/ci2.yml](file:///e:/projects/daos/.github/workflows/ci2.yml) 里，主要能看到两个大 job：

- `Build-and-test`
- `Build`

### `Build-and-test` 主要跑 DAOS/NLT 测试

这个 job 会：

- checkout 代码和 submodules
- 复用 docker layer cache
- 用 `utils/docker/Dockerfile.ubuntu` 构建镜像
- 在容器里执行 `./daos/utils/ci/run_in_gha.sh`
- 收集 `nlt-junit.xml`
- 发布测试结果

这说明 PR 流程里很重要的一类验证是：

- **先在较受控的 docker 环境里跑 build + Node Local Test 级别验证**

### `run_in_gha.sh` 进一步把“验证项”展开了

这个脚本会顺序做很多事：

- 重建部分依赖
- 跑 stack analyzer
- 验证 client-only 构建
- 验证 server-only 构建
- 验证增量 debug build + `test` target
- 安装产物检查
- 重新用 `ALT_PREFIX` 构建依赖和主仓库
- `pip install` pydaos
- 运行 `node_local_test.py --test cont_copy`

这非常值得注意，因为它说明 GHA 里的“Build-and-test”并不是：

- 编出来就算过

而是顺带验证：

- 不同 build 组合是否成立
- 安装树是否完整
- Python 绑定安装是否正常
- 至少一条本地闭环测试是否打通

### `Build` job 则更偏构建矩阵覆盖

它会在不同发行版与编译器组合下做构建：

- `rocky`
- `fedora`
- `leap.15`
- `clang`
- `gcc`

并分别测试：

- 常规 build
- Java build
- debug build
- dev build

这说明这部分流水线的目标不是深度运行时验证，而是：

- **扩大平台与配置组合覆盖，确保仓库能在这些矩阵上被成功构建。**

## 所以 DAOS 的 CI 其实在同时验证三件不同的事

把前面这些脚本和 workflow 放在一起看，会发现它们分别在回答不同问题。

### 第一类：能不能构建

看的是：

- `SConstruct`
- Docker build
- 多发行版/多编译器矩阵

回答：

- 代码当前是否还能被编出来

### 第二类：协议/安装/组合是否还自洽

看的是：

- proto 生成
- client-only / server-only build
- install tree 检查
- pydaos 安装

回答：

- 工程系统有没有因为跨模块改动而失配

### 第三类：运行时闭环是否还通

看的是：

- `run_utest.py`
- `node_local_test.py`
- `ftest`
- `ci/functional`

回答：

- 构建出的系统到底能不能按预期运行

这三类验证都不能少，只是成本和反馈速度不同。

## 一个很实用的新贡献者上手路径

这一篇最想落到实处的，其实是这个问题：

- 如果我是第一次准备给 DAOS 提补丁，应该怎么开始，而不是一上来就被整套 CI 吓住？

最推荐的顺序是下面这样。

### 第一步：先从顶层构建入口建立全局认知

先读：

- [SConstruct](file:///e:/projects/daos/SConstruct)
- `src/SConscript`
- 你准备修改目录下的 `SConscript`

目标不是立刻记住所有选项，而是先建立一个概念：

- 顶层怎么把子目录装起来
- 你的模块在 build 图里大概处于哪一层

### 第二步：如果改到协议，先把 `src/proto` 这条链读清

先看：

- [README.md](file:///e:/projects/daos/src/proto/README.md)
- [Makefile](file:///e:/projects/daos/src/proto/Makefile)

只要涉及 `.proto` 变更，就先确认：

- 生成到 Go 哪些路径
- 生成到 C 哪些路径
- 提交时是否应包含生成文件变化

### 第三步：优先跑离你修改最近的测试层次

最实用的原则是：

- 改局部实现，先跑 unit test
- 改单节点运行逻辑，再补 NLT
- 改系统行为或部署/恢复，再看 `ftest`

不要一上来就把自己扔进最重的多节点功能测试里。

### 第四步：把 CI 当作“验证分层地图”，不要当成黑盒

当你看到一个 job 失败时，先判断它属于哪层：

- 构建矩阵失败
- unit/memcheck 失败
- NLT 失败
- functional 失败

一旦能这样分层看，CI 信息量就不会再显得完全不可控。

## 反过来看，为什么这一套工程体系和 DAOS 架构本身是统一的

这是这一篇最后一个很值得强调的点。

前面我们在架构层已经看到 DAOS 非常强调：

- 分层
- 边界
- 各模块职责清晰

而现在看工程体系，其实也是同一套思路：

- 顶层入口与子目录构建分层
- 协议定义与生成代码分层
- 单元测试、NLT、功能测试分层
- 构建矩阵与运行时验证分层

也就是说，DAOS 不只是代码架构分层，它的：

- build system
- code generation
- test hierarchy
- CI orchestration

也都在贯彻这种“把复杂性拆开”的方法。

这也是为什么你一旦理解工程体系，反而会觉得整个仓库更好读。

## 小结

从构建到测试，DAOS 的工程组织其实非常有章法。

其中：

- `SConstruct` 是总装配入口
- `SConscript` 把复杂构建拆到各模块
- `src/proto` 把 Go/C 双语言通信契约统一在同一源头
- `src/tests` 承载源码树内测试资产和测试工具
- `utils/run_utest.py`、`node_local_test.py`、`ci/functional` 则分别覆盖不同层次的验证需求
- `.github/workflows/ci2.yml` 把构建矩阵、NLT 和结果发布编排成持续集成流程

如果把这一篇压缩成一句话，那就是：

**DAOS 不是“先写代码，再顺手补几个脚本”，而是一套把构建、协议生成、测试资产和 CI 编排都系统化组织起来的大型工程。**

理解这套工程体系之后，你才真正进入了“可以参与 DAOS 开发”的状态。
