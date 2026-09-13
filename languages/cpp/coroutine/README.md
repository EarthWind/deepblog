# C++ 协程专题

本目录把语言语义、可运行示例和 GCC 实现源码阅读放在一起：

1. [C++20 协程原理：从编译器变换到 Task 与事件循环](./C++20协程原理：从编译器变换到Task与事件循环.md)：从标准协议出发，实现 `Generator<T>`、`Task<T>` 和单线程事件循环；
2. [GCC 16 如何实现 C++20 协程：从语义分析到状态机 Lowering](./GCC如何实现C++20协程：从语义分析到状态机Lowering.md)：沿 GCC 16 源码追踪前端语义分析、frame、ramp/actor/destroyer、协程 IFN 和 GIMPLE pass；
3. [src](./src/)：完整教学程序、GCC lowering 探针与 CMake 构建文件；
4. [images](./images/)：文章使用的图片资源。

建议先读语言原理篇，再运行 `src/gcc_coroutine_probe.cpp`，最后对照 GCC 实现篇阅读 dump 和 GCC 源码。
