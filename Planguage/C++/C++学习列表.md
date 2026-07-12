下面是一条从零基础到 C++26 的完整学习路线。建议按阶段推进，不要简单地按照 C++98、11、14、17……逐版背诵；更高效的方法是先掌握现代 C++ 主干，再补充各版本演进。

> 截至 2026 年 7 月，C++26 仍应按照“工作草案及编译器实验性支持”看待。提案不等于标准，实际可用性需要检查编译器支持情况。[WG21 官方资料](https://www.open-std.org/JTC1/SC22/WG21/)、[C++26 编译器支持表](https://en.cppreference.com/w/cpp/compiler_support/26)

## 第一阶段：编程与 C++ 基础

目标：能够独立编写简单的命令行程序。

### 1. 开发环境

- 安装 GCC、Clang 或 MSVC
- IDE：Visual Studio、VS Code、CLion
- 编译、链接、运行的基本过程
- 常用编译参数：
  - `-std=c++23`
  - `-Wall -Wextra -Wpedantic`
  - `-g`
  - `-O0`、`-O2`
- 源文件、头文件、目标文件、可执行文件
- Debug 与 Release 模式

### 2. 基本语法

- 注释、关键字、标识符
- `main()` 函数
- 变量与常量
- 基本类型：
  - `bool`
  - `char`
  - 整数类型
  - 浮点类型
  - `void`
- 字面量与后缀
- `sizeof`、`alignof`
- 自动类型推导：`auto`
- 类型别名：`using`
- 作用域与生命周期

### 3. 表达式与控制流

- 算术、关系、逻辑、位运算
- 赋值与复合赋值
- 运算符优先级
- 隐式和显式类型转换
- `if`、`switch`
- `for`、范围 `for`
- `while`、`do-while`
- `break`、`continue`
- 三元运算符

### 4. 输入输出

- `std::cin`、`std::cout`、`std::cerr`
- `<iomanip>`
- 字符串输入
- 文件输入输出
- C++20 `std::format`
- C++23 `std::print`、`std::println`

### 练习项目

- 四则运算计算器
- 猜数字游戏
- 成绩统计程序
- 文本菜单系统
- 单词计数器

---

## 第二阶段：函数、数组、字符串与引用

目标：理解程序如何组织和传递数据。

### 1. 函数

- 声明与定义
- 参数与返回值
- 值传递、引用传递、指针传递
- 默认参数
- 函数重载
- 内联函数
- 递归
- 函数指针
- `constexpr`、`consteval`、`constinit`
- `noexcept`
- 尾置返回类型

### 2. 数组与字符串

- 原生数组
- 多维数组
- `std::array`
- C 风格字符串
- `std::string`
- `std::string_view`
- 字符编码基础：
  - ASCII
  - UTF-8
  - Unicode
  - `char8_t`

### 3. 指针与引用

- 地址与解引用
- 空指针 `nullptr`
- 指针运算
- 左值引用
- `const` 引用
- 指针与数组的关系
- 悬空指针和野指针
- 基本内存模型：栈、堆、静态存储区

### 练习项目

- 字符串处理工具
- 矩阵运算程序
- 通讯录
- 简单文本编辑器
- CSV 文件读取器

---

## 第三阶段：面向对象编程

目标：能够设计具有清晰职责的类。

### 1. 类的基础

- 类与对象
- 成员变量、成员函数
- `public`、`private`、`protected`
- 构造函数与析构函数
- 初始化列表
- 默认构造、复制构造、移动构造
- 复制赋值、移动赋值
- `this` 指针
- `static` 成员
- `const` 成员函数
- `explicit`
- `friend`
- 嵌套类

### 2. 对象生命周期

- RAII
- Rule of Zero
- Rule of Three
- Rule of Five
- 深复制与浅复制
- 对象切片
- 返回值优化 RVO/NRVO
- 复制消除
- 移动语义

### 3. 继承与多态

- 单继承、多继承
- 虚函数
- 纯虚函数与抽象类
- `override`、`final`
- 虚析构函数
- 动态绑定
- 虚继承
- 菱形继承
- 组合优于继承
- 接口设计

### 4. 运算符重载

- 算术、比较、下标、调用运算符
- 输入输出运算符
- 三路比较运算符 `<=>`
- 自定义转换
- 不应重载运算符的场景

### 练习项目

- 日期与时间类
- 高精度整数类
- 图形类层次结构
- 银行账户管理系统
- 简易棋类游戏

---

## 第四阶段：资源管理与现代 C++

目标：写出没有明显资源泄漏、符合现代 C++ 风格的代码。

### 1. 动态内存

- `new`、`delete`
- `new[]`、`delete[]`
- 内存泄漏
- 重复释放
- 越界访问
- 未定义行为

应当理解它们，但业务代码优先使用容器和智能指针。

### 2. 智能指针

- `std::unique_ptr`
- `std::shared_ptr`
- `std::weak_ptr`
- `std::make_unique`
- `std::make_shared`
- 自定义删除器
- 循环引用
- 所有权设计

### 3. 移动与完美转发

- 左值、右值
- 值类别：
  - lvalue
  - prvalue
  - xvalue
  - glvalue
- 右值引用
- `std::move`
- `std::forward`
- 转发引用
- 完美转发
- `noexcept` 对容器移动行为的影响

### 4. Lambda

- 捕获列表
- 值捕获与引用捕获
- 初始化捕获
- 泛型 Lambda
- 可变 Lambda
- 递归 Lambda
- `constexpr`、`consteval` Lambda
- 模板 Lambda
- 显式对象参数与 Lambda

### 练习项目

- RAII 文件包装器
- 简易智能指针
- 事件回调系统
- 对象池
- 命令模式任务系统

---

## 第五阶段：标准模板库 STL

目标：熟练使用标准容器和算法解决问题。

### 1. 序列容器

- `vector`
- `deque`
- `list`
- `forward_list`
- `array`
- `basic_string`

重点理解：

- 大小与容量
- 扩容
- 迭代器失效
- 时间复杂度
- 内存连续性

### 2. 关联容器

- `set`、`multiset`
- `map`、`multimap`
- `unordered_set`
- `unordered_map`
- 比较器与哈希函数
- 透明比较
- `flat_map`、`flat_set`（C++23）

### 3. 容器适配器与词汇类型

- `stack`
- `queue`
- `priority_queue`
- `tuple`
- `pair`
- `optional`
- `variant`
- `any`
- `expected`（C++23）
- `span`
- `mdspan`（C++23）

### 4. 迭代器与算法

- 迭代器分类
- `begin`、`end`
- 查找、排序、复制、变换
- 数值算法
- 删除—擦除惯用法
- 谓词与函数对象
- 投影
- 并行算法
- 算法复杂度分析

### 5. Ranges

- range 与 view
- `std::ranges` 算法
- 管道表达式
- `views::filter`
- `views::transform`
- `views::take`、`drop`
- `views::split`、`join`
- `views::zip`（C++23）
- 惰性求值
- 借用范围
- dangling range
- 自定义 view

### 练习项目

- 图书管理系统
- 日志分析器
- 文本搜索工具
- LRU 缓存
- 基于 Ranges 的数据处理流水线

---

## 第六阶段：模板与泛型编程

目标：能够阅读和编写中等复杂度的泛型库。

### 1. 模板基础

- 函数模板
- 类模板
- 变量模板
- 别名模板
- 模板参数推导
- CTAD
- 非类型模板参数
- 模板特化与偏特化
- 可变参数模板
- 参数包展开
- 折叠表达式

### 2. 类型系统工具

- `<type_traits>`
- `decltype`
- `decltype(auto)`
- `std::decay`
- `std::remove_reference`
- `std::conditional`
- `std::common_type`
- `std::invoke`
- `std::reference_wrapper`

### 3. 模板实例化机制

- 两阶段名称查找
- 依赖名称
- `typename`、`template`
- ADL
- ODR
- SFINAE
- 检测惯用法
- CRTP
- tag dispatch
- 编译期多态

### 4. Concepts

- `concept`
- `requires` 子句
- requires-expression
- 标准 Concepts
- 约束组合
- 约束排序
- 用 Concepts 替代部分 SFINAE
- 良好的约束与错误信息设计

### 练习项目

- 泛型矩阵库
- 固定容量容器
- 类型安全事件系统
- 序列化框架
- 自定义 Ranges 算法

---

## 第七阶段：异常、错误处理与工程设计

目标：构建可维护、可测试的中型程序。

### 1. 错误处理

- `throw`、`try`、`catch`
- 异常传播
- 栈展开
- 异常安全等级
- 自定义异常
- `std::exception`
- `std::error_code`
- `std::optional`
- `std::expected`
- 异常与错误值的选择
- `noexcept`
- 断言与契约式设计思想

### 2. 项目组织

- `.h/.hpp` 与 `.cpp`
- include guard、`#pragma once`
- 前置声明
- PImpl
- 静态库与动态库
- ABI 基础
- 命名空间
- ODR
- 模块边界设计

### 3. 构建系统

- CMake：
  - target
  - include directory
  - link library
  - package
  - install
  - preset
- Ninja
- vcpkg 或 Conan
- 项目目录设计
- 跨平台构建

### 4. 测试与质量工具

- GoogleTest 或 Catch2
- 单元测试、集成测试
- Mock 与依赖注入
- Sanitizers：
  - AddressSanitizer
  - UndefinedBehaviorSanitizer
  - ThreadSanitizer
- Valgrind
- `clang-tidy`
- `clang-format`
- 静态分析
- fuzz testing
- 基准测试
- 代码覆盖率

### 练习项目

- 带测试的配置文件库
- 跨平台命令行工具
- 日志库
- JSON 序列化库
- 插件系统

---

## 第八阶段：并发编程

目标：理解线程安全、内存模型和现代异步抽象。

### 1. 基础并发

- 进程与线程
- `std::thread`
- `std::jthread`
- `stop_token`
- `mutex`
- `lock_guard`
- `unique_lock`
- `scoped_lock`
- `condition_variable`
- `semaphore`
- `latch`
- `barrier`

### 2. 异步任务

- `std::future`
- `std::promise`
- `std::packaged_task`
- `std::async`
- 线程池
- 任务队列
- 工作窃取基本思想

### 3. 原子操作与内存模型

- 数据竞争
- happens-before
- `std::atomic`
- compare-and-exchange
- memory order：
  - relaxed
  - acquire
  - release
  - acq_rel
  - seq_cst
- fence
- false sharing
- lock-free 与 wait-free
- ABA 问题
- `atomic::wait/notify`

### 4. C++20 协程

- coroutine、promise、awaiter
- `co_await`
- `co_yield`
- `co_return`
- coroutine frame
- eager 与 lazy coroutine
- generator
- task
- 异常与生命周期
- 协程不是线程
- 协程与事件循环、异步 I/O 的结合

### 练习项目

- 线程池
- 生产者—消费者队列
- 并行文件搜索
- 协程生成器
- 简易异步网络服务器

---

## 第九阶段：底层原理与高性能 C++

目标：理解语言背后的机器模型，能够分析性能和未定义行为。

### 1. 对象与内存模型

- 对象表示和值表示
- alignment 与 padding
- trivial、standard-layout
- 生命周期开始与结束
- placement new
- strict aliasing
- pointer provenance
- `std::launder`
- `std::bit_cast`
- endian
- volatile 的真实用途
- 未定义、未指定、实现定义行为

### 2. 编译与链接

- 预处理
- 编译器前端、优化器、后端
- 符号与名称修饰
- 静态链接与动态链接
- ELF/PE 基础
- vtable、RTTI 的常见实现
- 模板代码膨胀
- LTO
- PGO
- 汇编代码基本阅读
- Compiler Explorer

### 3. 性能分析

- Big-O 与实际常数
- cache locality
- CPU cache 层级
- branch prediction
- SIMD
- 内存分配成本
- benchmark 陷阱
- profiling
- perf、VTune、Visual Studio Profiler
- 数据导向设计
- AoS 与 SoA
- 小对象优化

### 4. 自定义内存管理

- allocator
- `std::allocator_traits`
- polymorphic allocator
- `<memory_resource>`
- arena、pool、monotonic resource
- intrusive data structure
- 自定义容器的异常安全

### 练习项目

- Arena allocator
- 固定块内存池
- 高性能哈希表
- SIMD 图像处理
- 无锁队列（高级练习）

---

## 第十阶段：C++20 专题

应系统掌握：

- Concepts
- Ranges
- Coroutines
- Modules
- 三路比较 `<=>`
- `consteval`、`constinit`
- `std::span`
- `std::format`
- `std::jthread`
- semaphore、latch、barrier
- `atomic::wait/notify`
- `std::source_location`
- 日历与时区
- 位操作库
- feature-test macros

### Modules

重点学习：

- `export module`
- `import`
- module interface/unit
- module partition
- header unit
- 模块与传统头文件混用
- 构建系统对模块的支持
- 模块不等于包管理器
- 模块主要解决编译隔离和宏污染，而非所有构建问题

---

## 第十一阶段：C++23 专题

重点覆盖：

### 语言特性

- 显式对象参数（deducing `this`）
- `if consteval`
- 多维下标运算符
- `static operator()`
- Lambda 属性改进
- 扩展浮点类型
- `auto(x)` 与 `auto{x}`
- 简化隐式移动

### 标准库

- `std::expected`
- `std::print`、`std::println`
- `std::stacktrace`
- `std::mdspan`
- `std::generator`
- `std::flat_map`、`std::flat_set`
- `std::move_only_function`
- `std::byteswap`
- `std::unreachable`
- `std::to_underlying`
- `std::out_ptr`、`std::inout_ptr`
- `std::ranges::to`
- `views::zip`
- `views::enumerate`
- `views::repeat`
- monadic `optional` 和 `expected`
- 容器范围操作，如 `append_range`

### 练习项目

- 使用 `expected` 的解析器
- 使用 `generator` 的惰性数据流
- 使用 `mdspan` 的矩阵库
- 使用 `stacktrace` 的诊断系统

---

## 第十二阶段：C++26 专题

C++26 内容较多，不需要初学阶段逐项钻研。应优先掌握以下主线。

### 1. Contracts

- 函数前置条件与后置条件
- assertion
- contract violation
- 契约检查语义
- 契约与异常、断言、静态分析的区别
- API 契约设计
- 标准库 hardening

### 2. Execution Control Library

围绕 sender/receiver 模型学习：

- sender
- receiver
- scheduler
- execution environment
- completion signatures
- `connect`、`start`
- `just`
- `then`
- `let_value`
- `when_all`
- error 与 stopped 通道
- structured concurrency
- 协程和 sender 的互操作
- 异步任务组合

这是 C++26 并发与异步编程最值得重点投入的方向之一。

### 3. Pack Indexing

- 参数包按索引访问
- 类型包和表达式包
- 减少递归模板与辅助类型
- 与可变参数模板、tuple 元编程结合

### 4. 编译期能力增强

- `constexpr` placement new
- 更多 constexpr 容器与适配器
- constexpr 异常类型
- constexpr 稳定排序
- constexpr 格式化
- 用户生成的 `static_assert` 消息
- 从 `void*` 进行 constexpr 转换
- 编译期类型排序
- 常量表达式诊断能力

### 5. 新容器与所有权类型

- `std::inplace_vector`
  - 固定容量
  - 运行时可变长度
  - 无动态分配
- `std::hive`
  - 擦除后复用存储
  - 元素地址稳定性
- `std::indirect`
- `std::polymorphic`
- 更丰富的 `optional` range 支持

### 6. SIMD 与数值计算

- `<simd>`
- 数据并行类型
- mask
- SIMD 数学操作
- permutation
- `std::simd::chunk`
- SIMD 与自动向量化的关系
- `<linalg>` 线性代数接口
- 饱和算术
- `std::philox_engine`
- `mdspan` 改进

### 7. 并发与无锁内存回收

- Hazard Pointers
- Read-Copy Update（RCU）
- 原子浮点 min/max
- 原子 reduction
- observable checkpoints
- `memory_order::consume` 的弱化与弃用方向

这部分属于高级内容，需要先掌握 C++ 内存模型和无锁编程。

### 8. Ranges 扩展

- 并行 range 算法
- `views::indices`
- `as_input_view`
- `approximately_sized_range`
- `reserve_hint`
- filter view 安全性改进
- integer sequence 的结构化绑定及展开支持

### 9. 诊断、文本与其他库扩展

- `<debugging>`
- `std::text_encoding`
- 动态格式字符串
- 指针格式化
- `filesystem::path` 显示与原生编码字符串
- `exception_ptr_cast`
- `function_ref` 相关改进
- freestanding 标准库扩展
- `<stdbit.h>` 与 `<stdckdint.h>`

C++26 各项功能的编译器支持并不同步，练习时应结合 feature-test macro，而不是只判断 `__cplusplus`。[C++26 支持状态](https://en.cppreference.com/w/cpp/compiler_support/26)

---

## 第十三阶段：专业方向选择

完成公共主线后，选择一到两个方向深入。

### 系统与基础设施

- 操作系统原理
- 网络编程
- 文件系统
- IPC
- 序列化与 RPC
- 高并发服务器
- io_uring、IOCP
- lock-free 数据结构

### 游戏开发

- 数学与线性代数
- ECS
- 渲染管线
- OpenGL、Vulkan、DirectX
- 物理引擎
- Unreal Engine
- 资源管理与帧性能

### 嵌入式

- freestanding C++
- 中断与寄存器
- 内存映射 I/O
- 无动态内存设计
- 编译期配置
- 实时系统
- MISRA C++、AUTOSAR C++

### 高性能计算

- SIMD
- OpenMP
- MPI
- CUDA、HIP、SYCL
- BLAS
- 稀疏矩阵
- 性能建模
- NUMA

### 桌面与应用开发

- Qt
- GUI 事件模型
- Model/View
- 多线程 UI
- 插件系统
- 跨平台打包

### 编译器与语言工具

- 词法、语法分析
- AST
- 类型检查
- IR
- LLVM
- 静态分析
- Clang tooling

---

## 推荐学习顺序

可以按下面的顺序安排：

1. 基础语法、函数、字符串和容器
2. 类、RAII、智能指针
3. STL 算法与 Ranges
4. 工程化、CMake、测试和调试
5. 模板、Concepts 和泛型编程
6. 并发、内存模型和协程
7. C++20、C++23 的重要能力
8. 底层原理与性能优化
9. C++26 的 Contracts、Execution、SIMD 等主线
10. 选择专业方向深入

## 建议的学习周期

以每天学习 1.5～2 小时为例：

| 阶段 | 预计时间 |
|---|---:|
| 基础语法 | 1～2 个月 |
| 类、RAII、STL | 2～3 个月 |
| 工程化与项目实践 | 1～2 个月 |
| 模板与现代 C++ | 2～3 个月 |
| 并发、协程与内存模型 | 2～4 个月 |
| 底层原理和性能 | 3～6 个月 |
| C++26 与专业方向 | 持续学习 |

完整掌握不应以“背完知识点”为标准。比较实际的里程碑是：

- 3 个月：能写结构清楚的小型程序
- 6 个月：能完成带测试和构建系统的项目
- 12 个月：能阅读多数现代 C++ 项目
- 18～24 个月：能在一个专业方向承担中等复杂度工作
- 此后：通过真实项目持续深化

最重要的原则是：以 C++20/23 的现代写法为主线，理解旧代码，但避免在新代码里主动使用裸资源管理、宏技巧和复杂继承。C++26 则先掌握方向与模型，等编译器和标准库实现成熟后再投入生产使用。