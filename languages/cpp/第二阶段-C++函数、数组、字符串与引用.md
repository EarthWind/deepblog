# 第二阶段：函数、数组、字符串与引用

> 前置要求：完成第一阶段，能够使用变量、分支、循环和标准输入输出编写命令行程序。  
> 建议标准：C++23  
> 建议周期：5～7 周，每天 1～2 小时  
> 阶段目标：能够使用函数拆分程序，正确传递数据，并使用现代字符串与数组工具完成多文件项目。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 声明、定义和调用函数，并理解参数、返回值及作用域。
2. 根据数据大小和所有权选择值传递、引用传递或指针传递。
3. 使用函数重载、默认参数、`constexpr` 和 Lambda 组织逻辑。
4. 熟练使用 `std::array`、`std::vector`、`std::string` 和 `std::string_view`。
5. 理解原生数组、指针和引用的基本关系及常见风险。
6. 使用头文件与源文件组织小型项目。
7. 通过调试器观察调用栈、参数和对象生命周期。
8. 独立完成一个由多个函数和源文件组成的命令行项目。

本阶段会介绍裸指针，但不会使用裸指针管理动态内存。资源所有权和智能指针将在后续阶段深入学习。

---

## 二、函数基础

### 2.1 为什么需要函数

函数用于：

- 为一段逻辑命名。
- 消除重复代码。
- 把复杂问题拆成可测试的小问题。
- 隐藏实现细节。
- 明确输入、输出和职责。

```cpp
#include <iostream>

int square(int number)
{
    return number * number;
}

int main()
{
    std::cout << square(5) << '\n';
}
```

`int square(int number)` 中：

- 第一个 `int` 是返回类型。
- `square` 是函数名。
- `int number` 是形参。
- `square(5)` 中的 `5` 是实参。

### 2.2 声明、定义与调用

函数必须在调用位置之前已经声明：

```cpp
#include <iostream>

int add(int left, int right); // 声明

int main()
{
    std::cout << add(2, 3) << '\n'; // 调用
}

int add(int left, int right) // 定义
{
    return left + right;
}
```

函数声明也称函数原型，它告诉编译器函数的名称、返回类型和参数类型。

### 2.3 返回值

```cpp
double average(double total, int count)
{
    if (count <= 0) {
        return 0.0;
    }
    return total / count;
}
```

每条可能执行到的路径都应返回符合类型的值。没有返回值的函数使用 `void`：

```cpp
void print_separator()
{
    std::cout << "----------------\n";
}
```

不要返回局部变量的引用或指针：

```cpp
const int& wrong()
{
    int value{42};
    return value; // 错误：函数结束后 value 已不存在
}
```

### 2.4 函数的作用域与局部变量

不同函数的局部变量互不影响：

```cpp
void first()
{
    int value{1};
}

void second()
{
    int value{2}; // 与 first 中的 value 是不同变量
}
```

局部变量通常在进入其作用域时创建，离开作用域时销毁。尽量避免可变全局变量，因为它会隐藏依赖并增加调试难度。

### 2.5 设计单一职责函数

一个函数最好只做一件明确的事。例如，不要让“计算平均值”函数同时读取输入、打印菜单和保存文件。

```cpp
double calculate_average(double total, int count);
void print_report(double average);
```

好函数的常见特征：

- 名称表达动作或问题。
- 参数数量较少且含义清晰。
- 返回值含义明确。
- 不依赖隐藏的全局状态。
- 长度适中，逻辑层次单一。

### 本节练习

1. 编写 `max_of_three`，返回三个整数中的最大值。
2. 编写 `is_leap_year`，判断年份是否为闰年。
3. 编写 `fahrenheit_to_celsius`。
4. 将第一阶段计算器拆分为菜单、输入、计算和输出函数。
5. 找出一个职责过多的函数，并将其拆分成至少三个函数。

---

## 三、参数传递与返回策略

### 3.1 值传递

```cpp
void increase(int number)
{
    ++number;
}
```

形参是实参的副本，修改形参不会修改调用者的变量。对于 `int`、`double`、`char`、`bool`、枚举等小型类型，值传递通常简单而高效。

### 3.2 引用传递

```cpp
void increase(int& number)
{
    ++number;
}
```

`int&` 是左值引用，它是已有对象的别名。调用后原变量会改变：

```cpp
int score{10};
increase(score); // score 变为 11
```

非 `const` 引用意味着函数可能修改调用者对象，应谨慎使用并让函数名体现这种行为。

### 3.3 `const` 引用

大型对象只读传入时，常使用 `const T&`：

```cpp
#include <string>

void print_name(const std::string& name)
{
    std::cout << name << '\n';
}
```

它避免复制，同时禁止函数修改该字符串。但对于 `int` 等小型类型，直接值传递通常更合适。

### 3.4 指针参数

```cpp
void reset(int* value)
{
    if (value != nullptr) {
        *value = 0;
    }
}

int score{90};
reset(&score);
```

指针参数可以为空，因此常用于表达“对象可选”。若参数必须存在且不转移所有权，通常优先使用引用。

```text
小型只读值           → T
大型只读对象         → const T&
必须存在且需要修改   → T&
可以不存在           → T*（用 nullptr 表示不存在）
```

这只是入门规则；性能敏感或涉及所有权时还需要结合具体类型分析。

### 3.5 多个输出值

不建议滥用多个输出引用：

```cpp
void calculate(int value, int& doubled, int& tripled);
```

可以返回一个结构体，让结果更有名称：

```cpp
struct CalculationResult
{
    int doubled{};
    int tripled{};
};

CalculationResult calculate(int value)
{
    return {value * 2, value * 3};
}
```

现代 C++ 返回对象通常很高效，编译器可以使用复制消除或移动语义。

### 3.6 默认参数

```cpp
void print_line(char character = '-', int width = 40);
```

调用方式：

```cpp
print_line();
print_line('=');
print_line('*', 20);
```

默认参数应从右向左连续出现，并且通常只在声明处写一次。默认参数适合稳定、直观的默认行为，不适合隐藏重要业务选择。

### 3.7 函数重载

```cpp
int absolute(int value);
double absolute(double value);
```

同名函数可以拥有不同的参数列表。不能只依靠返回类型区分重载：

```cpp
int parse();
double parse(); // 错误：调用处无法仅凭返回类型选择
```

谨防隐式转换造成重载歧义。

### 本节练习

1. 分别使用值、引用和指针编写三个修改整数的函数，观察差异。
2. 编写 `swap_values(int&, int&)`，交换两个整数。
3. 设计结构体，一次返回字符串中的字母数、数字数和空白数。
4. 为面积计算函数设计圆形和矩形重载。
5. 判断五个函数参数应该采用值、`const&`、`&` 还是指针，并说明理由。

---

## 四、现代函数特性

### 4.1 `auto` 与尾置返回类型

```cpp
auto multiply(double left, double right) -> double
{
    return left * right;
}
```

普通函数一般直接写返回类型更清晰。尾置返回类型在模板或返回类型依赖参数表达式时更有价值。

### 4.2 `constexpr` 函数

```cpp
constexpr int square(int number)
{
    return number * number;
}

static_assert(square(5) == 25);
```

`constexpr` 函数既可以在编译期执行，也可以在运行期执行。是否在编译期求值取决于调用上下文和实参。

### 4.3 `consteval`

```cpp
consteval int compile_time_square(int number)
{
    return number * number;
}
```

`consteval` 函数必须在编译期求值。它适用于必须由编译器验证或生成的结果，不应为了“看起来现代”而滥用。

### 4.4 `noexcept`

```cpp
int add(int left, int right) noexcept
{
    return left + right;
}
```

`noexcept` 表示函数承诺不让异常逃出。若违反承诺，程序会终止。本阶段先理解其语义，不要给尚未确认不会抛异常的函数随意添加它。

### 4.5 Lambda 表达式入门

Lambda 是可在使用位置定义的函数对象：

```cpp
auto is_even = [](int number) {
    return number % 2 == 0;
};

std::cout << std::boolalpha << is_even(8) << '\n';
```

捕获外部变量：

```cpp
int threshold{60};
auto passed = [threshold](int score) {
    return score >= threshold;
};
```

常见捕获形式：

- `[]`：不捕获。
- `[value]`：按值捕获指定变量。
- `[&value]`：按引用捕获指定变量。
- `[=]`：默认按值捕获使用到的变量。
- `[&]`：默认按引用捕获使用到的变量。

引用捕获时必须确保被捕获对象仍然存活。初学阶段优先显式捕获，避免无意依赖太多外部状态。

### 4.6 递归

```cpp
unsigned long long factorial(unsigned int number)
{
    if (number <= 1) {
        return 1;
    }
    return number * factorial(number - 1);
}
```

递归必须具有：

- 终止条件。
- 每次调用都更接近终止条件。

递归会使用调用栈，过深可能导致栈溢出。能简单使用循环完成的问题，不必强行递归。

### 本节练习

1. 编写可在 `static_assert` 中验证的 `constexpr` 闰年函数。
2. 用 Lambda 判断成绩是否及格。
3. 分别用循环和递归实现整数各位数字之和。
4. 研究错误的无限递归在调试器调用栈中的表现。

---

## 五、数组与连续数据

### 5.1 原生数组

```cpp
int scores[5]{90, 85, 76, 92, 88};
```

下标从 0 开始，有效范围是 `0～4`：

```cpp
std::cout << scores[0] << '\n';
scores[4] = 100;
```

越界访问属于未定义行为，C++ 不会自动为原生数组检查边界。

### 5.2 `std::array`

固定长度数据优先考虑 `std::array`：

```cpp
#include <array>
#include <iostream>

int main()
{
    std::array scores{90, 85, 76, 92, 88};

    std::cout << scores.size() << '\n';
    std::cout << scores.front() << '\n';
    std::cout << scores.back() << '\n';

    for (int score : scores) {
        std::cout << score << ' ';
    }
}
```

- `operator[]` 不检查边界。
- `.at(index)` 会检查边界，越界时抛出异常。
- `.size()` 返回元素数量。
- `.fill(value)` 填充全部元素。

### 5.3 `std::vector`

运行时才知道元素数量时使用 `std::vector`：

```cpp
#include <vector>

std::vector<int> scores;
scores.push_back(90);
scores.push_back(85);
```

```cpp
std::vector<int> values(count); // 创建 count 个元素
```

不要写成：

```cpp
std::vector<int> values{count}; // 创建一个元素，其值为 count
```

本阶段重点掌握：

- `size`、`empty`
- `push_back`、`pop_back`
- `front`、`back`
- `[]`、`at`
- 范围 `for`

容量、迭代器失效和复杂度将在 STL 阶段深入学习。

### 5.4 将数组传给函数

原生数组作为函数参数时通常会退化为指针，长度信息丢失：

```cpp
void print_values(const int values[], std::size_t size);
```

更现代的选择是 `std::span`：

```cpp
#include <span>

void print_values(std::span<const int> values)
{
    for (int value : values) {
        std::cout << value << ' ';
    }
}
```

它可以观察多种连续存储：

```cpp
int raw[]{1, 2, 3};
std::array fixed{4, 5, 6};
std::vector dynamic{7, 8, 9};

print_values(raw);
print_values(fixed);
print_values(dynamic);
```

`std::span` 不拥有数据，只保存对外部连续区域的观察。原数据销毁后，span 也会悬空。

### 5.5 多维数组

```cpp
std::array<std::array<int, 3>, 2> matrix{{
    {1, 2, 3},
    {4, 5, 6}
}};

for (const auto& row : matrix) {
    for (int value : row) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
}
```

使用 `const auto&` 可避免复制每一行。

### 5.6 常用算法初步

```cpp
#include <algorithm>
#include <numeric>

std::array values{5, 2, 8, 1};
std::sort(values.begin(), values.end());

const int sum = std::accumulate(values.begin(), values.end(), 0);
const auto position = std::find(values.begin(), values.end(), 8);
```

优先使用标准算法表达意图，避免每次都手写循环。迭代器和 Ranges 会在后续阶段系统讲解。

### 本节练习

1. 使用 `std::array` 统计五门成绩的总分、最高分和最低分。
2. 使用 `std::vector` 接收数量不固定的整数，计算平均值。
3. 编写接受 `std::span<const int>` 的 `contains` 函数。
4. 实现 3×3 矩阵转置。
5. 分别用循环和标准算法查找最大值。

---

## 六、字符串与文本处理

### 6.1 `std::string`

```cpp
#include <string>

std::string first{"Hello"};
std::string second{"C++"};
std::string message = first + ", " + second;
```

常用操作：

```cpp
message.size();
message.empty();
message.front();
message.back();
message.append("!");
message += "!";
message.find("C++");
message.substr(0, 5);
message.erase(0, 2);
message.insert(0, "Say: ");
```

`find` 找不到时返回 `std::string::npos`：

```cpp
if (message.find("C++") != std::string::npos) {
    std::cout << "找到了\n";
}
```

### 6.2 遍历和修改字符

```cpp
#include <cctype>

for (char& character : text) {
    const auto value = static_cast<unsigned char>(character);
    character = static_cast<char>(std::toupper(value));
}
```

对可能为负的 `char` 直接调用 `<cctype>` 函数可能产生未定义行为，因此先转换为 `unsigned char`。

注意：这种逐字节处理不等于正确处理中文或完整 Unicode 文本。UTF-8 中一个汉字通常由多个字节组成。

### 6.3 `std::string_view`

只读查看字符串内容且无需拥有它时，可以使用 `std::string_view`：

```cpp
#include <string_view>

void print_message(std::string_view message)
{
    std::cout << message << '\n';
}
```

它可以接受字符串字面量和 `std::string`，通常无需复制：

```cpp
std::string name{"Alice"};
print_message(name);
print_message("Hello");
```

`string_view` 不拥有字符数据，必须关注生命周期：

```cpp
std::string_view wrong()
{
    std::string local{"temporary"};
    return local; // 错误：返回后 view 悬空
}
```

此外，`string_view` 的字符序列不保证以空字符结尾，不应随意把 `.data()` 传给要求 C 字符串的接口。

### 6.4 C 风格字符串

```cpp
const char text[]{"hello"};
```

末尾包含空字符 `\0`。传统接口常接收 `const char*`。现代业务代码优先使用 `std::string` 和 `std::string_view`，但需要理解 C 字符串以便对接系统 API 和旧代码。

常见风险：

- 缓冲区越界。
- 忘记结尾空字符。
- 手动长度计算错误。
- 指针为空或悬空。

### 6.5 数字与字符串转换

```cpp
std::string text = std::to_string(42);
int value = std::stoi("42");
double price = std::stod("19.95");
```

`stoi` 等函数可能抛异常。高性能和精细错误控制场景可使用 `<charconv>` 中的 `std::from_chars` 与 `std::to_chars`，本阶段先了解其用途。

### 6.6 文本拆分示例

```cpp
#include <string_view>
#include <vector>

std::vector<std::string_view> split(std::string_view text, char delimiter)
{
    std::vector<std::string_view> parts;

    while (!text.empty()) {
        const auto position = text.find(delimiter);
        parts.push_back(text.substr(0, position));

        if (position == std::string_view::npos) {
            break;
        }
        text.remove_prefix(position + 1);
    }

    return parts;
}
```

返回的视图依赖原始文本，调用者必须保证原始字符串活得足够久。

### 本节练习

1. 统计字符串中字母、数字、空白和其他字符的数量。
2. 判断英文字符串是否为回文，忽略大小写。
3. 实现单词查找与替换。
4. 使用 `string_view` 实现不复制的前缀和后缀判断。
5. 拆分逗号分隔的一行文本，并讨论结果的生命周期。

---

## 七、引用与指针基础

### 7.1 对象、地址和指针

```cpp
int value{42};
int* pointer{&value};

std::cout << value << '\n';
std::cout << &value << '\n';
std::cout << pointer << '\n';
std::cout << *pointer << '\n';
```

- `&value` 取得对象地址。
- `int*` 表示指向 `int` 的指针类型。
- `*pointer` 解引用指针，访问所指对象。

### 7.2 空指针

```cpp
int* pointer{nullptr};

if (pointer != nullptr) {
    std::cout << *pointer << '\n';
}
```

使用 `nullptr`，不要使用 `0` 或 `NULL` 表示现代 C++ 空指针。空指针不能解引用。

### 7.3 引用

```cpp
int value{42};
int& reference{value};
reference = 100; // value 也变为 100
```

引用必须在定义时绑定，通常不能重新绑定，也不应为空。可以把引用理解为对象的别名，但具体语言规则仍需按引用类型理解。

### 7.4 `const` 与指针

```cpp
const int* pointer_to_const{}; // 不能通过指针修改所指 int
int* const const_pointer{&value}; // 指针自身不能改指向
const int* const both_const{&value}; // 两者都受限
```

从变量名向外阅读有助于理解：

- `const int* p`：p 指向 const int。
- `int* const p`：p 是 const 指针，指向 int。

### 7.5 指针与数组

在许多表达式中，原生数组会转换为首元素指针：

```cpp
int values[]{10, 20, 30};
int* first{values};

std::cout << *first << '\n';
std::cout << *(first + 1) << '\n';
```

指针运算只应在同一数组对象及其尾后位置范围内进行。不能通过首元素指针自动得知数组长度，因此优先使用容器、`std::span` 和范围循环。

### 7.6 常见危险

```cpp
int* dangling{};
{
    int local{42};
    dangling = &local;
}
// *dangling; // 错误：local 生命周期已结束
```

需要避免：

- 未初始化指针。
- 空指针解引用。
- 悬空指针和悬空引用。
- 数组越界。
- 返回局部变量的地址或引用。
- 保存指向临时对象的数据视图。

建议使用 AddressSanitizer 辅助发现内存错误：

```powershell
g++ -std=c++23 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined main.cpp -o app.exe
```

MSVC 可使用 `/fsanitize=address`。不同工具链的支持情况可能有所区别。

### 本节练习

1. 通过指针和引用分别修改一个整数。
2. 画图解释值、地址、指针变量和解引用结果。
3. 编写安全接受可空指针的打印函数。
4. 找出五段包含空指针、悬空或越界问题的代码。
5. 开启 AddressSanitizer，观察数组越界报告。

---

## 八、多文件项目与头文件

### 8.1 基本拆分

`math_utils.hpp`：

```cpp
#ifndef MATH_UTILS_HPP
#define MATH_UTILS_HPP

int add(int left, int right);
int square(int value);

#endif
```

`math_utils.cpp`：

```cpp
#include "math_utils.hpp"

int add(int left, int right)
{
    return left + right;
}

int square(int value)
{
    return value * value;
}
```

`main.cpp`：

```cpp
#include "math_utils.hpp"

#include <iostream>

int main()
{
    std::cout << add(2, 3) << '\n';
}
```

编译：

```powershell
g++ -std=c++23 -Wall -Wextra -Wpedantic -g main.cpp math_utils.cpp -o app.exe
```

### 8.2 头文件原则

- 头文件通常放声明，源文件放定义。
- 头文件应具备 include guard，或在常见工具链中使用 `#pragma once`。
- 头文件应尽量自包含：单独包含它时也能编译。
- 使用某个类型或函数的文件应直接包含其声明来源。
- 不要在头文件全局写 `using namespace std;`。
- 非 `inline` 普通函数不要在多个翻译单元都包含其定义，否则可能违反 ODR。

### 8.3 命名空间

```cpp
namespace deepblog::text
{
    bool is_palindrome(std::string_view text);
}
```

命名空间用于组织名称、避免冲突，不等同于目录。库代码应使用有辨识度的命名空间。

### 8.4 基础 CMake

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.25)
project(contact_book LANGUAGES CXX)

add_executable(contact_book
    src/main.cpp
    src/contact.cpp
)

target_compile_features(contact_book PRIVATE cxx_std_23)
target_include_directories(contact_book PRIVATE include)

if(MSVC)
    target_compile_options(contact_book PRIVATE /W4 /permissive-)
else()
    target_compile_options(contact_book PRIVATE -Wall -Wextra -Wpedantic)
endif()
```

构建：

```powershell
cmake -S . -B build
cmake --build build
```

不要手工向项目全局添加大量编译参数，优先使用 target 级 CMake 命令。

### 8.5 推荐目录结构

```text
contact-book/
├─ CMakeLists.txt
├─ include/
│  └─ contact.hpp
├─ src/
│  ├─ contact.cpp
│  └─ main.cpp
└─ tests/
```

### 本节练习

1. 把字符串统计程序拆分成 `.hpp`、`.cpp` 和 `main.cpp`。
2. 故意只声明不定义一个函数，观察链接错误。
3. 为自己的函数建立命名空间。
4. 使用 CMake 构建至少包含两个源文件的程序。

---

## 九、调试与测试思维

### 9.1 调用栈

函数调用时会形成调用链。调试器的调用栈能够显示：

```text
main → run_menu → read_command → parse_command
```

出现错误时，先观察当前函数，再沿调用栈向上检查参数来自哪里。

### 9.2 前置条件、后置条件与不变量

```cpp
#include <cassert>

double average(double total, int count)
{
    assert(count > 0);
    return total / count;
}
```

- 前置条件：调用者在调用前必须满足的条件。
- 后置条件：函数成功结束时保证的结果。
- 不变量：在某个有效状态期间始终成立的条件。

`assert` 适合检查程序员错误，不适合替代用户输入验证，因为 Release 构建可能禁用断言。

### 9.3 为纯函数设计测试

```cpp
bool is_leap_year(int year);
```

至少测试：

- 普通闰年：2024。
- 普通平年：2023。
- 世纪平年：1900。
- 世纪闰年：2000。
- 边界或不支持的输入。

优先把计算逻辑写成不直接读写控制台的函数，这样更容易测试。

### 9.4 编译器和分析工具

建议组合：

- 严格编译警告。
- Debug 构建和断点调试。
- AddressSanitizer。
- UndefinedBehaviorSanitizer。
- `clang-tidy`（环境允许时）。
- 格式化工具 `clang-format`。

### 本节练习

1. 为回文判断函数列出等价类和边界测试。
2. 用断点观察递归阶乘的每一层调用。
3. 把项目中的输入输出与计算逻辑分离。
4. 使用 Sanitizer 检查一个故意越界的练习程序。

---

## 十、阶段项目

### 项目一：文本分析器

最低要求：

- 接收一行或多行英文文本。
- 统计字符数、单词数、数字数、空白数和标点数。
- 查找指定单词出现次数。
- 输出最长单词。
- 将统计逻辑拆分成独立函数。

推荐接口：

```cpp
struct TextStatistics
{
    std::size_t letters{};
    std::size_t digits{};
    std::size_t spaces{};
    std::size_t words{};
};

TextStatistics analyze_text(std::string_view text);
```

进阶要求：

- 忽略英文大小写。
- 支持按分隔符拆分。
- 从文件读取文本。
- 编写独立测试程序。

### 项目二：矩阵运算工具

最低要求：

- 使用固定大小二维 `std::array`。
- 支持矩阵输入和格式化输出。
- 支持加法、转置和标量乘法。
- 检查输入有效性。
- 每种运算使用独立函数。

进阶要求：

- 实现矩阵乘法。
- 使用模板支持不同固定尺寸；模板可在后续阶段再完善。
- 比较原生二维数组与 `std::array` 的接口差异。

### 项目三：通讯录

最低要求：

- 定义 `Contact` 结构体，包含姓名、电话和邮箱。
- 使用 `std::vector<Contact>` 存储联系人。
- 支持添加、列出、查找、修改和删除。
- 使用多个函数拆分菜单与业务逻辑。
- 使用至少三个源文件和 CMake 构建。

推荐接口：

```cpp
struct Contact
{
    std::string name;
    std::string phone;
    std::string email;
};

const Contact* find_contact(
    const std::vector<Contact>& contacts,
    std::string_view name);
```

这个返回指针表达“可能找不到”。调用者必须检查是否为 `nullptr`，且不得在 `vector` 发生可能重新分配的修改后继续持有旧指针。

进阶要求：

- 文件保存和加载。
- 按姓名排序。
- 重复联系人检查。
- 输入字段验证。
- 将查找结果改为更现代的索引或可选结果设计，并比较优缺点。

### 项目验收标准

- 使用 CMake 构建，至少包含两个 `.cpp` 文件。
- 严格警告下无警告。
- 函数职责清晰，没有超长 `main`。
- 只读大型参数避免不必要复制。
- 不返回局部对象的引用、指针或视图。
- 数组访问有明确边界。
- 用户输入失败时能够恢复或安全退出。
- 至少为三个核心函数设计正常、边界和异常测试。
- AddressSanitizer 与 UndefinedBehaviorSanitizer 检查通过。

---

## 十一、六周学习安排

### 第 1 周：函数基础

- 声明、定义、调用、返回值。
- 局部作用域和调用栈。
- 函数职责拆分。
- 重构第一阶段计算器。

### 第 2 周：参数设计

- 值传递、引用和 `const` 引用。
- 指针参数与可空语义。
- 结构体返回多个结果。
- 默认参数和函数重载。

### 第 3 周：数组与容器

- 原生数组和越界风险。
- `std::array`、`std::vector`。
- `std::span`。
- 二维数组和基础算法。

### 第 4 周：字符串

- `std::string` 常用操作。
- `std::string_view` 与生命周期。
- C 风格字符串基础。
- 完成文本分析器。

### 第 5 周：指针、引用和工程组织

- 地址、解引用、`nullptr`。
- `const` 与指针。
- 悬空与越界问题。
- 头文件、源文件、命名空间和 CMake。

### 第 6 周：综合项目

- 设计通讯录的数据结构和接口。
- 实现增删改查。
- 分离输入输出与业务逻辑。
- 开启警告、Sanitizer 并测试。
- 复盘和阶段自测。

---

## 十二、阶段自测

### 概念题

1. 函数声明和定义有什么区别？
2. 值传递为什么不会修改调用者变量？
3. 什么情况下使用 `const T&`？
4. 引用和指针在“是否可为空”方面有何差异？
5. 为什么不应返回局部变量的引用、指针或 `string_view`？
6. 默认参数和函数重载分别适合什么情况？
7. `constexpr` 函数是否只能在编译期调用？
8. 原生数组作为函数参数时为什么需要额外长度？
9. `std::array`、`std::vector` 和 `std::span` 是否拥有数据？
10. `std::string` 与 `std::string_view` 的核心区别是什么？
11. `string_view::data()` 为什么不一定能当作 C 字符串？
12. `const int*` 与 `int* const` 有何区别？
13. 什么是悬空指针和悬空引用？
14. 头文件为什么需要 include guard？
15. 编译错误和链接错误应如何区分？

### 编程题

1. 编写接受 `std::span<const double>` 的平均值函数。
2. 编写英文单词频率统计程序。
3. 实现字符串拆分，并说明返回视图的生命周期要求。
4. 实现二维矩阵的加法和转置。
5. 将一个单文件程序重构为头文件和两个源文件。
6. 编写能够处理“未找到”情况的联系人查找函数。

### 进入下一阶段前的检查清单

- [ ] 能独立设计和调用函数。
- [ ] 能解释值、引用和指针传递的区别。
- [ ] 能避免返回局部对象的引用、指针或视图。
- [ ] 熟悉 `std::array`、`std::vector` 和 `std::span` 的基本用途。
- [ ] 熟悉 `std::string` 和 `std::string_view`。
- [ ] 能识别空指针、悬空和数组越界风险。
- [ ] 能使用头文件、源文件和命名空间组织代码。
- [ ] 能使用 CMake 构建多文件程序。
- [ ] 会查看调试器调用栈。
- [ ] 完成至少两个阶段项目。
- [ ] 项目通过严格警告和 Sanitizer 检查。

---

## 十三、易错点汇总

1. 把大型对象全部按值传入，造成无意义复制。
2. 为了“性能”把所有参数都改成引用，使接口语义混乱。
3. 使用非 `const` 引用输出多个结果，而不是返回具名结构体。
4. 返回局部字符串的 `string_view`。
5. 假设 `string_view.data()` 总以 `\0` 结束。
6. 使用数组下标却没有验证范围。
7. 把 `std::vector<int> values{count}` 误认为创建 `count` 个元素。
8. 解引用 `nullptr` 或未初始化指针。
9. 混淆 `const int*` 和 `int* const`。
10. 在头文件中写 `using namespace std;`。
11. 在多个源文件包含的头文件中定义普通非 `inline` 函数。
12. 把所有逻辑都保留在 `main` 中。
13. 用 `assert` 检查用户输入。
14. 递归缺少终止条件。
15. 只测试正常输入，不测试空字符串、零长度和边界下标。

---

## 十四、下一阶段

下一阶段建议进入面向对象与资源管理基础：

- 类与对象。
- 构造函数、析构函数和初始化列表。
- 封装与类不变量。
- 复制、移动与对象生命周期。
- RAII。
- 运算符重载。
- 继承、虚函数与运行时多态。
- Rule of Zero、Three、Five。

完成本阶段的真正标志，是能够把一个需求拆成数据结构和函数接口，解释每个参数的传递方式，并让多文件程序在严格警告和 Sanitizer 下稳定运行。
