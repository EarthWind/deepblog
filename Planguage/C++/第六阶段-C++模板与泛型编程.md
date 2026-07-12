# 第六阶段：C++ 模板与泛型编程

> 前置要求：熟悉 STL、迭代器、算法、Ranges、值类别与完美转发。  
> 建议标准：C++23  
> 建议周期：8～10 周，每天 1～2 小时  
> 阶段目标：能够设计类型安全、约束清晰的泛型组件，阅读常见模板代码，并理解模板实例化和编译期计算机制。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 编写函数模板、类模板、变量模板和别名模板。
2. 解释模板参数推导、显式实参、CTAD 和非类型模板参数。
3. 使用全特化、偏特化和重载表达不同类型策略。
4. 使用可变参数模板、参数包和折叠表达式。
5. 使用 `decltype`、`type_traits`、`invoke` 等类型工具。
6. 理解依赖名称、两阶段查找、实例化、ODR 和模板代码组织。
7. 阅读传统 SFINAE 和检测惯用法代码。
8. 使用 Concepts 与 requires-expression 编写现代约束。
9. 理解约束归一化、包含关系和重载排序的基本规则。
10. 构建固定容量容器、单位类型、序列化器等泛型项目。

本阶段的目标不是制造复杂元编程技巧，而是让泛型接口更正确、更清晰、更容易诊断。

---

## 二、泛型编程思想

### 2.1 从重复代码开始

```cpp
int maximum(int left, int right)
{
    return left < right ? right : left;
}

double maximum(double left, double right)
{
    return left < right ? right : left;
}
```

算法相同，只是类型不同，可以抽象为模板：

```cpp
template<class T>
T maximum(T left, T right)
{
    return left < right ? right : left;
}
```

模板描述一族代码。编译器根据实际模板实参生成或选择所需实体，这一过程称为实例化。

### 2.2 泛型代码依赖能力

`maximum` 并不真正要求 `T` 是数字，它只要求：

- 能按值构造参数。
- 能使用 `<` 比较。
- 能返回选中的值。

现代泛型设计应表达所需能力，而不是根据想象限制具体类型。

### 2.3 静态多态

模板的多态通常在编译期选择：

- 不要求共同虚基类。
- 编译器可针对具体类型优化。
- 错误在实例化时发现。
- 可能增加编译时间和代码体积。

它与虚函数运行时多态、`variant` 封闭类型多态各有适用场景。

### 2.4 泛型设计原则

- 只要求算法真正需要的能力。
- 接口约束与实现使用保持一致。
- 优先组合标准 Concepts。
- 错误应尽量发生在接口边界。
- 不为“泛型”牺牲可读性。
- 没有复用需求时，普通函数往往更好。

---

## 三、函数模板

### 3.1 基本语法

```cpp
template<typename T>
T add(T left, T right)
{
    return left + right;
}
```

`class` 和 `typename` 在类型模板参数声明中含义相同：

```cpp
template<class T>
```

调用：

```cpp
auto first = add(1, 2);          // T 推导为 int
auto second = add(1.5, 2.5);     // T 推导为 double
auto third = add<double>(1, 2.5); // 显式指定 T
```

### 3.2 推导要求一致

```cpp
add(1, 2.5); // T 从第一个参数推导为 int，从第二个推导为 double，冲突
```

可使用两个类型参数和推导返回类型：

```cpp
template<class Left, class Right>
auto add(Left left, Right right)
{
    return left + right;
}
```

但支持混合类型后必须考虑结果语义、隐式转换和对称性。

### 3.3 参数传递

```cpp
template<class T>
void inspect(const T& value);

template<class T>
void consume(T value);

template<class T>
void forward_to_sink(T&& value);
```

选择规则仍基于语义：只读观察、取得一份值或保留值类别。不要因为模板就一律使用 `const T&` 或 `T&&`。

### 3.4 数组引用推导长度

```cpp
template<class T, std::size_t N>
constexpr std::size_t array_size(const T (&)[N]) noexcept
{
    return N;
}
```

引用阻止原生数组退化为指针，从而让 `N` 可推导。

### 3.5 返回类型

```cpp
template<class Left, class Right>
auto multiply(const Left& left, const Right& right)
    -> decltype(left * right)
{
    return left * right;
}
```

C++14 起普通 `auto` 返回推导通常更简洁。尾置返回类型适合返回表达式依赖参数且需要在函数体之前写出类型的情况。

### 3.6 模板与普通重载

```cpp
template<class T>
void print(const T& value);

void print(const std::string& value);
```

重载决议综合考虑参数匹配和模板/非模板规则。不要用“普通函数永远优先”这类过度简化规则代替实际重载分析。

### 本节练习

1. 编写支持不同算术类型的 `clamp_value`。
2. 编写能推导原生数组长度的打印函数。
3. 为通用 `contains` 函数比较按 Range、迭代器对和具体容器接口。
4. 设计测试观察普通重载与模板重载的选择。

---

## 四、类模板

### 4.1 基本定义

```cpp
template<class T>
class Box
{
public:
    explicit Box(T value)
        : value_{std::move(value)}
    {
    }

    [[nodiscard]] const T& value() const& noexcept
    {
        return value_;
    }

    [[nodiscard]] T value() &&
    {
        return std::move(value_);
    }

private:
    T value_;
};
```

使用：

```cpp
Box<int> number{42};
Box<std::string> text{"hello"};
```

### 4.2 成员定义

类外定义必须带模板头和完整类型：

```cpp
template<class T>
const T& Box<T>::value() const& noexcept
{
    return value_;
}
```

模板定义通常必须在实例化位置可见，因此常放在头文件，或由 `.hpp` 包含 `.tpp/.ipp` 实现文件。

### 4.3 默认模板参数

```cpp
template<class T, class Allocator = std::allocator<T>>
class DynamicArray;
```

默认参数适合具有明确常用策略的可配置组件。

### 4.4 类模板参数推导 CTAD

```cpp
std::pair pair{1, 2.5};
std::vector values{1, 2, 3};
Box box{std::string{"hello"}};
```

编译器从构造参数推导模板实参。CTAD 只推导类模板参数，不是“变量类型变成动态类型”。

### 4.5 推导指引

```cpp
template<class Iterator>
Range(Iterator first, Iterator last)
    -> Range<typename std::iterator_traits<Iterator>::value_type>;
```

只有隐式生成的推导规则不能表达正确语义时才编写自定义 deduction guide。

### 4.6 成员函数模板

```cpp
class Printer
{
public:
    template<class T>
    void print(const T& value) const
    {
        std::cout << value;
    }
};
```

普通类可有成员模板，类模板也可有独立模板参数的成员模板。

### 本节练习

1. 实现 `FixedStack<T, Capacity>` 的基础接口。
2. 为 `Box` 观察左值和右值对象提供合适访问函数。
3. 编写一个需要自定义推导指引的小类型并验证推导结果。
4. 将模板声明与 `.tpp` 实现文件正确组织。

---

## 五、非类型模板参数

### 5.1 编译期值参数

```cpp
template<class T, std::size_t N>
class FixedBuffer
{
private:
    std::array<T, N> data_{};
};
```

`N` 是类型的一部分：

```cpp
FixedBuffer<int, 8> first;
FixedBuffer<int, 16> second; // 不同类型
```

### 5.2 `auto` 非类型参数

```cpp
template<auto Value>
struct Constant
{
    static constexpr auto value = Value;
};
```

### 5.3 结构化类型参数

现代 C++ 支持满足结构化类型规则的类类型作为非类型模板参数：

```cpp
struct Dimensions
{
    std::size_t rows;
    std::size_t columns;

    constexpr bool operator==(const Dimensions&) const = default;
};

template<Dimensions Size>
class Matrix;
```

需要满足标准对结构化类型的限制。

### 5.4 字符串编译期参数

字符串字面量不能像整数一样直接作为普通值参数使用。高级库常用结构化固定字符串包装器实现：

```cpp
template<std::size_t N>
struct FixedString
{
    char data[N];

    constexpr FixedString(const char (&text)[N])
    {
        std::copy_n(text, N, data);
    }
};

template<FixedString Name>
struct NamedField;
```

这是编译期 DSL 常见技巧，但会增加复杂度，只有明确收益时使用。

### 本节练习

1. 实现固定尺寸矩阵类型。
2. 用非类型模板参数表达环形缓冲区容量。
3. 使用 `auto` 参数创建编译期常量包装器。

---

## 六、特化与重载

### 6.1 全特化

```cpp
template<class T>
struct TypeName;

template<>
struct TypeName<int>
{
    static constexpr std::string_view value{"int"};
};
```

全特化为一组确定模板实参提供不同实现。

### 6.2 类模板偏特化

```cpp
template<class T>
struct IsPointer
{
    static constexpr bool value{false};
};

template<class T>
struct IsPointer<T*>
{
    static constexpr bool value{true};
};
```

函数模板不能偏特化；通常使用函数重载、Concepts 或辅助类模板。

### 6.3 变量模板

```cpp
template<class T>
inline constexpr bool is_pointer_v = IsPointer<T>::value;
```

变量模板常为类型特征提供简洁 `_v` 接口。

### 6.4 别名模板

```cpp
template<class T>
using StringMap = std::unordered_map<std::string, T>;
```

别名模板不能像类模板一样被特化，但可引用已经特化的辅助类型。

### 6.5 优先重载还是特化

- 函数行为差异：通常优先重载和约束。
- 类型映射或结构差异：类模板特化常见。
- 标准库模板只能在标准允许的特定情形为用户类型特化，不能随意向 `std` 添加内容。

### 6.6 特化可见性

特化必须在首次会导致对应实例化之前可见。不同翻译单元看到不一致特化可能造成 ODR 问题。

### 本节练习

1. 实现教学版 `remove_const` 和 `is_pointer`。
2. 为指针和非指针分别设计重载，而不是偏特化函数模板。
3. 使用类偏特化识别 `std::vector<T, A>`。

---

## 七、可变参数模板

### 7.1 参数包

```cpp
template<class... Types>
struct TypeList;

template<class... Args>
void log(Args&&... args);
```

- `Types` 是模板参数包。
- `args` 是函数参数包。
- `sizeof...(Args)` 得到包大小。

### 7.2 包展开

```cpp
template<class... Args>
void print_all(Args&&... args)
{
    (std::cout << ... << std::forward<Args>(args));
}
```

`std::forward<Args>(args)...` 对包中每个对应元素展开。

### 7.3 折叠表达式

```cpp
template<class... Values>
auto sum(Values... values)
{
    return (values + ...);
}
```

四种形式：

```text
(pack op ...)          一元右折叠
(... op pack)          一元左折叠
(pack op ... op init)  二元右折叠
(init op ... op pack)  二元左折叠
```

空包是否合法取决于运算符和是否提供初始值：

```cpp
template<class... Values>
auto sum(Values... values)
{
    return (0 + ... + values);
}
```

### 7.4 逗号折叠

```cpp
template<class... Args>
void print_lines(const Args&... args)
{
    ((std::cout << args << '\n'), ...);
}
```

括号很重要，应明确展开单位和求值顺序。

### 7.5 完美转发包

```cpp
template<class Callable, class... Args>
decltype(auto) call(Callable&& callable, Args&&... args)
{
    return std::invoke(
        std::forward<Callable>(callable),
        std::forward<Args>(args)...);
}
```

`decltype(auto)` 保留被调用结果可能具有的引用性质。只有确实需要保留它时才使用。

### 7.6 递归展开与索引序列

C++17 前常用递归处理参数包。现代代码优先折叠表达式。需要按索引处理 tuple 时可使用：

```cpp
std::index_sequence<0, 1, 2>
std::make_index_sequence<N>
```

C++26 引入包索引能力后，一些辅助递归会进一步简化；本阶段先掌握当前通用方法。

### 本节练习

1. 实现带分隔符的可变参数打印。
2. 使用逻辑与折叠判断全部条件。
3. 编写完美转发到 `std::invoke` 的包装器。
4. 使用 `index_sequence` 输出 tuple 所有元素。

---

## 八、`decltype` 与类型工具

### 8.1 `decltype`

```cpp
int value{};
int& reference = value;

decltype(value) first{};        // int
decltype((value)) second = value; // int&
decltype(reference) third = value; // int&
```

未加括号的名称表达式遵循特殊规则；其他表达式根据值类别得到类型。

### 8.2 `decltype(auto)`

```cpp
decltype(auto) access(Container& container, std::size_t index)
{
    return (container[index]);
}
```

括号可能使返回类型推导为引用。误用会返回悬空引用，因此必须明确被引用对象生命周期。

### 8.3 常用 type traits

- `is_same`。
- `is_integral`、`is_floating_point`、`is_arithmetic`。
- `is_enum`、`is_class`、`is_pointer`。
- `is_constructible`、`is_convertible`。
- `is_copy_constructible`、`is_move_constructible`。
- `is_invocable`、`is_nothrow_invocable`。
- `remove_reference`、`remove_cv`、`remove_cvref`。
- `decay`。
- `conditional`。
- `common_type`、`common_reference`。

```cpp
static_assert(std::is_integral_v<int>);
using Clean = std::remove_cvref_t<const int&>;
```

### 8.4 `decay` 与 `remove_cvref`

`remove_cvref_t<T>` 只移除顶层 cv 和引用。`decay_t<T>` 还会执行类似按值参数的数组到指针、函数到指针转换。不要把二者当作等价工具。

### 8.5 `std::invoke` 相关类型

```cpp
std::invoke_result_t<Callable, Args...>
std::is_invocable_v<Callable, Args...>
std::is_nothrow_invocable_v<Callable, Args...>
```

它们帮助描述通用可调用接口。

### 8.6 `if constexpr`

```cpp
template<class T>
void describe(const T& value)
{
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integer: " << value;
    } else {
        std::cout << "other: " << value;
    }
}
```

未选中的分支不会为当前实例化生成，但非依赖的语法和名称仍需在模板定义阶段合法。

### 本节练习

1. 对多个 cv/ref 类型计算 `remove_cvref_t` 和 `decay_t`。
2. 使用 `if constexpr` 为枚举与普通值选择输出方式。
3. 编写保留引用返回的访问包装器并分析生命周期。
4. 使用 `is_invocable` 验证回调接口。

---

## 九、模板实例化与名称查找

### 9.1 两阶段查找

模板中的名称大致分为：

- 非依赖名称：定义模板时查找。
- 依赖名称：实例化时结合模板实参查找。

这解释了为什么一些模板错误在定义时出现，另一些只有具体实例化时出现。

### 9.2 依赖类型与 `typename`

```cpp
template<class Container>
void process(const Container& container)
{
    typename Container::const_iterator iterator = container.begin();
}
```

`Container::const_iterator` 依赖模板参数，编译器不能预先确定它是类型，需要 `typename` 消除歧义。

### 9.3 依赖模板与 `template`

```cpp
template<class Object>
void invoke_member(Object& object)
{
    object.template convert<int>();
}
```

`template` 告诉编译器后面的 `<` 开始模板实参列表，而不是小于运算符。

### 9.4 派生类中的依赖基类

```cpp
template<class T>
class Derived : public Base<T>
{
public:
    void run()
    {
        this->work();
        // 或 Base<T>::work();
    }
};
```

依赖基类成员不会自动作为非依赖名称找到，常需 `this->` 或限定名。

### 9.5 ADL

参数依赖查找会在实参类型关联的命名空间中寻找函数：

```cpp
using std::swap;
swap(left, right);
```

这种写法允许标准 `swap` 与用户类型同命名空间的定制 `swap` 协作。

### 9.6 ODR 与代码组织

模板定义通常放头文件，因为实例化点必须看到完整定义。常见选择：

- 全部放 `.hpp`。
- 声明放 `.hpp`，定义放 `.tpp`，末尾包含 `.tpp`。
- 对固定类型做显式实例化，把实现隐藏在 `.cpp`。

不同翻译单元中的模板定义必须符合 ODR。

### 9.7 显式实例化

```cpp
// 头文件
extern template class Matrix<double, 4, 4>;

// 源文件
template class Matrix<double, 4, 4>;
```

可减少重复实例化和控制支持类型，但会降低模板的开放性，需按编译时间和库边界选择。

### 本节练习

1. 修复缺少 `typename` 的依赖类型错误。
2. 修复依赖基类成员查找问题。
3. 为自定义类型提供 ADL 可发现的 `swap`。
4. 尝试对固定模板组合显式实例化。

---

## 十、SFINAE 与检测惯用法

### 10.1 SFINAE

SFINAE 表示“替换失败不是错误”。模板实参替换在特定立即上下文失败时，该候选从重载集合移除，而不是让整个程序立刻失败。

传统写法：

```cpp
template<class T,
         std::enable_if_t<std::is_integral_v<T>, int> = 0>
T twice(T value)
{
    return value * 2;
}
```

现代代码通常使用 Concept：

```cpp
template<std::integral T>
T twice(T value)
{
    return value * 2;
}
```

仍需阅读 SFINAE，因为大量 C++11/14/17 库使用它。

### 10.2 `void_t` 检测

```cpp
template<class, class = void>
struct HasSize : std::false_type {};

template<class T>
struct HasSize<T, std::void_t<decltype(std::declval<const T&>().size())>>
    : std::true_type {};
```

`std::declval<T>()` 只用于未求值上下文，模拟得到某类型表达式，不可实际调用。

### 10.3 标签分派

```cpp
template<class Iterator>
void advance_impl(Iterator& iterator, std::ptrdiff_t distance,
                  std::random_access_iterator_tag);

template<class Iterator>
void advance_impl(Iterator& iterator, std::ptrdiff_t distance,
                  std::input_iterator_tag);
```

它通过类型标签在编译期选择实现，是传统 STL 常见技术。现代代码可使用重载 Concepts 或 `if constexpr`。

### 10.4 SFINAE 的边界

- 不是所有模板错误都属于替换失败。
- 函数体深处的错误通常不会让候选优雅消失。
- 复杂 `enable_if` 容易导致难懂诊断。
- 不同位置的约束可能影响签名和重声明规则。

### 本节练习

1. 阅读一个 `enable_if` 接口并改写为 Concept。
2. 用 `void_t` 检测 `.size()`。
3. 用 tag dispatch 与 `if constexpr` 分别实现两种策略。
4. 构造一个不属于 SFINAE 立即上下文的错误并观察诊断。

---

## 十一、Concepts 与约束

### 11.1 标准 Concept

```cpp
#include <concepts>

template<std::integral T>
T gcd(T left, T right);
```

常用标准 Concepts：

- `same_as`、`derived_from`。
- `convertible_to`。
- `integral`、`floating_point`。
- `constructible_from`、`default_initializable`。
- `copyable`、`movable`、`semiregular`、`regular`。
- `invocable`、`predicate`、`relation`。
- Ranges 的各类 range 和 iterator Concepts。

### 11.2 定义 Concept

```cpp
template<class T>
concept Printable = requires(std::ostream& output, const T& value) {
    { output << value } -> std::same_as<std::ostream&>;
};
```

Concept 是编译期布尔谓词，但它的关键价值是参与模板约束与排序。

### 11.3 requires-expression

```cpp
template<class T>
concept ContainerLike = requires(T container, const T const_container) {
    typename T::value_type;                       // 类型要求
    { const_container.size() } -> std::convertible_to<std::size_t>; // 复合要求
    { container.begin() };                        // 简单要求
    requires std::same_as<                        // 嵌套要求
        decltype(container.begin()),
        decltype(container.end())>;
};
```

四类要求：

- 简单要求。
- 类型要求。
- 复合要求。
- 嵌套要求。

### 11.4 约束写法

```cpp
template<class T>
requires std::integral<T>
T first(T value);

template<std::integral T>
T second(T value);

auto third(std::integral auto value);
```

选择团队中一致、可读的形式。

### 11.5 语法有效不等于语义正确

Concept 通常只能检查表达式是否存在及类型关系，无法自动证明运行时语义。例如 `std::equality_comparable` 不能证明用户的 `==` 真正满足等价关系公理。

### 11.6 约束与实现一致

```cpp
template<std::ranges::input_range R>
void algorithm(R&& range)
{
    // 若实现多次遍历，input_range 约束太弱，应要求 forward_range
}
```

约束过强会排除本可支持的类型；约束过弱会让错误深入实现。应要求最低但充分的能力。

### 11.7 约束重载

```cpp
template<std::ranges::range R>
void inspect(const R&);

template<std::ranges::sized_range R>
void inspect(const R&);
```

更受约束的候选可在满足条件时优先，但是否“更受约束”基于标准的约束归一化和包含关系，不只是人类觉得表达式更严格。

### 11.8 Concept 复用

```cpp
template<class T>
concept Number = std::integral<T> || std::floating_point<T>;
```

优先通过命名 Concept 组合原子约束。重复书写语法看似相同的布尔表达式，未必产生期望的包含关系。

### 本节练习

1. 定义 `StreamWritable` Concept。
2. 为矩阵元素定义最低必要运算约束。
3. 将三个 SFINAE 接口改写为 Concepts。
4. 找出一个约束过强和一个约束过弱的算法。
5. 建立一般 Range 和 sized Range 重载并验证选择。

---

## 十二、编译期编程

### 12.1 `constexpr`

```cpp
constexpr int factorial(int value)
{
    int result{1};
    for (int current{2}; current <= value; ++current) {
        result *= current;
    }
    return result;
}

static_assert(factorial(5) == 120);
```

`constexpr` 函数可在编译期或运行期执行。

### 12.2 `consteval`

```cpp
consteval std::uint32_t hash_literal(std::string_view text)
{
    // 编译期哈希
}
```

立即函数的每次潜在求值调用都必须产生常量表达式，适合必须编译期验证的配置、格式和生成任务。

### 12.3 `constinit`

```cpp
constinit int global_counter = 0;
```

`constinit` 要求静态或线程存储期对象进行静态初始化，但不表示对象之后不可修改。不要与 `const` 混淆。

### 12.4 `static_assert`

```cpp
static_assert(sizeof(void*) >= 4);
static_assert(std::is_nothrow_move_constructible_v<T>,
              "T must be nothrow movable");
```

优先用 Concept 表达接口可用性，用 `static_assert` 检查实现内部不变量或给出专门诊断。

### 12.5 类型计算与值计算

传统模板元编程用递归类模板计算：

```cpp
template<int N>
struct Factorial
{
    static constexpr int value = N * Factorial<N - 1>::value;
};

template<>
struct Factorial<0>
{
    static constexpr int value = 1;
};
```

现代值计算优先普通 `constexpr` 函数；类型变换仍常使用模板和 type traits。

### 12.6 类型列表入门

```cpp
template<class... Types>
struct TypeList {};

template<class List>
struct Size;

template<class... Types>
struct Size<TypeList<Types...>>
    : std::integral_constant<std::size_t, sizeof...(Types)> {};
```

类型列表是理解 tuple、variant 和编译期注册表的基础，但不要在普通业务代码中构建不必要的元编程框架。

### 本节练习

1. 用 `constexpr` 循环生成素数表。
2. 使用 `consteval` 验证编译期标识符。
3. 解释 `constinit`、`constexpr` 和 `const` 的区别。
4. 为 TypeList 实现大小和是否包含某类型。

---

## 十三、泛型设计模式

### 13.1 CRTP

```cpp
template<class Derived>
class Printable
{
public:
    void print() const
    {
        static_cast<const Derived&>(*this).print_impl();
    }
};
```

CRTP 可实现静态多态、mixin 和接口复用，但会增加耦合与诊断复杂度。若虚函数成本并非问题，运行时多态可能更清晰。

### 13.2 Policy-Based Design

```cpp
template<class StoragePolicy, class ErrorPolicy>
class Repository
{
    StoragePolicy storage_;
    ErrorPolicy errors_;
};
```

策略模板把变化点放入类型参数，适合编译期配置。策略组合过多会产生类型爆炸和长错误信息。

### 13.3 Tag Invoke 与定制点概念

标准库和现代库常通过定制点对象协调默认实现与用户扩展。学习重点是：

- 避免直接向 `std` 添加不允许的重载。
- 让定制函数与用户类型位于关联命名空间。
- 防止无意 ADL 和递归调用。

实现完整通用定制点较复杂，本阶段以阅读和使用现有库为主。

### 13.4 类型擦除与模板边界

模板将类型传播到编译期，类型擦除把不同具体类型统一到稳定运行时接口。大型系统常在内部使用模板优化，在 ABI 或模块边界使用类型擦除控制编译依赖和代码体积。

### 13.5 强类型与单位

```cpp
template<class Tag, class Rep>
class StrongType
{
public:
    explicit constexpr StrongType(Rep value) : value_{value} {}
    [[nodiscard]] constexpr Rep value() const { return value_; }

private:
    Rep value_;
};

struct MeterTag;
struct SecondTag;

using Meter = StrongType<MeterTag, double>;
using Second = StrongType<SecondTag, double>;
```

不同标签类型不可意外混用。进一步可通过维度模板实现单位运算。

### 13.6 表达式模板概念

数值库可能把 `a + b + c` 表示为轻量表达式树，延迟到赋值时一次计算，以减少临时对象。但它会带来生命周期、错误信息和编译时间问题。本阶段理解用途即可。

---

## 十四、诊断、编译时间与代码体积

### 14.1 阅读模板错误

建议从以下顺序定位：

1. 找到自己代码中的首次实例化位置。
2. 找到“required from”或约束不满足链。
3. 查看实际推导出的模板实参。
4. 找到最早的有效失败原因。
5. 暂时缩小表达式，建立最小复现。

Concepts 诊断通常比深层 SFINAE 更清晰，但仍需理解调用链。

### 14.2 控制实例化

- 不在公共头文件包含无关实现。
- 避免每个调用点生成巨大 Lambda 或模板组合。
- 把不依赖模板参数的逻辑移到普通函数。
- 对固定支持类型考虑显式实例化。
- 使用前置声明与稳定边界减少重编译。
- 测量后再优化编译时间。

### 14.3 代码膨胀

每组模板实参可能生成独立机器码，但编译器和链接器也可能合并相同实体。不能只凭源代码判断最终体积，应检查构建产物和符号。

### 14.4 调试模板代码

- 使用 `static_assert` 检查推导类型。
- 使用 `std::same_as` 编写编译期测试。
- 为每个 Concept 建立正例和反例。
- 分开测试约束、编译期结果和运行时行为。
- 不依赖未指定的编译器类型名称字符串作为正式逻辑。

---

## 十五、阶段项目

### 项目一：固定容量容器

实现 `StaticVector<T, Capacity>`：

- 在对象内部保存固定容量存储。
- 运行时长度可变且不动态分配。
- 提供 `size`、`capacity`、`empty`、`push_back`、`emplace_back`、`pop_back`。
- 提供下标、`at`、`begin/end`。
- 正确构造和销毁非平凡元素。
- 复制、移动和异常安全语义明确。

这是高级项目。可先用 `std::array<std::optional<T>, Capacity>` 完成安全版本，再学习原始存储和显式生命周期。

进阶要求：

- 满足常用 Range Concepts。
- 条件 `noexcept`。
- 使用 Concepts 约束操作。
- 与 C++26 `std::inplace_vector` 的设计目标比较。

### 项目二：强类型单位库

- 使用标签区分米、秒、千克等单位。
- 禁止无关单位直接相加。
- 支持同维度单位转换。
- 使用非类型模板参数表达维度指数。
- 运算结果类型在编译期推导。
- 使用 `static_assert` 验证类型。

进阶要求：

- 支持速度、加速度等派生维度。
- 支持不同数值表示类型。
- 使用 Concepts 限制算术表示类型。

### 项目三：泛型序列化框架

- 为整数、浮点、字符串和容器提供序列化。
- 使用重载和 Concepts 选择实现。
- 使用 `expected` 返回解析错误。
- 对用户类型提供明确扩展点。
- 禁止重载歧义和无限递归。

进阶要求：

- 支持 tuple 和 variant。
- 使用 fold expression 展开字段。
- 加入版本信息和向后兼容策略。

### 项目四：编译期命令路由器

- 使用固定字符串或枚举作为命令标识。
- 使用参数包注册处理器。
- 编译期检查重复命令。
- 使用 `std::invoke` 调用不同可调用对象。
- 对处理器签名提供 Concept 诊断。

### 项目五：泛型矩阵库

- 模板参数表达元素类型和固定维度。
- 支持加法、标量乘法和合法尺寸矩阵乘法。
- 使用 Concepts 表达元素操作要求。
- 提供迭代器或 Range 接口。
- 编译期拒绝尺寸不匹配。

### 项目验收标准

- 模板只要求实现真正需要的能力。
- 约束在接口层清晰表达。
- 正例与反例编译期测试齐全。
- 不使用难以维护的无必要元编程技巧。
- 转发接口正确使用 `std::forward`。
- 不返回悬空引用或表达式对象。
- 模板定义组织符合 ODR。
- 对代码体积和编译时间有基本测量。
- 严格警告、Sanitizer 与运行时测试通过。

---

## 十六、九周学习安排

### 第 1 周：函数模板

- 泛型思想和实例化。
- 推导、显式实参和重载。
- 参数与返回值设计。

### 第 2 周：类模板与非类型参数

- 类模板和成员定义。
- CTAD 与推导指引。
- 编译期容量和结构化参数。

### 第 3 周：特化与参数包

- 全特化和偏特化。
- 变量/别名模板。
- 可变参数模板和折叠表达式。

### 第 4 周：类型工具

- `decltype`、`decltype(auto)`。
- type traits、`if constexpr`。
- `invoke` 与可调用类型。

### 第 5 周：实例化与查找

- 两阶段查找。
- `typename`、`template`。
- ADL、ODR、显式实例化。

### 第 6 周：SFINAE

- `enable_if`。
- `void_t` 检测惯用法。
- tag dispatch。
- 阅读旧式模板库代码。

### 第 7 周：Concepts

- 标准 Concepts。
- requires-expression。
- 约束重载和最低充分要求。

### 第 8 周：编译期编程与设计模式

- constexpr、consteval、constinit。
- 类型列表、CRTP、策略设计。
- 模板与类型擦除边界。

### 第 9 周：综合项目

- 完成单位库、矩阵库或序列化框架。
- 编写编译期和运行时测试。
- 检查诊断、编译时间和代码体积。

---

## 十七、阶段自测

### 概念题

1. 模板定义与模板实例化有什么区别？
2. 函数模板推导为什么可能因两个参数类型不同而失败？
3. 模板定义为什么通常放在头文件？
4. CTAD 推导的是什么？
5. 非类型模板参数为什么会影响最终类型？
6. 函数模板能否偏特化？应使用什么替代？
7. 参数包与包展开分别是什么？
8. 一元折叠处理空包时有什么限制？
9. `decltype(name)` 与 `decltype((name))` 为什么可能不同？
10. `decay_t` 与 `remove_cvref_t` 有什么区别？
11. `if constexpr` 和普通 `if` 的实例化行为有何差异？
12. 为什么依赖类型前常需要 `typename`？
13. 依赖基类成员为什么常需 `this->`？
14. ADL 如何帮助定制 `swap`？
15. 什么是 SFINAE 的立即上下文？
16. `void_t` 检测惯用法解决什么问题？
17. Concept 与 requires-expression 有何关系？
18. 为什么约束应是最低但充分的？
19. Concept 能否证明运算满足完整数学语义？
20. `constexpr`、`consteval` 和 `constinit` 有何区别？

### 编程题

1. 实现固定容量栈模板。
2. 使用参数包和折叠表达式实现格式简单的日志函数。
3. 使用 `index_sequence` 遍历 tuple。
4. 用传统检测惯用法和 Concept 分别检测可输出类型。
5. 为 Range 算法设计最低充分约束。
6. 实现类型安全单位的基础版本。
7. 为一个模板类编写显式实例化。
8. 把深层 SFINAE 代码改写为 Concepts。

### 进入下一阶段前的检查清单

- [ ] 熟练编写函数和类模板。
- [ ] 理解推导、CTAD 和非类型模板参数。
- [ ] 能正确选择重载、全特化和偏特化。
- [ ] 熟练使用参数包和折叠表达式。
- [ ] 掌握常用 type traits 和 `decltype`。
- [ ] 能解释依赖名称与两阶段查找。
- [ ] 能阅读 SFINAE 和检测惯用法。
- [ ] 能使用 Concepts 设计清晰约束。
- [ ] 能编写编译期正例和反例测试。
- [ ] 能控制模板代码组织和实例化边界。
- [ ] 完成至少两个阶段项目。
- [ ] 项目通过严格警告、Sanitizer 和测试。

---

## 十八、易错点汇总

1. 没有复用需求也强行模板化。
2. 对混合类型运算不考虑返回和转换语义。
3. 模板定义只放在 `.cpp`，导致其他翻译单元无法实例化。
4. 把 CTAD 误解为函数模板实参推导或动态类型。
5. 尝试偏特化函数模板。
6. 特化在首次实例化之后才可见。
7. 折叠表达式括号或展开单位错误。
8. 对空参数包没有定义合理行为。
9. `decltype(auto)` 意外返回局部对象引用。
10. 混淆 `decay` 与 `remove_cvref`。
11. 认为 `if constexpr` 的未选分支可以包含任何语法错误。
12. 依赖类型遗漏 `typename`。
13. 依赖模板调用遗漏 `template`。
14. 依赖基类成员查找失败。
15. 不受控制的 ADL 引入意外重载。
16. 认为任何模板错误都会触发 SFINAE。
17. 使用复杂 `enable_if` 而不采用更清晰 Concept。
18. 约束比实现需要更强或更弱。
19. 认为 Concept 自动验证运行时语义公理。
20. 元编程技巧造成编译时间、代码体积和维护成本失控。

---

## 十九、下一阶段

下一阶段建议学习错误处理与工程化：

- 异常机制、栈展开和异常安全保证。
- `error_code`、`optional`、`expected` 的错误策略。
- API 边界与契约式设计。
- CMake target、依赖管理和安装导出。
- 单元测试、Mock、覆盖率和基准测试。
- Sanitizer、静态分析和 fuzz testing。
- ABI、PImpl、静态库、动态库和发布流程。

完成本阶段的真正标志，是能够为泛型算法写出最低充分约束，让错误在调用接口处以可理解方式出现，并能解释模板代码何时、在哪里以及为什么被实例化。
