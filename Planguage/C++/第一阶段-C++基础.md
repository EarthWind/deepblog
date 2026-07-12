# 第一阶段：C++ 编程基础

> 适合人群：零基础或刚接触 C++ 的学习者  
> 建议标准：C++23（本阶段代码也基本兼容 C++17/20）  
> 建议周期：4～6 周，每天 1～2 小时  
> 阶段目标：能够独立编写、编译、调试一个结构清晰的命令行程序。

---

## 一、学习成果

完成本阶段后，你应当能够：

1. 解释源代码从编译到运行的大致过程。
2. 使用编译器或 IDE 创建并运行 C++ 程序。
3. 正确使用变量、基本类型、运算符和控制语句。
4. 使用标准输入输出完成简单的人机交互。
5. 将一个小问题拆解为顺序、分支和循环结构。
6. 阅读常见编译错误，并通过调试器定位简单逻辑错误。
7. 独立完成计算器、猜数字和成绩统计等小项目。

本阶段暂不深入类、模板、指针和动态内存。这些内容会在后续阶段系统学习。

---

## 二、开发环境

### 2.1 C++ 程序如何变成可执行文件

一个典型 C++ 项目会经历：

```text
源代码（.cpp） → 预处理 → 编译 → 汇编 → 目标文件 → 链接 → 可执行文件
```

- **预处理**：处理 `#include`、`#define` 等指令。
- **编译**：检查语法和类型，将代码转换为较低层表示。
- **汇编**：生成目标文件。
- **链接**：把目标文件和需要的库组合成可执行程序。

初学时最常遇到两类错误：

- 编译错误：语法错误、类型不匹配、变量未声明等。
- 链接错误：函数只有声明而没有定义、缺少需要链接的库等。

### 2.2 编译器选择

Windows 推荐以下任一种方案：

- Visual Studio 2022，并安装“使用 C++ 的桌面开发”。
- MSYS2/MinGW-w64 提供的 GCC。
- LLVM/Clang。

不要把 VS Code 当作编译器。VS Code 是编辑器，仍需另外安装和配置编译器。

检查 GCC：

```powershell
g++ --version
```

检查 Clang：

```powershell
clang++ --version
```

检查 Visual C++ 编译器（应在 Developer PowerShell 中执行）：

```powershell
cl
```

### 2.3 第一个程序

创建 `hello.cpp`：

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, C++!\n";
    return 0;
}
```

使用 GCC 或 Clang 编译：

```powershell
g++ -std=c++23 -Wall -Wextra -Wpedantic -g hello.cpp -o hello.exe
./hello.exe
```

参数含义：

- `-std=c++23`：使用 C++23 标准。
- `-Wall -Wextra -Wpedantic`：开启常用警告。
- `-g`：加入调试信息。
- `-o hello.exe`：指定输出文件名。

使用 MSVC 编译：

```powershell
cl /std:c++latest /W4 /EHsc /Zi hello.cpp
./hello.exe
```

### 2.4 读懂第一个程序

- `#include <iostream>` 引入标准输入输出设施。
- `int main()` 是程序入口。
- `{}` 包围函数体。
- `std::cout` 表示标准输出流。
- `<<` 把右侧内容写入输出流。
- `\n` 表示换行。
- `return 0` 表示程序正常结束；在 `main` 末尾可省略。
- C++ 语句通常以分号结尾。

### 本节练习

1. 输出你的姓名、城市和学习目标，每项占一行。
2. 故意删除一个分号，观察编译器错误。
3. 把 `std::cout` 错写成 `std::cot`，尝试读懂错误信息。
4. 分别用 Debug 和 Release 配置编译程序。

---

## 三、变量、常量与基本类型

### 3.1 变量

变量是带有类型和名称的存储位置：

```cpp
int age{18};
double height{1.75};
char grade{'A'};
bool is_student{true};
```

推荐优先使用花括号初始化，因为它能够阻止部分危险的窄化转换：

```cpp
int value{3.14}; // 编译错误，避免静默丢失小数部分
```

不要读取未初始化的局部变量：

```cpp
int number;              // 值不确定
std::cout << number;     // 错误做法，可能产生未定义行为
```

### 3.2 常用基本类型

| 类型 | 常见用途 | 示例 |
|---|---|---|
| `bool` | 真或假 | `true` |
| `char` | 单个窄字符 | `'A'` |
| `short` | 较小整数 | `short n{10};` |
| `int` | 普通整数 | `int count{42};` |
| `long long` | 较大整数 | `long long population{...};` |
| `float` | 单精度浮点数 | `1.5F` |
| `double` | 常用浮点数 | `3.14159` |

类型的具体大小可能因平台而异。需要确定宽度的整数时，可以使用 `<cstdint>`：

```cpp
#include <cstdint>

std::int32_t score{100};
std::uint64_t file_size{4096};
```

不要混淆：

```cpp
'A'       // 字符
"A"       // 字符串字面量
42        // 整数字面量
42.0      // double 字面量
42.0F     // float 字面量
```

### 3.3 `sizeof` 与数值范围

```cpp
#include <iostream>
#include <limits>

int main()
{
    std::cout << "int bytes: " << sizeof(int) << '\n';
    std::cout << "int min: " << std::numeric_limits<int>::min() << '\n';
    std::cout << "int max: " << std::numeric_limits<int>::max() << '\n';
}
```

有符号整数溢出属于未定义行为。不要假设超过最大值后一定绕回最小值。

### 3.4 常量

确定不会改变的值应声明为 `const`：

```cpp
const double pi{3.141592653589793};
const int days_per_week{7};
```

可以在编译期求值的常量可使用 `constexpr`：

```cpp
constexpr int seconds_per_minute{60};
constexpr int seconds_per_hour{60 * seconds_per_minute};
```

初学阶段可这样区分：

- 运行期间不允许修改：`const`。
- 希望并能够在编译期计算：`constexpr`。

### 3.5 `auto` 类型推导

```cpp
auto count = 10;       // int
auto price = 19.9;     // double
auto letter = 'C';     // char
```

`auto` 不是动态类型，类型仍在编译期确定。它适合类型明显或类型名称很长的场景，不应让代码变得难懂：

```cpp
auto result = 10; // 如果 result 的业务含义和单位不明确，显式类型也解决不了命名问题
```

优先改善名称：

```cpp
int remaining_attempts{10};
```

### 3.6 作用域和生命周期

```cpp
int main()
{
    int outer{1};

    {
        int inner{2};
        std::cout << outer + inner << '\n';
    } // inner 的生命周期在这里结束

    // std::cout << inner; // 错误：inner 已超出作用域
}
```

变量应尽量靠近首次使用的位置声明，并保持作用域尽可能小。

### 本节练习

1. 输出常用基本类型在你的平台上占用的字节数。
2. 定义半径并计算圆的周长和面积。
3. 将秒数换算成小时、分钟和秒。
4. 观察 `int x{3.5};` 和 `int x = 3.5;` 的区别。
5. 使用 `<limits>` 输出 `double` 的最大值和最低值。

---

## 四、表达式与运算符

### 4.1 算术运算

```cpp
int a{7};
int b{3};

std::cout << a + b << '\n'; // 10
std::cout << a - b << '\n'; // 4
std::cout << a * b << '\n'; // 21
std::cout << a / b << '\n'; // 2，整数除法
std::cout << a % b << '\n'; // 1，余数
```

若需要小数结果，至少一个操作数必须是浮点类型：

```cpp
double result = static_cast<double>(a) / b;
```

除数为零是严重错误；整数除以零属于未定义行为。

### 4.2 比较和逻辑运算

```cpp
age >= 18
score == 100
password != expected_password

age >= 18 && has_id      // 并且
is_admin || is_owner     // 或者
!is_disabled             // 取反
```

`=` 是赋值，`==` 才是相等比较。

逻辑与和逻辑或具有短路特性：

```cpp
if (divisor != 0 && value / divisor > 10) {
    // divisor 为 0 时，右侧表达式不会执行
}
```

### 4.3 自增、自减和复合赋值

```cpp
count += 5;
count -= 2;
count *= 3;
count /= 2;

++count;
--count;
```

不要在同一个复杂表达式里多次修改同一变量。清晰性比少写一行更重要。

### 4.4 类型转换

```cpp
double average = static_cast<double>(total) / count;
```

现代 C++ 优先使用有明确含义的转换，如 `static_cast`，少用 C 风格转换：

```cpp
int value = (int)price; // 不推荐
```

转换可能导致：

- 小数部分丢失。
- 大整数转换为小类型后无法表示。
- 负数转换为无符号类型后产生意外结果。

### 4.5 运算符优先级

不确定优先级时使用括号：

```cpp
double average = static_cast<double>(math + english + physics) / 3;
```

括号不仅控制计算顺序，也能表达意图。

### 本节练习

1. 输入两个整数，输出和、差、积、商和余数。
2. 输入摄氏温度，转换为华氏温度。
3. 判断一个整数是否为偶数。
4. 输入总价和折扣率，计算最终价格。
5. 分析整数除法导致平均值错误的原因并修正。

---

## 五、标准输入输出

### 5.1 基本输入

```cpp
#include <iostream>

int main()
{
    int age{};
    std::cout << "请输入年龄：";
    std::cin >> age;
    std::cout << "明年你将满 " << age + 1 << " 岁。\n";
}
```

`std::cin >>` 会按照目标变量类型解析输入，并跳过前导空白。

### 5.2 检查输入是否成功

用户可能输入错误内容：

```cpp
int age{};
std::cout << "请输入年龄：";

if (!(std::cin >> age)) {
    std::cerr << "输入无效：年龄必须是整数。\n";
    return 1;
}
```

`std::cerr` 用于错误和诊断信息。返回非零值通常表示程序异常结束。

### 5.3 字符串和整行输入

```cpp
#include <iostream>
#include <string>

int main()
{
    std::string name;
    std::cout << "请输入姓名：";
    std::getline(std::cin, name);
    std::cout << "你好，" << name << "！\n";
}
```

混用 `>>` 和 `getline` 时，输入缓冲区中可能残留换行符：

```cpp
#include <limits>

int age{};
std::cin >> age;
std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

std::string name;
std::getline(std::cin, name);
```

### 5.4 格式化输出

```cpp
#include <iomanip>
#include <iostream>

int main()
{
    double price{12.3456};
    std::cout << std::fixed << std::setprecision(2) << price << '\n';
}
```

常用操纵器：

- `std::fixed`：固定小数形式。
- `std::setprecision(n)`：设置精度。
- `std::setw(n)`：设置下一项的字段宽度。
- `std::left`、`std::right`：左对齐或右对齐。
- `std::boolalpha`：把布尔值输出为 `true/false`。

### 5.5 `std::print`（C++23）

当标准库已实现 `<print>` 时，可以写：

```cpp
#include <print>

int main()
{
    std::string name{"Alice"};
    int score{95};
    std::println("{} 的成绩是 {}", name, score);
}
```

如果编译器或标准库尚不支持，继续使用 `std::cout` 即可。

### 本节练习

1. 输入姓名和年龄，输出一段自我介绍。
2. 输入商品名称、单价和数量，输出保留两位小数的总价。
3. 对非法数字输入显示错误信息并返回非零状态码。
4. 打印一个对齐的三行成绩表。

---

## 六、分支结构

### 6.1 `if` 语句

```cpp
if (score >= 90) {
    std::cout << "优秀\n";
} else if (score >= 60) {
    std::cout << "及格\n";
} else {
    std::cout << "不及格\n";
}
```

即使分支只有一条语句，也建议保留花括号，避免后续修改引入错误。

### 6.2 初始化语句

C++17 起，可以在 `if` 中初始化仅供判断使用的变量：

```cpp
if (int remainder = number % 2; remainder == 0) {
    std::cout << "偶数\n";
} else {
    std::cout << "奇数，余数为 " << remainder << '\n';
}
```

### 6.3 `switch`

```cpp
switch (choice) {
case 1:
    std::cout << "新建文件\n";
    break;
case 2:
    std::cout << "打开文件\n";
    break;
case 0:
    std::cout << "退出\n";
    break;
default:
    std::cout << "未知选项\n";
    break;
}
```

没有 `break` 时会继续执行下一个分支，称为贯穿。若确实需要贯穿，可使用 `[[fallthrough]]` 明确表达意图。

### 6.4 条件运算符

```cpp
const char* result = age >= 18 ? "成年" : "未成年";
```

条件运算符适合简单的二选一表达式，不要嵌套成难以阅读的复杂结构。

### 本节练习

1. 判断年份是否为闰年。
2. 将百分制成绩转换为等级。
3. 输入三个数，输出最大值。
4. 编写简单菜单，根据选项执行不同计算。
5. 判断三条边能否构成三角形，并判断其类型。

---

## 七、循环结构

### 7.1 `while`

```cpp
int count{1};
while (count <= 5) {
    std::cout << count << '\n';
    ++count;
}
```

适合循环次数事先不明确、由条件决定的场景。

### 7.2 `do-while`

```cpp
int choice{};
do {
    std::cout << "1. 继续  0. 退出\n";
    std::cin >> choice;
} while (choice != 0);
```

循环体至少执行一次。

### 7.3 `for`

```cpp
for (int i{1}; i <= 10; ++i) {
    std::cout << i << ' ';
}
```

适合计数式循环。尽量使用 `++i`，这是通用且易形成习惯的写法。

### 7.4 范围 `for`

```cpp
#include <array>

std::array scores{88, 92, 75, 100};
for (int score : scores) {
    std::cout << score << '\n';
}
```

本阶段只需会使用，数组和容器将在下一阶段深入学习。

### 7.5 `break` 与 `continue`

- `break`：立即结束当前循环。
- `continue`：跳过本轮剩余部分，进入下一轮。

```cpp
for (int i{1}; i <= 20; ++i) {
    if (i % 2 != 0) {
        continue;
    }
    if (i > 10) {
        break;
    }
    std::cout << i << ' ';
}
```

### 7.6 常见循环错误

- 忘记更新条件变量，形成死循环。
- 边界写成 `<` 还是 `<=`，导致少执行或多执行一次。
- 使用无符号整数倒序时发生下溢。
- 循环体内重复进行不必要的昂贵计算。
- 嵌套层次过深，逻辑难以理解。

### 本节练习

1. 计算 `1` 到 `100` 的和。
2. 输出九九乘法表。
3. 计算一个非负整数的阶乘，并考虑溢出范围。
4. 判断一个整数是否为质数。
5. 输出指定高度的字符三角形。
6. 反复读取输入，直到用户输入合法数字。

---

## 八、调试与代码习惯

### 8.1 警告不是可以忽略的装饰

建议始终开启较严格的警告，并努力做到零警告。警告经常能够提前发现：

- 未使用变量。
- 有符号和无符号比较。
- 隐式窄化转换。
- 分支遗漏。
- 可能未初始化的变量。

不要为了让警告消失而盲目添加类型转换；先理解问题根源。

### 8.2 使用调试器

至少掌握：

- 设置断点。
- 单步进入和单步跳过。
- 查看局部变量。
- 查看调用栈。
- 添加监视表达式。
- 条件断点。

推荐练习：给“猜数字”程序设置断点，观察每轮中秘密数字、输入值和剩余次数的变化。

### 8.3 常见错误分类

1. **语法错误**：括号或分号缺失。
2. **类型错误**：把不兼容的值用于某个类型。
3. **链接错误**：缺少函数定义或库。
4. **运行时错误**：非法输入、除零等。
5. **逻辑错误**：程序能运行，但结果不符合要求。
6. **未定义行为**：标准没有规定结果，程序表现不可依赖。

### 8.4 基础编码规范

- 名称表达含义：`student_count` 优于 `sc`。
- 一个变量只承担一个明确职责。
- 常量替代魔法数字。
- 缩进保持一致。
- 控制结构始终使用花括号。
- 注释解释“为什么”，不要重复代码已经说明的“是什么”。
- 减少全局变量。
- 不使用 `using namespace std;` 污染全局命名空间。
- 先写正确、清晰的代码，再考虑优化。

### 8.5 一个较完整的示例

```cpp
#include <iomanip>
#include <iostream>

int main()
{
    constexpr int subject_count{3};

    double total{};
    for (int index{1}; index <= subject_count; ++index) {
        double score{};
        std::cout << "请输入第 " << index << " 门成绩（0～100）：";

        if (!(std::cin >> score) || score < 0 || score > 100) {
            std::cerr << "成绩输入无效。\n";
            return 1;
        }

        total += score;
    }

    const double average{total / subject_count};
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "平均分：" << average << '\n';

    if (average >= 90) {
        std::cout << "等级：优秀\n";
    } else if (average >= 60) {
        std::cout << "等级：及格\n";
    } else {
        std::cout << "等级：需要继续努力\n";
    }
}
```

尝试解释其中每一行的作用，然后增加最高分和最低分统计。

---

## 九、阶段项目

### 项目一：命令行计算器

最低要求：

- 输入两个数字和一个运算符。
- 支持 `+`、`-`、`*`、`/`。
- 使用 `switch` 选择运算。
- 检查除数是否为零。
- 检查输入是否有效。
- 支持重复计算，直到用户选择退出。

进阶要求：

- 增加取余、乘方和历史记录计数。
- 将菜单和结果排版整齐。

### 项目二：猜数字游戏

最低要求：

- 随机生成 `1～100` 的整数。
- 用户最多猜 7 次。
- 每次提示“太大”或“太小”。
- 猜中后输出使用次数。
- 次数耗尽后公布答案。

进阶要求：

- 提供难度选择。
- 记录最佳成绩。
- 输入无效时不消耗次数。

随机数建议使用 `<random>`，不要依赖 `rand()`：

```cpp
#include <random>

std::random_device device;
std::mt19937 generator{device()};
std::uniform_int_distribution distribution{1, 100};
const int secret{distribution(generator)};
```

### 项目三：成绩统计系统

最低要求：

- 输入学生人数及每名学生的成绩。
- 检查人数和成绩范围。
- 统计总分、平均分、最高分和最低分。
- 统计各成绩等级人数。
- 输出格式整齐的统计报告。

进阶要求：

- 增加菜单和重复查询。
- 使用简单文本文件保存最终报告；文件流可先查阅资料尝试，后续阶段会详细学习。

### 项目验收标准

- 在严格警告选项下成功编译。
- 正常输入能得到正确结果。
- 非法输入不会导致不可理解的崩溃或结果。
- 边界情况已测试，如 0、负数、最大允许值和除零。
- 名称清晰，缩进统一，没有大段重复代码。
- 能够向别人解释关键逻辑。

---

## 十、四周学习安排

### 第 1 周：环境与基本语法

- 第 1 天：安装编译器，运行 Hello World。
- 第 2 天：变量、初始化和基本类型。
- 第 3 天：常量、`auto`、作用域。
- 第 4 天：算术、比较和逻辑运算。
- 第 5 天：类型转换和整数除法。
- 第 6 天：完成温度换算和时间换算练习。
- 第 7 天：复习并整理错题。

### 第 2 周：输入输出与分支

- 第 1～2 天：`cin`、`cout`、`getline` 和输入检查。
- 第 3 天：格式化输出。
- 第 4～5 天：`if`、`switch` 和条件表达式。
- 第 6～7 天：完成命令行计算器。

### 第 3 周：循环与调试

- 第 1 天：`while` 和 `do-while`。
- 第 2 天：`for` 和范围 `for`。
- 第 3 天：`break`、`continue` 和循环边界。
- 第 4 天：嵌套循环。
- 第 5 天：断点、单步执行和变量监视。
- 第 6～7 天：完成猜数字游戏。

### 第 4 周：综合训练

- 第 1 天：设计成绩统计系统的输入输出。
- 第 2～3 天：实现统计逻辑。
- 第 4 天：处理非法输入和边界情况。
- 第 5 天：整理代码和消除警告。
- 第 6 天：编写测试用例并手动验证。
- 第 7 天：复盘并完成阶段自测。

如果每天学习时间较少，可以把四周计划拉长为六周，不必赶进度。

---

## 十一、阶段自测

如果以下问题大部分能独立回答并编码实现，就可以进入下一阶段。

### 概念题

1. 编译错误和链接错误有什么区别？
2. 为什么推荐花括号初始化？
3. `const` 和 `constexpr` 的基本区别是什么？
4. `auto` 是否意味着变量能在运行时改变类型？
5. `7 / 3` 和 `7.0 / 3` 的结果为什么不同？
6. `=` 与 `==` 有什么区别？
7. `&&` 和 `||` 的短路行为有什么用途？
8. `while`、`do-while` 和 `for` 分别适合什么场景？
9. 什么是作用域和生命周期？
10. 为什么不应读取未初始化的局部变量？
11. `std::cout` 和 `std::cerr` 的用途有什么区别？
12. 为什么应重视编译器警告？

### 编程题

1. 输入一个年份，判断是否为闰年。
2. 输入正整数，输出它的所有因数。
3. 输入若干成绩，输出平均分和等级。
4. 输出前 `n` 项斐波那契数，并考虑整数溢出。
5. 编写带输入验证的四则运算计算器。

### 进入下一阶段前的检查清单

- [ ] 能从终端编译和运行单个 `.cpp` 文件。
- [ ] 能看懂基础编译错误。
- [ ] 能正确选择常见基本类型。
- [ ] 会使用 `const`、`constexpr` 和 `auto`。
- [ ] 掌握输入输出及基本输入验证。
- [ ] 掌握分支和三种循环。
- [ ] 会设置断点并查看变量。
- [ ] 完成至少两个阶段项目。
- [ ] 代码在常用警告选项下无警告。
- [ ] 能主动测试边界条件。

---

## 十二、推荐资料与下一阶段

日常查询建议使用：

- [cppreference](https://en.cppreference.com/w/cpp)：语言和标准库参考手册。
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)：现代 C++ 设计与编码建议。
- 编译器错误信息与 IDE 调试器：它们也是学习工具。

不要试图一次记住参考手册。先掌握本阶段高频知识，通过练习形成直觉；遇到细节时再查询资料。

下一阶段建议学习：

- 函数声明、定义、参数和返回值。
- 引用与基础指针。
- 数组、`std::array`、`std::string` 和 `std::string_view`。
- 多文件项目及基础 CMake。
- 用函数拆分本阶段的三个项目。

完成本阶段的标志不是“看完文档”，而是能够在不复制完整答案的情况下完成一个带输入验证的命令行项目。
