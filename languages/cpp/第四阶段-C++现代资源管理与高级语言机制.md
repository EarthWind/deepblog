# 第四阶段：现代资源管理与高级语言机制

> 前置要求：掌握类、RAII、复制与移动、继承和运行时多态。  
> 建议标准：C++23  
> 建议周期：6～8 周，每天 1～2 小时  
> 阶段目标：能够清晰表达资源所有权，正确使用智能指针，理解值类别与完美转发，并使用现代 Lambda 构建安全的回调和任务系统。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 画出程序中的资源所有权图，并区分所有者与观察者。
2. 根据语义选择值、`std::unique_ptr`、`std::shared_ptr` 或 `std::weak_ptr`。
3. 正确使用 `make_unique`、`make_shared`、自定义删除器和别名构造。
4. 避免重复控制块、循环引用、错误 `this` 管理等常见智能指针问题。
5. 准确区分 lvalue、prvalue、xvalue 和 glvalue。
6. 理解移动构造、移动赋值、复制消除和返回值优化之间的关系。
7. 编写转发引用接口并使用 `std::forward` 保留值类别。
8. 熟练使用 Lambda 捕获、初始化捕获、泛型 Lambda 和模板 Lambda。
9. 正确管理异步回调中的对象生命周期。
10. 实现资源安全的多态集合、任务队列和缓存项目。

这一阶段的核心原则是：先设计所有权，再选择工具；智能指针不能替代所有权设计。

---

## 二、资源与所有权模型

### 2.1 资源所有权

资源所有权回答三个问题：

1. 谁负责释放资源？
2. 资源能够存在多久？
3. 谁可以访问或转移它？

常见关系：

```text
独占所有：A ──owns──> Resource
共享所有：A ──┐
              ├──owns──> Resource
           B ──┘
非拥有观察：Observer ──observes──> Resource
```

设计接口时应让这些关系尽可能从类型中看出来。

### 2.2 优先使用值语义

如果对象可以直接成为另一个对象的成员，通常不需要智能指针：

```cpp
class User
{
private:
    std::string name_;
    Address address_;
};
```

直接成员具有：

- 清晰的生命周期。
- 较少的动态分配。
- 更好的局部性。
- 更简单的复制和移动行为。

不要因为“面向对象”就把每个成员都放进堆中。

### 2.3 裸指针和引用的角色

现代 C++ 中，裸指针和引用通常用于非拥有访问：

```cpp
void print_user(const User& user); // 必须存在
void print_user(const User* user); // 可以不存在
```

它们不能单独证明对象仍然存活，因此生命周期必须由调用关系、文档或更强类型保证。

### 2.4 所有权转移接口

```cpp
class Window
{
public:
    void set_widget(std::unique_ptr<Widget> widget)
    {
        widget_ = std::move(widget);
    }

private:
    std::unique_ptr<Widget> widget_;
};
```

按值接收 `unique_ptr` 清晰表达“调用者把所有权交给函数”。调用处必须显式移动：

```cpp
window.set_widget(std::move(widget));
```

### 2.5 所有权设计检查表

对每个资源回答：

- 能否直接按值保存？
- 是否只有一个明确所有者？
- 是否真的需要共享生命周期？
- 观察者可能比所有者活得更久吗？
- 是否存在环？
- 销毁顺序是否确定？
- 多线程是否同时访问所有权状态或对象内容？

### 本节练习

1. 为通讯录、图形系统和树结构分别画所有权图。
2. 找出一个不必要使用动态分配的设计并改为值成员。
3. 为插件注册函数设计明确的所有权转移接口。
4. 解释“指针指向对象”为什么不等于“指针拥有对象”。

---

## 三、`std::unique_ptr`

### 3.1 独占所有权

```cpp
#include <memory>

auto user = std::make_unique<User>("Alice");
user->print();
```

`unique_ptr`：

- 独占所管理对象。
- 不可复制。
- 可以移动。
- 析构时自动释放资源。
- 通常与裸指针大小相近，但自定义删除器可能改变大小。

### 3.2 为什么优先 `make_unique`

```cpp
auto widget = std::make_unique<Widget>(argument1, argument2);
```

它比直接写 `new` 更简洁，能够让资源立即进入所有者，并减少手动类型重复。

```cpp
std::unique_ptr<Widget> widget{new Widget{argument1, argument2}}; // 通常不必这样写
```

### 3.3 转移所有权

```cpp
auto first = std::make_unique<Widget>();
auto second = std::move(first);

if (!first) {
    // first 已不再拥有对象
}
```

移动后源 `unique_ptr` 保证为空。这个保证属于 `unique_ptr` 的契约，不能推广到所有可移动类型。

### 3.4 参数与返回值

```cpp
std::unique_ptr<Shape> make_shape();

void consume(std::unique_ptr<Shape> shape);
void observe(const Shape& shape);
void maybe_observe(const Shape* shape);
```

- 返回 `unique_ptr`：把新资源的所有权交给调用者。
- 按值接收：取得所有权。
- 接收引用或裸指针：只观察对象。
- 通常不要仅为了调用对象成员而传 `const unique_ptr<T>&`，这会把不相关的所有权实现暴露给函数。

### 3.5 多态所有权

```cpp
std::unique_ptr<Shape> shape = std::make_unique<Circle>(2.0);
```

通过基类 `unique_ptr` 销毁派生对象时，基类必须拥有虚析构函数：

```cpp
virtual ~Shape() = default;
```

### 3.6 数组

```cpp
auto values = std::make_unique<int[]>(100);
```

但固定大小优先 `std::array`，动态大小优先 `std::vector`。`unique_ptr<T[]>` 更常用于底层接口或实现容器时。

### 3.7 自定义删除器

```cpp
#include <cstdio>
#include <memory>

struct FileCloser
{
    void operator()(std::FILE* file) const noexcept
    {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

FilePtr open_file(const char* path, const char* mode)
{
    return FilePtr{std::fopen(path, mode)};
}
```

智能指针不仅能管理 `new` 创建的对象，也可通过删除器包装符合“句柄 + 释放函数”模式的资源。

### 3.8 `get`、`release` 和 `reset`

```cpp
Widget* observer = widget.get(); // 非拥有观察
widget.reset();                  // 销毁当前对象并变空
Widget* raw = widget.release();  // 放弃所有权但不销毁，风险很高
```

`release()` 会把释放责任交给调用者，应只在对接明确接管所有权的旧接口时使用。普通代码很少需要它。

### 本节练习

1. 将图形集合改为 `vector<unique_ptr<Shape>>`。
2. 实现返回不同派生图形的工厂函数。
3. 使用自定义删除器包装 `FILE*`。
4. 解释 `get()` 与 `release()` 的所有权差异。
5. 找出一个错误传递 `const unique_ptr<T>&` 的接口并改进。

---

## 四、`std::shared_ptr`

### 4.1 共享所有权

```cpp
auto document = std::make_shared<Document>();
auto first_view = document;
auto second_view = document;
```

多个 `shared_ptr` 可以共享对象所有权。最后一个所有者销毁或重置后，对象才被释放。

共享所有权应表示真实业务语义，而不是为了避免思考对象由谁拥有。

### 4.2 控制块

`shared_ptr` 通常关联一个控制块，其中保存：

- 强引用计数。
- 弱引用计数。
- 删除器和分配器等信息。

复制 `shared_ptr` 会更新引用计数；这通常涉及原子操作，具有时间和空间成本。

### 4.3 `make_shared`

```cpp
auto task = std::make_shared<Task>(arguments...);
```

通常优点包括：

- 一次分配同时容纳对象与控制块。
- 异常安全且代码简洁。
- 更好的局部性。

但弱引用长期存在时，合并分配的整块内存可能延后释放；需要特殊删除器时也不能直接用普通 `make_shared`。

### 4.4 不要从同一裸指针创建多个控制块

```cpp
Widget* raw = new Widget;
std::shared_ptr<Widget> first{raw};
std::shared_ptr<Widget> second{raw}; // 严重错误：两个独立控制块会重复删除
```

对象一旦由 `shared_ptr` 管理，应复制现有 `shared_ptr`，不要重新从 `.get()` 创建新的所有者。

### 4.5 `use_count` 不能用于业务同步

```cpp
if (resource.use_count() == 1) {
    // 不能据此可靠推断并发环境中接下来仍是唯一所有者
}
```

`use_count()` 适合诊断和教学，不适合用来实现并发正确性或核心业务决策。

### 4.6 线程安全边界

不同 `shared_ptr` 实例共享同一控制块时，引用计数更新可以安全并发进行。但这不意味着所指对象本身是线程安全的。

```text
shared_ptr 控制块安全 ≠ T 的成员函数线程安全
```

### 4.7 `enable_shared_from_this`

对象需要在成员函数中获得共享自身的 `shared_ptr` 时：

```cpp
class Session : public std::enable_shared_from_this<Session>
{
public:
    void start()
    {
        auto self = shared_from_this();
        // 把 self 交给异步操作
    }
};
```

必须先让对象由 `shared_ptr` 管理：

```cpp
auto session = std::make_shared<Session>();
session->start();
```

不要在构造函数中调用 `shared_from_this()`；此时共享控制关系通常尚未建立。现代标准还提供 `weak_from_this()`。

### 4.8 别名构造

```cpp
auto owner = std::make_shared<Container>();
std::shared_ptr<Item> item{owner, &owner->item};
```

别名 `shared_ptr` 指向成员 `item`，但共享 `owner` 的控制块，从而保证整个容器活着。它是高级工具，必须区分“存储的指针”和“拥有的控制块”。

### 本节练习

1. 用图表示 `shared_ptr` 对象、控制块和被管理对象。
2. 演示最后一个强引用离开作用域时对象销毁。
3. 复现两个控制块的错误设计，但只用代码审查分析，不运行导致重复释放的程序。
4. 为异步会话正确使用 `enable_shared_from_this`。
5. 解释为什么普通树结构通常不需要所有节点互相 `shared_ptr`。

---

## 五、`std::weak_ptr` 与循环引用

### 5.1 非拥有共享观察

`weak_ptr` 观察由 `shared_ptr` 管理的对象，但不增加强引用计数：

```cpp
std::weak_ptr<Document> cached_document = document;
```

使用前必须尝试提升：

```cpp
if (auto locked = cached_document.lock()) {
    locked->render();
} else {
    // 对象已经销毁
}
```

`lock()` 返回一个 `shared_ptr`，成功时在当前作用域内保持对象存活。

### 5.2 循环引用

```cpp
class Parent
{
public:
    std::shared_ptr<Child> child;
};

class Child
{
public:
    std::shared_ptr<Parent> parent;
};
```

即使外部所有者消失，两者的强引用计数仍不为零，造成逻辑资源泄漏。若父拥有子，而子只回看父，应写：

```cpp
std::weak_ptr<Parent> parent;
```

### 5.3 缓存和订阅者列表

`weak_ptr` 常用于：

- 不延长对象寿命的缓存。
- 观察者或订阅者列表。
- 对象图中的反向链接。
- 异步回调检查目标是否仍存在。

### 5.4 异步回调中的生命周期

```cpp
std::weak_ptr<Session> weak = shared_from_this();

executor.post([weak] {
    if (auto session = weak.lock()) {
        session->process();
    }
});
```

捕获强 `shared_ptr` 会把对象寿命延长到任务销毁或执行结束；捕获 `weak_ptr` 则允许对象提前结束。选择哪一种取决于业务语义。

### 本节练习

1. 使用 `weak_ptr` 修复父子循环引用。
2. 实现不延长对象生命周期的简单缓存。
3. 对比异步 Lambda 捕获强引用和弱引用的行为。
4. 为观察者列表设计过期订阅者清理逻辑。

---

## 六、值类别完整模型

### 6.1 为什么学习值类别

值类别影响：

- 表达式能否取地址。
- 能否绑定到某种引用。
- 选择复制还是移动重载。
- 临时对象的生命周期。
- 完美转发是否保留调用者语义。

值类别是表达式的属性，不是变量自身的存储类别。

### 6.2 分类关系

```text
expression
├─ glvalue（有身份）
│  ├─ lvalue
│  └─ xvalue
└─ rvalue
   ├─ prvalue
   └─ xvalue
```

- lvalue：有身份，通常不会被隐式视为可搬走资源。
- prvalue：通常用于初始化结果对象的纯右值表达式。
- xvalue：有身份，但资源可被复用的“将亡值”。
- glvalue：lvalue 与 xvalue 的总称，强调身份。
- rvalue：prvalue 与 xvalue 的总称，强调可作为右值使用。

### 6.3 常见表达式

```cpp
Widget widget;

widget;                 // lvalue
Widget{};               // prvalue
std::move(widget);      // xvalue
```

一个有名字的右值引用变量在表达式中仍是 lvalue：

```cpp
Widget&& reference = Widget{};
consume(reference);            // reference 表达式是 lvalue
consume(std::move(reference)); // xvalue
```

### 6.4 引用绑定

```cpp
Widget& a = widget;             // 非 const 左值引用绑定 lvalue
const Widget& b = Widget{};     // const 左值引用可绑定临时对象
Widget&& c = Widget{};          // 右值引用绑定 rvalue
```

临时对象绑定到局部 `const T&` 或 `T&&` 时，生命周期通常延长到该引用作用域结束，但生命周期延长规则存在边界，不能简单推导到成员、返回值或通过函数参数传递的所有情形。

### 6.5 `decltype` 观察

```cpp
int value{};

static_assert(std::is_same_v<decltype(value), int>);
static_assert(std::is_same_v<decltype((value)), int&>);
```

未加括号的名称表达式具有 `decltype` 特殊规则；加括号后按表达式值类别推导。模板阶段会更深入使用。

### 本节练习

1. 判断十个表达式属于 lvalue、prvalue 还是 xvalue。
2. 解释为什么命名右值引用是 lvalue。
3. 用复制和移动日志观察不同值类别选择的重载。
4. 比较 `decltype(name)` 与 `decltype((name))`。

---

## 七、移动、复制消除与返回值

### 7.1 `std::move` 是转换

近似理解：

```cpp
std::move(value)
```

把 `value` 转换为可绑定右值引用的表达式。它不保证目标类型存在移动构造；若没有合适移动操作，仍可能复制。

### 7.2 不要移动仍需使用的对象

```cpp
send(std::move(message));
// 此后不再依赖 message 的原内容
```

移动表达的是“允许接收者取走资源”，应该发生在原对象当前值最后一次使用的位置附近。

### 7.3 `const` 抑制移动

```cpp
const std::string text{"hello"};
std::string copy{std::move(text)};
```

大多数移动构造接收 `T&&`，无法绑定 `const T&&`，因此这里通常调用复制构造。移动的目的往往是修改源对象以转移资源。

### 7.4 保证的复制消除

C++17 起，部分 prvalue 初始化场景直接构造结果对象：

```cpp
Widget make_widget()
{
    return Widget{};
}
```

这里不需要先构造临时对象再移动。

### 7.5 NRVO

```cpp
Widget make_widget()
{
    Widget result;
    // 配置 result
    return result;
}
```

编译器通常可以进行命名返回值优化（NRVO）。不要写：

```cpp
return std::move(result);
```

这可能阻碍 NRVO。返回局部对象时通常直接 `return result;`。

### 7.6 隐式移动

返回符合条件的局部对象或参数时，语言可把它视为移动候选。不要为了“确保移动”而到处添加 `std::move`。

### 7.7 按值接收再移动

当函数需要拥有一份参数值时：

```cpp
class Person
{
public:
    explicit Person(std::string name)
        : name_{std::move(name)}
    {
    }

private:
    std::string name_;
};
```

调用者传左值会复制到参数，再移动到成员；传右值可移动到参数，再移动到成员。接口简单，但对永远传左值或成本极高的类型可比较其他重载方案。

### 本节练习

1. 对比 `return local;` 与 `return std::move(local);` 的构造日志。
2. 解释为什么 `const` 对象通常不能真正移动资源。
3. 为保存字符串成员的类比较按值参数和两个引用重载方案。
4. 编写返回 `unique_ptr` 的工厂，不在返回语句中使用多余 `move`。

---

## 八、转发引用与完美转发

### 8.1 问题：包装函数丢失值类别

```cpp
void process(const Widget&);
void process(Widget&&);

template<class T>
void wrapper(T&& value)
{
    process(value); // value 有名字，因此表达式是 lvalue
}
```

即使调用者传入右值，`value` 在函数体内仍是 lvalue。

### 8.2 转发引用

当 `T` 由函数调用推导，形如 `T&&` 时，它是转发引用：

```cpp
template<class T>
void wrapper(T&& value);
```

注意：

```cpp
Widget&& value;                  // 普通右值引用
template<class T> void f(T&&);   // 转发引用
template<class T> void f(const T&&); // 不是转发引用
```

### 8.3 引用折叠

折叠规则可概括为：只要组合中出现左值引用，结果就是左值引用；只有 `&&` 与 `&&` 组合才仍是右值引用。

```text
&  + &  → &
&  + && → &
&& + &  → &
&& + && → &&
```

### 8.4 `std::forward`

```cpp
template<class T>
void wrapper(T&& value)
{
    process(std::forward<T>(value));
}
```

`std::forward<T>` 根据推导出的 `T` 恢复调用者传入时的值类别。

### 8.5 `std::move` 与 `std::forward`

- `std::move(x)`：无条件把 `x` 当作可移动来源。
- `std::forward<T>(x)`：在转发上下文中有条件恢复原值类别。

不要把 `forward` 当作“更高级的 move”。它们表达不同意图。

### 8.6 完美转发构造

```cpp
template<class T, class... Args>
std::unique_ptr<T> make_object(Args&&... args)
{
    return std::unique_ptr<T>{new T(std::forward<Args>(args)...)};
}
```

这展示了 `make_unique` 一类工厂的核心机制。实际代码直接使用标准 `make_unique`。

### 8.7 完美转发的陷阱

- 花括号初始化列表通常无法直接推导为普通模板参数。
- `0`、`NULL` 转发后不是 `nullptr`。
- 重载函数名可能缺少唯一类型。
- 位域不能绑定普通非 const 引用。
- 万能转发构造函数可能劫持复制构造。
- 过度泛型会产生难懂错误信息和意外重载。

使用 Concepts 可约束转发接口；模板阶段将系统学习。

### 本节练习

1. 编写两组重载打印接收到左值还是右值。
2. 先错误地直接传命名参数，再使用 `forward` 修正。
3. 实现教学用对象工厂。
4. 构造一个转发构造函数劫持复制的例子，并通过约束或显式特殊成员修复。

---

## 九、Lambda 深入

### 9.1 Lambda 的本质

每个 Lambda 表达式产生一个唯一、未命名的闭包类型，其捕获通常成为闭包对象的数据成员：

```cpp
int threshold{60};
auto passed = [threshold](int score) {
    return score >= threshold;
};
```

### 9.2 值捕获与 `mutable`

默认情况下，按值捕获的成员不能在 Lambda 调用中修改：

```cpp
int count{};
auto next = [count]() mutable {
    return ++count;
};
```

这里修改的是闭包中的副本，不是外部 `count`。

### 9.3 引用捕获与生命周期

```cpp
auto make_bad_callback()
{
    std::string message{"temporary"};
    return [&message] { std::cout << message; }; // 悬空引用
}
```

返回、存储或异步执行 Lambda 时，必须特别审查引用捕获。短期同步算法调用中的引用捕获通常更容易保证安全。

### 9.4 初始化捕获

```cpp
auto resource = std::make_unique<Resource>();

auto task = [resource = std::move(resource)]() mutable {
    resource->run();
};
```

初始化捕获可以：

- 移动独占资源进入闭包。
- 创建新的捕获成员。
- 改变捕获类型。

捕获 `unique_ptr` 后闭包通常不可复制，只能移动。

### 9.5 捕获 `this`

```cpp
class Worker
{
public:
    auto callback()
    {
        return [this] { run(); };
    }

private:
    void run();
};
```

`[this]` 捕获的是指针，不会自动延长对象生命周期。若回调在对象销毁后执行，就会悬空。

`[*this]` 捕获当前对象的副本，但复制成本和语义必须合理：

```cpp
return [*this] { run(); };
```

共享所有对象的异步回调可根据语义捕获强 `shared_ptr` 或弱 `weak_ptr`。

### 9.6 泛型 Lambda

```cpp
auto add = [](const auto& left, const auto& right) {
    return left + right;
};
```

参数使用 `auto` 时，闭包的调用运算符是函数模板。

### 9.7 模板 Lambda

C++20 起可显式声明模板参数：

```cpp
auto first = []<class T>(const std::vector<T>& values) -> const T& {
    return values.front();
};
```

它允许参数间共享类型参数，并方便添加约束。

### 9.8 递归 Lambda

C++23 显式对象参数可用于递归：

```cpp
auto factorial = [](this auto self, unsigned int value) -> unsigned long long {
    return value <= 1 ? 1 : value * self(value - 1);
};
```

编译器支持不足时，可把自身作为普通参数传入：

```cpp
auto factorial = [](auto self, unsigned int value) -> unsigned long long {
    return value <= 1 ? 1 : value * self(self, value - 1);
};
```

### 9.9 `constexpr` 与 `consteval` Lambda

```cpp
constexpr auto square = [](int value) constexpr {
    return value * value;
};

static_assert(square(5) == 25);
```

无捕获且实现满足常量表达式规则的 Lambda 很适合编译期算法。

### 本节练习

1. 分别用值捕获和引用捕获实现计数器并观察差异。
2. 移动 `unique_ptr` 进入任务 Lambda。
3. 修复返回 Lambda 时悬空引用的问题。
4. 使用泛型 Lambda 对不同数值类型求和。
5. 使用模板 Lambda 打印任意 `vector<T>` 的首元素。

---

## 十、可调用对象与类型擦除

### 10.1 可调用对象

C++ 中可调用对象包括：

- 普通函数。
- 函数指针。
- Lambda。
- 重载 `operator()` 的函数对象。
- 成员函数指针。

```cpp
struct Multiplier
{
    int factor{};

    int operator()(int value) const
    {
        return value * factor;
    }
};
```

### 10.2 `std::invoke`

```cpp
#include <functional>

std::invoke(callable, arguments...);
```

它统一调用普通函数、函数对象和成员指针，是泛型调用设施的基础。

### 10.3 `std::function`

```cpp
std::function<void(int)> callback;
callback = [](int value) { std::cout << value; };
```

`std::function` 提供可复制可调用对象的类型擦除，优点是接口稳定，代价可能包括：

- 间接调用。
- 可能的动态分配。
- 要求目标可复制。
- 丢失目标具体类型。

模板参数或 `auto` 能保留具体类型并利于内联；只有需要统一存储不同可调用对象时才考虑类型擦除。

### 10.4 `std::move_only_function`（C++23）

```cpp
std::move_only_function<void()> task =
    [resource = std::make_unique<Resource>()] {
        resource->run();
    };
```

它支持不可复制、只能移动的可调用目标，适合拥有独占资源的任务队列。使用前检查标准库支持情况。

### 10.5 函数指针

无捕获 Lambda 可转换为兼容函数指针：

```cpp
using Operation = int (*)(int, int);
Operation add = [](int a, int b) { return a + b; };
```

函数指针成本低，但不能保存捕获状态。

### 本节练习

1. 使用函数对象保存乘数状态。
2. 用 `std::invoke` 调用普通函数和成员函数。
3. 比较模板回调、函数指针和 `std::function` 的能力。
4. 使用 `move_only_function` 存储捕获 `unique_ptr` 的任务。

---

## 十一、异常安全与资源操作

### 11.1 异常安全等级

- 无保证：失败后对象可能损坏。
- 基本保证：无资源泄漏，对象仍有效，但状态可能改变。
- 强保证：操作失败时状态像未发生一样。
- 不抛异常保证：操作承诺不会抛异常。

### 11.2 先准备，后提交

```cpp
void Document::set_content(std::string content)
{
    validate(content);       // 可能抛异常，但对象尚未改变
    content_ = std::move(content); // 提交新状态
}
```

复杂操作可先在临时对象中完成所有可能失败的工作，成功后通过 `swap` 提交。

### 11.3 析构与清理必须可靠

析构函数、删除器和资源释放操作通常应为 `noexcept`。如果底层关闭操作可能失败，可在显式 `close()` 中返回错误，而析构只做尽力清理，具体策略由资源契约决定。

### 11.4 智能指针与异常

资源应尽快进入 RAII 对象：

```cpp
auto resource = std::make_unique<Resource>();
perform_other_work();
```

后续操作抛异常时，局部智能指针仍会正确清理资源。

### 11.5 删除不完整类型

`unique_ptr` 可作为 PImpl 成员持有不完整类型，但拥有它的类通常需要在实现文件中定义析构函数，让删除发生在实现类型完整的位置：

```cpp
// widget.hpp
class Widget
{
public:
    Widget();
    ~Widget();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

```cpp
// widget.cpp
class Widget::Impl
{
    // ...
};

Widget::~Widget() = default;
```

### 本节练习

1. 分析容器更新操作提供哪一级异常安全。
2. 用临时对象和 `swap` 实现强保证的配置更新。
3. 使用 `unique_ptr` 完成一个简单 PImpl 类。
4. 检查资源删除器是否可能抛异常。

---

## 十二、性能与正确性边界

### 12.1 动态分配不是免费的

潜在成本包括：

- 分配器调用。
- 内存碎片。
- 缓存局部性变差。
- 指针间接访问。
- 共享计数更新。

不要为了避免复制几字节的小对象而引入堆分配。

### 12.2 `shared_ptr` 不是垃圾回收

引用计数：

- 无法自动解决强引用环。
- 只管理被包装资源的生命周期。
- 不判断对象内容是否仍有业务价值。
- 不保证对象线程安全。

### 12.3 小心隐藏的复制

```cpp
for (auto item : items) { }        // 复制每个元素
for (const auto& item : items) { } // 只读观察
```

但小型标量按值使用通常更好。不要形成“任何地方都加引用”的机械习惯。

### 12.4 测量后优化

智能指针和类型擦除的成本应通过基准和性能分析确认。优先保证：

- 所有权正确。
- 接口清晰。
- 无未定义行为。
- 测试覆盖合理。

然后才针对真实热点优化。

### 12.5 工具

继续使用：

- AddressSanitizer：越界、use-after-free 等。
- UndefinedBehaviorSanitizer。
- LeakSanitizer（平台支持时）。
- 静态分析器。
- 调试器观察析构和引用计数。
- 基准工具验证性能假设。

---

## 十三、阶段项目

### 项目一：多态图形文档

最低要求：

- 使用 `vector<unique_ptr<Shape>>` 独占保存图形。
- 提供图形工厂并返回 `unique_ptr<Shape>`。
- 支持添加、删除、遍历和面积统计。
- 基类拥有虚析构函数。
- 不暴露内部智能指针实现给只读算法。

进阶要求：

- 为每个图形实现 `clone()`，获得多态深复制。
- 使用 PImpl 隐藏文档实现。
- 导出 SVG。

### 项目二：资源安全任务队列

最低要求：

- 使用 `std::move_only_function<void()>` 或自定义移动任务包装器。
- 任务可捕获 `unique_ptr`。
- 支持添加、执行下一项、执行全部和清空。
- 清空未执行任务时资源自动释放。
- 禁止不明确的共享所有权。

进阶要求：

- 统计成功、失败和待执行任务。
- 使用异常边界防止单个任务终止队列。
- 后续并发阶段将其升级为线程池队列。

### 项目三：文档观察与缓存系统

最低要求：

- 文档通过 `shared_ptr` 表达真实共享生命周期。
- 观察者列表使用 `weak_ptr`。
- 通知时通过 `lock()` 获得临时强引用。
- 自动清理已经过期的观察者。
- 不产生强引用环。

进阶要求：

- 异步回调分别实现“保证完成”和“允许取消”两种生命周期策略。
- 实现弱引用缓存。
- 输出所有权关系诊断信息，但不依赖 `use_count()` 做业务判断。

### 项目四：通用事件系统

最低要求：

- 支持注册和注销回调。
- 允许有状态 Lambda。
- 订阅令牌通过 RAII 自动注销。
- 避免回调捕获悬空 `this`。
- 触发过程中删除订阅者时行为明确。

进阶要求：

- 支持不同事件类型。
- 支持只执行一次的移动回调。
- 明确线程安全范围，为并发阶段预留接口。

### 项目验收标准

- 每个动态资源只有明确且可解释的所有权模型。
- 能直接按值保存的对象没有无意义堆分配。
- 独占资源优先 `unique_ptr`。
- `shared_ptr` 只用于真实共享生命周期。
- 所有反向链接和缓存不会形成强引用环。
- 异步 Lambda 捕获经过生命周期审查。
- 没有从 `.get()` 重建共享所有权。
- 没有不必要的 `std::move` 阻碍复制消除。
- 严格警告、Sanitizer 和项目测试通过。

---

## 十四、七周学习安排

### 第 1 周：所有权设计与 `unique_ptr`

- 所有者、观察者和所有权转移。
- `make_unique`、移动、参数与返回值。
- 多态所有权和自定义删除器。

### 第 2 周：`shared_ptr` 与控制块

- 共享所有权语义。
- `make_shared` 和控制块。
- 重复控制块问题。
- `enable_shared_from_this`。

### 第 3 周：`weak_ptr` 与生命周期图

- 循环引用。
- 缓存、观察者和反向链接。
- 异步强捕获与弱捕获。

### 第 4 周：值类别与复制消除

- lvalue、prvalue、xvalue。
- 引用绑定和临时对象。
- `std::move`、NRVO、隐式移动。

### 第 5 周：转发与高级 Lambda

- 转发引用、引用折叠、`std::forward`。
- 初始化捕获、泛型和模板 Lambda。
- 捕获 `this` 的生命周期。

### 第 6 周：可调用对象与异常安全

- 函数对象、`std::invoke`。
- `std::function` 和 `move_only_function`。
- 异常安全等级、PImpl。

### 第 7 周：综合项目

- 完成任务队列或事件系统。
- 绘制并审查所有权图。
- 运行严格警告和 Sanitizer。
- 完成阶段自测和代码复盘。

---

## 十五、阶段自测

### 概念题

1. 为什么值成员通常优于智能指针成员？
2. `unique_ptr` 的复制为什么被禁止？
3. `get()`、`release()` 和 `reset()` 有什么区别？
4. 什么接口应按值接收 `unique_ptr`？
5. `shared_ptr` 控制块通常保存什么？
6. 为什么不能从同一裸指针创建多个 `shared_ptr`？
7. `make_shared` 有哪些优点和特殊内存行为？
8. `shared_ptr` 的引用计数线程安全是否意味着对象线程安全？
9. `enable_shared_from_this` 解决什么问题？
10. `weak_ptr::lock()` 为什么返回 `shared_ptr`？
11. 如何使用 `weak_ptr` 打破循环引用？
12. lvalue、prvalue 和 xvalue 的核心差异是什么？
13. 为什么有名字的右值引用变量是 lvalue？
14. `std::move` 实际做了什么？
15. 为什么不应写 `return std::move(local);`？
16. 转发引用成立需要什么条件？
17. `std::forward` 与 `std::move` 的意图有何区别？
18. Lambda 引用捕获最重要的风险是什么？
19. `[this]` 是否延长当前对象生命周期？
20. `std::function` 与模板可调用参数有哪些取舍？

### 编程题

1. 用 `unique_ptr` 实现异构多态集合。
2. 使用自定义删除器包装 C 文件句柄。
3. 修复一个父子对象的 `shared_ptr` 循环引用。
4. 实现弱引用缓存。
5. 编写保留值类别的日志包装函数。
6. 使用初始化捕获建立拥有独占资源的任务。
7. 实现 RAII 订阅令牌。
8. 使用 PImpl 隐藏类的私有实现。

### 进入下一阶段前的检查清单

- [ ] 能先画所有权图，再选择智能指针。
- [ ] 不会用 `shared_ptr` 代替所有权决策。
- [ ] 熟练使用 `unique_ptr` 表达独占所有权。
- [ ] 能解释控制块和强弱引用。
- [ ] 能识别并修复循环引用。
- [ ] 能安全处理异步回调生命周期。
- [ ] 能判断常见表达式的值类别。
- [ ] 正确区分 `move` 和 `forward`。
- [ ] 理解复制消除和 NRVO。
- [ ] 熟练使用高级 Lambda 捕获。
- [ ] 完成至少两个阶段项目。
- [ ] 项目通过测试、严格警告和 Sanitizer。

---

## 十六、易错点汇总

1. 能按值保存却使用动态分配。
2. 把所有裸指针都机械替换为 `shared_ptr`。
3. 使用 `unique_ptr.get()` 创建另一个所有者。
4. 随意调用 `release()` 后忘记释放资源。
5. 从同一裸指针构造多个 `shared_ptr`。
6. 使用 `use_count()` 实现并发业务逻辑。
7. 误以为 `shared_ptr` 会让对象自动线程安全。
8. 在构造函数中调用 `shared_from_this()`。
9. 双向关系全部使用强 `shared_ptr`。
10. 忘记检查 `weak_ptr.lock()` 是否成功。
11. 移动对象后继续依赖其旧值。
12. 认为 `std::move` 一定调用移动构造。
13. 从 `const` 对象期待真正的资源移动。
14. 在返回局部对象时多余地使用 `std::move`。
15. 把普通 `T&&` 一律称作转发引用。
16. 在转发函数里忘记 `std::forward`。
17. 用 `std::forward` 无条件移动普通局部变量。
18. 返回捕获局部引用的 Lambda。
19. 异步捕获裸 `this`，对象却可能提前销毁。
20. 不需要类型擦除时滥用 `std::function`。

---

## 十七、下一阶段

下一阶段建议系统学习标准模板库：

- 序列容器和关联容器。
- 容量、复杂度和迭代器失效。
- 迭代器分类与算法。
- 自定义比较器和哈希函数。
- `optional`、`variant`、`any`、`expected`。
- Ranges、Views 和惰性求值。
- 标准算法与数据处理流水线。

完成本阶段的真正标志，是看到一个对象关系时能够说明谁拥有谁、谁只观察谁、何时销毁，以及回调跨越作用域后为什么仍然安全。
