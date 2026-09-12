# 第三阶段：C++ 面向对象编程

> 前置要求：掌握函数、引用、基础指针、字符串、数组、结构体和多文件项目。  
> 建议标准：C++23  
> 建议周期：6～8 周，每天 1～2 小时  
> 阶段目标：能够设计维护有效状态的类，理解对象生命周期，并合理使用组合、继承和运行时多态。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 区分类与对象、接口与实现、状态与行为。
2. 使用访问控制和类不变量实现封装。
3. 正确编写构造函数、析构函数和成员初始化列表。
4. 解释对象创建、复制、移动和销毁的完整过程。
5. 根据资源所有权应用 Rule of Zero、Three 和 Five。
6. 设计符合直觉且保持不变量的运算符重载。
7. 使用组合组织对象关系，并判断何时才需要继承。
8. 正确使用虚函数、抽象类、`override` 和虚析构函数。
9. 识别对象切片、多继承、虚函数构造期调用等常见陷阱。
10. 完成带测试的多文件面向对象项目。

本阶段的重点不是“把所有东西写成类”，而是学习如何让类型表达约束、所有权和业务含义。

---

## 二、从结构体到类

### 2.1 类与对象

类描述一种类型，对象是该类型的具体实例：

```cpp
#include <string>

class BankAccount
{
public:
    void deposit(double amount);
    [[nodiscard]] double balance() const;

private:
    std::string owner_;
    double balance_{};
};
```

```cpp
BankAccount first_account;
BankAccount second_account;
```

两个对象具有相同的类型和行为，但各自保存独立状态。

### 2.2 `class` 与 `struct`

二者能力基本相同，主要默认访问权限不同：

- `class` 的成员和基类默认是 `private`。
- `struct` 的成员和基类默认是 `public`。

常见约定：

- 简单数据聚合使用 `struct`。
- 需要维护不变量、隐藏实现的类型使用 `class`。

```cpp
struct Point
{
    double x{};
    double y{};
};
```

### 2.3 访问控制

- `public`：类型对外提供的接口。
- `private`：仅类自身及友元可访问的实现。
- `protected`：类和派生类可访问；应谨慎使用。

不要为每个私有成员机械添加 getter 和 setter。无约束 setter 往往只是把公开数据换了一种写法。

```cpp
class Temperature
{
public:
    explicit Temperature(double celsius)
        : celsius_{celsius}
    {
    }

    [[nodiscard]] double celsius() const noexcept
    {
        return celsius_;
    }

private:
    double celsius_{};
};
```

### 2.4 类不变量

类不变量是对象处于有效状态时始终满足的条件。例如：

- 账户余额不能低于允许的透支额度。
- 日期必须是有效年月日。
- 分数必须在 `0～100`。

```cpp
#include <stdexcept>

class Score
{
public:
    explicit Score(int value)
        : value_{value}
    {
        if (value < 0 || value > 100) {
            throw std::out_of_range{"score must be in [0, 100]"};
        }
    }

    [[nodiscard]] int value() const noexcept { return value_; }

private:
    int value_{};
};
```

构造成功的对象应立即有效，公开操作也不能破坏其不变量。

### 2.5 成员函数与 `this`

非静态成员函数通过隐藏的 `this` 指针知道正在操作哪个对象：

```cpp
void BankAccount::deposit(double amount)
{
    this->balance_ += amount;
}
```

通常无需显式写 `this->`，但参数与成员同名时可用来区分。更简单的做法是采用统一成员命名规则，例如后缀 `_`。

### 2.6 `const` 成员函数

```cpp
[[nodiscard]] double balance() const noexcept
{
    return balance_;
}
```

末尾的 `const` 表示函数不会修改对象的可观察状态。`const` 对象只能调用 `const` 成员函数。

### 本节练习

1. 将第一阶段的成绩概念封装为 `Score` 类。
2. 设计 `Rectangle` 类，保证宽高不为负数。
3. 解释为什么无条件提供 `set_balance` 会破坏账户抽象。
4. 给每个查询函数添加合理的 `const` 和 `[[nodiscard]]`。

---

## 三、构造函数与析构函数

### 3.1 构造函数

构造函数负责建立对象的初始有效状态：

```cpp
class User
{
public:
    User(std::string name, int age)
        : name_{std::move(name)}, age_{age}
    {
        if (name_.empty()) {
            throw std::invalid_argument{"name cannot be empty"};
        }
        if (age_ < 0) {
            throw std::invalid_argument{"age cannot be negative"};
        }
    }

private:
    std::string name_;
    int age_{};
};
```

按值接收字符串再移动到成员，是一种简单且常用的“接管一份值”接口。

### 3.2 成员初始化列表

成员在进入构造函数体之前已经初始化：

```cpp
User(std::string name, int age)
    : name_{std::move(name)}, age_{age}
{
}
```

这不同于先默认构造再赋值。以下成员必须通过初始化列表处理：

- 引用成员。
- `const` 成员。
- 没有默认构造函数的成员对象。
- 基类子对象。

成员实际初始化顺序由它们在类中的声明顺序决定，而不是初始化列表的书写顺序。建议两者保持一致。

### 3.3 默认构造函数

```cpp
class Counter
{
public:
    Counter() = default;

private:
    int value_{};
};
```

只有当“默认状态”具有明确业务含义时才提供默认构造函数。不要为了让容器或框架暂时编译而创建无效默认状态。

### 3.4 `explicit`

单参数构造函数可能成为隐式转换：

```cpp
class Meter
{
public:
    explicit Meter(double value) : value_{value} {}

private:
    double value_{};
};
```

加入 `explicit` 后，必须明确构造：

```cpp
Meter distance{10.0};
```

除非确实希望发生隐式转换，否则单参数构造函数通常应使用 `explicit`。

### 3.5 委托构造

```cpp
class Color
{
public:
    Color() : Color{0, 0, 0} {}

    Color(int red, int green, int blue)
        : red_{red}, green_{green}, blue_{blue}
    {
        validate();
    }

private:
    void validate() const;
    int red_{};
    int green_{};
    int blue_{};
};
```

委托构造可将公共初始化逻辑集中到一个构造函数中。

### 3.6 析构函数

析构函数在对象生命周期结束时执行：

```cpp
class Trace
{
public:
    ~Trace()
    {
        std::cout << "destroyed\n";
    }
};
```

析构函数常用于释放对象拥有的资源。析构函数不应让异常逃出，否则栈展开期间可能导致程序终止。

### 3.7 构造与销毁顺序

构造顺序通常为：

1. 虚基类。
2. 直接基类。
3. 成员对象，按声明顺序。
4. 当前类构造函数体。

销毁顺序与构造顺序相反。

### 本节练习

1. 编写始终表示有效日期的 `Date` 类。
2. 使用打印日志的小类观察成员构造和销毁顺序。
3. 给单参数构造函数去掉 `explicit`，观察意外隐式转换。
4. 使用委托构造为 `Color` 提供黑色默认值。

---

## 四、RAII 与资源所有权

### 4.1 什么是资源

资源不只是内存，还包括：

- 文件句柄。
- 网络连接。
- 互斥锁。
- 数据库事务。
- 操作系统句柄。
- 图形资源。

资源通常需要成对操作：获取与释放、打开与关闭、加锁与解锁。

### 4.2 RAII

RAII（Resource Acquisition Is Initialization）把资源生命周期绑定到对象生命周期：

- 构造函数取得资源。
- 对象保持资源所有权。
- 析构函数释放资源。

```cpp
#include <fstream>
#include <stdexcept>

void save_report(const std::string& path)
{
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error{"cannot open output file"};
    }

    output << "report\n";
} // output 析构，文件自动关闭
```

标准文件流本身就是 RAII 类型，通常不需要手动调用 `close()`。

### 4.3 所有者与观察者

- 所有者负责确保资源最终释放。
- 观察者只临时访问资源，不负责释放。

类的接口应尽量让所有权一目了然。裸引用和裸指针通常表示非拥有观察；拥有关系优先使用值、容器或智能指针。

### 4.4 手写资源包装器练习

```cpp
#include <cstdio>
#include <stdexcept>

class File
{
public:
    File(const char* path, const char* mode)
        : handle_{std::fopen(path, mode)}
    {
        if (handle_ == nullptr) {
            throw std::runtime_error{"failed to open file"};
        }
    }

    ~File()
    {
        std::fclose(handle_);
    }

    File(const File&) = delete;
    File& operator=(const File&) = delete;

private:
    std::FILE* handle_{};
};
```

这个类先禁止复制，避免两个对象重复关闭同一个句柄。稍后可为它实现移动操作。

### 4.5 异常安全直觉

RAII 的重要价值是：函数无论正常返回还是因异常离开，已经构造完成的局部对象都会被销毁，从而自动清理资源。

不要写依赖每条控制路径都手动释放的代码：

```cpp
// 获取资源
// 中途多个 return 或可能抛异常的操作
// 最后手动释放资源
```

### 本节练习

1. 列出操作系统资源的五个例子及对应释放操作。
2. 编写用于记录函数进入和离开的 `ScopeTrace`。
3. 使用标准文件流证明提前 `return` 时文件仍会关闭。
4. 解释为什么两个 `File` 对象不能共同认为自己独占同一句柄。

---

## 五、复制语义

### 5.1 复制构造与复制赋值

```cpp
Widget first;
Widget second{first}; // 复制构造

Widget third;
third = first;        // 复制赋值
```

若没有声明，编译器通常会生成逐成员复制操作。这对于 `std::string`、`std::vector` 等值语义成员通常正是正确行为。

### 5.2 Rule of Zero

如果类只由能够自行正确管理资源的成员组成，就不要手写析构、复制或移动操作：

```cpp
class Contact
{
public:
    Contact(std::string name, std::string phone)
        : name_{std::move(name)}, phone_{std::move(phone)}
    {
    }

private:
    std::string name_;
    std::string phone_;
};
```

这就是 Rule of Zero，也是普通业务类最优先的方案。

### 5.3 浅复制问题

如果类直接保存拥有资源的裸指针，逐成员复制只会复制地址：

```cpp
class Buffer
{
private:
    std::size_t size_{};
    int* data_{}; // 假设拥有动态数组
};
```

默认复制可能导致两个对象指向同一资源，并在销毁时重复释放。生产代码优先使用 `std::vector<int>` 代替这种设计。

### 5.4 Rule of Three

在旧式手动资源管理中，如果需要自定义以下任意一个，通常需要同时考虑三个：

1. 析构函数。
2. 复制构造函数。
3. 复制赋值运算符。

```cpp
class Buffer
{
public:
    explicit Buffer(std::size_t size);
    ~Buffer();
    Buffer(const Buffer& other);
    Buffer& operator=(const Buffer& other);

private:
    std::size_t size_{};
    int* data_{};
};
```

复制应得到独立且等价的资源，即深复制。

### 5.5 自赋值与异常安全

复制赋值必须考虑：

```cpp
buffer = buffer;
```

一种简化策略是 copy-and-swap：

```cpp
Buffer& Buffer::operator=(Buffer other)
{
    swap(other);
    return *this;
}
```

实参按值复制完成后再交换状态，能够自然处理自赋值并提供较强异常安全，但也应理解其成本和适用范围。

### 5.6 禁止复制

独占资源或身份对象通常不应复制：

```cpp
File(const File&) = delete;
File& operator=(const File&) = delete;
```

### 本节练习

1. 观察包含 `std::string` 和 `std::vector` 的类默认复制行为。
2. 解释拥有裸指针的类为何不能简单逐成员复制。
3. 为教学用 `Buffer` 实现深复制并用 Sanitizer 验证。
4. 比较 Rule of Zero 版本和手动资源版本的代码量与风险。

---

## 六、移动语义

### 6.1 为什么需要移动

复制资源可能昂贵，而临时对象或即将被销毁的对象可以把资源转交给新对象：

```cpp
std::vector<int> create_values();
std::vector<int> values = create_values();
```

现代 C++ 可通过移动转移内部资源，通常无需复制全部元素。

### 6.2 移动构造与移动赋值

```cpp
class File
{
public:
    File(File&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)}
    {
    }

    File& operator=(File&& other) noexcept
    {
        if (this != &other) {
            if (handle_ != nullptr) {
                std::fclose(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

private:
    std::FILE* handle_{};
};
```

被移动对象必须仍然可析构、可赋值，但其具体值通常未指定。不要假设所有类型移动后都为空，除非类型契约明确保证。

### 6.3 `std::move`

```cpp
std::string first{"large text"};
std::string second{std::move(first)};
```

`std::move` 本身不移动任何东西，它只是把表达式转换为可供移动操作选择的右值类别。真正的移动由目标类型的构造或赋值操作完成。

移动后不要再依赖 `first` 的旧内容；可以销毁它或给它赋新值。

### 6.4 Rule of Five

手动拥有资源时，应一起考虑：

1. 析构函数。
2. 复制构造函数。
3. 复制赋值运算符。
4. 移动构造函数。
5. 移动赋值运算符。

独占资源常见策略是禁止复制、允许移动。

### 6.5 `noexcept` 的重要性

许多标准容器在重新分配时，只有确认移动构造不会抛异常，才会放心移动元素；否则可能选择复制以维持异常安全保证。

确定移动操作不会抛出时，应将其声明为 `noexcept`。

### 6.6 `= default` 与 `= delete`

```cpp
class Value
{
public:
    Value(const Value&) = default;
    Value& operator=(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;
};
```

优先让编译器生成正确操作；只有语义确实不同或需要管理底层资源时才手写。

### 本节练习

1. 为 `File` 包装器实现移动构造和移动赋值。
2. 打印复制与移动构造日志，观察 `std::vector` 扩容行为。
3. 验证移动操作是否标记 `noexcept` 对容器行为的影响。
4. 解释 `std::move` 为什么不能移动 `const` 对象中的资源。

---

## 七、静态成员、友元与嵌套类型

### 7.1 静态数据成员

静态成员属于类型，而不是某个对象：

```cpp
class IdGenerator
{
public:
    static int next() noexcept
    {
        return next_id_++;
    }

private:
    inline static int next_id_{1};
};
```

多线程环境下，这样的计数器还存在数据竞争；并发阶段会处理。

### 7.2 静态成员函数

静态成员函数没有 `this` 指针，不能直接访问非静态成员。它适合与类型密切相关但不需要具体对象的操作。

### 7.3 友元

```cpp
class Vector2
{
    friend std::ostream& operator<<(std::ostream&, const Vector2&);

private:
    double x_{};
    double y_{};
};
```

友元可以访问私有成员。它不是成员函数，也不会被继承。友元适合紧密协作的非成员操作，但过多友元会削弱封装。

### 7.4 嵌套类型

```cpp
class Dictionary
{
public:
    struct Entry
    {
        std::string word;
        std::string meaning;
    };
};
```

当一个类型主要服务于另一个类型时，可以嵌套定义，但不要制造过深层次。

### 本节练习

1. 使用静态成员统计当前存活对象数量。
2. 为二维向量设计流输出友元。
3. 判断某个工具函数应是成员、静态成员还是普通非成员函数。

---

## 八、运算符重载

### 8.1 基本原则

运算符重载应保持用户对该运算符的自然预期：

- `a + b` 不应偷偷修改 `a`。
- `a == b` 应表达等价关系。
- `operator[]` 应表现为索引访问。
- 不要仅为了语法炫技而重载运算符。

至少一个操作数必须是用户自定义类型，且不能改变运算符优先级、结合性或操作数数量。

### 8.2 算术运算符

```cpp
class Vector2
{
public:
    Vector2(double x, double y) : x_{x}, y_{y} {}

    Vector2& operator+=(const Vector2& other) noexcept
    {
        x_ += other.x_;
        y_ += other.y_;
        return *this;
    }

    friend Vector2 operator+(Vector2 left, const Vector2& right) noexcept
    {
        left += right;
        return left;
    }

private:
    double x_{};
    double y_{};
};
```

常见做法是先实现复合赋值，再用它实现对应二元运算。

### 8.3 比较运算符

```cpp
#include <compare>

class Version
{
public:
    auto operator<=>(const Version&) const = default;

private:
    int major_{};
    int minor_{};
    int patch_{};
};
```

默认三路比较按成员声明顺序进行。只有当这种顺序符合类型语义时才能直接使用。

浮点数包含 NaN，其比较并非普通全序，设计比较语义时要特别小心。

### 8.4 输入输出运算符

```cpp
std::ostream& operator<<(std::ostream& output, const Vector2& value)
{
    return output << '(' << value.x() << ", " << value.y() << ')';
}
```

返回流引用以支持链式输出。输入运算符应在解析失败时保留可理解的对象状态。

### 8.5 下标和调用运算符

```cpp
class FixedBuffer
{
public:
    int& operator[](std::size_t index) { return values_[index]; }
    const int& operator[](std::size_t index) const { return values_[index]; }

private:
    std::array<int, 10> values_{};
};
```

通常同时提供 const 和非 const 版本。若需要边界检查，可另提供 `at()`。

### 8.6 类型转换运算符

```cpp
class Handle
{
public:
    explicit operator bool() const noexcept
    {
        return valid_;
    }

private:
    bool valid_{};
};
```

转换运算符一般应使用 `explicit`，避免类型在意外上下文中自动转换。

### 本节练习

1. 为 `Vector2` 实现 `+`、`-`、`+=`、`==` 和输出运算符。
2. 为 `Date` 设计比较语义。
3. 为小型固定数组同时实现 const 和非 const 下标操作。
4. 找出三个不适合重载运算符的设计并说明原因。

---

## 九、组合与对象关系

### 9.1 组合：has-a

```cpp
class Engine
{
public:
    void start();
};

class Car
{
public:
    void start()
    {
        engine_.start();
    }

private:
    Engine engine_;
};
```

`Car` 拥有 `Engine`，二者生命周期绑定。这是组合关系。

### 9.2 聚合与非拥有关系

如果对象只观察另一个对象，可以保存引用或指针，但必须明确生命周期：

```cpp
class ReportPrinter
{
public:
    explicit ReportPrinter(std::ostream& output)
        : output_{output}
    {
    }

private:
    std::ostream& output_;
};
```

`ReportPrinter` 不拥有输出流，调用者必须确保输出流活得更久。

### 9.3 依赖注入基础

把依赖通过构造函数传入，而不是在类内部写死，可提高可测试性：

```cpp
class OrderService
{
public:
    explicit OrderService(Repository& repository)
        : repository_{repository}
    {
    }

private:
    Repository& repository_;
};
```

### 9.4 组合优于继承

组合通常具有：

- 更弱的耦合。
- 更清晰的所有权。
- 更容易替换的组件。
- 更少的脆弱基类问题。

继承适合真正的“is-a”替换关系，而不只是为了复用几行代码。

### 本节练习

1. 使用组合设计 `Computer`、`Cpu` 和 `Memory`。
2. 判断“汽车—发动机”“正方形—矩形”“日志服务—输出流”分别适合何种关系。
3. 通过构造函数为报告生成器注入输出流。

---

## 十、继承与运行时多态

### 10.1 基本继承

```cpp
class Shape
{
public:
    [[nodiscard]] virtual double area() const = 0;
    virtual ~Shape() = default;
};

class Circle final : public Shape
{
public:
    explicit Circle(double radius) : radius_{radius} {}

    [[nodiscard]] double area() const override
    {
        return std::numbers::pi * radius_ * radius_;
    }

private:
    double radius_{};
};
```

- `virtual` 启用运行时动态分派。
- `= 0` 声明纯虚函数。
- 含纯虚函数的类是抽象类，不能直接实例化。
- `override` 让编译器检查是否正确覆盖。
- `final` 禁止继续继承或覆盖。

### 10.2 多态调用

```cpp
void print_area(const Shape& shape)
{
    std::cout << shape.area() << '\n';
}
```

传入 `Circle` 时会调用 `Circle::area()`。运行时多态需要通过基类引用或指针使用。

### 10.3 虚析构函数

若对象可能通过基类指针销毁，基类析构函数必须是虚函数：

```cpp
virtual ~Shape() = default;
```

否则删除派生对象时行为未定义。即使当前示例还没有手动删除，也应为多态基类建立正确接口。

### 10.4 对象切片

```cpp
void wrong(ShapeBase shape); // 按值接收基类
```

派生对象按值转换为基类对象时，派生部分会被切掉。多态接口通常使用基类引用或指针。

标准容器若要保存不同派生类型，通常保存智能指针：

```cpp
std::vector<std::unique_ptr<Shape>> shapes;
```

智能指针将在下一阶段系统学习。

### 10.5 访问权限与继承方式

公开继承表示派生类可替代基类。保护继承和私有继承改变基类接口的可见性，实际项目中远少于公开继承；仅想复用实现时通常优先组合。

### 10.6 构造析构期间的虚调用

构造和析构期间调用虚函数不会分派到尚未构造或已经销毁的派生部分。不要依赖构造函数中的虚调用执行派生行为。

### 10.7 RTTI 与向下转换

```cpp
if (const auto* circle = dynamic_cast<const Circle*>(&shape)) {
    // 确实是 Circle
}
```

频繁向下转换往往说明基类接口不足或设计过度依赖具体类型。优先通过虚函数表达多态行为。

### 10.8 Liskov 替换直觉

若 `Derived` 公开继承 `Base`，使用者应能在期望 `Base` 的地方使用 `Derived`，而不破坏正确性。派生类不应：

- 加强基类操作的前置条件。
- 削弱基类承诺的后置条件。
- 破坏基类不变量。
- 让本应支持的操作突然失去合理语义。

“正方形是否应该继承可独立设置宽高的矩形”就是典型思考题。

### 本节练习

1. 实现 `Shape`、`Circle`、`Rectangle` 和 `Triangle`。
2. 使用基类引用调用不同派生对象。
3. 故意移除 `override` 并写错函数签名，观察差异。
4. 用日志观察通过基类指针销毁对象时虚析构函数的作用。
5. 分析正方形与矩形是否满足替换原则。

---

## 十一、多继承与虚继承

### 11.1 多继承

```cpp
class Printable
{
public:
    virtual void print(std::ostream&) const = 0;
    virtual ~Printable() = default;
};

class Savable
{
public:
    virtual void save() const = 0;
    virtual ~Savable() = default;
};

class Document : public Printable, public Savable
{
    // ...
};
```

继承多个纯接口有时合理。继承多个带状态和实现的基类会显著增加复杂度。

### 11.2 菱形继承

若两个中间基类都继承同一个基类，最终派生类可能包含两份公共基类子对象，产生歧义和状态重复。

虚继承可以让最底层对象共享一份虚基类：

```cpp
class TeachingAssistant : public virtual Student, public virtual Employee
{
};
```

但虚继承会增加初始化和理解成本。优先重新审视模型能否改用组合或接口。

### 11.3 接口类建议

接口基类通常：

- 没有可变数据成员。
- 提供纯虚行为。
- 具有虚析构函数。
- 不承担共享实现和状态。

### 本节练习

1. 使用两个纯接口模拟可打印且可序列化的对象。
2. 画出菱形继承对象结构，解释歧义来源。
3. 将一个复杂多继承设计改写为组合。

---

## 十二、面向对象设计原则

### 12.1 高内聚、低耦合

- 高内聚：一个类型的成员围绕同一职责。
- 低耦合：类型尽量少依赖其他类型的内部细节。

### 12.2 最小公开接口

公开接口一旦被使用就难以修改。只公开用户真正需要的操作，把辅助函数和表示细节留在 `private`。

### 12.3 告诉对象做什么

与其取出全部内部数据在外部修改，不如提供表达业务意图的操作：

```cpp
account.withdraw(amount);
```

通常优于：

```cpp
account.set_balance(account.balance() - amount);
```

前者可集中检查余额、限额和交易规则。

### 12.4 值语义与实体语义

值对象：

- 由内容决定身份。
- 复制后两个值相互独立。
- 例如日期、坐标、金额。

实体对象：

- 由稳定身份标识。
- 即使属性相同也可能是不同实体。
- 例如用户账户、数据库订单。

先判断类型属于哪种语义，再决定复制、比较和所有权策略。

### 12.5 SOLID 入门

- 单一职责：类型只有一个主要变化原因。
- 开闭原则：可扩展行为，减少修改稳定代码。
- Liskov 替换：派生类型能可靠替代基类型。
- 接口隔离：避免强迫用户依赖不需要的操作。
- 依赖倒置：高层策略依赖抽象，而不是写死底层细节。

这些是思考工具，不是必须机械执行的规则。过度抽象同样会降低可维护性。

### 12.6 常见坏味道

- 上帝类：一个类负责几乎所有事情。
- 贫血模型：类只有 getter/setter，规则散落外部。
- 继承只为复用代码。
- 基类接口不断使用 `dynamic_cast` 补洞。
- 对象存在大量临时无效状态。
- 双向依赖和循环所有权。
- 每个类都对应一个接口，即使没有替换需求。

### 本节练习

1. 将一个拥有十多个职责的管理类拆成协作类型。
2. 区分 `Money`、`Order`、`Point`、`UserAccount` 的值或实体语义。
3. 找出通讯录项目中的贫血模型，并加入能维护规则的行为。

---

## 十三、多文件类设计

### 13.1 头文件

`include/bank/account.hpp`：

```cpp
#pragma once

#include <string>

namespace bank
{
class Account
{
public:
    Account(std::string owner, double initial_balance);

    void deposit(double amount);
    bool withdraw(double amount);

    [[nodiscard]] const std::string& owner() const noexcept;
    [[nodiscard]] double balance() const noexcept;

private:
    std::string owner_;
    double balance_{};
};
}
```

### 13.2 实现文件

`src/account.cpp`：

```cpp
#include "bank/account.hpp"

#include <stdexcept>
#include <utility>

namespace bank
{
Account::Account(std::string owner, double initial_balance)
    : owner_{std::move(owner)}, balance_{initial_balance}
{
    if (owner_.empty()) {
        throw std::invalid_argument{"owner cannot be empty"};
    }
    if (initial_balance < 0.0) {
        throw std::invalid_argument{"initial balance cannot be negative"};
    }
}

void Account::deposit(double amount)
{
    if (amount <= 0.0) {
        throw std::invalid_argument{"deposit must be positive"};
    }
    balance_ += amount;
}

bool Account::withdraw(double amount)
{
    if (amount <= 0.0 || amount > balance_) {
        return false;
    }
    balance_ -= amount;
    return true;
}

const std::string& Account::owner() const noexcept { return owner_; }
double Account::balance() const noexcept { return balance_; }
}
```

### 13.3 依赖管理

- 头文件只包含接口真正需要的头。
- 能前置声明时可减少依赖，但值成员需要完整类型。
- 实现需要的头放入 `.cpp`。
- 不依赖传递包含。
- 类声明和定义保持在同一命名空间。

### 13.4 CMake

```cmake
add_library(bank
    src/account.cpp
)

target_include_directories(bank PUBLIC include)
target_compile_features(bank PUBLIC cxx_std_23)

add_executable(bank_app src/main.cpp)
target_link_libraries(bank_app PRIVATE bank)
```

用库 target 表达可复用业务模块，比把所有源文件直接塞进一个可执行目标更清晰。

---

## 十四、测试对象行为

### 14.1 测试公开行为

测试应关注类型承诺，而不是私有实现：

```cpp
Account account{"Alice", 100.0};
account.deposit(50.0);
assert(account.balance() == 150.0);
```

不要为了测试而把私有成员全部公开。

### 14.2 测试不变量

至少覆盖：

- 合法构造。
- 非法构造。
- 操作前后的状态。
- 边界值。
- 连续操作。
- 复制后是否独立。
- 移动后能否安全销毁和重新赋值。
- 通过基类接口调用派生行为。

### 14.3 观察特殊成员函数

可以建立教学类型记录：

```cpp
class Tracer
{
public:
    Tracer();
    Tracer(const Tracer&);
    Tracer(Tracer&&) noexcept;
    Tracer& operator=(const Tracer&);
    Tracer& operator=(Tracer&&) noexcept;
    ~Tracer();
};
```

然后测试函数返回、容器扩容、按值传参和 `std::move`，理解何时发生复制、移动或复制消除。

### 14.4 工具检查

继续保持：

- GCC/Clang：`-Wall -Wextra -Wpedantic`。
- MSVC：`/W4 /permissive-`。
- AddressSanitizer。
- UndefinedBehaviorSanitizer。
- Debug 和 Release 构建均可通过。
- `clang-tidy` 的现代化与生命周期检查（工具链支持时）。

---

## 十五、阶段项目

### 项目一：日期与日程系统

最低要求：

- `Date` 始终表示有效日期。
- 支持年月日查询。
- 支持日期比较和流输出。
- `Event` 包含标题、日期和说明。
- `Schedule` 管理事件集合，支持添加、删除、查找和排序。
- 计算逻辑与控制台 UI 分离。

进阶要求：

- 日期加减天数。
- 检测日程冲突。
- 文件序列化。
- 为核心行为编写单元测试。

### 项目二：银行账户系统

最低要求：

- 账户构造后始终有效。
- 支持存款、取款和转账。
- 拒绝非法金额和余额不足操作。
- 保存交易记录。
- 区分普通账户和具有不同规则的账户。
- 使用虚函数前先评估组合或策略是否更合适。

进阶要求：

- 通过策略对象实现手续费规则。
- 通过抽象输出接口生成报告。
- 支持撤销交易。
- 使用整数最小货币单位，避免直接使用二进制浮点表示金额。

### 项目三：图形系统

最低要求：

- 定义抽象 `Shape`。
- 实现圆、矩形和三角形。
- 计算面积与周长。
- 通过基类接口打印信息。
- 使用 `std::unique_ptr<Shape>` 存放不同图形；可提前查阅智能指针基础。
- 基类拥有虚析构函数。

进阶要求：

- 添加颜色、位置等组合对象。
- 实现图形组。
- 输出简单 SVG。
- 对比虚函数多态与 `std::variant` 方案。

### 项目四：RAII 文件包装器

最低要求：

- 构造时打开文件，析构时关闭。
- 打开失败时报告错误。
- 禁止复制。
- 支持移动构造和移动赋值。
- 移动后源对象可安全析构。
- 提供最小必要读写接口。

进阶要求：

- 实现 `swap`。
- 支持不同打开模式。
- 使用临时文件实现安全保存。
- 对比直接使用 `std::fstream` 的优缺点。

### 项目验收标准

- 类构造成功后始终满足不变量。
- 公开接口最小且表达业务意图。
- 默认优先 Rule of Zero。
- 手动资源类明确禁止或实现复制，并正确实现移动。
- 多态基类具有虚析构函数和清晰接口。
- 没有对象切片、悬空观察者或重复释放。
- 严格警告与 Sanitizer 检查通过。
- 至少一个项目包含独立库 target 和测试 target。

---

## 十六、八周学习安排

### 第 1 周：类与封装

- 类、对象、访问控制。
- 成员函数和 `this`。
- `const` 成员函数。
- 类不变量与最小接口。

### 第 2 周：构造与析构

- 构造函数和初始化列表。
- 默认、委托和 `explicit` 构造。
- 析构与成员销毁顺序。
- 完成 `Date` 或 `Score` 类型。

### 第 3 周：RAII 与复制

- 资源所有权。
- Rule of Zero 和 Rule of Three。
- 深复制、自赋值和 copy-and-swap。
- 使用 Sanitizer 检查资源类。

### 第 4 周：移动语义

- 左值、右值和 `std::move` 的直觉。
- 移动构造、移动赋值和 `noexcept`。
- Rule of Five。
- 完成文件包装器。

### 第 5 周：运算符与对象关系

- 算术、比较和流运算符。
- 静态成员、友元。
- 组合、观察关系和依赖注入。

### 第 6 周：继承与多态

- 公开继承和虚函数。
- 抽象类、`override`、`final`。
- 虚析构、对象切片。
- 多继承和虚继承概念。

### 第 7 周：设计与测试

- 值语义和实体语义。
- SOLID 基础。
- 行为测试、不变量测试。
- 多文件库与 CMake target。

### 第 8 周：综合项目

- 完成银行系统或图形系统。
- 清理公开接口和依赖。
- 运行严格警告、Sanitizer 和测试。
- 复盘阶段知识。

---

## 十七、阶段自测

### 概念题

1. `class` 与 `struct` 的语言层面差异是什么？
2. 类不变量是什么，为什么应在构造时建立？
3. 为什么成员应在初始化列表中初始化？
4. 成员初始化顺序由什么决定？
5. `explicit` 能防止什么问题？
6. RAII 如何处理提前返回和异常路径？
7. 所有者和观察者有什么区别？
8. Rule of Zero 为什么通常优于手写特殊成员函数？
9. 浅复制资源指针为什么可能导致重复释放？
10. 复制构造与复制赋值何时调用？
11. `std::move` 是否自己执行移动？
12. 被移动对象必须满足哪些最低要求？
13. 为什么移动构造常声明为 `noexcept`？
14. 哪些运算符适合作为非成员函数？
15. 组合和继承分别表达什么关系？
16. 为什么多态基类通常需要虚析构函数？
17. 什么是对象切片？
18. `override` 能帮助发现什么错误？
19. 为什么构造函数中的虚调用不会调用完整派生行为？
20. 值对象和实体对象的复制与比较语义有何差异？

### 编程题

1. 实现始终有效的 `Fraction`，并支持约分、四则运算和比较。
2. 实现 Rule of Zero 版本的动态整数序列包装类。
3. 实现一个禁止复制、允许移动的资源包装器。
4. 使用抽象基类实现多种通知方式。
5. 将一个继承复用设计改写为组合，并解释收益。
6. 为账户类设计覆盖不变量和边界的测试。

### 进入下一阶段前的检查清单

- [ ] 能用类不变量指导构造函数和公开接口设计。
- [ ] 熟悉构造、析构及成员初始化顺序。
- [ ] 理解 RAII 和资源所有权。
- [ ] 能解释 Rule of Zero、Three、Five。
- [ ] 理解复制与移动的区别。
- [ ] 能正确使用 `std::move`，不依赖移动后旧值。
- [ ] 能设计自然的基础运算符重载。
- [ ] 优先使用组合，只在满足替换关系时公开继承。
- [ ] 能安全使用虚函数和虚析构函数。
- [ ] 能识别对象切片和悬空观察者。
- [ ] 完成至少两个阶段项目。
- [ ] 项目通过测试、严格警告和 Sanitizer。

---

## 十八、易错点汇总

1. 构造函数结束后对象仍处于无效状态。
2. 把赋值写进构造函数体，而不是正确初始化成员。
3. 误以为初始化列表书写顺序决定成员初始化顺序。
4. 单参数构造函数遗漏 `explicit`。
5. 为所有成员生成无约束 setter。
6. 析构函数抛出异常。
7. 拥有裸资源却依赖默认浅复制。
8. 手写析构后忘记检查复制和移动操作。
9. 在 `std::move` 后继续依赖对象原值。
10. 给可能抛异常的移动操作错误添加 `noexcept`。
11. 重载运算符却违反其自然含义。
12. 为复用代码滥用继承。
13. 多态基类缺少虚析构函数。
14. 按值传递基类导致对象切片。
15. 覆盖虚函数时不写 `override`。
16. 在构造或析构期间依赖派生类虚行为。
17. 保存外部对象的引用却没有明确生命周期要求。
18. 使用大量 `dynamic_cast` 弥补不完整的抽象接口。
19. 为简单问题创建过多类和接口。
20. 测试私有实现而不是公开行为。

---

## 十九、下一阶段

下一阶段建议学习现代资源管理与 STL：

- `std::unique_ptr`、`std::shared_ptr`、`std::weak_ptr`。
- 所有权图与循环引用。
- 值类别、右值引用和完美转发深入。
- 标准容器的复杂度与迭代器失效。
- 迭代器、算法和函数对象。
- `optional`、`variant`、`any`、`expected`。
- Ranges 与 Views。

完成本阶段的真正标志，不是能够背出面向对象术语，而是能设计一个不容易被误用的类型，并能清楚解释它的有效状态、复制移动语义、所有权关系和多态边界。
