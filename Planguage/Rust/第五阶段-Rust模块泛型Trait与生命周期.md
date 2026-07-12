# 第五阶段：Rust 模块、泛型、Trait 与生命周期

> 本阶段解决两个核心问题：如何把不断增长的程序组织成清晰工程，以及如何在保持类型安全的前提下复用代码。模块负责边界，泛型负责类型复用，Trait 负责行为抽象，生命周期负责引用关系。

## 一、阶段目标

完成本阶段后，你应当能够：

- 区分 package、crate、模块与路径
- 使用 `mod`、`pub`、`use`、`crate`、`super` 组织代码
- 把单文件程序拆成库与二进制入口
- 编写泛型函数、结构体、枚举和方法
- 使用 Trait 定义共享行为与默认实现
- 使用 Trait bound 和 `where` 子句约束泛型
- 区分静态分发与动态分发
- 正确选择泛型、`impl Trait` 和 `dyn Trait`
- 使用关联类型和关联常量
- 理解生命周期标注描述的是引用关系，而不是延长数据寿命
- 应用生命周期省略规则并识别何时必须显式标注
- 准确理解 `'static`
- 完成可扩展命令执行框架

---

## 二、Package、Crate 与模块

## 2.1 Package

Cargo package 是包含 `Cargo.toml` 的项目发布和构建单元。一个 package：

- 最多包含一个 library crate
- 可以包含多个 binary crate
- 至少包含一个 crate

常见结构：

```text
command-framework/
├── Cargo.toml
├── src/
│   ├── lib.rs       # library crate 根
│   ├── main.rs      # 默认 binary crate 根
│   └── ...
└── tests/           # 集成测试
```

`src/lib.rs` 和 `src/main.rs` 分别是两个 crate 的根。它们属于同一个 package，但拥有各自模块树。

## 2.2 Crate

Crate 是 Rust 编译器一次编译的代码单元。

- binary crate：生成可执行程序，通常包含 `main`
- library crate：提供供其他代码调用的 API

crate 根决定模块树起点。模块声明从 `lib.rs` 或 `main.rs` 向下展开。

## 2.3 模块

模块用于：

- 组织相关代码
- 控制可见性
- 建立命名空间
- 隔离实现细节

```rust
mod network {
    pub fn connect() {
        println!("connected");
    }

    fn validate_address() {
        println!("validated");
    }
}

fn main() {
    network::connect();
}
```

模块默认私有，模块成员也默认私有。

---

## 三、模块文件布局

推荐现代布局：

```text
src/
├── lib.rs
├── parser.rs
├── report.rs
└── model/
    ├── mod.rs
    ├── entry.rs
    └── level.rs
```

`lib.rs`：

```rust,ignore
pub mod model;
pub mod parser;
pub mod report;
```

`model/mod.rs`：

```rust,ignore
mod entry;
mod level;

pub use entry::LogEntry;
pub use level::Level;
```

也可以使用 `model.rs` 配合 `model/entry.rs`。一个模块只选择一种根文件形式，不要同时创建 `model.rs` 和 `model/mod.rs`。

## 3.1 模块声明不是文本包含

`mod parser;` 告诉编译器模块存在于对应文件中。它不是 C/C++ 的头文件包含，而是构建模块树。

## 3.2 可见性

```rust
pub mod api {
    pub fn run() {}

    pub(crate) fn internal_for_crate() {}

    pub(super) fn visible_to_parent() {}

    fn private_helper() {}
}
```

- `pub`：从可达的公共路径对外可见
- `pub(crate)`：当前 crate 内可见
- `pub(super)`：父模块可见
- 无修饰：当前模块及子模块可按规则访问

公共结构体的字段仍默认私有：

```rust
pub struct Account {
    pub id: u64,
    balance: i64,
}
```

私有字段能迫使调用者通过构造器和方法维护不变量。

---

## 四、路径与 `use`

## 4.1 绝对路径和相对路径

```rust
mod service {
    pub mod user {
        pub fn create() {}
    }
}

fn call() {
    crate::service::user::create();
    service::user::create();
}
```

- `crate::`：从当前 crate 根开始
- `self::`：从当前模块开始
- `super::`：从父模块开始

## 4.2 `use` 引入名称

```rust
use crate::service::user;

fn call() {
    user::create();
}
```

惯例：

- 引入函数时通常引入其父模块，使调用来源清楚
- 引入结构体、枚举、Trait 时通常引入完整名称

```rust
use std::collections::HashMap;
```

## 4.3 重命名与嵌套路径

```rust
use std::fmt::Result as FmtResult;
use std::io::{self, Read, Write};
use std::collections::{HashMap, HashSet};
```

## 4.4 `pub use` 重导出

```rust,ignore
mod model;
pub use model::LogEntry;
```

调用者可以使用 `your_crate::LogEntry`，无需知道内部模块位置。重导出用于设计稳定、友好的公共 API，而不是随意把所有内部符号公开。

---

## 五、库与二进制分离

原则：

- `lib.rs`：业务逻辑、数据模型、可测试 API
- `main.rs`：读取参数、调用库、输出结果、确定退出码

```rust,ignore
// src/lib.rs
pub fn run_query<'a>(text: &'a str, keyword: &str) -> Vec<&'a str> {
    text.lines().filter(|line| line.contains(keyword)).collect()
}
```

```rust,ignore
// src/main.rs
use command_framework::run_query;

fn main() {
    let result = run_query("rust\njava", "rust");
    println!("{result:?}");
}
```

包名中的连字符在代码路径中转换为下划线。

把纯逻辑放入库能降低测试成本，也让未来的 CLI、Web API 或 GUI 共用同一实现。

---

## 六、泛型函数

泛型让同一算法适用于多种类型。

```rust
fn largest<T: PartialOrd>(values: &[T]) -> Option<&T> {
    let mut iterator = values.iter();
    let mut largest = iterator.next()?;

    for value in iterator {
        if value > largest {
            largest = value;
        }
    }

    Some(largest)
}
```

`T` 是类型参数，`T: PartialOrd` 表示它必须支持比较。

## 6.1 为什么返回引用

如果返回 `T`，需要移动或复制元素。返回 `Option<&T>` 可借用输入中的最大元素，适用于非 `Copy` 类型。

## 6.2 多个类型参数

```rust
#[derive(Debug)]
struct Pair<T, U> {
    left: T,
    right: U,
}

fn main() {
    let pair = Pair {
        left: "age",
        right: 20,
    };
    println!("{pair:?}");
}
```

类型参数名称应短而有意义。复杂领域泛型可使用 `Item`、`Error`、`Request` 等名称。

## 6.3 单态化

Rust 通常在编译期为实际使用的具体类型生成代码，称为单态化。泛型抽象通常没有运行时动态分派成本，但可能增加编译时间和二进制体积。

---

## 七、泛型结构体、枚举与方法

```rust
#[derive(Debug)]
struct Point<T> {
    x: T,
    y: T,
}

impl<T> Point<T> {
    fn x(&self) -> &T {
        &self.x
    }
}

impl Point<f64> {
    fn distance_from_origin(&self) -> f64 {
        (self.x.powi(2) + self.y.powi(2)).sqrt()
    }
}
```

第一段 `impl<T>` 适用于所有 `Point<T>`，第二段只适用于 `Point<f64>`。

方法可以引入不同泛型：

```rust
impl<T> Point<T> {
    fn mixup<U>(self, other: Point<U>) -> Pair<T, U> {
        Pair {
            left: self.x,
            right: other.y,
        }
    }
}
```

标准库的 `Option<T>`、`Result<T, E>`、`Vec<T>` 都是泛型类型。

---

## 八、Trait：共享行为

Trait 定义一组类型可以实现的行为。

```rust
trait Summary {
    fn summarize(&self) -> String;
}

struct Article {
    title: String,
    author: String,
}

impl Summary for Article {
    fn summarize(&self) -> String {
        format!("{} — {}", self.title, self.author)
    }
}
```

## 8.1 默认实现

```rust
trait Summary {
    fn title(&self) -> &str;

    fn summarize(&self) -> String {
        format!("摘要：{}", self.title())
    }
}
```

实现者必须提供 `title`，可直接使用或覆盖 `summarize`。

默认方法可以调用 Trait 中其他方法，但不能直接假设具体类型存在某字段。

## 8.2 孤儿规则

实现 Trait 时，Trait 或目标类型至少有一个定义在当前 crate。不能为外部类型实现外部 Trait：

```rust,compile_fail
impl std::fmt::Display for Vec<String> {
    // Trait 和类型都来自标准库
}
```

可用新类型包装：

```rust
struct StringList(Vec<String>);
```

再为本地 `StringList` 实现外部 Trait。

---

## 九、Trait Bound 与 `where`

## 9.1 参数约束

```rust
fn notify<T: Summary>(item: &T) {
    println!("{}", item.summarize());
}
```

多个约束：

```rust
use std::fmt::{Debug, Display};

fn print_item<T: Display + Debug>(item: &T) {
    println!("display={item}, debug={item:?}");
}
```

## 9.2 `where` 子句

复杂签名用 `where` 更清楚：

```rust
fn compare_and_print<T, U>(left: &T, right: &U)
where
    T: std::fmt::Display + PartialOrd<U>,
    U: std::fmt::Display,
{
    if left > right {
        println!("{left} > {right}");
    } else {
        println!("{left} <= {right}");
    }
}
```

## 9.3 条件实现

```rust
struct Container<T> {
    value: T,
}

impl<T> Container<T> {
    fn new(value: T) -> Self {
        Self { value }
    }
}

impl<T: std::fmt::Display> Container<T> {
    fn print(&self) {
        println!("{}", self.value);
    }
}
```

只有 `T: Display` 时才有 `print` 方法。

Blanket implementation 为满足约束的所有类型统一实现 Trait。标准库中的 `ToString` 就与 `Display` 实现相关。

---

## 十、`impl Trait`

## 10.1 参数位置

```rust
fn notify(item: &impl Summary) {
    println!("{}", item.summarize());
}
```

这是简单 Trait bound 的简写。若两个参数必须是同一具体类型，泛型写法更明确：

```rust
fn same_type<T: Summary>(left: &T, right: &T) {}
```

而：

```rust
fn possibly_different(left: &impl Summary, right: &impl Summary) {}
```

允许两个参数是不同具体类型。

## 10.2 返回位置

```rust
fn make_summary() -> impl Summary {
    Article {
        title: String::from("Rust"),
        author: String::from("Ferris"),
    }
}
```

调用方知道返回值实现 `Summary`，但不知道具体类型。函数的所有返回路径必须是同一个具体类型：

```rust,compile_fail
fn choose(flag: bool) -> impl Summary {
    if flag {
        Article { /* ... */ }
    } else {
        AnotherType { /* ... */ }
    }
}
```

需要返回不同具体类型时，可使用枚举统一它们，或使用 Trait 对象。

---

## 十一、静态分发与动态分发

## 11.1 静态分发

泛型和 `impl Trait` 通常在编译期确定具体实现：

```rust
fn execute<T: Command>(command: &T) {
    command.execute();
}
```

特点：

- 易于内联和优化
- 没有虚表调用开销
- 返回具体类型能力强
- 异构集合不方便
- 可能增加生成代码体积

## 11.2 动态分发

Trait 对象在运行时通过虚表选择方法：

```rust
trait Draw {
    fn draw(&self);
}

struct Screen {
    components: Vec<Box<dyn Draw>>,
}

impl Screen {
    fn run(&self) {
        for component in &self.components {
            component.draw();
        }
    }
}
```

`Box<dyn Draw>` 可以存放不同具体类型，只要都实现 `Draw`。

特点：

- 适合运行时异构集合和插件边界
- 减少调用方对具体类型的依赖
- 有一次间接调用和可能的堆分配
- 部分方法不能通过 Trait 对象调用

## 11.3 `dyn Trait` 为什么需要指针

不同实现类型大小不同，`dyn Trait` 是动态大小类型，通常放在某种指针后：

- `&dyn Trait`：临时借用对象
- `&mut dyn Trait`：可变借用对象
- `Box<dyn Trait>`：拥有堆上的对象
- 后续还会见到 `Arc<dyn Trait>`

## 11.4 对象安全直觉

要通过 `dyn Trait` 调用的方法必须能在不知道具体 `Self` 类型时工作。常见障碍：

- 方法返回 `Self`
- 方法拥有自己的泛型类型参数
- 方法要求 `Self: Sized`

可以把仅适用于具体类型的方法加上 `where Self: Sized`，保留其他方法供 Trait 对象调用。

## 11.5 选择表

| 需求 | 选择 |
|---|---|
| 编译期已知类型、追求静态优化 | 泛型 / `impl Trait` |
| 返回单一但隐藏的具体类型 | `impl Trait` |
| 一个集合存放多种实现 | `Box<dyn Trait>` |
| 只临时调用未知实现 | `&dyn Trait` |
| 变体集合有限且希望穷尽匹配 | 枚举 |

枚举和 Trait 对象都能表示多态。变体集合封闭时优先考虑枚举；实现集合需要开放扩展时考虑 Trait。

---

## 十二、关联类型与关联常量

## 12.1 关联类型

```rust
trait Source {
    type Item;

    fn next_item(&mut self) -> Option<Self::Item>;
}
```

实现时确定 `Item`：

```rust
struct Counter {
    current: u32,
    end: u32,
}

impl Source for Counter {
    type Item = u32;

    fn next_item(&mut self) -> Option<Self::Item> {
        if self.current < self.end {
            let value = self.current;
            self.current += 1;
            Some(value)
        } else {
            None
        }
    }
}
```

关联类型表示“每个实现对应一个确定类型”。泛型 Trait 参数则允许同一个类型对不同参数多次实现。标准库 `Iterator` 使用关联类型 `Item`。

## 12.2 关联常量

```rust
trait Protocol {
    const VERSION: u16;
    fn name(&self) -> &'static str;
}

struct Http;

impl Protocol for Http {
    const VERSION: u16 = 2;

    fn name(&self) -> &'static str {
        "HTTP"
    }
}
```

关联常量适合每个实现固定的类型级配置。

---

## 十三、生命周期解决什么问题

生命周期标注描述多个引用之间的有效期关系，帮助编译器确认返回引用不会悬垂。它不延长任何值的寿命。

```rust,compile_fail
fn longest(left: &str, right: &str) -> &str {
    if left.len() >= right.len() { left } else { right }
}
```

返回值可能来自任一参数，编译器需要知道关系：

```rust
fn longest<'a>(left: &'a str, right: &'a str) -> &'a str {
    if left.len() >= right.len() { left } else { right }
}
```

含义：返回引用的有效范围不会超过两个输入引用共同有效范围。

`'a` 是关系参数，不代表实际时长，也不是要求两个输入活得完全一样久。

## 13.1 生命周期不能修复悬垂引用

```rust,compile_fail
fn invalid<'a>() -> &'a str {
    let text = String::from("temporary");
    &text
}
```

局部 `String` 在函数结束时释放。添加生命周期标注不能让它继续存在。正确方案是返回 `String`。

---

## 十四、生命周期省略规则

许多常见函数无需显式标注：

```rust
fn first_word(text: &str) -> &str {
    text.split_whitespace().next().unwrap_or("")
}
```

编译器应用三条主要规则：

1. 每个引用参数获得各自生命周期参数。
2. 如果只有一个输入生命周期，它赋给所有省略的输出生命周期。
3. 方法有 `&self` 或 `&mut self` 时，`self` 的生命周期赋给省略的输出生命周期。

这些规则仍无法确定时，必须显式标注。

## 14.1 不需要标注的例子

```rust
fn length(text: &str) -> usize {
    text.len()
}
```

返回值不是引用，无需描述引用关系。

```rust
fn first(text: &str) -> &str {
    &text[..1]
}
```

只有一个输入引用，输出自然关联它。

## 14.2 需要标注的例子

```rust
fn choose<'a>(first: &'a str, second: &'a str, use_first: bool) -> &'a str {
    if use_first { first } else { second }
}
```

两个引用都可能成为输出来源。

## 14.3 更精确的关系

如果返回值只可能来自第一个参数，不必把第二个参数绑定到同一生命周期：

```rust
fn announce_and_return<'a>(text: &'a str, announcement: &str) -> &'a str {
    println!("{announcement}");
    text
}
```

生命周期应表达真实来源，避免不必要地限制调用者。

---

## 十五、结构体中的引用

结构体保存引用时必须标注生命周期：

```rust
#[derive(Debug)]
struct Excerpt<'a> {
    text: &'a str,
}

impl<'a> Excerpt<'a> {
    fn new(text: &'a str) -> Self {
        Self { text }
    }

    fn text(&self) -> &str {
        self.text
    }
}
```

`Excerpt<'a>` 不能比它借用的字符串活得更久。

```rust,compile_fail
let excerpt;
{
    let owned = String::from("short lived");
    excerpt = Excerpt::new(&owned);
}
println!("{excerpt:?}");
```

## 15.1 拥有还是借用

结构体字段选择：

- 需要长期独立保存：`String`、`Vec<T>` 等拥有类型
- 只在限定处理阶段查看外部数据：`&str`、`&[T]`
- 不确定时，先让业务实体拥有数据，避免到处传播生命周期参数

借用结构体适合解析视图、零拷贝读取等场景，但会把生命周期关系传递给使用它的上层类型。

---

## 十六、Trait 与生命周期结合

```rust
trait Find<'a> {
    fn find_in(&self, text: &'a str) -> Option<&'a str>;
}
```

更常见的是在方法上引入生命周期：

```rust
trait Selector {
    fn select<'a>(&self, values: &'a [String]) -> Option<&'a str>;
}
```

返回值来自 `values`，不来自 `self`。

Trait bound 也可带生命周期：

```rust
fn print_ref<T>(value: &T)
where
    T: std::fmt::Display + ?Sized,
{
    println!("{value}");
}
```

高阶生命周期约束等高级形式在实际遇到回调和借用返回值时再深入，不应在基础 API 中过早复杂化。

---

## 十七、`'static` 的准确含义

`'static` 常见两种语境。

## 17.1 `&'static T`

引用指向的数据可存活整个程序：

```rust
let text: &'static str = "字符串字面量";
```

字符串字面量存放在程序二进制中，因此可作为 `'static` 引用。

## 17.2 `T: 'static`

表示类型 `T` 不包含生命周期短于 `'static` 的借用。它不表示该值一定永远存在。

```rust
fn require_static<T: 'static>(value: T) {
    drop(value); // 值仍可立即被释放
}

fn main() {
    require_static(String::from("owned"));
}
```

拥有数据的 `String` 满足 `T: 'static`，但函数结束时照样释放。

不要看到生命周期错误就添加 `'static`。先判断引用真正来自哪里、需要活到哪里，以及是否应该改为拥有数据。

---

## 十八、阶段项目：可扩展命令执行框架

## 18.1 目标

- 定义统一 `Command` Trait
- 实现多个不同命令
- 支持静态泛型执行和动态注册
- 解析名称与参数
- 返回统一执行结果
- 业务逻辑放在库中，CLI 放在二进制入口

## 18.2 项目结构

```text
command-framework/
├── Cargo.toml
└── src/
    ├── lib.rs
    ├── main.rs
    ├── command.rs
    ├── context.rs
    └── commands/
        ├── mod.rs
        ├── echo.rs
        ├── add.rs
        └── count.rs
```

## 18.3 核心接口

`src/command.rs`：

```rust,ignore
use crate::Context;

#[derive(Debug, PartialEq, Eq)]
pub struct CommandOutput {
    pub text: String,
}

pub trait Command {
    fn name(&self) -> &'static str;
    fn description(&self) -> &'static str;

    fn execute(
        &self,
        arguments: &[String],
        context: &mut Context,
    ) -> Result<CommandOutput, String>;
}
```

名称和描述是字符串字面量，返回 `&'static str`。执行结果拥有 `String`，不会借用临时参数。

`src/context.rs`：

```rust,ignore
use std::collections::HashMap;

#[derive(Debug, Default)]
pub struct Context {
    variables: HashMap<String, String>,
}

impl Context {
    pub fn set(&mut self, key: String, value: String) {
        self.variables.insert(key, value);
    }

    pub fn get(&self, key: &str) -> Option<&str> {
        self.variables.get(key).map(String::as_str)
    }
}
```

`get` 的输出生命周期通过省略规则关联到 `&self`。

## 18.4 命令实现

`src/commands/echo.rs`：

```rust,ignore
use crate::{Command, CommandOutput, Context};

pub struct Echo;

impl Command for Echo {
    fn name(&self) -> &'static str { "echo" }
    fn description(&self) -> &'static str { "原样输出参数" }

    fn execute(
        &self,
        arguments: &[String],
        _context: &mut Context,
    ) -> Result<CommandOutput, String> {
        Ok(CommandOutput {
            text: arguments.join(" "),
        })
    }
}
```

`src/commands/add.rs`：

```rust,ignore
use crate::{Command, CommandOutput, Context};

pub struct Add;

impl Command for Add {
    fn name(&self) -> &'static str { "add" }
    fn description(&self) -> &'static str { "把整数参数相加" }

    fn execute(
        &self,
        arguments: &[String],
        _context: &mut Context,
    ) -> Result<CommandOutput, String> {
        if arguments.is_empty() {
            return Err(String::from("add 至少需要一个整数"));
        }

        let total: i64 = arguments
            .iter()
            .map(|argument| {
                argument
                    .parse::<i64>()
                    .map_err(|_| format!("无效整数：{argument}"))
            })
            .collect::<Result<Vec<_>, _>>()?
            .into_iter()
            .sum();

        Ok(CommandOutput {
            text: total.to_string(),
        })
    }
}
```

也可直接用 `try_fold` 避免中间 `Vec`：

```rust,ignore
let total = arguments.iter().try_fold(0_i64, |total, argument| {
    let value = argument
        .parse::<i64>()
        .map_err(|_| format!("无效整数：{argument}"))?;
    total.checked_add(value).ok_or_else(|| String::from("求和溢出"))
})?;
```

`src/commands/count.rs`：

```rust,ignore
use crate::{Command, CommandOutput, Context};

pub struct Count;

impl Command for Count {
    fn name(&self) -> &'static str { "count" }
    fn description(&self) -> &'static str { "统计参数数量" }

    fn execute(
        &self,
        arguments: &[String],
        context: &mut Context,
    ) -> Result<CommandOutput, String> {
        let count = arguments.len();
        context.set(String::from("last_count"), count.to_string());
        Ok(CommandOutput { text: count.to_string() })
    }
}
```

## 18.5 模块重导出

`src/commands/mod.rs`：

```rust,ignore
mod add;
mod count;
mod echo;

pub use add::Add;
pub use count::Count;
pub use echo::Echo;
```

调用者看到稳定的 `commands::Add`，不依赖内部文件层级。

`src/lib.rs`：

```rust,ignore
mod command;
mod context;
pub mod commands;

pub use command::{Command, CommandOutput};
pub use context::Context;

pub struct Registry {
    commands: Vec<Box<dyn Command>>,
}

impl Registry {
    pub fn new() -> Self {
        Self { commands: Vec::new() }
    }

    pub fn register<C>(&mut self, command: C)
    where
        C: Command + 'static,
    {
        self.commands.push(Box::new(command));
    }

    pub fn find(&self, name: &str) -> Option<&dyn Command> {
        self.commands
            .iter()
            .find(|command| command.name() == name)
            .map(Box::as_ref)
    }

    pub fn commands(&self) -> impl Iterator<Item = &dyn Command> {
        self.commands.iter().map(Box::as_ref)
    }
}

impl Default for Registry {
    fn default() -> Self {
        Self::new()
    }
}

pub fn execute_static<C>(
    command: &C,
    arguments: &[String],
    context: &mut Context,
) -> Result<CommandOutput, String>
where
    C: Command,
{
    command.execute(arguments, context)
}
```

这里同时展示：

- `execute_static`：泛型静态分发
- `Registry`：`Box<dyn Command>` 动态分发
- `register<C>`：在边界把具体类型转换为 Trait 对象
- `commands()`：`impl Iterator` 隐藏具体迭代器类型
- `C: 'static`：注册对象不能含有短期外部借用，因为注册表长期拥有它

## 18.6 二进制入口

`src/main.rs`：

```rust,ignore
use std::env;
use std::process;

use command_framework::commands::{Add, Count, Echo};
use command_framework::{Context, Registry};

fn run() -> Result<(), String> {
    let mut registry = Registry::new();
    registry.register(Echo);
    registry.register(Add);
    registry.register(Count);

    let arguments: Vec<String> = env::args().skip(1).collect();
    let Some((name, command_arguments)) = arguments.split_first() else {
        println!("可用命令：");
        for command in registry.commands() {
            println!("  {:<10} {}", command.name(), command.description());
        }
        return Ok(());
    };

    let command = registry
        .find(name)
        .ok_or_else(|| format!("未知命令：{name}"))?;
    let mut context = Context::default();
    let output = command.execute(command_arguments, &mut context)?;
    println!("{}", output.text);
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("错误：{error}");
        process::exit(1);
    }
}
```

运行：

```bash
cargo run
cargo run -- echo hello rust
cargo run -- add 10 20 30
cargo run -- count a b c
```

## 18.7 项目扩展

1. 用 `HashMap<String, Box<dyn Command>>` 加速名称查找。
2. 注册时检查重复命令名。
3. 增加别名 Trait 方法。
4. 增加 `set` 和 `get` 命令使用上下文变量。
5. 让命令返回自定义错误枚举。
6. 添加中间件 Trait，在执行前后记录时间。
7. 用泛型 Registry 比较纯静态分发设计。
8. 用枚举统一固定命令，比较枚举和 Trait 对象。
9. 为公共 API 编写文档测试。
10. 为框架增加集成测试。

---

## 十九、常见误区

- 把所有模块和字段都设为 `pub`
- 让调用者依赖深层内部模块路径
- 在 `main.rs` 中堆积业务逻辑
- 为一个具体类型过早引入泛型
- 创建只有一个实现且没有抽象价值的巨大 Trait
- Trait 方法职责过多，迫使实现者依赖无关能力
- 混淆 `impl Trait` 参数中不同参数是否同一类型
- 需要异构集合时仍强行使用单一泛型参数
- 变体集合固定却无条件使用 Trait 对象
- 看到生命周期错误就添加 `'static`
- 认为生命周期标注会延长局部变量寿命
- 给所有引用都显式写生命周期，制造噪声
- 让借用结构体传播到整个业务模型，而拥有数据更简单
- 将关联类型和泛型 Trait 参数视为完全相同

---

## 二十、针对性练习

1. 把第四阶段日志分析器拆成 `model`、`parser`、`filter`、`report` 模块。
2. 设计公共重导出，使调用者只需导入少量顶层类型。
3. 编写泛型 `Stack<T>`，实现 `push`、`pop`、`peek`。
4. 定义 `Serialize` Trait，为三个类型实现文本序列化。
5. 编写接收 `impl Display` 和泛型 `T: Display` 的两个版本，比较差异。
6. 用 `Vec<Box<dyn Shape>>` 保存不同图形并计算总面积。
7. 用枚举重新实现同一图形集合并比较取舍。
8. 为迭代器风格数据源定义关联类型。
9. 实现返回两个字符串中较长者的函数，并解释生命周期。
10. 定义保存字符串切片的解析结果结构体，验证其不能超过原文本寿命。

---

## 二十一、知识自测

1. package、crate、模块分别是什么？
2. `lib.rs` 和 `main.rs` 是同一个 crate 吗？
3. `pub` 为何不一定让项目在所有路径上可达？
4. `pub use` 的 API 设计价值是什么？
5. `crate`、`self`、`super` 路径分别从哪里开始？
6. 泛型单态化意味着什么？
7. Trait 默认方法能否直接访问实现类型字段？
8. 孤儿规则限制什么？
9. 何时使用 `where` 子句？
10. 参数位置 `impl Trait` 与泛型参数有何细微差异？
11. 返回 `impl Trait` 能否在不同分支返回不同具体类型？
12. 静态分发与动态分发各有什么优缺点？
13. `dyn Trait` 为什么通常需要放在引用或智能指针后？
14. 枚举和 Trait 对象如何选择？
15. 关联类型和泛型参数表达的关系有何区别？
16. 生命周期标注描述什么？
17. 为什么生命周期不能修复局部引用返回？
18. 三条生命周期省略规则是什么？
19. `&'static str` 与 `T: 'static` 有何区别？
20. 为什么 `String` 可以满足 `T: 'static` 却仍会被释放？

---

## 二十二、阶段验收清单

### 工程组织

- [ ] 能区分 package、crate 和模块
- [ ] 能把业务逻辑拆入 library crate
- [ ] 能合理使用 `pub`、`pub(crate)` 和私有成员
- [ ] 能通过 `pub use` 设计稳定公共 API
- [ ] 模块名称和职责清楚，无循环式职责依赖

### 泛型与 Trait

- [ ] 能编写泛型函数、结构体和方法
- [ ] 能使用 Trait 定义小而清晰的行为
- [ ] 能编写默认实现和条件实现
- [ ] 能使用 Trait bound 与 `where`
- [ ] 能解释孤儿规则和新类型模式
- [ ] 能使用关联类型与关联常量

### 分发策略

- [ ] 能解释静态分发和动态分发
- [ ] 能合理选择泛型、`impl Trait`、枚举与 `dyn Trait`
- [ ] 能使用 `&dyn Trait` 和 `Box<dyn Trait>`
- [ ] 能识别常见对象安全限制

### 生命周期

- [ ] 能解释生命周期是引用关系而非寿命延长器
- [ ] 能应用生命周期省略规则
- [ ] 能为多输入引用返回值编写必要标注
- [ ] 能让结构体安全保存引用
- [ ] 能准确解释 `&'static T` 与 `T: 'static`
- [ ] 不用 `'static` 粗暴掩盖所有权设计问题

### 项目

- [ ] 完成命令 Trait 和至少三个实现
- [ ] 完成动态命令注册与执行
- [ ] 框架库和 CLI 入口分离
- [ ] 公共 API 通过重导出保持简洁
- [ ] 能比较静态和动态执行路径
- [ ] 至少完成三个扩展任务

### 质量检查

```bash
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test
cargo doc --no-deps
cargo run -- add 10 20 30
```

当你能够把一个单文件项目拆成边界清楚的模块，设计恰当的泛型和 Trait API，并能用自己的语言解释返回引用的生命周期关系时，即可进入第六阶段：错误处理、测试与工程化。

---

## 二十三、进入下一阶段前的预习

下一阶段将重点处理：

- 哪些失败应该 panic，哪些应该返回 `Result`？
- 如何设计能被调用者匹配和追踪来源的错误类型？
- 如何组织单元测试、集成测试和文档测试？
- 如何利用 Cargo workspace、feature 和构建配置管理工程？
- 如何建立格式化、Clippy、测试和构建质量门禁？

建议为命令框架补齐测试，并记录当前全部 `String` 错误。下一阶段会将它们重构为结构化错误类型，并建立完整测试与工程检查流程。
