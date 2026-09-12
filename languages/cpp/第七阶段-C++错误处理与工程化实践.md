# 第七阶段：C++ 错误处理与工程化实践

> 前置要求：掌握现代 C++、STL、模板、Concepts 和多文件项目。  
> 建议标准：C++23  
> 建议周期：8～10 周，每天 1～2 小时  
> 阶段目标：能够设计一致的错误模型，构建可测试、可诊断、可安装和可持续维护的 C++ 工程。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 区分程序员错误、环境错误、业务失败和系统故障。
2. 根据边界选择异常、`error_code`、`expected`、`optional` 或状态值。
3. 解释栈展开和四级异常安全保证。
4. 编写资源安全、失败后状态明确的 API。
5. 使用 CMake target 构建库、程序、测试和安装包。
6. 使用单元测试、集成测试、属性测试和基准测试。
7. 使用 Sanitizer、静态分析、覆盖率和模糊测试发现缺陷。
8. 理解静态库、动态库、符号、ABI 和 PImpl。
9. 建立 CI 质量门禁和可复现构建流程。
10. 完成一个具备生产项目基本骨架的 C++ 库。

---

## 二、错误分类与处理策略

### 2.1 先分类，再选择机制

常见错误类型：

| 类型 | 示例 | 常见处理 |
|---|---|---|
| 程序员错误 | 越界、违反前置条件 | 断言、契约、修复代码 |
| 可预期业务失败 | 登录失败、余额不足 | `expected`、业务结果类型 |
| 无结果 | 未找到可选配置 | `optional` |
| 环境或系统错误 | 文件不存在、权限不足 | 异常、`error_code`、`expected` |
| 不可恢复故障 | 内部不变量严重损坏 | 记录诊断并终止 |

不要用一种机制处理所有失败。

### 2.2 `optional`

只表达“有值或无值”，不保存原因：

```cpp
std::optional<User> find_user(std::string_view name);
```

如果调用者需要区分数据库断开与用户不存在，`optional` 信息不足。

### 2.3 `expected`

```cpp
enum class ParseErrorCode
{
    empty,
    invalid_character,
    out_of_range
};

struct ParseError
{
    ParseErrorCode code{};
    std::size_t position{};
    std::string message;
};

std::expected<int, ParseError> parse_integer(std::string_view text);
```

适合错误是正常控制流的一部分、调用者通常要立即处理或传播的场景。

### 2.4 异常

异常适合：

- 构造函数无法建立有效对象。
- 错误需要跨越多层调用传播。
- 失败路径较少而成功代码应保持清晰。
- 项目和运行环境允许使用异常。

异常不适合表达普通分支，也不能替代输入验证和不变量设计。

### 2.5 `std::error_code`

`error_code` 表示错误值及其错误类别，常用于系统和库边界：

```cpp
std::error_code error;
std::filesystem::remove(path, error);

if (error) {
    std::cerr << error.message();
}
```

它避免异常传播，但调用者可能忘记检查。可将其包装到 `expected<T, error_code>` 中获得更显式的接口。

### 2.6 不应使用特殊魔法值

```cpp
int find_index(); // 返回 -1 表示失败，但有效范围和错误语义不直观
```

优先：

```cpp
std::optional<std::size_t> find_index();
```

### 2.7 项目级一致性

一个库应记录：

- 哪些函数可能抛异常。
- 哪些错误使用 `expected`。
- 是否接受异常跨越模块或 ABI 边界。
- 日志在哪一层记录。
- 谁负责把底层错误转换为用户信息。

错误只应在拥有处理上下文的层记录；每一层都记录会产生重复噪声。

### 本节练习

1. 为十种失败场景选择处理机制并说明理由。
2. 将返回 `-1` 的解析函数改为 `expected`。
3. 将只表示“未找到”的接口改为 `optional`。
4. 为项目写一页错误处理约定。

---

## 三、异常机制

### 3.1 抛出与捕获

```cpp
try {
    load_configuration(path);
} catch (const std::filesystem::filesystem_error& error) {
    std::cerr << error.what();
} catch (const std::exception& error) {
    std::cerr << error.what();
}
```

通常按 `const&` 捕获，具体异常放在通用异常之前。

### 3.2 栈展开

异常传播时，从抛出点到处理点之间已经构造完成的自动对象会按逆序销毁。这正是 RAII 保证异常路径资源安全的基础。

```cpp
void process()
{
    std::ifstream input{"data.txt"};
    std::vector<Record> records;
    parse(input, records); // 抛异常时 input 和 records 自动清理
}
```

### 3.3 自定义异常

```cpp
class ConfigurationError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};
```

异常类型应提供稳定分类。不要迫使调用者解析 `what()` 字符串判断错误种类。

### 3.4 重新抛出

```cpp
catch (...) {
    cleanup_diagnostics();
    throw; // 保留原异常对象
}
```

`throw error;` 可能复制并切片异常；重新传播当前异常使用单独的 `throw;`。

### 3.5 嵌套异常

高层补充上下文时可使用 `std::throw_with_nested` 和 `std::nested_exception`，也可在边界转换为包含 cause 信息的项目错误。关键是保留根因且不重复记录。

### 3.6 析构函数与异常

析构函数不应让异常逃出。栈展开期间再次抛出异常通常会调用 `std::terminate`。可能失败的提交或关闭操作应提供显式函数：

```cpp
writer.commit(); // 可报告失败
```

析构只执行不抛异常的回滚或尽力清理。

### 3.7 `noexcept`

```cpp
void swap(Value& other) noexcept;
```

`noexcept` 是接口承诺，也会影响容器优化和类型特征。只有确认异常不会逃出时才声明。

```cpp
template<class T>
void wrapper(T& value) noexcept(noexcept(value.flush()))
{
    value.flush();
}
```

### 3.8 异常边界

异常不应无控制地越过：

- C ABI 回调。
- 操作系统线程入口。
- 插件 ABI。
- 某些实时或嵌入式边界。
- `main` 的顶层。

在边界捕获、记录上下文并转换成约定结果。

### 本节练习

1. 用日志观察异常传播时对象销毁顺序。
2. 设计结构化异常类型而非解析消息字符串。
3. 演示 `throw;` 与 `throw error;` 的差异。
4. 为线程入口和 `main` 添加异常边界。

---

## 四、异常安全保证

### 4.1 四个等级

- 无保证：失败后状态不可依赖。
- 基本保证：无泄漏，对象仍满足不变量，但值可能改变。
- 强保证：失败后状态与操作前相同。
- 不抛保证：操作不会抛异常。

### 4.2 先准备，后提交

```cpp
void Document::replace_content(std::string content)
{
    validate(content);          // 尚未修改对象
    content_.swap(content);     // 不抛地提交
}
```

复杂更新可在临时状态完成，再通过 `swap` 提交。

### 4.3 RAII 守卫

事务型操作可用作用域守卫自动回滚：

```cpp
Transaction transaction{database};
perform_steps();
transaction.commit();
```

未提交时析构回滚。守卫析构应不抛异常。

### 4.4 容器操作与元素类型

容器能提供何种保证取决于：

- 元素复制或移动是否抛异常。
- 分配器是否失败。
- 操作位置和实现要求。

这也是移动构造常应 `noexcept` 的原因。

### 4.5 测试失败路径

可设计计数型类型，在第 N 次复制、分配或操作时抛异常，验证：

- 无资源泄漏。
- 不变量仍成立。
- 强保证操作保持原状态。
- 后续仍能正常使用对象。

### 本节练习

1. 判断五个 API 提供哪一级保证。
2. 用 copy-and-swap 实现强保证赋值。
3. 编写自动回滚的事务守卫。
4. 注入异常测试容器更新的不变量。

---

## 五、断言、契约与防御边界

### 5.1 `assert`

```cpp
assert(index < values.size());
```

断言用于程序员错误和内部不变量，Release 构建可能禁用，不能用于用户输入或必须执行的安全检查。

### 5.2 前置条件与后置条件

```cpp
// 前置：denominator != 0
// 后置：返回 numerator / denominator
double divide(double numerator, double denominator);
```

公开 API 应记录调用者责任、成功结果、错误形式、失效规则和生命周期要求。

### 5.3 边界验证

在不可信数据进入系统的边界完整验证，内部使用已验证的强类型：

```text
原始文本 → 解析/验证 → ValidatedConfig → 核心逻辑
```

避免在每个内部函数重复松散检查。

### 5.4 C++26 Contracts 方向

C++26 引入契约相关语言和库支持，但编译器实现成熟度可能不同。本阶段应先掌握稳定的前置条件、断言和 API 契约思想，再根据工具链采用 C++26 语法。不要用尚未广泛支持的功能替代生产检查。

---

## 六、CMake 工程化

### 6.1 target 是核心

```cmake
cmake_minimum_required(VERSION 3.25)
project(deep_parser VERSION 1.0.0 LANGUAGES CXX)

add_library(deep_parser
    src/parser.cpp
)

target_compile_features(deep_parser PUBLIC cxx_std_23)
target_include_directories(deep_parser
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
```

编译要求、包含目录、宏和链接依赖都应附着在 target 上。

### 6.2 `PUBLIC`、`PRIVATE`、`INTERFACE`

- `PRIVATE`：只用于当前 target。
- `PUBLIC`：当前 target 使用，消费者也需要。
- `INTERFACE`：当前 target 自己不使用，仅消费者需要。

依赖传播应根据公开头文件是否暴露该依赖决定。

### 6.3 警告 target

```cmake
add_library(project_warnings INTERFACE)

target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(deep_parser PRIVATE project_warnings)
```

不要把第三方依赖也强制套用项目的 `-Werror`。

### 6.4 构建类型与多配置生成器

- 单配置生成器常通过 `CMAKE_BUILD_TYPE` 选择。
- Visual Studio 等多配置生成器在构建时通过 `--config Debug/Release` 选择。

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### 6.5 Presets

`CMakePresets.json` 可统一生成器、缓存变量和构建/测试配置，避免依赖个人命令历史。将通用 preset 纳入版本控制，个人配置放 `CMakeUserPresets.json`。

### 6.6 依赖管理

常见方式：

- 系统包和 `find_package`。
- vcpkg。
- Conan。
- `FetchContent`。

应固定可复现版本、检查许可证、避免每次配置静默拉取不受控主分支。

### 6.7 安装与导出

库项目应支持：

- 安装头文件和库。
- 导出 CMake targets。
- 生成 `Config.cmake` 和版本文件。
- 命名空间 target，例如 `deep::parser`。
- 构建树和安装树均可被测试消费。

### 6.8 推荐目录

```text
project/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ cmake/
├─ include/deep/parser.hpp
├─ src/parser.cpp
├─ apps/cli.cpp
├─ tests/
├─ benchmarks/
├─ fuzz/
├─ docs/
└─ README.md
```

---

## 七、测试体系

### 7.1 测试层次

- 单元测试：小范围、快速、定位精确。
- 集成测试：多个组件和真实依赖协作。
- 端到端测试：从用户入口验证完整流程。
- 回归测试：固定曾经失败的案例。
- 属性测试：验证广泛输入下的不变量。

### 7.2 测试结构

```cpp
TEST_CASE("valid integer is parsed")
{
    const auto result = parse_integer("42");
    REQUIRE(result.has_value());
    CHECK(*result == 42);
}
```

可选择 Catch2 或 GoogleTest。测试名称应描述行为，不要只写函数名。

### 7.3 测试边界

至少覆盖：

- 空输入。
- 最小和最大值。
- 边界前后。
- 重复数据。
- 非法编码或格式。
- 资源失败。
- 异常和回滚路径。

### 7.4 Mock 与依赖注入

只有需要隔离昂贵、非确定或外部依赖时使用 Mock。优先通过小接口、函数对象或内存实现替换依赖，避免测试与调用细节过度耦合。

### 7.5 属性测试

示例属性：

- `decode(encode(x)) == x`。
- 排序结果有序且元素多重集合不变。
- 缓存大小永不超过容量。
- 序列化结果再次解析等价。

属性测试能生成大量输入，特别适合解析器、容器和算法。

### 7.6 CTest

```cmake
include(CTest)
if(BUILD_TESTING)
    add_executable(parser_tests tests/parser_tests.cpp)
    target_link_libraries(parser_tests PRIVATE deep_parser Catch2::Catch2WithMain)
    add_test(NAME parser_tests COMMAND parser_tests)
endif()
```

### 7.7 覆盖率

覆盖率帮助发现未执行区域，但 100% 行覆盖不等于正确。更重要的是分支、边界、错误路径和断言质量。覆盖率构建应与性能构建分开。

---

## 八、动态与静态分析

### 8.1 Sanitizers

常用工具：

- AddressSanitizer：越界、use-after-free 等。
- UndefinedBehaviorSanitizer：多种未定义行为。
- ThreadSanitizer：数据竞争。
- LeakSanitizer：泄漏，支持视平台而定。
- MemorySanitizer：未初始化读取，工具链限制较多。

GCC/Clang 示例：

```powershell
cmake -S . -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

不要把互不兼容的 Sanitizer 随意组合。MSVC 支持范围也不同。

### 8.2 静态分析

- 编译器警告。
- `clang-tidy`。
- MSVC Code Analysis。
- cppcheck 等辅助工具。

静态分析应配置项目规则、固定版本并逐步清理基线，避免海量噪声使团队忽略真正问题。

### 8.3 格式化

`clang-format` 解决风格一致性，不证明代码正确。配置文件纳入版本控制，并在 CI 检查改动是否符合格式。

### 8.4 模糊测试

解析器特别适合 fuzzing：

```cpp
extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data, std::size_t size)
{
    const auto text = std::string_view{
        reinterpret_cast<const char*>(data), size};
    (void)parse(text);
    return 0;
}
```

目标是让工具自动寻找崩溃、超时、Sanitizer 报告和不变量破坏。保存最小失败输入为回归测试。

### 8.5 基准测试

使用 Google Benchmark 等工具时：

- 使用优化构建。
- 防止计算被优化掉。
- 分离准备阶段。
- 使用真实规模和数据分布。
- 报告编译器、选项和硬件。
- 不把微基准结果直接外推到整个系统。

---

## 九、库、链接与 ABI

### 9.1 静态库与动态库

- 静态库代码在链接时进入最终程序。
- 动态库在加载或运行时解析，多个程序可共享，但引入部署和 ABI 问题。

选择应考虑部署、插件需求、更新策略、体积和平台规则。

### 9.2 符号与可见性

动态库应明确导出公开 API，隐藏内部符号。Windows 常使用 `__declspec(dllexport/dllimport)` 宏，GCC/Clang 可使用 visibility 属性与默认隐藏策略。

### 9.3 ABI 风险

ABI 可能受以下因素影响：

- 编译器与版本。
- 标准库实现和构建模式。
- 类布局、虚函数表。
- 异常和 RTTI 设置。
- 编译选项、调用约定和对齐。
- 公开模板与 inline 实现。

“源代码兼容”不等于“二进制兼容”。

### 9.4 PImpl

```cpp
class Widget
{
public:
    Widget();
    ~Widget();
    Widget(Widget&&) noexcept;
    Widget& operator=(Widget&&) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

PImpl 可隐藏依赖和布局，降低重编译并改善 ABI 稳定性，但会增加分配、间接访问和实现复杂度。

### 9.5 C ABI 边界

跨编译器或语言边界可暴露窄 C API：

- 不让异常越界。
- 使用不透明句柄。
- 明确内存由哪一侧分配和释放。
- 明确字符串编码与长度。
- 返回稳定错误码。

### 9.6 版本策略

使用语义化版本或项目明确规则记录：

- 源兼容变化。
- ABI 破坏变化。
- 弃用周期。
- 最低编译器和标准库版本。
- 支持平台矩阵。

---

## 十、日志与可观测性

### 10.1 日志级别

- trace/debug：开发诊断。
- info：关键正常事件。
- warning：可恢复异常状态。
- error：操作失败。
- critical：系统无法继续或核心功能失效。

### 10.2 结构化日志

相比拼接长文本，结构化字段更易查询：

```text
event=parse_failed file=config.json line=12 code=invalid_number
```

不要记录密码、令牌、隐私数据或未经清理的大块用户输入。

### 10.3 日志与错误传播

底层函数通常返回错误上下文，高层在决定处理方式的边界记录一次。捕获后完全处理的错误可降级记录；继续传播时避免每层重复输出。

### 10.4 `std::source_location`

```cpp
void log(std::string_view message,
         std::source_location location = std::source_location::current());
```

可自动携带文件、行号和函数信息，适合诊断设施。

---

## 十一、CI 与质量门禁

建议流水线至少包含：

1. 配置并构建 Debug 与 Release。
2. 多编译器或至少主支持编译器。
3. 单元和集成测试。
4. 严格警告。
5. 格式检查。
6. 静态分析。
7. ASan/UBSan 测试任务。
8. 安装后消费者测试。
9. 可选覆盖率与 fuzzing 定时任务。

### 11.1 可复现性

- 锁定依赖版本。
- 记录编译器和构建选项。
- 不依赖开发者机器上的隐式路径。
- 测试不依赖执行顺序。
- 随机测试失败时记录 seed。
- 时间和时区敏感测试注入时钟。

### 11.2 警告作为错误

自身代码可在 CI 使用 `-Werror` 或 `/WX`，但发布库时不应把该选项传播给消费者，也不要让第三方头文件警告阻塞项目。

---

## 十二、阶段项目

### 项目一：生产级配置解析库

最低要求：

- 输入与核心模型分离。
- 使用 `expected<Config, ParseError>`。
- 错误包含代码、位置和上下文。
- 有单元、边界和属性测试。
- 支持 CMake 安装与 `find_package`。
- 提供命令行示例程序。
- 通过 ASan/UBSan 和静态分析。

进阶要求：

- 添加 libFuzzer 目标。
- PImpl 隐藏解析器实现。
- 测量大配置性能。
- 安装树消费者测试。

### 项目二：跨平台日志库

- 日志级别、结构化字段和 source location。
- 输出目标通过接口或可调用对象注入。
- 文件资源使用 RAII。
- 写入失败策略明确。
- 不在析构函数抛异常。
- 测试格式、过滤和错误路径。

### 项目三：事务型文件保存器

- 写入同目录临时文件。
- 完成后原子或尽可能安全地替换目标。
- 失败时保留原文件并清理临时资源。
- 明确不同平台语义。
- 提供强异常安全保证。
- 故障注入测试每个步骤。

### 项目四：可发布算法库

- 公共头文件与实现分离。
- Concepts 约束泛型接口。
- CMake export、install 和版本文件。
- Catch2/GoogleTest 测试。
- Google Benchmark 基准。
- CI 构建矩阵。
- README、API 示例、版本与许可证信息。

### 项目验收标准

- 有书面的错误处理策略。
- API 失败语义和异常保证明确。
- 资源在所有失败路径正确清理。
- CMake 不使用不必要的全局设置。
- 测试覆盖边界、失败与回滚路径。
- Debug、Release、Sanitizer 构建通过。
- 库可安装并被独立消费者使用。
- 日志不重复且不泄露敏感信息。
- ABI 和版本兼容范围有文档。

---

## 十三、九周学习安排

### 第 1 周：错误模型

- 错误分类。
- optional、expected、error_code 与异常选择。
- 编写项目错误策略。

### 第 2 周：异常与栈展开

- 自定义异常、捕获和重新抛出。
- RAII 与异常边界。
- noexcept。

### 第 3 周：异常安全

- 四级保证。
- prepare-commit、swap 和事务守卫。
- 故障注入测试。

### 第 4 周：现代 CMake

- target、依赖传播和 presets。
- 测试 target。
- 安装与导出。

### 第 5 周：测试体系

- 单元、集成、属性与回归测试。
- Mock 和依赖注入。
- 覆盖率。

### 第 6 周：质量工具

- Sanitizers。
- clang-tidy、格式检查。
- fuzzing 和基准测试。

### 第 7 周：库与 ABI

- 静态/动态链接。
- 符号可见性、PImpl、C ABI。
- 版本策略。

### 第 8 周：CI 与诊断

- 构建矩阵和质量门禁。
- 日志、source_location。
- 可复现构建。

### 第 9 周：综合项目

- 完成配置解析库或算法库。
- 安装消费者验证。
- 运行全部质量检查并复盘。

---

## 十四、阶段自测

### 概念题

1. 程序员错误与可预期业务失败应如何区别？
2. optional 与 expected 分别表达什么？
3. error_code 的错误类别有什么用途？
4. 异常传播时栈展开会做什么？
5. 为什么通常按 `const&` 捕获异常？
6. `throw;` 与 `throw error;` 有何差异？
7. 为什么析构函数不应抛异常？
8. 四级异常安全保证分别是什么？
9. 如何使用 prepare-commit 提供强保证？
10. assert 为什么不能验证用户输入？
11. CMake 的 PUBLIC、PRIVATE、INTERFACE 如何选择？
12. 为什么应使用 target 而非全局编译设置？
13. 单元测试与集成测试的边界是什么？
14. 100% 行覆盖为什么不等于正确？
15. ASan、UBSan 和 TSan 分别发现什么？
16. fuzzing 最适合哪些组件？
17. 源兼容与 ABI 兼容有何不同？
18. PImpl 的收益和成本是什么？
19. 为什么异常不应越过 C ABI？
20. 底层和高层为什么不应重复记录同一错误？

### 编程题

1. 将一个混用魔法值和异常的模块重构为一致错误模型。
2. 实现强保证的配置替换操作。
3. 编写不抛异常的事务回滚守卫。
4. 建立包含库、CLI 和测试的 CMake 工程。
5. 为解析器编写 fuzz target。
6. 使用 PImpl 隐藏库实现。
7. 编写安装后独立消费者测试。
8. 设计 CI 质量矩阵。

### 进入下一阶段前的检查清单

- [ ] 能根据错误语义选择处理机制。
- [ ] 理解栈展开和异常边界。
- [ ] 能说明 API 的异常安全保证。
- [ ] 会使用 RAII 实现自动回滚。
- [ ] 能编写 target-based CMake。
- [ ] 能安装和导出 CMake 库。
- [ ] 能设计多层测试体系。
- [ ] 熟练使用 Sanitizer 和静态分析。
- [ ] 能为解析器建立 fuzzing。
- [ ] 理解 ABI、PImpl 和 C 边界。
- [ ] 完成至少一个可安装综合项目。
- [ ] CI 质量门禁全部通过。

---

## 十五、易错点汇总

1. 用异常表达普通业务分支。
2. 用 optional 隐藏调用者需要的错误原因。
3. 返回错误码却不强制或检查结果。
4. 每一层都重复记录同一异常。
5. 通过解析 `what()` 文本分类异常。
6. 捕获异常后用 `throw error;` 重新抛出。
7. 析构或删除器让异常逃出。
8. 错误声明 `noexcept` 导致意外 terminate。
9. 只测试成功路径。
10. 用 assert 验证外部输入。
11. CMake 使用全局 include 和 compile flags。
12. PUBLIC/PRIVATE 传播范围错误。
13. 构建时拉取未锁定的依赖主分支。
14. 用覆盖率数字代替测试质量。
15. 只在 Debug 构建测试。
16. 把 `-Werror` 传播给库消费者或第三方代码。
17. 混用不兼容 Sanitizer 而不理解限制。
18. 微基准准备工作混入测量。
19. 将 C++ 异常和 STL 类型直接暴露到不稳定 ABI。
20. 日志包含密钥、令牌或隐私数据。

---

## 十六、下一阶段

下一阶段建议学习并发编程：

- `thread`、`jthread` 和停止令牌。
- mutex、锁、条件变量。
- semaphore、latch 和 barrier。
- future、promise、packaged_task。
- C++ 内存模型和原子操作。
- lock-free 基础与 ABA 问题。
- C++20 协程和异步任务模型。

完成本阶段的真正标志，是把一个库交给另一位开发者后，对方能够可靠构建、测试、安装和诊断它，并能从接口明确知道每种失败如何发生、如何传播以及由谁处理。
