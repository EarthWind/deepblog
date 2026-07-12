# 第一阶段：Rust 语法与程序结构入门

> 本阶段面向刚开始接触 Rust 的学习者。学完后，你应当能够读懂并编写由变量、基本数据类型、函数、条件判断、循环、数组、元组和基础模式匹配组成的小型程序。

## 一、阶段目标

完成本阶段后，你应当能够：

- 使用 Cargo 创建、检查、运行和构建项目
- 理解 Rust 中变量默认不可变的设计
- 使用整数、浮点数、布尔值、字符、元组和数组
- 区分语句与表达式
- 编写带参数和返回值的函数
- 熟练使用 `if`、`loop`、`while` 和 `for`
- 使用 `match`、`if let` 和 `while let` 处理简单模式
- 看懂常见编译错误，并根据编译器提示修改代码
- 独立完成若干命令行小程序

本阶段暂时不深入讨论所有权、借用和生命周期。这些内容会在第二阶段系统学习，但遇到相关现象时会给出必要说明。

---

## 二、创建第一个项目

### 2.1 使用 Cargo 创建项目

在终端中执行：

```bash
cargo new rust-stage-one
cd rust-stage-one
cargo run
```

默认目录结构如下：

```text
rust-stage-one/
├── Cargo.toml
└── src/
    └── main.rs
```

- `Cargo.toml`：项目配置和依赖清单
- `src/main.rs`：二进制程序的入口文件
- `target/`：编译产物目录，第一次构建后生成

默认的 `main.rs`：

```rust
fn main() {
    println!("Hello, world!");
}
```

`main` 是二进制程序入口。`println!` 名称末尾的 `!` 表示它是宏，而不是普通函数。

### 2.2 常用命令

```bash
cargo check          # 快速检查代码能否编译，不生成最终可执行文件
cargo run            # 编译并运行程序
cargo build          # 生成 debug 构建
cargo build --release # 生成优化后的 release 构建
cargo fmt            # 格式化代码
cargo clippy         # 执行常见问题和代码风格检查
```

开发过程中优先使用 `cargo check`，它通常比完整构建更快。

### 2.3 注释

```rust
// 单行注释

/*
   块注释，可以跨越多行。
*/

/// 文档注释，通常用于说明紧随其后的项目。
fn add(left: i32, right: i32) -> i32 {
    left + right
}
```

普通说明优先使用 `//`。公共函数、结构体等 API 的说明使用 `///`，后续可以通过 `cargo doc` 生成文档。

---

## 三、变量、可变性、常量与遮蔽

### 3.1 变量默认不可变

```rust
fn main() {
    let score = 90;
    println!("score = {score}");
}
```

下面的代码不能通过编译：

```rust,compile_fail
fn main() {
    let score = 90;
    score = 95;
}
```

Rust 变量默认不可变。这能减少状态被意外修改的可能，使代码更容易理解。

### 3.2 使用 `mut` 声明可变变量

```rust
fn main() {
    let mut score = 90;
    println!("before: {score}");

    score = 95;
    println!("after: {score}");
}
```

只在确实需要重新赋值时添加 `mut`。如果变量从创建到结束都不改变，就保持不可变。

### 3.3 常量

```rust
const MAX_USERS: u32 = 1_000;
const SECONDS_PER_HOUR: u32 = 60 * 60;

fn main() {
    println!("最多允许 {MAX_USERS} 个用户");
}
```

常量与不可变变量的主要区别：

- 常量使用 `const` 声明
- 常量必须写出类型
- 常量名称通常使用全大写蛇形命名法
- 常量只能由编译期可计算的表达式初始化
- 常量不能添加 `mut`

### 3.4 遮蔽（shadowing）

可以使用 `let` 再次声明同名变量：

```rust
fn main() {
    let spaces = "   ";
    let spaces = spaces.len();

    println!("空格数量：{spaces}");
}
```

这里第一个 `spaces` 是字符串切片，第二个 `spaces` 是整数。遮蔽会创建一个新变量，因此可以改变类型。

而 `mut` 表示对同一个变量重新赋值，不能随意改变类型：

```rust,compile_fail
fn main() {
    let mut spaces = "   ";
    spaces = spaces.len();
}
```

### 3.5 遮蔽的常见用途

```rust
fn main() {
    let input = " 42 ";
    let input = input.trim();
    let input: i32 = input.parse().expect("请输入有效整数");

    println!("解析结果：{input}");
}
```

这里变量名始终表达“当前处理后的输入”，但每一步都创建新值，避免维护多个临时名称。

### 小练习

1. 声明一个不可变变量 `price` 并输出。
2. 声明一个可变变量 `count`，将其增加 1 后输出。
3. 定义常量 `MINUTES_PER_DAY`。
4. 使用遮蔽把字符串 `"128"` 转换为整数，再乘以 2。

---

## 四、基本数据类型

Rust 是静态类型语言。编译器通常可以推断类型，但有歧义时需要显式标注。

```rust
let age: u8 = 18;
let distance = 12.5; // 默认推断为 f64
```

## 4.1 整数类型

| 长度 | 有符号 | 无符号 |
|---|---|---|
| 8 位 | `i8` | `u8` |
| 16 位 | `i16` | `u16` |
| 32 位 | `i32` | `u32` |
| 64 位 | `i64` | `u64` |
| 128 位 | `i128` | `u128` |
| 平台相关 | `isize` | `usize` |

- `i32` 是普通整数字面量的默认类型
- `usize` 常用于集合索引和长度
- 无符号整数不能表示负数

### 整数字面量

```rust
fn main() {
    let decimal = 98_222;
    let hexadecimal = 0xff;
    let octal = 0o77;
    let binary = 0b1111_0000;
    let byte = b'A';

    println!("{decimal}, {hexadecimal}, {octal}, {binary}, {byte}");
}
```

下划线只用于提高可读性，不影响数值。

### 整数溢出

整数类型能够表示的范围有限。例如 `u8` 的范围是 0～255。不要依赖溢出行为；需要明确控制时，可使用：

- `checked_add`：溢出时返回 `None`
- `wrapping_add`：按二进制补码回绕
- `saturating_add`：限制在最大值或最小值
- `overflowing_add`：同时返回结果与是否溢出

```rust
fn main() {
    let value: u8 = 250;

    println!("checked: {:?}", value.checked_add(10));
    println!("wrapping: {}", value.wrapping_add(10));
    println!("saturating: {}", value.saturating_add(10));
}
```

## 4.2 浮点类型

Rust 提供 `f32` 和 `f64`，默认是 `f64`。

```rust
fn main() {
    let width = 10.5;
    let height: f32 = 4.0;
    let area = width * f64::from(height);

    println!("area = {area}");
}
```

Rust 不会在不同数值类型之间进行随意的隐式转换，因此示例中显式把 `f32` 转换为 `f64`。

浮点数不适合直接表示需要完全精确的小数，例如货币。下面的判断可能与你的直觉不同：

```rust
fn main() {
    let result = 0.1_f64 + 0.2_f64;
    println!("{result:.17}");
    println!("是否严格等于 0.3：{}", result == 0.3);
}
```

## 4.3 数值运算

```rust
fn main() {
    let sum = 5 + 10;
    let difference = 95.5 - 4.3;
    let product = 4 * 30;
    let quotient = 56.7 / 32.2;
    let truncated = 5 / 3;
    let remainder = 43 % 5;

    println!("{sum}, {difference}, {product}, {quotient}");
    println!("整数除法：{truncated}，余数：{remainder}");
}
```

两个整数相除仍得到整数，小数部分会被截断。

## 4.4 布尔类型

```rust
fn main() {
    let is_rust_fun: bool = true;
    let is_finished = false;

    println!("{is_rust_fun}, {is_finished}");
}
```

布尔值只有 `true` 和 `false`，常用于条件判断。

## 4.5 字符类型

Rust 的 `char` 使用单引号，表示一个 Unicode 标量值，占 4 字节。

```rust
fn main() {
    let letter = 'R';
    let chinese = '锈';
    let emoji = '🦀';

    println!("{letter} {chinese} {emoji}");
}
```

字符与字符串不同：

```rust
let character = 'A'; // char
let text = "A";      // &str
```

## 4.6 类型转换

基础数值转换可以使用 `as`，但窄化转换可能丢失数据：

```rust
fn main() {
    let large: u16 = 300;
    let narrowed = large as u8;

    println!("{large} 转为 u8 后是 {narrowed}");
}
```

当转换必须保证安全时，优先使用 `try_from`：

```rust
use std::convert::TryFrom;

fn main() {
    let large: u16 = 300;
    let result = u8::try_from(large);

    println!("{result:?}");
}
```

### 小练习

1. 分别声明 `i32`、`u64`、`f32`、`bool` 和 `char` 类型的变量。
2. 计算半径为 5.0 的圆面积。
3. 使用 `checked_mul` 判断两个较大整数相乘是否溢出。
4. 解释为什么数组索引通常使用 `usize`。

---

## 五、复合数据类型

## 5.1 元组

元组把多个不同类型的值组合成一个固定长度的整体。

```rust
fn main() {
    let user = ("Alice", 20, true);

    println!("name = {}", user.0);
    println!("age = {}", user.1);
    println!("active = {}", user.2);
}
```

可以使用模式解构：

```rust
fn main() {
    let point = (3.0, 4.0);
    let (x, y) = point;

    println!("x = {x}, y = {y}");
}
```

没有元素的元组 `()` 称为单元类型，其唯一值也是 `()`。没有显式返回值的函数默认返回它。

## 5.2 数组

数组中的元素必须具有相同类型，长度在编译期固定。

```rust
fn main() {
    let months = ["Jan", "Feb", "Mar", "Apr"];
    let scores: [i32; 5] = [90, 85, 78, 92, 88];
    let zeros = [0; 10];

    println!("第一个月：{}", months[0]);
    println!("第三个成绩：{}", scores[2]);
    println!("zeros 长度：{}", zeros.len());
}
```

`[0; 10]` 表示创建 10 个值都为 0 的元素。

### 安全访问数组

直接使用越界索引会导致程序 panic：

```rust
fn main() {
    let numbers = [10, 20, 30];
    let index = 8;

    match numbers.get(index) {
        Some(value) => println!("值为 {value}"),
        None => println!("索引 {index} 越界"),
    }
}
```

输入或计算产生的索引优先使用 `get`，因为它能显式处理越界情况。

## 5.3 切片初步

切片是对一段连续元素的借用视图，不拥有元素。数组切片类型写作 `&[T]`。

```rust
fn print_numbers(numbers: &[i32]) {
    for number in numbers {
        println!("{number}");
    }
}

fn main() {
    let values = [10, 20, 30, 40, 50];

    print_numbers(&values);
    print_numbers(&values[1..4]);
}
```

范围 `1..4` 包含索引 1、2、3，不包含索引 4。

常见范围形式：

```rust
let values = [10, 20, 30, 40, 50];

let all = &values[..];
let first_two = &values[..2];
let from_third = &values[2..];
let inclusive = &values[1..=3];

println!("{:?} {:?} {:?} {:?}", all, first_two, from_third, inclusive);
```

切片和借用的原理会在第二阶段详细学习。此处先记住：接收 `&[T]` 的函数通常比只接收固定长度数组的函数更通用。

### 小练习

1. 用元组表示姓名、年龄和身高，并分别读取各字段。
2. 创建一个包含一周温度的数组，计算平均温度。
3. 使用 `get` 安全读取用户指定位置的元素。
4. 编写函数，接收 `&[i32]` 并返回所有元素之和。

---

## 六、语句、表达式与代码块

理解表达式是掌握 Rust 函数、条件分支和代码块的关键。

### 6.1 语句

语句执行操作，但不产生可供使用的值。

```rust
fn main() {
    let x = 5;
    let mut total = 0;
    total += x;
    println!("{total}");
}
```

`let x = 5;` 是语句。Rust 不能像某些语言那样把赋值表达式继续赋给另一个变量。

### 6.2 表达式

表达式会计算出一个值。函数调用、宏调用、代码块、`if` 和 `match` 都可以是表达式。

```rust
fn main() {
    let y = {
        let x = 3;
        x + 1
    };

    println!("y = {y}");
}
```

`x + 1` 没有分号，因此它是代码块的最终表达式，整个代码块的值是 4。

如果添加分号：

```rust,compile_fail
fn main() {
    let y: i32 = {
        let x = 3;
        x + 1;
    };
}
```

代码块会返回 `()`，因而无法赋给 `i32`。

### 6.3 `if` 作为表达式

```rust
fn main() {
    let score = 82;
    let level = if score >= 60 { "通过" } else { "未通过" };

    println!("结果：{level}");
}
```

`if` 各分支必须产生兼容的类型：

```rust,compile_fail
fn main() {
    let condition = true;
    let value = if condition { 5 } else { "six" };
}
```

### 核心判断

- 末尾有分号：通常把表达式结果丢弃，整个语句返回 `()`
- 末尾无分号：可能作为代码块或函数的返回值
- `if` 和 `match` 可以直接产生值

---

## 七、函数

## 7.1 定义和调用函数

Rust 函数使用蛇形命名法：

```rust
fn greet() {
    println!("欢迎学习 Rust！");
}

fn main() {
    greet();
}
```

函数可以定义在调用位置之后，关键是处于调用者可见的作用域中。

## 7.2 函数参数

每个参数都必须写出类型：

```rust
fn print_sum(left: i32, right: i32) {
    println!("{} + {} = {}", left, right, left + right);
}

fn main() {
    print_sum(12, 8);
}
```

## 7.3 返回值

返回类型写在 `->` 后。通常使用函数体最后一个表达式作为返回值：

```rust
fn square(number: i32) -> i32 {
    number * number
}

fn main() {
    let result = square(6);
    println!("{result}");
}
```

也可以使用 `return` 提前返回：

```rust
fn absolute(number: i32) -> i32 {
    if number < 0 {
        return -number;
    }

    number
}
```

惯例是：普通末尾返回使用表达式，提前结束函数时使用 `return`。

## 7.4 一个函数只做一件事

下面把输入、计算和输出分开：

```rust
use std::io;

fn read_number() -> i32 {
    let mut input = String::new();
    io::stdin()
        .read_line(&mut input)
        .expect("读取输入失败");

    input.trim().parse().expect("请输入有效整数")
}

fn is_even(number: i32) -> bool {
    number % 2 == 0
}

fn main() {
    println!("请输入一个整数：");
    let number = read_number();

    if is_even(number) {
        println!("{number} 是偶数");
    } else {
        println!("{number} 是奇数");
    }
}
```

此处 `String` 和 `&mut` 涉及所有权与可变借用，第二阶段会详细解释。现阶段先掌握标准输入的固定写法。

### 小练习

1. 编写 `max(a, b)`，返回两个整数中的较大值。
2. 编写 `is_leap_year(year)`，判断某年是否为闰年。
3. 编写 `rectangle_area(width, height)`。
4. 把一段全部写在 `main` 中的代码拆成输入、计算、输出三个函数。

---

## 八、条件控制流

## 8.1 `if` 与 `else`

```rust
fn main() {
    let temperature = 28;

    if temperature >= 35 {
        println!("天气炎热");
    } else if temperature >= 20 {
        println!("天气舒适");
    } else {
        println!("天气较冷");
    }
}
```

条件必须是 `bool`，Rust 不会把整数自动当作真假值：

```rust,compile_fail
fn main() {
    let number = 3;
    if number {
        println!("number 非零");
    }
}
```

应当明确写出条件：

```rust
let number = 3;
if number != 0 {
    println!("number 非零");
}
```

## 8.2 逻辑运算符

```rust
fn main() {
    let age = 20;
    let has_ticket = true;

    if age >= 18 && has_ticket {
        println!("允许入场");
    }

    let is_weekend = false;
    let is_holiday = true;

    if is_weekend || is_holiday {
        println!("今天休息");
    }

    if !is_weekend {
        println!("今天不是周末");
    }
}
```

- `&&`：逻辑与
- `||`：逻辑或
- `!`：逻辑非

`&&` 和 `||` 具有短路行为：结果确定后，右侧表达式可能不会执行。

---

## 九、循环

Rust 提供 `loop`、`while` 和 `for` 三种循环。

## 9.1 `loop` 无限循环

```rust
fn main() {
    let mut count = 0;

    loop {
        count += 1;
        println!("count = {count}");

        if count == 3 {
            break;
        }
    }
}
```

`break` 结束循环，`continue` 跳过本轮剩余代码。

### 从循环返回值

```rust
fn main() {
    let mut counter = 0;

    let result = loop {
        counter += 1;

        if counter == 10 {
            break counter * 2;
        }
    };

    println!("result = {result}");
}
```

`break value` 使整个 `loop` 表达式产生一个值。

## 9.2 循环标签

嵌套循环中可以用标签指定要控制哪一层：

```rust
fn main() {
    let mut outer = 0;

    'outer_loop: loop {
        outer += 1;
        let mut inner = 0;

        loop {
            inner += 1;
            println!("outer={outer}, inner={inner}");

            if outer == 2 && inner == 2 {
                break 'outer_loop;
            }

            if inner == 3 {
                break;
            }
        }
    }
}
```

## 9.3 `while` 条件循环

```rust
fn main() {
    let mut number = 3;

    while number != 0 {
        println!("{number}");
        number -= 1;
    }

    println!("发射！");
}
```

适合“只要条件成立就继续”的场景。

## 9.4 `for` 遍历

```rust
fn main() {
    let values = [10, 20, 30, 40];

    for value in values {
        println!("value = {value}");
    }
}
```

遍历范围：

```rust
fn main() {
    for number in 1..5 {
        println!("{number}");
    }

    for number in (1..=3).rev() {
        println!("{number}");
    }
}
```

- `1..5`：1、2、3、4
- `1..=5`：1、2、3、4、5
- `rev()`：反向迭代

## 9.5 如何选择循环

| 场景 | 推荐写法 |
|---|---|
| 不确定执行次数，直到主动退出 | `loop` |
| 条件成立时持续执行 | `while` |
| 遍历集合或范围 | `for` |
| 需要让循环产生返回值 | `loop` + `break value` |

遍历集合时优先使用 `for`，它比手动管理索引更安全、意图更清楚。

### 小练习

1. 使用 `for` 输出 1～100 中所有能被 3 整除的数。
2. 使用 `while` 计算 1～100 的和。
3. 使用 `loop` 反复读取输入，用户输入 `quit` 时退出。
4. 用嵌套循环打印九九乘法表。

---

## 十、基础模式匹配

模式匹配是 Rust 中非常重要的语言能力。本阶段先掌握最常用的写法。

## 10.1 `match`

```rust
fn main() {
    let number = 3;

    match number {
        1 => println!("一"),
        2 => println!("二"),
        3 => println!("三"),
        _ => println!("其他数字"),
    }
}
```

`match` 必须覆盖所有可能情况。`_` 匹配剩余所有值。

多个模式可以使用 `|`：

```rust
fn main() {
    let day = 6;

    match day {
        1..=5 => println!("工作日"),
        6 | 7 => println!("周末"),
        _ => println!("无效日期"),
    }
}
```

`match` 也是表达式：

```rust
fn main() {
    let grade = 'B';

    let description = match grade {
        'A' => "优秀",
        'B' => "良好",
        'C' => "及格",
        'D' | 'F' => "需要提高",
        _ => "未知等级",
    };

    println!("{description}");
}
```

所有分支必须返回兼容类型。

## 10.2 匹配元组

```rust
fn main() {
    let point = (0, 5);

    match point {
        (0, 0) => println!("原点"),
        (0, y) => println!("位于 Y 轴，y={y}"),
        (x, 0) => println!("位于 X 轴，x={x}"),
        (x, y) => println!("普通点：({x}, {y})"),
    }
}
```

模式既能判断形状，也能把匹配到的部分绑定到变量。

## 10.3 匹配守卫

```rust
fn main() {
    let number = 8;

    match number {
        value if value < 0 => println!("负数"),
        value if value % 2 == 0 => println!("非负偶数"),
        _ => println!("非负奇数"),
    }
}
```

`if` 后的附加条件称为匹配守卫。

## 10.4 `Option<T>` 初识

`Option<T>` 表示“可能有值，也可能没有值”：

```rust
enum Option<T> {
    None,
    Some(T),
}
```

实际使用时无需自己定义，标准库已经提供：

```rust
fn main() {
    let numbers = [10, 20, 30];
    let value = numbers.get(1);

    match value {
        Some(number) => println!("找到：{number}"),
        None => println!("没有找到"),
    }
}
```

## 10.5 `if let`

只关心一个模式时，可以使用 `if let`：

```rust
fn main() {
    let numbers = [10, 20, 30];

    if let Some(value) = numbers.get(1) {
        println!("找到：{value}");
    }
}
```

带 `else` 的写法：

```rust
fn main() {
    let numbers = [10, 20, 30];

    if let Some(value) = numbers.get(10) {
        println!("找到：{value}");
    } else {
        println!("索引越界");
    }
}
```

如果需要处理多个分支，`match` 通常更清晰；只关心一个成功模式时，`if let` 更简洁。

## 10.6 `while let`

只要模式持续匹配，就继续循环：

```rust
fn main() {
    let mut values = vec![10, 20, 30];

    while let Some(value) = values.pop() {
        println!("取出：{value}");
    }
}
```

这里使用了动态数组 `Vec`，第四阶段会深入学习。现在只需知道 `pop()` 每次取出最后一个元素，数组为空时返回 `None`。

### 小练习

1. 使用 `match` 把 1～7 转换为星期名称。
2. 使用范围模式把分数划分为优秀、良好、及格和不及格。
3. 使用元组匹配判断一个点位于坐标轴、原点还是普通位置。
4. 分别用 `match` 和 `if let` 处理 `array.get(index)` 的结果。

---

## 十一、读取用户输入

命令行程序经常需要从标准输入读取文本。

```rust
use std::io;

fn main() {
    println!("请输入姓名：");

    let mut name = String::new();
    io::stdin()
        .read_line(&mut name)
        .expect("读取输入失败");

    let name = name.trim();
    println!("你好，{name}！");
}
```

关键步骤：

1. `String::new()` 创建可增长字符串。
2. `read_line` 把输入写入字符串。
3. `trim()` 去掉行尾换行符和两端空白。
4. `expect` 在读取失败时终止程序并显示说明。

### 解析数字

```rust
use std::io;

fn main() {
    println!("请输入年龄：");

    let mut input = String::new();
    io::stdin()
        .read_line(&mut input)
        .expect("读取输入失败");

    let age: u32 = input
        .trim()
        .parse()
        .expect("请输入有效的非负整数");

    println!("明年你将 {} 岁", age + 1);
}
```

`parse()` 可以解析成多种类型，因此通过 `let age: u32` 告诉编译器目标类型。

### 不因错误输入而退出

```rust
use std::io;

fn main() {
    loop {
        println!("请输入一个整数，或输入 q 退出：");

        let mut input = String::new();
        io::stdin()
            .read_line(&mut input)
            .expect("读取输入失败");

        let input = input.trim();

        if input.eq_ignore_ascii_case("q") {
            break;
        }

        let number: i32 = match input.parse() {
            Ok(value) => value,
            Err(_) => {
                println!("输入无效，请重试。");
                continue;
            }
        };

        println!("它的平方是 {}", number * number);
    }
}
```

`Result` 的系统用法会在第三和第六阶段学习。本阶段先认识 `Ok(value)` 表示成功，`Err(error)` 表示失败。

---

## 十二、格式化输出

## 12.1 占位符

```rust
fn main() {
    let name = "Ferris";
    let age = 8;

    println!("name = {}, age = {}", name, age);
    println!("name = {name}, age = {age}");
    println!("age = {0}, next year = {1}", age, age + 1);
}
```

## 12.2 调试输出

部分类型不能使用 `{}`，但可以使用 `{:?}`：

```rust
fn main() {
    let point = (3, 4);
    let values = [10, 20, 30];

    println!("point = {:?}", point);
    println!("values = {values:?}");
    println!("pretty = {values:#?}");
}
```

## 12.3 宽度和小数精度

```rust
fn main() {
    let pi = std::f64::consts::PI;

    println!("保留两位小数：{pi:.2}");
    println!("右对齐：{:>8}", 42);
    println!("左对齐：{:<8}!", 42);
    println!("前导零：{:04}", 42);
}
```

---

## 十三、作用域与命名规范

## 13.1 作用域

变量只在声明它的代码块及其内部可见：

```rust
fn main() {
    let outer = 10;

    {
        let inner = 20;
        println!("outer={outer}, inner={inner}");
    }

    println!("outer={outer}");
    // println!("inner={inner}"); // 无法编译
}
```

尽量缩小变量作用域，使状态更容易追踪。

## 13.2 常见命名风格

| 项目 | 风格 | 示例 |
|---|---|---|
| 变量、函数、模块 | `snake_case` | `user_name`、`read_input` |
| 类型、Trait、枚举 | `UpperCamelCase` | `UserAccount` |
| 常量、静态变量 | `SCREAMING_SNAKE_CASE` | `MAX_RETRIES` |

本阶段主要使用变量、函数和常量。保持名称准确，避免 `a`、`tmp`、`data2` 等缺乏含义的名称。

---

## 十四、常见编译错误与排查方法

## 14.1 忘记添加 `mut`

```text
error[E0384]: cannot assign twice to immutable variable
```

先判断是否真的需要修改；需要时在声明中使用 `let mut`。

## 14.2 类型不匹配

```text
error[E0308]: mismatched types
```

检查编译器显示的 `expected` 和 `found`，确认变量、参数、返回值或分支类型。

## 14.3 返回表达式误加分号

```rust,compile_fail
fn add_one(number: i32) -> i32 {
    number + 1;
}
```

最后的分号把值丢弃，使函数实际返回 `()`。删除分号即可。

## 14.4 数字解析类型不明确

```rust,compile_fail
fn main() {
    let number = "42".parse().expect("解析失败");
}
```

为变量标注目标类型：

```rust
let number: i32 = "42".parse().expect("解析失败");
```

## 14.5 数组越界

固定索引如果明显越界，编译器可能直接拒绝；动态索引越界会在运行时 panic。外部输入形成的索引应使用 `get`。

## 14.6 `if` 分支类型不一致

```rust,compile_fail
fn main() {
    let value = if true { 10 } else { "ten" };
}
```

如果用 `if` 产生值，每个可能分支都必须返回兼容类型。

## 14.7 排查步骤

1. 从第一条编译错误开始，不要先处理后续连锁错误。
2. 阅读错误代码、箭头指向的位置、`expected` 与 `found`。
3. 查看编译器给出的 `help` 建议，但理解后再修改。
4. 把复杂表达式拆成多个有明确类型的临时变量。
5. 用最小示例验证自己对语法或类型的理解。
6. 修改后先运行 `cargo check`。

可以执行 `rustc --explain E0308` 查看某个错误代码的详细解释。

---

## 十五、阶段项目一：温度转换器

### 需求

- 用户选择摄氏度转华氏度，或华氏度转摄氏度
- 读取一个温度数值
- 输出转换结果，保留两位小数
- 输入无效时给出提示，不直接崩溃
- 用户可以重复转换并选择退出

### 公式

```text
华氏度 = 摄氏度 × 9 / 5 + 32
摄氏度 = (华氏度 - 32) × 5 / 9
```

### 参考实现

```rust
use std::io;

fn read_line() -> String {
    let mut input = String::new();
    io::stdin()
        .read_line(&mut input)
        .expect("读取输入失败");
    input.trim().to_string()
}

fn celsius_to_fahrenheit(value: f64) -> f64 {
    value * 9.0 / 5.0 + 32.0
}

fn fahrenheit_to_celsius(value: f64) -> f64 {
    (value - 32.0) * 5.0 / 9.0
}

fn main() {
    loop {
        println!("\n请选择转换方向：");
        println!("1. 摄氏度转华氏度");
        println!("2. 华氏度转摄氏度");
        println!("q. 退出");

        let choice = read_line();

        if choice.eq_ignore_ascii_case("q") {
            println!("再见！");
            break;
        }

        if choice != "1" && choice != "2" {
            println!("无效选项，请重试。");
            continue;
        }

        println!("请输入温度：");
        let input = read_line();
        let temperature: f64 = match input.parse() {
            Ok(value) => value,
            Err(_) => {
                println!("请输入有效数字。");
                continue;
            }
        };

        match choice.as_str() {
            "1" => {
                let result = celsius_to_fahrenheit(temperature);
                println!("{temperature:.2}°C = {result:.2}°F");
            }
            "2" => {
                let result = fahrenheit_to_celsius(temperature);
                println!("{temperature:.2}°F = {result:.2}°C");
            }
            _ => unreachable!(),
        }
    }
}
```

### 扩展练习

- 增加开尔文温标
- 添加极端值提示
- 把菜单显示、输入解析和转换逻辑进一步拆成函数

---

## 十六、阶段项目二：Fibonacci 数列生成器

### 需求

- 用户输入需要生成的项数
- 输出从 0 开始的 Fibonacci 数列
- 检查输入是否合法
- 考虑整数溢出，不让程序悄悄产生错误结果

### 参考实现

```rust
use std::io;

fn main() {
    println!("请输入要生成的 Fibonacci 项数：");

    let mut input = String::new();
    io::stdin()
        .read_line(&mut input)
        .expect("读取输入失败");

    let count: usize = match input.trim().parse() {
        Ok(value) => value,
        Err(_) => {
            println!("请输入有效的非负整数。");
            return;
        }
    };

    let mut previous: u128 = 0;
    let mut current: u128 = 1;

    for index in 0..count {
        println!("F({index}) = {previous}");

        let next = match previous.checked_add(current) {
            Some(value) => value,
            None => {
                println!("数值超出 u128 表示范围，停止计算。");
                break;
            }
        };

        previous = current;
        current = next;
    }
}
```

### 思考题

- 为什么项数使用 `usize`？
- 为什么序列值使用 `u128`？
- 如果用递归计算，时间复杂度会发生什么变化？
- 如何只输出第 `n` 项，而不是整个序列？

---

## 十七、阶段项目三：猜数字游戏

该项目需要随机数依赖。推荐让 Cargo 添加当前兼容版本：

```bash
cargo add rand
```

下面示例采用 `rand` 当前文档提供的 `random_range` 接口。如果项目锁定了旧版本，请以该版本文档为准。

### 参考实现

```rust
use std::cmp::Ordering;
use std::io;

fn main() {
    let secret_number = rand::random_range(1..=100);
    let mut attempts = 0;

    println!("我生成了一个 1～100 之间的整数。");

    loop {
        println!("请输入你的猜测：");

        let mut input = String::new();
        io::stdin()
            .read_line(&mut input)
            .expect("读取输入失败");

        let guess: u32 = match input.trim().parse() {
            Ok(value) if (1..=100).contains(&value) => value,
            Ok(_) => {
                println!("请输入 1～100 之间的整数。");
                continue;
            }
            Err(_) => {
                println!("输入格式无效，请重试。");
                continue;
            }
        };

        attempts += 1;

        match guess.cmp(&secret_number) {
            Ordering::Less => println!("太小了！"),
            Ordering::Greater => println!("太大了！"),
            Ordering::Equal => {
                println!("猜对了！你一共尝试了 {attempts} 次。");
                break;
            }
        }
    }
}
```

### 扩展练习

- 设置最多尝试次数
- 增加难度等级，不同等级使用不同范围
- 根据尝试次数输出评级
- 游戏结束后询问是否再玩一次

---

## 十八、阶段项目四：四则运算计算器

### 需求

- 输入两个浮点数和一个运算符
- 支持 `+`、`-`、`*`、`/`
- 防止除以零
- 使用 `match` 选择运算
- 输入错误时允许重新输入

### 参考核心逻辑

```rust
fn calculate(left: f64, operator: char, right: f64) -> Option<f64> {
    match operator {
        '+' => Some(left + right),
        '-' => Some(left - right),
        '*' => Some(left * right),
        '/' if right != 0.0 => Some(left / right),
        '/' => None,
        _ => None,
    }
}

fn main() {
    let left = 12.0;
    let right = 4.0;
    let operator = '/';

    match calculate(left, operator, right) {
        Some(result) => println!("{left} {operator} {right} = {result}"),
        None => println!("运算无效，请检查运算符或除数。"),
    }
}
```

### 扩展练习

- 支持余数、乘方和平方根
- 区分“未知运算符”和“除以零”两类错误
- 保存上一次计算结果
- 增加连续运算菜单

---

## 十九、综合练习题

### 基础题

1. 输出 1～100 中的偶数。
2. 计算 1～`n` 的累加和。
3. 判断一个整数是正数、负数还是零。
4. 判断一个年份是否为闰年。
5. 计算一个整数各位数字之和。
6. 反向输出一个固定整数数组。
7. 查找数组中的最大值与最小值。
8. 统计数组中大于平均值的元素数量。

### 进阶题

1. 打印指定高度的直角三角形和等腰三角形。
2. 判断一个正整数是否为质数。
3. 输出指定范围内的全部质数。
4. 求两个正整数的最大公约数和最小公倍数。
5. 判断一个整数是否为回文数。
6. 使用 `match` 实现简易菜单系统。
7. 安全读取数组索引并输出对应元素。
8. 实现不使用标准库排序方法的冒泡排序。

### 挑战题

1. 实现剪刀石头布命令行游戏。
2. 实现支持多轮计分的答题程序。
3. 生成指定行数的杨辉三角。
4. 实现简单的 ATM 菜单：查询、存款、取款、退出。
5. 将四个阶段项目中重复的输入逻辑提取为函数。

---

## 二十、知识自测

尝试不查资料回答：

1. `let`、`let mut` 和 `const` 有什么区别？
2. 遮蔽与修改可变变量有什么区别？
3. `i32` 和 `u32` 的取值范围有何本质差别？
4. 为什么整数除法 `5 / 2` 得到 2？
5. `char` 与长度为 1 的字符串有什么区别？
6. 元组和数组分别适合什么场景？
7. `&[i32]` 表示什么？
8. 语句与表达式的区别是什么？
9. 为什么函数末尾表达式通常不能加分号？
10. `if` 为什么能赋值给变量？
11. `loop`、`while` 和 `for` 应如何选择？
12. `1..5` 与 `1..=5` 有什么区别？
13. `break value` 有什么作用？
14. 为什么 `match` 必须覆盖所有情况？
15. `match`、`if let` 和 `while let` 各适合什么场景？
16. 为什么访问外部输入指定的数组索引时建议使用 `get`？
17. `parse()` 为什么经常需要类型标注？
18. 遇到多个编译错误时应先处理哪一个？

如果有三道以上无法清楚解释，建议返回对应章节并重新完成练习。

---

## 二十一、阶段验收清单

### 语法能力

- [ ] 能创建不可变变量、可变变量和常量
- [ ] 能解释并正确使用变量遮蔽
- [ ] 能选择常见整数和浮点类型
- [ ] 能使用布尔值、字符、元组、数组和切片
- [ ] 能区分语句和表达式
- [ ] 能编写带参数和返回值的函数
- [ ] 能根据场景选择 `if`、`loop`、`while` 和 `for`
- [ ] 能使用 `match` 完整处理分支
- [ ] 能使用 `if let` 和 `while let` 简化单模式处理

### 工具能力

- [ ] 能使用 Cargo 创建、检查、运行和构建项目
- [ ] 能使用 `cargo fmt` 格式化代码
- [ ] 能使用 `cargo clippy` 检查常见问题
- [ ] 能从第一条编译错误开始定位问题
- [ ] 能用 `rustc --explain` 查看错误说明

### 实践能力

- [ ] 独立完成温度转换器
- [ ] 独立完成 Fibonacci 数列生成器
- [ ] 独立完成猜数字游戏
- [ ] 独立完成四则运算计算器
- [ ] 项目能够正确处理基本的无效输入
- [ ] 程序已拆分为职责清楚的函数

### 阶段质量检查

在每个项目目录执行：

```bash
cargo fmt --check
cargo clippy
cargo check
cargo run
```

当上述清单基本完成，并且能够不照抄示例独立写出一个菜单式命令行程序时，即可进入第二阶段：所有权、借用与切片。

---

## 二十二、进入下一阶段前的预习

下一阶段将重点回答这些问题：

- 为什么有些变量赋值后不能再使用？
- `String` 和 `&str` 有什么区别？
- `&value` 与 `&mut value` 分别代表什么？
- 为什么同一时刻不能随意创建多个可变引用？
- 数组切片为什么不复制原数组？
- 函数参数应该接收值还是引用？

建议保留本阶段所有项目。学习所有权后，再回头检查它们是否存在不必要的复制、过大的变量作用域或不合理的参数设计，这会是一次很有价值的重构练习。
