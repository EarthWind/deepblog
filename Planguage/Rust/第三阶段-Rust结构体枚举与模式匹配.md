# 第三阶段：Rust 结构体、枚举与模式匹配

> 本阶段学习如何用类型表达业务模型。结构体用于组合“同时存在”的数据，枚举用于表达“有限且互斥”的状态，模式匹配用于安全地拆解和处理这些数据。

## 一、阶段目标

完成本阶段后，你应当能够：

- 定义和使用普通结构体、元组结构体与单元结构体
- 理解字段所有权、结构体更新语法及部分移动
- 为类型实现方法和关联函数
- 使用枚举表达有限状态及不同形态的数据
- 使用 `Option<T>` 表达值可能缺失
- 使用 `Result<T, E>` 表达操作可能失败
- 使用 `match`、`if let`、`let else` 解构数据
- 使用范围、守卫、`@` 绑定、忽略模式和嵌套模式
- 根据业务语义设计结构体和枚举
- 使用类型系统实现简单状态机
- 完成支持文件持久化的待办事项 CLI

---

## 二、结构体基础

结构体把多个相关字段组合成一个有名字的类型。

```rust
#[derive(Debug)]
struct User {
    username: String,
    email: String,
    active: bool,
    sign_in_count: u64,
}

fn main() {
    let user = User {
        username: String::from("alice"),
        email: String::from("alice@example.com"),
        active: true,
        sign_in_count: 1,
    };

    println!("用户名：{}", user.username);
    println!("{user:#?}");
}
```

`#[derive(Debug)]` 让结构体支持 `{:?}` 和 `{:#?}` 调试输出。

## 2.1 修改字段

整个实例必须声明为可变，Rust 不支持只把某一个字段声明为可变：

```rust
fn main() {
    let mut user = User {
        username: String::from("alice"),
        email: String::from("old@example.com"),
        active: true,
        sign_in_count: 1,
    };

    user.email = String::from("new@example.com");
    user.sign_in_count += 1;
}
```

## 2.2 字段初始化简写

变量名和字段名相同时可省略重复部分：

```rust
fn build_user(username: String, email: String) -> User {
    User {
        username,
        email,
        active: true,
        sign_in_count: 0,
    }
}
```

## 2.3 结构体更新语法

```rust
fn main() {
    let first = User {
        username: String::from("alice"),
        email: String::from("alice@example.com"),
        active: true,
        sign_in_count: 8,
    };

    let second = User {
        email: String::from("bob@example.com"),
        username: String::from("bob"),
        ..first
    };

    println!("{}", second.sign_in_count);
}
```

`..first` 会为未显式指定的字段取用 `first` 的值。实现 `Copy` 的字段被复制；未实现 `Copy` 的字段会被移动。

如果 `first` 的某个 `String` 字段被移动，之后不能再把 `first` 作为完整值使用。字段没有被移动时，仍可能单独使用。

## 2.4 字段所有权

结构体拥有其字段：

```rust
struct Article {
    title: String,
    body: String,
}
```

如果写成引用字段，结构体必须保证引用不比原数据活得更久，这会涉及生命周期标注。第五阶段再系统学习。当前阶段通常让业务结构体拥有 `String`，在只读方法中借用为 `&str`。

---

## 三、元组结构体与单元结构体

## 3.1 元组结构体

字段不需要名称，但整个类型需要独立含义时，可使用元组结构体：

```rust
#[derive(Debug)]
struct Color(u8, u8, u8);

#[derive(Debug)]
struct Point(i32, i32);

fn main() {
    let black = Color(0, 0, 0);
    let origin = Point(0, 0);

    println!("{black:?} {origin:?}");
    println!("红色分量：{}", black.0);
}
```

即使字段类型完全相同，`Color` 和 `Point` 也是不同类型，不能混用。

适合场景：

- 为基础类型增加业务语义
- 字段很少且位置含义清楚
- 创建“新类型”防止单位或标识符混用

```rust
struct UserId(u64);
struct OrderId(u64);
```

## 3.2 单元结构体

没有字段的结构体：

```rust
struct ConsoleLogger;

impl ConsoleLogger {
    fn log(&self, message: &str) {
        println!("[LOG] {message}");
    }
}
```

单元结构体适合表示只携带类型行为、不保存实例数据的对象，也可作为 Trait 的实现载体。

---

## 四、方法与关联函数

使用 `impl` 为类型定义行为。

```rust
#[derive(Debug)]
struct Rectangle {
    width: f64,
    height: f64,
}

impl Rectangle {
    fn area(&self) -> f64 {
        self.width * self.height
    }

    fn perimeter(&self) -> f64 {
        2.0 * (self.width + self.height)
    }

    fn can_hold(&self, other: &Rectangle) -> bool {
        self.width > other.width && self.height > other.height
    }
}

fn main() {
    let large = Rectangle {
        width: 30.0,
        height: 20.0,
    };
    let small = Rectangle {
        width: 10.0,
        height: 5.0,
    };

    println!("面积：{}", large.area());
    println!("能容纳：{}", large.can_hold(&small));
}
```

## 4.1 `self` 的三种常见形式

| 接收者 | 含义 | 常见用途 |
|---|---|---|
| `&self` | 不可变借用实例 | 查询、计算、展示 |
| `&mut self` | 可变借用实例 | 修改字段 |
| `self` | 取得实例所有权 | 转换、消费、移交资源 |

```rust
struct Counter {
    value: u64,
}

impl Counter {
    fn value(&self) -> u64 {
        self.value
    }

    fn increment(&mut self) {
        self.value += 1;
    }

    fn into_value(self) -> u64 {
        self.value
    }
}
```

方法签名就是所有权契约：调用 `into_value` 后原实例被消费，不能再使用。

## 4.2 关联函数

不接收 `self` 的函数称为关联函数，通常用作构造器：

```rust
impl Rectangle {
    fn new(width: f64, height: f64) -> Self {
        Self { width, height }
    }

    fn square(size: f64) -> Self {
        Self {
            width: size,
            height: size,
        }
    }
}

fn main() {
    let rectangle = Rectangle::new(8.0, 6.0);
    let square = Rectangle::square(5.0);
    println!("{rectangle:?} {square:?}");
}
```

`Self` 表示当前正在实现的类型。关联函数使用 `Type::function()` 调用。

## 4.3 构造时维护不变量

如果宽高必须为正，可以让构造器返回 `Option`：

```rust
impl Rectangle {
    fn try_new(width: f64, height: f64) -> Option<Self> {
        if width > 0.0 && height > 0.0 {
            Some(Self { width, height })
        } else {
            None
        }
    }
}
```

更详细的错误可使用 `Result`，后文会介绍。

---

## 五、枚举基础

枚举定义一个值可能属于的有限种变体。

```rust
#[derive(Debug)]
enum Direction {
    North,
    South,
    East,
    West,
}

fn move_one_step(direction: Direction) {
    println!("向 {direction:?} 移动一步");
}
```

一个 `Direction` 值同一时刻只能是四个变体之一。

## 5.1 变体携带数据

每个变体可以携带不同形状的数据：

```rust
#[derive(Debug)]
enum Message {
    Quit,
    Move { x: i32, y: i32 },
    Write(String),
    ChangeColor(u8, u8, u8),
}
```

这相当于把多种相关数据形态统一在一个类型中：

- `Quit`：无数据
- `Move`：具名字段
- `Write`：一个字符串
- `ChangeColor`：三个位置字段

```rust
impl Message {
    fn describe(&self) {
        match self {
            Message::Quit => println!("退出"),
            Message::Move { x, y } => println!("移动到 ({x}, {y})"),
            Message::Write(text) => println!("文本：{text}"),
            Message::ChangeColor(r, g, b) => {
                println!("颜色：rgb({r}, {g}, {b})");
            }
        }
    }
}
```

## 5.2 结构体还是枚举

- 多个字段同时构成一个实体：结构体
- 一个值只可能处于若干互斥状态之一：枚举
- 各状态携带的数据不同：带数据的枚举

错误设计：

```rust
struct RequestState {
    is_pending: bool,
    is_running: bool,
    is_finished: bool,
}
```

它允许多个布尔值同时为真，形成非法状态。更合适：

```rust
enum RequestState {
    Pending,
    Running,
    Finished,
}
```

让非法状态无法被构造，是类型设计的重要目标。

---

## 六、`Option<T>`：明确表达值可能缺失

Rust 没有普通空值。标准库使用枚举表达“有值或无值”：

```rust
enum Option<T> {
    Some(T),
    None,
}
```

## 6.1 基本使用

```rust
fn find_even(numbers: &[i32]) -> Option<i32> {
    for number in numbers {
        if number % 2 == 0 {
            return Some(*number);
        }
    }

    None
}

fn main() {
    match find_even(&[1, 3, 8, 9]) {
        Some(value) => println!("找到偶数：{value}"),
        None => println!("没有偶数"),
    }
}
```

`Option<i32>` 与 `i32` 是不同类型，使用前必须处理 `None` 情况。

## 6.2 常用方法

```rust
fn main() {
    let name = Some("Alice");
    let missing: Option<&str> = None;

    println!("是否有值：{}", name.is_some());
    println!("是否为空：{}", missing.is_none());
    println!("默认值：{}", missing.unwrap_or("匿名用户"));

    let length = name.map(str::len);
    println!("名称长度：{length:?}");
}
```

常用组合：

- `map`：有值时转换内部值
- `and_then`：有值时继续执行可能返回 `Option` 的操作
- `filter`：保留满足条件的内部值
- `unwrap_or`：无值时使用已有默认值
- `unwrap_or_else`：无值时通过闭包计算默认值
- `as_ref`：把 `Option<T>` 转成 `Option<&T>`，避免移动内部值

## 6.3 避免草率 `unwrap()`

```rust,should_panic
fn main() {
    let value: Option<i32> = None;
    println!("{}", value.unwrap());
}
```

`unwrap()` 在 `None` 时 panic。只有在不变量已经由代码严格保证，或临时示例、测试中，才应谨慎使用。业务输入、文件内容、网络响应等不确定数据应显式处理。

## 6.4 `as_ref` 避免移动

```rust
fn print_optional_name(name: &Option<String>) {
    match name.as_ref() {
        Some(value) => println!("{value}"),
        None => println!("没有名称"),
    }
}
```

直接按值匹配拥有的 `Option<String>` 可能移动内部 `String`；借用或使用 `as_ref()` 可以只读取。

---

## 七、`Result<T, E>`：明确表达操作可能失败

```rust
enum Result<T, E> {
    Ok(T),
    Err(E),
}
```

`Ok(T)` 携带成功值，`Err(E)` 携带错误信息。

## 7.1 解析示例

```rust
fn parse_age(input: &str) -> Result<u8, String> {
    let age: u8 = match input.trim().parse() {
        Ok(value) => value,
        Err(_) => return Err(String::from("年龄必须是 0～255 的整数")),
    };

    if age > 150 {
        Err(String::from("年龄不能超过 150"))
    } else {
        Ok(age)
    }
}

fn main() {
    match parse_age("42") {
        Ok(age) => println!("年龄：{age}"),
        Err(message) => eprintln!("输入错误：{message}"),
    }
}
```

## 7.2 `?` 运算符初步

`?` 在成功时取出 `Ok` 内部值，在失败时提前返回错误：

```rust
use std::num::ParseIntError;

fn add_text_numbers(left: &str, right: &str) -> Result<i32, ParseIntError> {
    let left: i32 = left.parse()?;
    let right: i32 = right.parse()?;
    Ok(left + right)
}
```

它近似于对 `Result` 执行匹配并传播错误，但还会进行兼容的错误转换。系统错误设计和自定义错误类型将在第六阶段深入学习。

## 7.3 `Option` 与 `Result` 的选择

| 情况 | 推荐类型 |
|---|---|
| 值可能不存在，缺失本身不需要原因 | `Option<T>` |
| 操作可能失败，调用方需要错误原因 | `Result<T, E>` |
| 必须同时区分“未找到”和其他错误 | 通常 `Result<Option<T>, E>` |

例如查找缓存：未命中可能是正常情况，使用 `Option`；读取文件失败需要保留原因，使用 `Result`。

---

## 八、`match` 深入

`match` 会按顺序测试分支，第一个匹配成功的分支被执行。

## 8.1 匹配字面量、范围和多个模式

```rust
fn describe(number: i32) -> &'static str {
    match number {
        0 => "零",
        1 | 2 => "一或二",
        3..=9 => "三到九",
        _ => "其他",
    }
}
```

## 8.2 解构结构体

```rust
#[derive(Debug)]
struct Point {
    x: i32,
    y: i32,
}

fn describe_point(point: Point) {
    match point {
        Point { x: 0, y: 0 } => println!("原点"),
        Point { x: 0, y } => println!("Y 轴：{y}"),
        Point { x, y: 0 } => println!("X 轴：{x}"),
        Point { x, y } => println!("普通点：({x}, {y})"),
    }
}
```

字段名和绑定变量同名时可简写为 `{ x, y }`。

## 8.3 解构枚举

```rust
fn handle(message: Message) {
    match message {
        Message::Quit => println!("退出程序"),
        Message::Move { x, y } => println!("移动到 {x}, {y}"),
        Message::Write(text) => println!("写入：{text}"),
        Message::ChangeColor(red, green, blue) => {
            println!("颜色：{red}, {green}, {blue}");
        }
    }
}
```

## 8.4 嵌套解构

```rust
enum Event {
    Click { position: (i32, i32) },
    Key(char),
}

fn handle_event(event: Event) {
    match event {
        Event::Click { position: (0, 0) } => println!("点击原点"),
        Event::Click { position: (x, y) } => println!("点击 ({x}, {y})"),
        Event::Key('q') => println!("请求退出"),
        Event::Key(key) => println!("按键：{key}"),
    }
}
```

## 8.5 匹配守卫

```rust
fn classify(value: Option<i32>) {
    match value {
        Some(number) if number < 0 => println!("负数：{number}"),
        Some(number) if number % 2 == 0 => println!("非负偶数：{number}"),
        Some(number) => println!("非负奇数：{number}"),
        None => println!("没有值"),
    }
}
```

守卫不参与穷尽性证明，所以仍需覆盖其余情况。

## 8.6 `@` 绑定

`@` 允许验证模式的同时绑定完整值：

```rust
fn describe_age(age: u8) {
    match age {
        child @ 0..=12 => println!("儿童：{child} 岁"),
        teenager @ 13..=17 => println!("青少年：{teenager} 岁"),
        adult => println!("成年人：{adult} 岁"),
    }
}
```

## 8.7 匹配引用与所有权

按值匹配可能移动字段：

```rust
fn consume_message(message: Message) {
    match message {
        Message::Write(text) => println!("{text}"),
        _ => println!("其他消息"),
    }
}
```

只想读取时匹配引用：

```rust
fn inspect_message(message: &Message) {
    match message {
        Message::Write(text) => println!("{text}"),
        _ => println!("其他消息"),
    }
}
```

匹配工效会根据被匹配值是所有权值还是引用，自动采用相应的借用绑定。遇到移动错误时先检查 `match` 的输入是否应该写成引用。

---

## 九、忽略模式

## 9.1 `_` 忽略整个值

```rust
match some_value {
    Some(value) => println!("{value}"),
    None => (),
}
```

也可写成 `_ => ()` 忽略所有剩余情况。

## 9.2 `_name` 与 `_` 的差异

以下划线开头的变量仍会绑定值，可能发生移动：

```rust,compile_fail
fn main() {
    let value = Some(String::from("hello"));

    if let Some(_text) = value {
        println!("有文本");
    }

    println!("{value:?}");
}
```

若只想忽略内部值，使用 `_`：

```rust
fn main() {
    let value = Some(String::from("hello"));

    if let Some(_) = value {
        println!("有文本");
    }

    println!("{value:?}");
}
```

## 9.3 `..` 忽略剩余部分

```rust
struct Configuration {
    host: String,
    port: u16,
    retries: u8,
    verbose: bool,
}

fn print_address(config: &Configuration) {
    let Configuration { host, port, .. } = config;
    println!("{host}:{port}");
}
```

元组中也能使用 `..`，但每个模式中只能出现一次，以免产生歧义。

---

## 十、`if let`、`let else` 与 `while let`

## 10.1 `if let`

只关心一种模式时：

```rust
fn print_name(name: Option<&str>) {
    if let Some(value) = name {
        println!("名称：{value}");
    }
}
```

可以带 `else`，但分支增多后应考虑 `match`。

## 10.2 `let else`

`let else` 适合先处理失败路径，让主要逻辑保持平直：

```rust
fn print_first(numbers: &[i32]) {
    let Some(first) = numbers.first() else {
        println!("列表为空");
        return;
    };

    println!("第一个数字：{first}");
}
```

`else` 分支必须发散，即通过 `return`、`break`、`continue` 或 panic 等方式结束当前控制流。

解析示例：

```rust
fn double_input(input: &str) -> Option<i32> {
    let Ok(number) = input.trim().parse::<i32>() else {
        return None;
    };

    Some(number * 2)
}
```

## 10.3 `while let`

```rust
fn main() {
    let mut stack = vec![1, 2, 3];

    while let Some(value) = stack.pop() {
        println!("{value}");
    }
}
```

适合重复处理“只要仍有值”的状态。

## 10.4 选择建议

| 需求 | 写法 |
|---|---|
| 完整处理所有分支 | `match` |
| 只处理一个模式 | `if let` |
| 失败时立即退出，成功值供后续使用 | `let else` |
| 模式持续匹配时循环 | `while let` |

---

## 十一、用枚举实现状态机

以文章发布流程为例：草稿只能提交审核，审核中的文章可以通过或驳回，已发布文章不能回到草稿。

```rust
#[derive(Debug)]
enum ArticleState {
    Draft,
    InReview { reviewer: String },
    Published { published_at: String },
    Rejected { reason: String },
}

#[derive(Debug)]
struct Article {
    title: String,
    body: String,
    state: ArticleState,
}

impl Article {
    fn new(title: String, body: String) -> Self {
        Self {
            title,
            body,
            state: ArticleState::Draft,
        }
    }

    fn submit(&mut self, reviewer: String) -> Result<(), String> {
        match self.state {
            ArticleState::Draft | ArticleState::Rejected { .. } => {
                self.state = ArticleState::InReview { reviewer };
                Ok(())
            }
            _ => Err(String::from("只有草稿或被驳回的文章可以提交审核")),
        }
    }

    fn publish(&mut self, published_at: String) -> Result<(), String> {
        if matches!(self.state, ArticleState::InReview { .. }) {
            self.state = ArticleState::Published { published_at };
            Ok(())
        } else {
            Err(String::from("只有审核中的文章可以发布"))
        }
    }

    fn reject(&mut self, reason: String) -> Result<(), String> {
        if matches!(self.state, ArticleState::InReview { .. }) {
            self.state = ArticleState::Rejected { reason };
            Ok(())
        } else {
            Err(String::from("只有审核中的文章可以驳回"))
        }
    }
}
```

`matches!` 适合只判断是否符合某个模式而不需要取出字段。

这个设计仍允许直接构造任意 `ArticleState`，但已经比多个布尔字段清晰得多。更严格的“类型状态模式”会在掌握泛型后继续学习。

---

## 十二、建模案例：支付结果

不推荐：

```rust
struct Payment {
    success: bool,
    transaction_id: Option<String>,
    error_message: Option<String>,
}
```

它允许 `success = true` 却没有交易号，也允许成功时同时有错误信息。

更准确：

```rust
enum PaymentResult {
    Succeeded { transaction_id: String },
    Declined { reason: String },
    Pending { check_after_seconds: u64 },
}
```

每个变体只携带该状态真正需要的数据。处理时 `match` 迫使调用者考虑全部结果。

建模步骤：

1. 列出实体同时拥有的共同数据。
2. 列出互斥状态。
3. 确定每种状态独有的数据。
4. 删除无法解释的可选字段组合。
5. 用构造器和方法维护业务不变量。

---

## 十三、阶段项目：待办事项 CLI

## 13.1 功能需求

- 新增任务
- 列出全部、待办或已完成任务
- 标记任务完成
- 删除任务
- 显示任务详情
- 保存到文本文件并在启动时恢复
- 对不存在的任务、无效命令和损坏数据给出错误信息

为避免提前引入第三方序列化依赖，参考实现使用简单的制表符分隔格式。实际项目更适合在后续使用 JSON 和 `serde`。

## 13.2 数据模型

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum TaskStatus {
    Pending,
    Completed,
}

#[derive(Debug)]
struct Task {
    id: u64,
    title: String,
    status: TaskStatus,
}
```

状态使用枚举而不是 `bool`，以后可以自然增加 `Cancelled`、`Archived` 等状态。

## 13.3 完整参考实现

```rust
use std::env;
use std::fs;
use std::path::Path;
use std::process;

const DATA_FILE: &str = "tasks.db";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum TaskStatus {
    Pending,
    Completed,
}

impl TaskStatus {
    fn as_str(self) -> &'static str {
        match self {
            Self::Pending => "pending",
            Self::Completed => "completed",
        }
    }

    fn parse(input: &str) -> Result<Self, String> {
        match input {
            "pending" => Ok(Self::Pending),
            "completed" => Ok(Self::Completed),
            _ => Err(format!("未知任务状态：{input}")),
        }
    }
}

#[derive(Debug)]
struct Task {
    id: u64,
    title: String,
    status: TaskStatus,
}

impl Task {
    fn new(id: u64, title: String) -> Result<Self, String> {
        let title = title.trim();

        if title.is_empty() {
            return Err(String::from("任务标题不能为空"));
        }

        if title.contains(|character| matches!(character, '\t' | '\n' | '\r')) {
            return Err(String::from("任务标题不能包含制表符或换行符"));
        }

        Ok(Self {
            id,
            title: title.to_string(),
            status: TaskStatus::Pending,
        })
    }

    fn complete(&mut self) -> Result<(), String> {
        match self.status {
            TaskStatus::Pending => {
                self.status = TaskStatus::Completed;
                Ok(())
            }
            TaskStatus::Completed => Err(format!("任务 {} 已经完成", self.id)),
        }
    }

    fn serialize(&self) -> String {
        format!("{}\t{}\t{}", self.id, self.status.as_str(), self.title)
    }

    fn parse(line: &str) -> Result<Self, String> {
        let mut fields = line.splitn(3, '\t');

        let Some(id_text) = fields.next() else {
            return Err(String::from("缺少任务编号"));
        };
        let Some(status_text) = fields.next() else {
            return Err(String::from("缺少任务状态"));
        };
        let Some(title) = fields.next() else {
            return Err(String::from("缺少任务标题"));
        };

        let id = id_text
            .parse::<u64>()
            .map_err(|_| format!("无效任务编号：{id_text}"))?;
        let status = TaskStatus::parse(status_text)?;

        Ok(Self {
            id,
            title: title.to_string(),
            status,
        })
    }
}

#[derive(Debug, Default)]
struct TaskList {
    tasks: Vec<Task>,
}

impl TaskList {
    fn load(path: &str) -> Result<Self, String> {
        if !Path::new(path).exists() {
            return Ok(Self::default());
        }

        let content = fs::read_to_string(path)
            .map_err(|error| format!("读取数据文件失败：{error}"))?;
        let mut tasks = Vec::new();

        for (index, line) in content.lines().enumerate() {
            if line.trim().is_empty() {
                continue;
            }

            let task = Task::parse(line)
                .map_err(|error| format!("第 {} 行数据损坏：{error}", index + 1))?;
            tasks.push(task);
        }

        Ok(Self { tasks })
    }

    fn save(&self, path: &str) -> Result<(), String> {
        let content = self
            .tasks
            .iter()
            .map(Task::serialize)
            .collect::<Vec<_>>()
            .join("\n");

        fs::write(path, content).map_err(|error| format!("保存数据失败：{error}"))
    }

    fn next_id(&self) -> u64 {
        self.tasks
            .iter()
            .map(|task| task.id)
            .max()
            .unwrap_or(0)
            + 1
    }

    fn add(&mut self, title: String) -> Result<u64, String> {
        let task = Task::new(self.next_id(), title)?;
        let id = task.id;
        self.tasks.push(task);
        Ok(id)
    }

    fn find(&self, id: u64) -> Option<&Task> {
        self.tasks.iter().find(|task| task.id == id)
    }

    fn find_mut(&mut self, id: u64) -> Option<&mut Task> {
        self.tasks.iter_mut().find(|task| task.id == id)
    }

    fn complete(&mut self, id: u64) -> Result<(), String> {
        let Some(task) = self.find_mut(id) else {
            return Err(format!("没有编号为 {id} 的任务"));
        };
        task.complete()
    }

    fn remove(&mut self, id: u64) -> Result<Task, String> {
        let Some(index) = self.tasks.iter().position(|task| task.id == id) else {
            return Err(format!("没有编号为 {id} 的任务"));
        };
        Ok(self.tasks.remove(index))
    }

    fn print(&self, filter: Option<TaskStatus>) {
        let mut count = 0;

        for task in &self.tasks {
            if filter.is_some_and(|status| task.status != status) {
                continue;
            }

            let mark = match task.status {
                TaskStatus::Pending => " ",
                TaskStatus::Completed => "x",
            };
            println!("[{}] {:>4}  {}", mark, task.id, task.title);
            count += 1;
        }

        if count == 0 {
            println!("没有符合条件的任务");
        }
    }
}

enum Command {
    Add { title: String },
    List { filter: Option<TaskStatus> },
    Show { id: u64 },
    Complete { id: u64 },
    Remove { id: u64 },
    Help,
}

fn parse_id(input: Option<&String>) -> Result<u64, String> {
    let Some(input) = input else {
        return Err(String::from("缺少任务编号"));
    };
    input
        .parse::<u64>()
        .map_err(|_| format!("无效任务编号：{input}"))
}

fn parse_command(arguments: &[String]) -> Result<Command, String> {
    let Some(command) = arguments.first().map(String::as_str) else {
        return Ok(Command::Help);
    };

    match command {
        "add" => {
            if arguments.len() < 2 {
                return Err(String::from("add 后需要任务标题"));
            }
            Ok(Command::Add {
                title: arguments[1..].join(" "),
            })
        }
        "list" => {
            let filter = match arguments.get(1).map(String::as_str) {
                None | Some("all") => None,
                Some("pending") => Some(TaskStatus::Pending),
                Some("completed") => Some(TaskStatus::Completed),
                Some(value) => return Err(format!("未知筛选条件：{value}")),
            };
            Ok(Command::List { filter })
        }
        "show" => Ok(Command::Show {
            id: parse_id(arguments.get(1))?,
        }),
        "complete" => Ok(Command::Complete {
            id: parse_id(arguments.get(1))?,
        }),
        "remove" => Ok(Command::Remove {
            id: parse_id(arguments.get(1))?,
        }),
        "help" | "--help" | "-h" => Ok(Command::Help),
        value => Err(format!("未知命令：{value}")),
    }
}

fn print_help(program: &str) {
    println!("用法：");
    println!("  {program} add <标题>");
    println!("  {program} list [all|pending|completed]");
    println!("  {program} show <编号>");
    println!("  {program} complete <编号>");
    println!("  {program} remove <编号>");
}

fn run() -> Result<(), String> {
    let arguments: Vec<String> = env::args().collect();
    let program = arguments.first().map(String::as_str).unwrap_or("todo");
    let command = parse_command(&arguments[1..])?;
    let mut list = TaskList::load(DATA_FILE)?;

    match command {
        Command::Add { title } => {
            let id = list.add(title)?;
            list.save(DATA_FILE)?;
            println!("已创建任务 {id}");
        }
        Command::List { filter } => list.print(filter),
        Command::Show { id } => match list.find(id) {
            Some(task) => println!("{task:#?}"),
            None => return Err(format!("没有编号为 {id} 的任务")),
        },
        Command::Complete { id } => {
            list.complete(id)?;
            list.save(DATA_FILE)?;
            println!("任务 {id} 已完成");
        }
        Command::Remove { id } => {
            let task = list.remove(id)?;
            list.save(DATA_FILE)?;
            println!("已删除任务 {}：{}", task.id, task.title);
        }
        Command::Help => print_help(program),
    }

    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("错误：{error}");
        process::exit(1);
    }
}
```

## 13.4 运行示例

```bash
cargo run -- add 学习 Rust 枚举
cargo run -- add 完成第三阶段练习
cargo run -- list
cargo run -- complete 1
cargo run -- list completed
cargo run -- show 2
cargo run -- remove 2
```

## 13.5 项目中的类型设计

- `Task`：编号、标题、状态同时构成任务，使用结构体
- `TaskStatus`：一个任务只能处于一种状态，使用枚举
- `Command`：一次只执行一种命令，每个命令参数不同，使用带数据枚举
- `Option<&Task>`：查找可能没有结果，缺失不需要底层错误原因
- `Result<T, String>`：解析、状态转换、文件读写可能失败，需要说明原因
- `let else`：处理缺少参数或找不到任务后立即返回
- `match`：完整分派命令和状态

## 13.6 所有权分析

- 新增命令拥有 `title: String`，执行时把标题移动给 `TaskList::add`
- `Task::new` 清理标题后创建自己拥有的 `String`
- `find` 返回 `Option<&Task>`，只借用列表
- `find_mut` 返回 `Option<&mut Task>`，允许修改列表中的任务
- `remove` 返回拥有所有权的 `Task`，因为任务已离开列表
- `TaskStatus` 很小且无资源，派生 `Copy`，筛选和匹配不会消耗状态

## 13.7 扩展任务

1. 增加优先级枚举 `Low`、`Medium`、`High`。
2. 增加截止日期 `Option<String>`。
3. 增加 `edit` 命令。
4. 增加 `reopen`，只允许已完成任务恢复待办。
5. 增加 `clear-completed`。
6. 保存前先写临时文件，再替换旧文件，降低数据损坏风险。
7. 将 `String` 错误升级为自定义错误枚举。
8. 把业务逻辑放入 `src/lib.rs`，命令行解析留在 `main.rs`。
9. 为构造、解析、完成、删除编写测试。
10. 后续使用 `serde` 将文件格式升级为 JSON。

---

## 十四、常见错误与分析

## 14.1 匹配不完整

枚举新增变体后，完整 `match` 会产生编译错误，提示尚未覆盖的模式。这是优点：编译器帮助定位所有需要适配新状态的位置。

不要为了快速通过而无思考地加 `_`。如果每种变体业务处理不同，应显式列出全部分支。

## 14.2 匹配导致字段移动

```rust,compile_fail
#[derive(Debug)]
struct User {
    name: String,
    age: u8,
}

fn main() {
    let user = User {
        name: String::from("Alice"),
        age: 20,
    };

    let User { name, .. } = user;
    println!("{name}");
    println!("{user:?}");
}
```

`name` 被移动出结构体，`user` 不再是完整值。只需读取时使用引用模式或解构引用：

```rust
let User { name, .. } = &user;
println!("{name}");
println!("{user:?}");
```

## 14.3 过度使用可选字段

多个 `Option` 字段可能形成大量无效组合。检查它们是否代表互斥状态；如果是，改为带数据枚举。

## 14.4 使用 `unwrap()` 假装错误不存在

用户输入、文件和网络结果都可能失败。`unwrap()` 会把可恢复错误变成进程 panic。应使用 `match`、`?`、`let else` 或提供合理默认值。

## 14.5 构造器没有维护不变量

如果任务标题不能为空，就不应允许公共构造路径创建空标题。让 `new` 返回 `Result<Self, E>`，在边界完成校验。

## 14.6 用字符串表示有限状态

```rust
struct Task {
    status: String,
}
```

它允许拼写错误和任意无效值。状态集合有限时使用枚举，解析只发生在输入边界。

---

## 十五、针对性练习

### 15.1 矩形

实现 `Rectangle`：

- 构造时宽高必须大于零
- `area`、`perimeter`
- `contains_point(x, y)`
- `intersects(other)` 判断两个轴对齐矩形是否相交

### 15.2 图形枚举

```rust
enum Shape {
    Circle { radius: f64 },
    Rectangle { width: f64, height: f64 },
    Triangle { base: f64, height: f64 },
}
```

实现面积计算和输入校验。思考构造非法图形时返回 `Option` 还是 `Result`。

### 15.3 安全解析器

解析 `"name:age"`：

```rust
struct Person {
    name: String,
    age: u8,
}

fn parse_person(input: &str) -> Result<Person, String>
```

处理缺少冒号、空姓名、年龄格式错误、年龄超出范围。

### 15.4 配置状态

设计枚举表达：未加载、加载成功、加载失败。每个状态只携带必要数据，并实现描述方法。

### 15.5 部分移动

编写包含两个 `String` 字段和一个 `u32` 字段的结构体，分别尝试：

- 按值解构
- 按引用解构
- 只移动一个字段
- 在部分移动后访问 `Copy` 字段

记录哪些代码能通过编译及原因。

### 15.6 状态机

实现订单状态：

```text
Created -> Paid -> Shipped -> Delivered
             \-> Refunded
Created -> Cancelled
```

非法转换返回 `Result`，每种状态携带适当数据，例如支付编号、物流单号或退款原因。

---

## 十六、知识自测

尝试不查资料回答：

1. 结构体适合表达什么关系？枚举适合表达什么关系？
2. 结构体更新语法可能导致哪些字段移动？
3. `&self`、`&mut self`、`self` 分别表达什么所有权契约？
4. 方法和关联函数有什么区别？
5. 为什么构造器有时应返回 `Result<Self, E>`？
6. 枚举的不同变体可以携带不同类型和数量的数据吗？
7. 多个布尔状态字段为何容易产生非法状态？
8. `Option<T>` 与 `T` 有何区别？
9. `Option<T>` 和 `Result<T, E>` 如何选择？
10. `map` 和 `and_then` 在处理 `Option` 时有何思路差别？
11. `?` 对 `Result` 做了什么？
12. 为什么 `match` 必须穷尽所有可能？
13. 匹配守卫是否能替代穷尽分支？
14. `@` 绑定有什么用途？
15. `_name` 与 `_` 是否完全相同？
16. `if let`、`let else`、`while let` 分别适合什么情况？
17. 按值匹配 `Option<String>` 可能发生什么？
18. 如何只借用结构体字段而不移动它？
19. 为什么 `Command` 适合设计成枚举？
20. 如何通过类型设计让非法状态难以构造？

---

## 十七、阶段验收清单

### 结构体与方法

- [ ] 能定义普通、元组和单元结构体
- [ ] 能解释字段初始化简写和更新语法
- [ ] 能分析结构体字段的移动与借用
- [ ] 能合理选择 `&self`、`&mut self` 和 `self`
- [ ] 能使用关联函数实现构造器
- [ ] 能在构造边界维护数据不变量

### 枚举与错误表达

- [ ] 能用枚举替代互斥布尔状态
- [ ] 能让枚举变体携带各自所需的数据
- [ ] 能使用 `Option` 表达正常缺失
- [ ] 能使用 `Result` 表达带原因的失败
- [ ] 不对不可信输入草率调用 `unwrap()`
- [ ] 能使用 `?` 传播兼容错误

### 模式匹配

- [ ] 能解构结构体、元组、枚举和嵌套数据
- [ ] 能使用字面量、范围、`|` 和守卫
- [ ] 能使用 `@` 绑定和 `..` 忽略字段
- [ ] 理解 `_name` 仍可能移动值
- [ ] 能按场景选择 `match`、`if let`、`let else`、`while let`
- [ ] 能避免因按值匹配导致意外移动

### 项目能力

- [ ] 完成待办事项 CLI 的新增、列表、完成、删除和详情功能
- [ ] 能从文件恢复任务并保存修改
- [ ] `TaskStatus` 和 `Command` 使用枚举建模
- [ ] 查找使用 `Option`，失败操作使用 `Result`
- [ ] 无效输入不会导致 panic
- [ ] 能解释项目中每个结构体和枚举的设计理由
- [ ] 至少完成三个扩展任务

### 质量检查

```bash
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
cargo run -- help
```

当你能够根据一段业务描述识别“共同字段”“互斥状态”“可选值”和“失败原因”，并将它们分别建模为结构体、枚举、`Option` 和 `Result`，即可进入第四阶段：集合、迭代器与闭包。

---

## 十八、进入下一阶段前的重构任务

1. 把待办列表的查找、筛选和排序改写为迭代器操作。
2. 观察 `iter`、`iter_mut`、`into_iter` 对所有权的影响。
3. 使用 `HashMap<TaskStatus, usize>` 统计各状态任务数量。
4. 为命令处理注册不同闭包，观察闭包如何捕获环境。
5. 分析 `collect()` 如何从迭代器创建不同集合。
6. 保留当前循环版本，后续比较普通循环和迭代器链的可读性。

第四阶段将从“如何建模单个值”转向“如何组织并处理一组值”，重点学习 `Vec<T>`、`HashMap<K, V>`、迭代器、闭包以及 `Fn`、`FnMut`、`FnOnce`。
