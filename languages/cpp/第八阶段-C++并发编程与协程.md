# 第八阶段：C++ 并发编程与协程

> 前置要求：掌握 RAII、智能指针、Lambda、模板、异常安全和工程化测试。  
> 建议标准：C++23  
> 建议周期：10～12 周，每天 1～2 小时  
> 阶段目标：能够编写可停止、可测试、无数据竞争的并发组件，并理解原子内存序与协程生命周期。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 区分并发、并行、异步和多线程。
2. 正确管理 `thread`、`jthread` 及其参数生命周期。
3. 使用 mutex、锁和条件变量保护共享状态。
4. 使用 semaphore、latch 和 barrier 协调阶段任务。
5. 使用 future、promise 和 packaged_task 传递结果与异常。
6. 设计具备关闭、排空和异常边界的线程池。
7. 解释数据竞争、happens-before 和内存序。
8. 正确使用常见 atomic 操作和 wait/notify。
9. 理解 lock-free、ABA 和安全内存回收的困难。
10. 编写基础 generator/task 协程并分析 coroutine frame 生命周期。

并发代码的第一目标是正确性。没有测量证据时，不要用无锁结构替代简单的锁。

---

## 二、并发基础

### 2.1 核心概念

- 并发：多个任务在时间上重叠推进。
- 并行：多个任务在同一时刻实际执行。
- 异步：发起操作后不必同步等待完成。
- 多线程：用多个执行线程实现并发的一种方式。

异步不等于一定创建线程，协程也不等于线程。

### 2.2 为什么并发困难

- 执行顺序不确定。
- 失败难以复现。
- 对共享状态的错误访问可能是未定义行为。
- 生命周期跨越线程和回调边界。
- 锁顺序可能导致死锁。
- 性能受缓存、调度和同步影响。

### 2.3 并发设计优先级

1. 尽量避免共享可变状态。
2. 使用消息传递和任务分解。
3. 必须共享时明确不变量和同步策略。
4. 优先标准同步原语。
5. 通过 ThreadSanitizer 和压力测试验证。
6. 测量后才优化锁竞争。

---

## 三、线程与生命周期

### 3.1 `std::thread`

```cpp
#include <thread>

void work(int id)
{
    // ...
}

int main()
{
    std::thread worker{work, 1};
    worker.join();
}
```

可 join 的 `thread` 析构会调用 `std::terminate`，因此必须 `join()` 或明确 `detach()`。

### 3.2 避免 `detach`

分离线程会失去：

- 等待完成的简单方式。
- 可靠的异常与结果传播。
- 明确的关闭顺序。
- 对捕获对象生命周期的控制。

大多数业务代码优先使用 `jthread`、线程池或更高层异步设施。

### 3.3 `std::jthread`

```cpp
std::jthread worker{[](std::stop_token token) {
    while (!token.stop_requested()) {
        perform_one_step();
    }
}};
```

`jthread` 析构时会请求停止并 join，提供更安全的结构化生命周期。

### 3.4 参数传递

`thread` 构造通常复制或移动参数。传引用需显式：

```cpp
std::thread worker{update, std::ref(state)};
```

必须保证 `state` 在线程结束前一直存活。捕获局部引用后分离线程是典型悬空错误。

### 3.5 线程中的异常

异常不能自动跨线程传播。线程函数中未捕获异常会终止进程：

```cpp
std::jthread worker{[] {
    try {
        run();
    } catch (...) {
        report_current_exception();
    }
}};
```

需要传播结果时使用 promise/future 或任务系统。

### 3.6 硬件并发

```cpp
const auto count = std::thread::hardware_concurrency();
```

它只是提示，可能返回 0，也不等于最佳线程数。线程数还取决于任务阻塞比例、NUMA、配额和实际负载。

### 本节练习

1. 使用多个 `jthread` 并行处理独立分块。
2. 演示线程参数按值与 `std::ref` 的区别。
3. 找出一个捕获悬空引用的线程 Lambda。
4. 为长任务添加协作停止。

---

## 四、互斥与锁

### 4.1 数据竞争

两个线程并发访问同一内存位置，至少一个访问是写，并且没有适当同步，就可能形成数据竞争。数据竞争属于未定义行为。

```cpp
int counter{};
// 多线程执行 ++counter; 不是安全的
```

### 4.2 `mutex` 与 RAII 锁

```cpp
class Counter
{
public:
    void increment()
    {
        std::lock_guard lock{mutex_};
        ++value_;
    }

private:
    std::mutex mutex_;
    int value_{};
};
```

不要手写容易遗漏异常路径的 `lock()`/`unlock()` 配对。

### 4.3 锁类型

- `lock_guard`：简单作用域加锁。
- `unique_lock`：可延迟、解锁、重新加锁，条件变量需要它。
- `scoped_lock`：一次安全锁住多个 mutex。
- `shared_lock`：配合 `shared_mutex` 进行共享读锁。

### 4.4 不变量与临界区

mutex 保护的不是“某一行”，而是一组共享状态及其不变量。相关字段必须在同一同步策略下访问。

尽量缩短临界区，但不要为了缩短而把需要原子完成的状态转换拆开。

### 4.5 死锁

常见条件：

- 互斥占有。
- 占有并等待。
- 不可抢占。
- 循环等待。

避免方法：

- 固定全局锁顺序。
- 使用 `scoped_lock` 同时获取多个锁。
- 不在持锁时调用未知外部代码。
- 避免嵌套锁。
- 用消息传递重新设计。

### 4.6 `shared_mutex`

允许多个读者或一个写者。但读多不代表一定更快；实现成本、写者饥饿和缓存竞争都需基准验证。

### 4.7 不要返回裸引用泄漏锁保护

```cpp
const Value& get() const; // 函数返回后锁已释放，引用可能被并发修改
```

可返回副本，或接收回调并在锁内执行受控操作。回调仍应避免重入和长时间执行。

### 本节练习

1. 为银行转账设计无死锁的双账户加锁。
2. 找出一段返回内部引用造成的同步泄漏。
3. 比较单 mutex 与 shared_mutex 的基准。
4. 写出组件的“哪些状态由哪个锁保护”表。

---

## 五、条件变量

### 5.1 等待状态变化

```cpp
std::mutex mutex;
std::condition_variable condition;
std::queue<Task> tasks;
bool stopping{};

void worker()
{
    for (;;) {
        std::unique_lock lock{mutex};
        condition.wait(lock, [] {
            return stopping || !tasks.empty();
        });

        if (stopping && tasks.empty()) {
            return;
        }

        Task task = std::move(tasks.front());
        tasks.pop();
        lock.unlock();
        task();
    }
}
```

### 5.2 为什么必须用谓词

条件变量可能虚假唤醒，且通知发生在等待前也不能被当作永久事件保存。共享状态才是真相，通知只是提示重新检查。

### 5.3 通知

- `notify_one()`：唤醒一个等待者。
- `notify_all()`：唤醒全部等待者。

通常先在锁内修改状态，再解锁并通知，以减少被唤醒线程立即阻塞；具体顺序需保证状态与等待协议正确。

### 5.4 超时等待

使用 `wait_for` 或 `wait_until` 时仍需谓词，并明确超时与状态满足的竞态语义。

### 5.5 停止与排空

队列关闭策略必须明确：

- 立即取消未执行任务。
- 拒绝新任务但排空已有任务。
- 请求任务协作停止。

析构中静默悬挂或丢弃任务通常不可接受。

### 本节练习

1. 实现有界生产者—消费者队列。
2. 正确处理虚假唤醒和队列关闭。
3. 分别实现立即停止与排空停止。
4. 压力测试多个生产者和消费者。

---

## 六、同步设施

### 6.1 semaphore

```cpp
std::counting_semaphore permits{4};

permits.acquire();
use_limited_resource();
permits.release();
```

适合限制并发数量、资源池许可和生产者消费者计数。应使用 RAII 守卫确保异常时归还许可。

### 6.2 latch

一次性倒计数同步点：

```cpp
std::latch ready{worker_count};
ready.count_down();
ready.wait();
```

适合等待一组初始化任务完成，不可重用。

### 6.3 barrier

可重复使用的阶段同步点。所有参与者到达后进入下一阶段，可配置完成函数。参与者数量和退出协议必须明确。

### 6.4 `call_once`

```cpp
std::once_flag flag;
std::call_once(flag, initialize);
```

用于一次初始化。函数抛异常时调用未成功，后续线程可重试。

函数局部静态变量初始化本身也是线程安全的，常是更简单的单例初始化方式。

---

## 七、异步结果设施

### 7.1 `std::async`

```cpp
auto result = std::async(std::launch::async, compute, input);
std::cout << result.get();
```

不指定策略时实现可选择异步或延迟执行。需要真实异步时明确 `launch::async`。

future 的析构和等待语义存在细节，不应把临时 future 当作“发后不管”。

### 7.2 promise 与 future

```cpp
std::promise<Result> promise;
auto future = promise.get_future();

std::jthread worker{[promise = std::move(promise)]() mutable {
    try {
        promise.set_value(compute());
    } catch (...) {
        promise.set_exception(std::current_exception());
    }
}};
```

future 的 `get()` 取得结果并重新抛出保存的异常，通常只能调用一次。

### 7.3 `shared_future`

多个观察者需要读取同一结果时，可从 future 转换为 `shared_future`。它共享结果，不意味着结果对象所有成员操作线程安全。

### 7.4 `packaged_task`

将可调用对象包装为与 future 相连的任务：

```cpp
std::packaged_task<int()> task{[] { return compute(); }};
auto future = task.get_future();
task();
```

适合放入任务队列，但 `packaged_task` 是 move-only 类型。

### 7.5 标准 future 的组合限制

C++23 标准 future 缺少完整的 continuation 组合接口。复杂异步图通常需要协程、第三方库或后续 C++26 execution 模型。

### 本节练习

1. 用 promise 在线程间传播值和异常。
2. 用 packaged_task 建立简单任务队列。
3. 比较 async 默认策略与明确 async 策略。
4. 设计多个消费者共享只读结果。

---

## 八、线程池

### 8.1 基本组成

```text
提交者 → 受保护任务队列 → N 个 worker
                ↑
          条件变量/关闭状态
```

线程池需要明确：

- 任务类型及所有权。
- 队列是否有界。
- 提交失败行为。
- 异常如何传播。
- 停止、排空与析构语义。
- worker 内提交新任务时的行为。

### 8.2 任务类型

C++23 可使用：

```cpp
using Task = std::move_only_function<void()>;
```

工具链未支持时，可使用 packaged_task、类型擦除包装器或受限的 `std::function`。

### 8.3 避免持锁执行任务

worker 应在锁内取出任务，释放锁后执行。否则所有 worker 会被串行化，并可能因任务重新提交或调用外部代码而死锁。

### 8.4 背压

无限队列可能在生产速度超过消费速度时耗尽内存。有界队列可以：

- 阻塞提交者。
- 返回拒绝结果。
- 超时。
- 丢弃或替换任务，但必须显式约定。

### 8.5 工作窃取概念

每个 worker 保存本地队列，空闲时从其他 worker 窃取任务，可减少中央队列竞争。但实现复杂度、负载公平性和内存模型要求显著提高。本阶段先完成可靠中央队列线程池。

---

## 九、C++ 内存模型

### 9.1 内存位置与修改顺序

每个原子对象有自己的 modification order。普通对象的并发冲突访问需要 happens-before 关系，否则形成数据竞争。

### 9.2 happens-before

happens-before 可由以下关系建立：

- 同一线程的 sequenced-before。
- mutex 解锁与随后成功加锁。
- 线程启动与 join。
- release/acquire 原子同步。
- 条件变量协议等标准同步设施。

它不是墙上时钟意义的“先发生”，而是标准定义的可见性和排序关系。

### 9.3 `volatile` 不是线程同步

`volatile` 主要用于特定硬件映射 I/O、信号等实现相关场景。它不提供原子性，也不建立线程间 happens-before。

### 9.4 编译器与 CPU 重排

只要保持单线程可观察行为，编译器和处理器可能重排操作。同步原语限制相关重排并建立可见性。不要依赖源代码行顺序推断线程间观察结果。

### 9.5 false sharing

不同线程修改不同变量，若变量落在同一缓存行，也可能产生严重缓存一致性流量。可通过数据布局、分片计数和 `hardware_destructive_interference_size`（实现支持时）缓解，但需性能分析验证。

---

## 十、原子操作

### 10.1 基础 atomic

```cpp
std::atomic<int> counter{0};
counter.fetch_add(1, std::memory_order_relaxed);
```

atomic 保证针对该对象的操作原子性。复合业务不变量涉及多个对象时，单独原子变量可能仍不够。

### 10.2 默认顺序一致

```cpp
counter.fetch_add(1); // 默认 memory_order_seq_cst
```

`seq_cst` 最易推理。先写正确版本，再在证明和基准支持下使用更弱内存序。

### 10.3 relaxed

`relaxed` 只保证原子性和该原子对象的修改顺序，不同步其他普通数据。适合不用于发布数据的统计计数等场景。

### 10.4 release/acquire

```cpp
Data data;
std::atomic<bool> ready{false};

// 生产者
data.initialize();
ready.store(true, std::memory_order_release);

// 消费者
if (ready.load(std::memory_order_acquire)) {
    data.use();
}
```

若 acquire 读取到对应 release 写入，release 前的操作对 acquire 后可见。

### 10.5 compare-exchange

```cpp
int expected = old_value;
while (!value.compare_exchange_weak(expected, new_value)) {
    // 失败时 expected 会更新为实际值
}
```

- `weak` 允许伪失败，适合循环。
- `strong` 不允许伪失败，适合不希望循环的比较。

循环中必须理解失败后 `expected` 的变化。

### 10.6 atomic wait/notify

```cpp
state.wait(old_state);
state.store(new_state);
state.notify_one();
```

C++20 atomic wait 可以避免忙等。仍需围绕状态写循环或协议，并选择正确内存序。

### 10.7 fence

内存栅栏是高级工具，容易写出表面合理但未建立正确同步的代码。优先在原子读写本身使用 acquire/release；只有能严格证明协议时才使用 fence。

### 10.8 lock-free 查询

```cpp
value.is_lock_free();
std::atomic<T>::is_always_lock_free
```

atomic 类型不保证一定用无锁指令实现。lock-free 也不自动意味着更快。

### 本节练习

1. 使用 relaxed atomic 实现纯统计计数并说明为何安全。
2. 使用 release/acquire 发布只读数据。
3. 实现 CAS 自增循环并处理 expected 更新。
4. 用 atomic wait/notify 实现状态通知。

---

## 十一、无锁编程基础

### 11.1 进展保证

- blocking：线程可能等待锁。
- lock-free：系统整体持续有线程取得进展。
- wait-free：每个操作在有限步骤内完成。

lock-free 不等于没有等待，也不等于实时安全。

### 11.2 ABA 问题

线程读取指针 A，期间其他线程把它改为 B 又改回 A。CAS 只看值相同，无法发现中间变化。可使用版本标签、不同算法或安全回收方案缓解。

### 11.3 内存回收

从无锁结构移除节点后，不能立即删除，因为其他线程可能仍持有指针。常见技术：

- Hazard Pointers。
- Epoch-Based Reclamation。
- Read-Copy Update。
- 引用计数及其变体。

安全内存回收往往比 CAS 本身更难。C++26 将加入 hazard pointer 和 RCU 相关标准库支持，但工具链成熟前应谨慎用于生产。

### 11.4 先使用成熟实现

无锁队列适合作为学习项目，但生产中优先成熟、经过证明和压力测试的库。ThreadSanitizer 也不能证明无锁算法正确。

---

## 十二、C++20 协程

### 12.1 协程是什么

包含以下关键字之一的函数可能成为协程：

- `co_await`。
- `co_yield`。
- `co_return`。

协程可暂停并稍后恢复。它本身不创建线程、不提供调度器，也不自动让阻塞 I/O 变成异步。

### 12.2 coroutine frame

编译器通常为协程状态创建 frame，保存：

- 参数副本。
- 跨暂停点存活的局部变量。
- promise 对象。
- 恢复位置和其他状态。

frame 何时分配、由谁销毁，是协程类型设计的核心。

### 12.3 promise type

返回类型通过 `promise_type` 定义协议：

- `get_return_object()`。
- `initial_suspend()`。
- `final_suspend()`。
- `return_value()` 或 `return_void()`。
- `yield_value()`。
- `unhandled_exception()`。

它不是 `std::promise`，只是名称相似。

### 12.4 awaiter 协议

`co_await expression` 最终得到 awaiter，核心接口：

```cpp
bool await_ready();
auto await_suspend(std::coroutine_handle<> handle);
Result await_resume();
```

- ready：是否无需暂停。
- suspend：暂停时如何安排恢复。
- resume：恢复后产生什么结果。

### 12.5 generator

```cpp
Generator<int> sequence(int count)
{
    for (int value{}; value < count; ++value) {
        co_yield value;
    }
}
```

generator 通常惰性地产生值。C++23 提供 `std::generator`，但标准库实现支持需检查。

### 12.6 task

异步 task 通常需要处理：

- eager 还是 lazy 启动。
- continuation 保存和恢复。
- 结果与异常。
- task 对象提前销毁。
- final suspend 时 frame 生命周期。
- 取消和执行器亲和性。

不要把教学版 task 直接用于生产网络服务。

### 12.7 生命周期陷阱

- 协程参数中的引用可能在恢复时悬空。
- Lambda 协程捕获对象可能在第一次暂停后失效。
- coroutine handle 不拥有 frame，除非包装类型明确如此设计。
- 销毁仍可能恢复的 frame 会导致灾难性错误。
- 多线程恢复同一协程需要严格协议。

### 12.8 异常

协程体异常通常进入 `promise_type::unhandled_exception()`，由返回对象在 await/resume/get 时重新传播或存储。不可静默吞掉。

### 12.9 协程与执行器

协程只表达暂停与恢复。真正异步系统还需要：

- I/O 完成机制。
- 调度器或执行器。
- 线程池。
- 定时器。
- 取消和背压。

C++26 execution control library 提供 sender/receiver 等组合模型，后续专题阶段学习。

### 本节练习

1. 实现只支持整数的教学 generator。
2. 画出一次 `co_yield` 前后的 frame 状态。
3. 实现立即完成的 awaiter。
4. 找出引用参数跨暂停点造成的悬空问题。
5. 为教学 task 记录 initial/final suspend 和异常流程。

---

## 十三、并发测试与诊断

### 13.1 ThreadSanitizer

GCC/Clang 示例：

```powershell
g++ -std=c++23 -g -O1 -fsanitize=thread app.cpp -o app
```

TSan 与 ASan 通常分开构建。它能发现许多数据竞争，但不能证明没有死锁、活锁或协议错误。

### 13.2 压力测试

- 多线程重复运行。
- 随机化任务数量和时序。
- 使用很小队列容量制造边界。
- 重复启动和停止。
- 在提交、执行和关闭时注入异常。
- 设置超时避免 CI 永久挂起。

### 13.3 确定性设计

测试中注入：

- 执行器。
- 时钟。
- 随机种子。
- 阻塞点或栅栏。

避免依赖 `sleep_for` 猜测线程已经到达某状态。使用 latch、barrier 或测试钩子建立确定同步。

### 13.4 死锁诊断

- 获取线程堆栈。
- 记录锁名称和获取顺序。
- 为测试设置超时。
- 缩小涉及的锁和线程数量。
- 检查持锁期间的回调、日志和析构。

### 13.5 性能分析

测量：

- 吞吐量和尾延迟。
- 队列等待时间。
- 锁竞争和上下文切换。
- CPU 利用率。
- 缓存 miss 和 false sharing。
- 随线程数扩展曲线。

---

## 十四、阶段项目

### 项目一：有界阻塞队列

- 多生产者、多消费者。
- 固定容量和背压。
- `push`、`pop`、超时版本。
- 关闭后拒绝新数据并可排空。
- 条件变量始终使用谓词。
- 不在锁内执行用户代码。
- TSan 和压力测试通过。

### 项目二：可停止线程池

- 使用 `jthread` worker。
- 有界任务队列。
- 返回 future 传递结果和异常。
- 支持排空关闭与立即取消策略。
- 析构行为明确。
- 拒绝提交有结构化错误结果。

进阶：工作窃取、任务优先级、统计与协作取消。

### 项目三：并行文件搜索

- 生产者遍历文件，worker 搜索内容。
- 限制并发和队列容量。
- stop_token 支持取消。
- 收集结果时避免共享热点。
- 文件错误不会终止整个搜索。
- 比较不同线程数性能。

### 项目四：协程数据流

- generator 惰性产生记录。
- 解析、过滤和转换分层。
- 正确传播异常。
- 明确 frame 所有权。
- 验证消费者提前停止时资源释放。

### 项目五：原子状态机

- 使用 enum 原子状态表示启动、运行、停止和终止。
- CAS 保证合法迁移。
- atomic wait/notify 等待状态。
- 文档化每个内存序和同步关系。
- 与 mutex 版本对比正确性和性能。

### 项目验收标准

- 没有 detach 线程或不受控后台生命周期。
- 所有共享状态有明确同步策略。
- 锁顺序有文档且不在锁内调用未知代码。
- 条件变量使用状态谓词。
- 关闭、排空、取消和异常语义明确。
- 每个弱内存序都有 happens-before 证明。
- 协程 frame 和捕获生命周期明确。
- TSan、压力测试、Sanitizer 和常规测试通过。
- 性能结论来自基准而不是猜测。

---

## 十五、十一周学习安排

1. 第 1 周：并发概念、thread 和 jthread。
2. 第 2 周：mutex、RAII 锁和死锁。
3. 第 3 周：条件变量和阻塞队列。
4. 第 4 周：semaphore、latch、barrier、call_once。
5. 第 5 周：future、promise、packaged_task、async。
6. 第 6 周：实现基础线程池。
7. 第 7 周：内存模型、happens-before、volatile 边界。
8. 第 8 周：atomic、CAS、wait/notify 和内存序。
9. 第 9 周：无锁概念、ABA 和内存回收。
10. 第 10 周：协程、generator、awaiter 和 task。
11. 第 11 周：综合项目、TSan、压力测试和性能分析。

---

## 十六、阶段自测

### 概念题

1. 并发、并行和异步有何区别？
2. thread 析构时仍 joinable 会怎样？
3. jthread 提供了哪些生命周期优势？
4. 为什么 detach 通常危险？
5. 什么条件构成数据竞争？
6. mutex 保护的是变量还是不变量？
7. 如何避免多锁死锁？
8. 条件变量为什么必须检查谓词？
9. latch 与 barrier 有何区别？
10. future 如何传播线程异常？
11. 线程池为什么不应持锁执行任务？
12. happens-before 表达什么？
13. volatile 为什么不能同步线程？
14. relaxed 内存序保证什么、不保证什么？
15. release/acquire 如何发布数据？
16. compare_exchange_weak 为什么适合循环？
17. lock-free 与 wait-free 有何区别？
18. ABA 问题是什么？
19. 协程为什么不等于线程？
20. coroutine frame 由谁拥有和销毁为何重要？

### 编程题

1. 实现线程安全计数器并比较 mutex 与 atomic。
2. 实现支持关闭的有界阻塞队列。
3. 用 promise/future 传播结果和异常。
4. 实现最小线程池及两种关闭策略。
5. 用 release/acquire 安全发布数据。
6. 用 atomic wait/notify 实现状态等待。
7. 实现教学 generator。
8. 为并发组件建立无 sleep 的确定性测试。

### 进入下一阶段前的检查清单

- [ ] 能正确管理线程生命周期和停止。
- [ ] 能设计 mutex 保护的不变量。
- [ ] 能避免常见死锁和同步泄漏。
- [ ] 熟练使用条件变量和同步设施。
- [ ] 能通过 future 传播结果与异常。
- [ ] 能实现有明确关闭语义的线程池。
- [ ] 理解数据竞争和 happens-before。
- [ ] 能解释常见内存序。
- [ ] 知道无锁内存回收的困难。
- [ ] 理解协程 frame、promise 和 awaiter。
- [ ] 项目通过 TSan 与压力测试。
- [ ] 能用基准验证并发性能。

---

## 十七、易错点汇总

1. 忘记 join thread。
2. detach 后捕获局部引用或 `this`。
3. 线程入口异常未捕获。
4. 把“单条语句”误认为原子操作。
5. 不同函数用不同锁保护同一不变量。
6. 持锁调用用户回调、日志或阻塞 I/O。
7. 条件变量不使用谓词。
8. 用 sleep 代替同步协议。
9. 队列没有关闭和背压策略。
10. 认为 shared_ptr 让对象内容线程安全。
11. 认为 volatile 可防止数据竞争。
12. 未证明就使用 relaxed 内存序。
13. 多个 atomic 变量无法共同维护复合不变量。
14. CAS 循环忽略 expected 被更新。
15. 把 lock-free 当作一定更快。
16. 无锁节点移除后立即 delete。
17. 认为协程自动异步或自动切换线程。
18. 协程引用参数跨暂停点悬空。
19. 销毁仍可能恢复的 coroutine frame。
20. 只跑一次测试就认为并发代码正确。

---

## 十八、下一阶段

下一阶段建议进入底层原理与高性能 C++：

- 对象表示、对齐、生命周期和别名规则。
- 编译、链接、符号和汇编。
- cache、分支预测、SIMD 和数据布局。
- allocator、memory_resource 和内存池。
- profiling、benchmark、LTO 与 PGO。
- 未定义行为和可移植底层代码。

完成本阶段的真正标志，是能够为每一次共享访问指出同步依据，为每个后台任务指出停止和销毁路径，并能说明协程暂停后其参数、局部状态和 frame 为什么仍然有效。
