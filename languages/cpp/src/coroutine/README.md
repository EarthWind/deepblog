# C++20 协程教学实现

`mini_coroutine.cpp` 包含三个刻意保持最小的组件：

- 惰性、同步、单遍历的 `Generator<T>`；
- 单线程 ready queue + timer queue 事件循环；
- 惰性、单消费者、支持异常和嵌套 `co_await` 的 `Task<T>`。

它用于解释编译器协议，不是生产级异步运行时。它没有实现取消、背压、线程安全、`Task<void>`、多等待者和 I/O 驱动。

直接编译：

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
  mini_coroutine.cpp -o mini_coroutine
./mini_coroutine
```

使用 CMake：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

预期输出：

```text
fibonacci: 0 1 1 2 3 5 8 13
pipeline result: 25
independent result: 25
task error: value must not be negative
```
