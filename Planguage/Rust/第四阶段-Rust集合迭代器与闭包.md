# 第四阶段：Rust 集合、迭代器与闭包

> 本阶段从“建模单个值”进入“组织和处理一组值”。重点不是记住所有 API，而是掌握集合选择、迭代器的数据流、所有权变化，以及闭包如何携带行为和环境。

## 一、阶段目标

完成本阶段后，你应当能够：

- 根据访问模式选择 `Vec<T>`、`HashMap<K, V>`、`HashSet<T>` 或 `VecDeque<T>`
- 安全处理 UTF-8 字符串，而不是把字节下标误当字符下标
- 准确说明 `iter`、`iter_mut`、`into_iter` 对所有权的影响
- 使用 `map`、`filter`、`filter_map`、`flat_map`、`fold` 和 `collect`
- 理解迭代器惰性求值与消费器
- 使用闭包捕获环境
- 区分 `Fn`、`FnMut`、`FnOnce`
- 使用 `From`、`Into`、`TryFrom`、`TryInto` 表达类型转换
- 编写清晰、不过度复杂的迭代器处理流程
- 完成支持筛选、统计与摘要输出的日志分析器

---

## 二、集合选择总览

Rust 标准库常用集合：

| 集合 | 核心特点 | 典型用途 |
|---|---|---|
| `Vec<T>` | 连续、可增长、按索引访问 | 列表、批量处理、栈 |
| `HashMap<K, V>` | 键到值的映射 | 索引、计数、缓存、配置 |
| `HashSet<T>` | 不重复元素集合 | 去重、成员判断、集合运算 |
| `VecDeque<T>` | 两端高效插入删除 | 队列、滑动窗口、BFS |

选择时先问：

1. 是否需要保持插入顺序？
2. 是否需要按整数索引访问？
3. 是否需要通过键快速查找？
4. 元素是否必须唯一？
5. 是否频繁从头部插入或删除？

不要默认所有数据都用 `Vec`，也不要因为哈希查找平均较快就把所有列表改成 `HashMap`。数据规模、顺序要求、内存开销和访问模式都很重要。

---

## 三、`Vec<T>` 动态数组

## 3.1 创建与修改

```rust
fn main() {
    let mut numbers = Vec::new();
    numbers.push(10);
    numbers.push(20);
    numbers.push(30);

    let initialized = vec![1, 2, 3, 4];

    println!("{numbers:?}");
    println!("{initialized:?}");
}
```

当编译器无法从后续代码推断元素类型时需显式标注：

```rust
let names: Vec<String> = Vec::new();
```

常用操作：

```rust
fn main() {
    let mut values = vec![10, 20, 30];

    values.push(40);
    let last = values.pop();
    values.insert(1, 15);
    let removed = values.remove(0);

    println!("last={last:?}, removed={removed}");
    println!("len={}, capacity={}", values.len(), values.capacity());
}
```

`insert` 和 `remove` 可能移动其后大量元素；频繁操作头部时考虑 `VecDeque`。

## 3.2 索引与安全访问

```rust
fn main() {
    let values = vec![10, 20, 30];

    let second = values[1];
    let maybe_value = values.get(8);

    println!("{second}");
    match maybe_value {
        Some(value) => println!("{value}"),
        None => println!("索引越界"),
    }
}
```

- `values[index]`：越界时 panic
- `values.get(index)`：返回 `Option<&T>`

索引来自用户输入或计算结果时，优先使用 `get`。

## 3.3 容量与重新分配

`Vec` 的长度是当前元素数，容量是无需重新分配即可容纳的元素数。

```rust
let mut values = Vec::with_capacity(100);
for number in 0..100 {
    values.push(number);
}
```

提前知道大致元素数时，`with_capacity` 可以减少重新分配。不要盲目预留巨大容量。

`push` 可能重新分配缓冲区，因此持有元素引用时不能同时修改 `Vec`：

```rust,compile_fail
let mut values = vec![1, 2, 3];
let first = &values[0];
values.push(4);
println!("{first}");
```

## 3.4 使用枚举存放不同形态

`Vec` 元素必须同类型，但枚举能统一多种变体：

```rust
#[derive(Debug)]
enum Cell {
    Integer(i64),
    Decimal(f64),
    Text(String),
}

fn main() {
    let row = vec![
        Cell::Integer(42),
        Cell::Decimal(3.14),
        Cell::Text(String::from("Rust")),
    ];
    println!("{row:?}");
}
```

---

## 四、`HashMap<K, V>` 键值映射

## 4.1 创建与插入

```rust
use std::collections::HashMap;

fn main() {
    let mut scores = HashMap::new();
    scores.insert(String::from("Alice"), 95);
    scores.insert(String::from("Bob"), 88);

    println!("{scores:?}");
}
```

键必须满足 `Eq + Hash`。整数、字符串等常用类型已实现。

## 4.2 所有权

```rust,compile_fail
use std::collections::HashMap;

fn main() {
    let name = String::from("Alice");
    let mut scores = HashMap::new();
    scores.insert(name, 95);
    println!("{name}");
}
```

`insert` 按值接收键和值，未实现 `Copy` 的值会移动进映射。若插入引用，映射的可用范围不能超过被引用数据。

## 4.3 查询

```rust
use std::collections::HashMap;

fn main() {
    let scores = HashMap::from([
        (String::from("Alice"), 95),
        (String::from("Bob"), 88),
    ]);

    match scores.get("Alice") {
        Some(score) => println!("Alice: {score}"),
        None => println!("未找到"),
    }

    println!("是否包含 Bob：{}", scores.contains_key("Bob"));
}
```

`HashMap<String, V>` 可以使用 `&str` 查询，这是借用式查询的常见便利。

## 4.4 更新策略

覆盖旧值：

```rust
scores.insert(String::from("Alice"), 100);
```

仅在键不存在时插入：

```rust
scores.entry(String::from("Carol")).or_insert(80);
```

根据旧值更新：

```rust
use std::collections::HashMap;

fn main() {
    let text = "red blue red green blue red";
    let mut counts = HashMap::new();

    for word in text.split_whitespace() {
        *counts.entry(word).or_insert(0) += 1;
    }

    println!("{counts:?}");
}
```

## 4.5 遍历顺序

标准 `HashMap` 不保证迭代顺序。需要稳定输出时，把结果收集到 `Vec` 后排序：

```rust
let mut entries: Vec<_> = counts.iter().collect();
entries.sort_by_key(|(word, _)| *word);
```

不要把某次运行观察到的顺序当作 API 保证。

---

## 五、`HashSet<T>` 集合

`HashSet` 只保存唯一值。

```rust
use std::collections::HashSet;

fn main() {
    let mut tags = HashSet::new();
    tags.insert("rust");
    tags.insert("backend");
    tags.insert("rust");

    println!("数量：{}", tags.len());
    println!("包含 rust：{}", tags.contains("rust"));
}
```

## 5.1 去重

```rust
use std::collections::HashSet;

fn main() {
    let values = vec![3, 1, 3, 2, 1, 4];
    let unique: HashSet<_> = values.into_iter().collect();
    println!("{unique:?}");
}
```

集合不保证原顺序。需要“保持首次出现顺序的去重”时，可同时使用 `HashSet` 做成员检查、`Vec` 保存输出。

## 5.2 集合运算

```rust
use std::collections::HashSet;

fn main() {
    let left = HashSet::from([1, 2, 3]);
    let right = HashSet::from([3, 4, 5]);

    println!("交集：{:?}", left.intersection(&right).collect::<Vec<_>>());
    println!("并集：{:?}", left.union(&right).collect::<Vec<_>>());
    println!("差集：{:?}", left.difference(&right).collect::<Vec<_>>());
}
```

这些方法返回迭代器，不立即创建新集合。

---

## 六、`VecDeque<T>` 双端队列

```rust
use std::collections::VecDeque;

fn main() {
    let mut queue = VecDeque::new();
    queue.push_back("task-1");
    queue.push_back("task-2");
    queue.push_front("urgent");

    while let Some(task) = queue.pop_front() {
        println!("处理：{task}");
    }
}
```

典型用途：

- 先进先出队列
- 广度优先搜索
- 滑动窗口
- 同时需要两端操作的缓冲区

如果只在尾部追加并按顺序遍历，`Vec` 通常更简单。

---

## 七、UTF-8 字符串处理

Rust 的 `String` 和 `str` 都保证有效 UTF-8。

```rust
fn main() {
    let text = "你好 Rust 🦀";

    println!("字节数：{}", text.len());
    println!("Unicode 标量数：{}", text.chars().count());
}
```

字节数不等于字符数。Rust 禁止 `text[0]`，因为一个数字索引无法明确表示字节、Unicode 标量还是用户感知字符。

## 7.1 字节遍历

```rust
for byte in "Rust".bytes() {
    println!("{byte}");
}
```

适合协议、编码、ASCII 检查等按原始字节处理的场景。

## 7.2 `char` 遍历

```rust
for character in "你好Rust".chars() {
    println!("{character}");
}
```

`char` 是 Unicode 标量值。一个用户看到的字形仍可能由多个 `char` 组合。

## 7.3 安全切片

```rust
fn first_character(text: &str) -> Option<char> {
    text.chars().next()
}
```

直接按字节范围切片只有在确定字符边界时才安全。普通文本处理优先使用 `chars`、`char_indices`、`split` 等 API。

```rust
fn first_n_chars(text: &str, count: usize) -> &str {
    let end = text
        .char_indices()
        .nth(count)
        .map(|(index, _)| index)
        .unwrap_or(text.len());
    &text[..end]
}
```

## 7.4 常用字符串处理

```rust
fn main() {
    let text = "  Rust,fast,safe  ";

    println!("{}", text.trim());
    println!("{}", text.to_lowercase());
    println!("{}", text.replace("safe", "reliable"));

    for part in text.trim().split(',') {
        println!("{}", part.trim());
    }
}
```

`trim`、`split` 通常返回借用切片；`to_lowercase`、`replace` 需要创建新的 `String`。

---

## 八、迭代器基础

迭代器按需产生一系列元素。核心 Trait 可简化理解为：

```rust,ignore
trait Iterator {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}
```

## 8.1 `next` 消耗迭代状态

```rust
fn main() {
    let values = vec![10, 20, 30];
    let mut iterator = values.iter();

    assert_eq!(iterator.next(), Some(&10));
    assert_eq!(iterator.next(), Some(&20));
    assert_eq!(iterator.next(), Some(&30));
    assert_eq!(iterator.next(), None);
}
```

`next` 需要 `&mut self`，因为每次调用都会改变迭代器当前位置。

## 8.2 惰性求值

```rust
let iterator = (1..=5).map(|number| {
    println!("处理 {number}");
    number * 2
});
```

仅创建迭代器不会执行闭包。添加消费操作后才开始处理：

```rust
let doubled: Vec<_> = iterator.collect();
```

惰性允许多个操作组合为一次遍历，并能处理无限序列的有限部分。

---

## 九、`iter`、`iter_mut` 与 `into_iter`

这是本阶段最重要的所有权知识。

## 9.1 `iter()`：产生共享引用

```rust
fn main() {
    let names = vec![String::from("Alice"), String::from("Bob")];

    for name in names.iter() {
        println!("{name}");
    }

    println!("仍可使用：{names:?}");
}
```

元素类型为 `&String`，集合和元素都没有被移动。

## 9.2 `iter_mut()`：产生可变引用

```rust
fn main() {
    let mut values = vec![1, 2, 3];

    for value in values.iter_mut() {
        *value *= 10;
    }

    println!("{values:?}");
}
```

元素类型为 `&mut i32`，允许原地修改。

## 9.3 `into_iter()`：消费集合并产生拥有值

```rust,compile_fail
fn main() {
    let names = vec![String::from("Alice"), String::from("Bob")];

    for name in names.into_iter() {
        println!("{name}");
    }

    println!("{names:?}");
}
```

元素类型为 `String`，整个 `Vec` 被消费，循环后不能再使用。

## 9.4 速查表

| 调用 | 常见 `Item` | 原集合之后可用 | 可修改元素 | 可取得元素所有权 |
|---|---|---|---|---|
| `collection.iter()` | `&T` | 是 | 否 | 否 |
| `collection.iter_mut()` | `&mut T` | 是 | 是 | 否 |
| `collection.into_iter()` | `T` | 否 | 拥有后可改 | 是 |

`for item in &collection` 等价于共享借用遍历，`for item in &mut collection` 等价于可变借用遍历，`for item in collection` 通常按值消费。

---

## 十、常用迭代器适配器

适配器接收迭代器并返回新迭代器，通常仍是惰性的。

## 10.1 `map`

```rust
let squares: Vec<_> = (1..=5).map(|number| number * number).collect();
```

一对一转换元素。不要用 `map` 只执行副作用而丢弃结果；副作用遍历更适合 `for` 或 `for_each`。

## 10.2 `filter`

```rust
let evens: Vec<_> = (1..=10).filter(|number| number % 2 == 0).collect();
```

闭包通常接收元素引用，因为需要判断但不消费候选元素。

## 10.3 `filter_map`

同时过滤和转换：

```rust
fn main() {
    let inputs = ["10", "invalid", "30", ""];
    let numbers: Vec<i32> = inputs
        .iter()
        .filter_map(|input| input.parse().ok())
        .collect();

    println!("{numbers:?}");
}
```

如果解析失败必须报告错误，而不是跳过，则应收集为 `Result<Vec<_>, _>`：

```rust
let numbers: Result<Vec<i32>, _> = inputs.iter().map(|input| input.parse()).collect();
```

## 10.4 `flat_map` 与 `flatten`

```rust
let nested = vec![vec![1, 2], vec![3, 4]];
let flat: Vec<_> = nested.into_iter().flatten().collect();
```

`flat_map` 适合每个输入产生零个或多个输出。

## 10.5 `enumerate`

```rust
for (index, value) in ["a", "b", "c"].iter().enumerate() {
    println!("{index}: {value}");
}
```

## 10.6 `zip`

```rust
let names = ["Alice", "Bob"];
let scores = [95, 88];

let pairs: Vec<_> = names.into_iter().zip(scores).collect();
```

较短迭代器结束时，`zip` 就结束。

## 10.7 `take`、`skip`、`take_while`

```rust
let page: Vec<_> = (1..=100).skip(20).take(10).collect();
let small: Vec<_> = (1..).take_while(|value| *value < 5).collect();
```

适合分页、窗口和有限消费无限序列。

## 10.8 `chain`

```rust
let combined: Vec<_> = [1, 2].into_iter().chain([3, 4]).collect();
```

要求两侧 `Item` 类型兼容。

---

## 十一、迭代器消费器

消费器驱动迭代器产生结果。

## 11.1 `collect`

目标类型必须明确：

```rust
use std::collections::{HashMap, HashSet};

let vector: Vec<_> = (1..=3).collect();
let set: HashSet<_> = [1, 1, 2].into_iter().collect();
let map: HashMap<_, _> = [("a", 1), ("b", 2)].into_iter().collect();
```

## 11.2 `fold`

```rust
let sum = (1..=5).fold(0, |accumulator, value| accumulator + value);
```

`fold` 将序列归约为一个值。累加、构建状态、组合字符串都可使用，但复杂状态更新用普通循环可能更易读。

```rust
let summary = ["Rust", "is", "fast"]
    .iter()
    .fold(String::new(), |mut output, word| {
        if !output.is_empty() {
            output.push(' ');
        }
        output.push_str(word);
        output
    });
```

## 11.3 查询消费器

```rust
let values = [3, 8, 12, 5];

let any_even = values.iter().any(|value| value % 2 == 0);
let all_positive = values.iter().all(|value| *value > 0);
let first_large = values.iter().find(|value| **value > 10);
let position = values.iter().position(|value| *value == 8);
let count = values.iter().filter(|value| **value > 5).count();
```

注意闭包参数可能因迭代器元素本身是引用而形成多层引用。可使用模式解构或 `copied()` 简化：

```rust
let first_large = values.iter().copied().find(|value| *value > 10);
```

## 11.4 `sum` 与 `product`

```rust
let sum: i32 = (1..=10).sum();
let factorial: u64 = (1..=10).product();
```

有歧义时标注结果类型。

---

## 十二、闭包

闭包是可以保存到变量、作为参数传递并捕获周围环境的匿名函数。

```rust
fn main() {
    let add = |left: i32, right: i32| left + right;
    println!("{}", add(3, 4));
}
```

编译器通常能推断参数和返回类型：

```rust
let double = |value| value * 2;
```

## 12.1 捕获不可变引用

```rust
fn main() {
    let threshold = 10;
    let values = vec![5, 12, 8, 20];

    let selected: Vec<_> = values
        .iter()
        .filter(|value| **value > threshold)
        .collect();

    println!("{selected:?}");
}
```

闭包读取 `threshold`，通常通过共享引用捕获。

## 12.2 捕获可变引用

```rust
fn main() {
    let mut calls = 0;
    let mut record_call = || {
        calls += 1;
        println!("第 {calls} 次调用");
    };

    record_call();
    record_call();
}
```

闭包修改环境，因此闭包绑定也需要 `mut` 才能多次调用。

## 12.3 按值捕获与 `move`

```rust
fn main() {
    let message = String::from("hello");
    let print_later = move || println!("{message}");

    print_later();
    // println!("{message}"); // message 已移动进闭包
}
```

`move` 强制闭包取得所用环境变量的所有权。在线程和异步任务中很常见，因为闭包可能比当前栈帧活得更久。

`move` 描述捕获方式，不一定意味着闭包只能调用一次；能否重复调用取决于闭包如何使用捕获值。

---

## 十三、`Fn`、`FnMut` 与 `FnOnce`

闭包会根据其使用捕获值的方式自动实现一个或多个调用 Trait。

## 13.1 `Fn`

只读取捕获环境，不修改或消费：

```rust
fn apply_twice<F>(value: i32, operation: F) -> i32
where
    F: Fn(i32) -> i32,
{
    operation(operation(value))
}

fn main() {
    let offset = 3;
    let result = apply_twice(10, |value| value + offset);
    println!("{result}");
}
```

## 13.2 `FnMut`

需要修改捕获环境：

```rust
fn repeat<F>(times: usize, mut action: F)
where
    F: FnMut(usize),
{
    for index in 0..times {
        action(index);
    }
}

fn main() {
    let mut total = 0;
    repeat(4, |value| total += value);
    println!("{total}");
}
```

## 13.3 `FnOnce`

消费捕获值，因此至少能调用一次：

```rust
fn run_once<F>(action: F)
where
    F: FnOnce(),
{
    action();
}

fn main() {
    let message = String::from("owned");
    run_once(|| drop(message));
}
```

## 13.4 关系与选择

- 所有闭包都至少实现 `FnOnce`
- 不消费捕获值的闭包还可能实现 `FnMut`
- 不修改也不消费捕获值的闭包还可能实现 `Fn`

设计接收闭包的函数时，选择满足实现需求的最宽松约束：

- 只调用一次：优先允许 `FnOnce`
- 需要多次调用且可能修改状态：`FnMut`
- 需要并发共享或保证不修改捕获状态时：`Fn`，并结合后续线程安全约束

普通函数也可以满足相应 `Fn` 约束：

```rust
fn square(value: i32) -> i32 {
    value * value
}

let values: Vec<_> = (1..=4).map(square).collect();
```

---

## 十四、类型转换：`From`、`Into` 与可失败转换

## 14.1 `From` 和 `Into`

```rust
#[derive(Debug)]
struct UserId(u64);

impl From<u64> for UserId {
    fn from(value: u64) -> Self {
        Self(value)
    }
}

fn main() {
    let first = UserId::from(42);
    let second: UserId = 100_u64.into();
    println!("{first:?} {second:?}");
}
```

实现 `From<A> for B` 后，通常自动获得 `Into<B> for A`。实现侧优先实现 `From`。

`From` 应用于确定不会失败的转换。

## 14.2 `TryFrom` 和 `TryInto`

```rust
use std::convert::TryFrom;

#[derive(Debug)]
struct Percentage(u8);

impl TryFrom<u8> for Percentage {
    type Error = String;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        if value <= 100 {
            Ok(Self(value))
        } else {
            Err(format!("百分比不能超过 100：{value}"))
        }
    }
}
```

可能失败的转换不要用静默截断或随意默认值掩盖。

## 14.3 字符串解析

`"42".parse::<i32>()` 基于 `FromStr` Trait。它返回 `Result`，因为文本可能无效。

---

## 十五、普通循环与迭代器如何选择

普通循环：

```rust
let mut output = Vec::new();
for value in 1..=10 {
    if value % 2 == 0 {
        output.push(value * value);
    }
}
```

迭代器：

```rust
let output: Vec<_> = (1..=10)
    .filter(|value| value % 2 == 0)
    .map(|value| value * value)
    .collect();
```

选择标准：

- 清晰表达筛选、转换、归约的数据管道：迭代器
- 有复杂分支、多个可变状态、提前跳转或大量副作用：普通循环可能更清晰
- 链过长时，用命名良好的辅助函数或中间变量分段
- 不要为了“函数式风格”牺牲可读性

迭代器通常能被编译器优化为高效循环，但性能结论仍应通过基准验证。

---

## 十六、阶段项目：日志分析器

## 16.1 日志格式

每行：

```text
时间|级别|模块|消息
```

示例：

```text
2026-07-12T10:00:00|INFO|server|service started
2026-07-12T10:00:03|WARN|database|slow query detected
2026-07-12T10:00:05|ERROR|server|request failed
```

## 16.2 功能

- 解析日志文件
- 按级别、模块、关键词筛选
- 统计各级别和模块数量
- 显示错误记录
- 输出最活跃模块
- 跳过空行，但报告格式错误的行

## 16.3 完整参考实现

```rust
use std::collections::{HashMap, HashSet};
use std::env;
use std::fs;
use std::process;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum Level {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
}

impl Level {
    fn parse(input: &str) -> Result<Self, String> {
        match input.to_ascii_uppercase().as_str() {
            "TRACE" => Ok(Self::Trace),
            "DEBUG" => Ok(Self::Debug),
            "INFO" => Ok(Self::Info),
            "WARN" => Ok(Self::Warn),
            "ERROR" => Ok(Self::Error),
            _ => Err(format!("未知日志级别：{input}")),
        }
    }

    fn as_str(self) -> &'static str {
        match self {
            Self::Trace => "TRACE",
            Self::Debug => "DEBUG",
            Self::Info => "INFO",
            Self::Warn => "WARN",
            Self::Error => "ERROR",
        }
    }
}

#[derive(Debug)]
struct LogEntry {
    timestamp: String,
    level: Level,
    module: String,
    message: String,
}

impl LogEntry {
    fn parse(line: &str) -> Result<Self, String> {
        let mut fields = line.splitn(4, '|');
        let timestamp = fields.next().unwrap_or_default().trim();
        let level = fields.next().unwrap_or_default().trim();
        let module = fields.next().unwrap_or_default().trim();
        let message = fields.next().unwrap_or_default().trim();

        if timestamp.is_empty() || level.is_empty() || module.is_empty() || message.is_empty() {
            return Err(String::from("字段不足或存在空字段"));
        }

        Ok(Self {
            timestamp: timestamp.to_string(),
            level: Level::parse(level)?,
            module: module.to_string(),
            message: message.to_string(),
        })
    }
}

#[derive(Debug, Default)]
struct Filter {
    level: Option<Level>,
    module: Option<String>,
    keyword: Option<String>,
}

impl Filter {
    fn matches(&self, entry: &LogEntry) -> bool {
        let level_matches = self.level.is_none_or(|level| entry.level == level);
        let module_matches = self
            .module
            .as_ref()
            .is_none_or(|module| entry.module.eq_ignore_ascii_case(module));
        let keyword_matches = self.keyword.as_ref().is_none_or(|keyword| {
            entry
                .message
                .to_lowercase()
                .contains(&keyword.to_lowercase())
        });

        level_matches && module_matches && keyword_matches
    }
}

fn parse_logs(content: &str) -> (Vec<LogEntry>, Vec<String>) {
    let mut entries = Vec::new();
    let mut errors = Vec::new();

    for (index, line) in content.lines().enumerate() {
        if line.trim().is_empty() {
            continue;
        }

        match LogEntry::parse(line) {
            Ok(entry) => entries.push(entry),
            Err(error) => errors.push(format!("第 {} 行：{error}", index + 1)),
        }
    }

    (entries, errors)
}

fn count_by_level(entries: &[&LogEntry]) -> HashMap<Level, usize> {
    entries.iter().fold(HashMap::new(), |mut counts, entry| {
        *counts.entry(entry.level).or_insert(0) += 1;
        counts
    })
}

fn count_by_module<'a>(entries: &[&'a LogEntry]) -> HashMap<&'a str, usize> {
    let mut counts = HashMap::new();
    for entry in entries {
        *counts.entry(entry.module.as_str()).or_insert(0) += 1;
    }
    counts
}

fn print_report(entries: &[LogEntry], filter: &Filter, parse_errors: &[String]) {
    let filtered: Vec<&LogEntry> = entries
        .iter()
        .filter(|entry| filter.matches(entry))
        .collect();

    println!("有效记录总数：{}", entries.len());
    println!("筛选后记录数：{}", filtered.len());
    println!("格式错误行数：{}", parse_errors.len());

    let levels = count_by_level(&filtered);
    println!("\n按级别统计：");
    for level in [Level::Trace, Level::Debug, Level::Info, Level::Warn, Level::Error] {
        println!("  {:<5} {}", level.as_str(), levels.get(&level).unwrap_or(&0));
    }

    let modules = count_by_module(&filtered);
    let mut module_counts: Vec<_> = modules.into_iter().collect();
    module_counts.sort_by(|left, right| {
        right.1.cmp(&left.1).then_with(|| left.0.cmp(right.0))
    });

    println!("\n模块统计：");
    for (module, count) in module_counts {
        println!("  {module:<20} {count}");
    }

    let unique_modules: HashSet<_> = filtered
        .iter()
        .map(|entry| entry.module.as_str())
        .collect();
    println!("\n涉及模块数：{}", unique_modules.len());

    println!("\n筛选结果：");
    for entry in &filtered {
        println!(
            "{} | {:<5} | {:<15} | {}",
            entry.timestamp,
            entry.level.as_str(),
            entry.module,
            entry.message
        );
    }

    if !parse_errors.is_empty() {
        println!("\n解析错误：");
        for error in parse_errors {
            println!("  {error}");
        }
    }
}

fn parse_arguments(arguments: &[String]) -> Result<(&str, Filter), String> {
    let Some(path) = arguments.first() else {
        return Err(String::from(
            "用法：log-analyzer <文件> [--level LEVEL] [--module NAME] [--keyword TEXT]",
        ));
    };

    let mut filter = Filter::default();
    let mut index = 1;

    while index < arguments.len() {
        let option = arguments[index].as_str();
        let Some(value) = arguments.get(index + 1) else {
            return Err(format!("选项 {option} 缺少值"));
        };

        match option {
            "--level" => filter.level = Some(Level::parse(value)?),
            "--module" => filter.module = Some(value.clone()),
            "--keyword" => filter.keyword = Some(value.clone()),
            _ => return Err(format!("未知选项：{option}")),
        }
        index += 2;
    }

    Ok((path, filter))
}

fn run() -> Result<(), String> {
    let arguments: Vec<String> = env::args().skip(1).collect();
    let (path, filter) = parse_arguments(&arguments)?;
    let content = fs::read_to_string(path)
        .map_err(|error| format!("无法读取 {path}：{error}"))?;
    let (entries, errors) = parse_logs(&content);
    print_report(&entries, &filter, &errors);
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("错误：{error}");
        process::exit(1);
    }
}
```

> 如果所用 Rust 工具链尚不支持 `Option::is_none_or`，可改写为 `option.as_ref().map_or(true, |value| ...)`。

## 16.4 运行

```bash
cargo run -- app.log
cargo run -- app.log --level ERROR
cargo run -- app.log --module server --keyword failed
```

## 16.5 所有权与迭代器分析

- `parse_logs` 创建并拥有 `Vec<LogEntry>`
- 筛选结果是 `Vec<&LogEntry>`，只保存对原日志的引用，避免复制整条记录
- `count_by_level` 使用可复制枚举作为键
- `count_by_module` 使用借用的 `&str` 作为临时统计键
- `iter()` 保留原日志集合，`into_iter()` 用于不再需要原集合时转移元素
- `HashSet<&str>` 只用于报告期间的唯一模块统计
- 排序前把无序 `HashMap` 收集为 `Vec`

## 16.6 优化练习

当前关键词筛选会为每条日志重复创建小写字符串。可以：

1. 创建 `Filter` 时预先把关键词转为小写。
2. 解析日志时根据需求保存规范化字段。
3. 若只处理 ASCII，考虑 `eq_ignore_ascii_case` 或按字符比较。
4. 建立基准后再判断哪种方案值得采用。

## 16.7 扩展任务

1. 支持多个级别筛选，使用 `HashSet<Level>`。
2. 支持 `--from`、`--to` 时间范围。
3. 按分钟或小时分组统计。
4. 输出频率最高的错误消息。
5. 支持从标准输入读取日志。
6. 增加 `--limit` 和分页。
7. 将报告导出为 CSV。
8. 对超大文件逐行处理，避免一次载入全部内容。
9. 把筛选器设计为接收闭包的通用函数。
10. 为解析、筛选、统计和排序编写单元测试。

---

## 十七、常见误区

- 默认所有序列都用 `Vec`
- 依赖 `HashMap` 或 `HashSet` 的迭代顺序
- 为查询 `HashMap<String, V>` 而创建临时 `String`
- 在持有 `Vec` 元素引用时继续 `push`
- 用数字下标访问 UTF-8 字符串
- 把字节数当作字符数
- 创建 `map` 或 `filter` 后忘记消费迭代器
- 需要保留集合时误用 `into_iter`
- 为避免引用层级而随意 `clone`
- 使用 `filter_map` 静默丢弃本应报告的错误
- 把所有循环强行改写为一条超长迭代器链
- 误以为 `move` 闭包必定只能调用一次
- 给闭包参数设置比实际需要更严格的 `Fn` 约束
- 使用 `From` 表示可能失败的转换

---

## 十八、针对性练习

1. 使用 `VecDeque` 实现先进先出任务队列。
2. 使用 `HashMap` 统计文本中每个字符出现次数。
3. 使用 `HashSet` 求两组用户的共同标签和独有标签。
4. 保持首次出现顺序对 `Vec<String>` 去重。
5. 分别使用循环和迭代器找出偶数平方和。
6. 把 `Vec<Option<i32>>` 展平为 `Vec<i32>`。
7. 把一组字符串全部解析为整数；任何一个失败则返回错误。
8. 实现 `apply_until`，反复调用 `FnMut` 直到谓词成立。
9. 实现带范围校验的 `Port(u16)` 转换。
10. 从待办项目中统计各状态任务数，并按标题排序。

---

## 十九、知识自测

1. 四种常用集合分别适合什么访问模式？
2. `Vec` 的长度和容量有什么区别？
3. `Vec::push` 为什么可能让已有元素引用失效？
4. `HashMap::entry` 解决了什么问题？
5. 为什么不能依赖 `HashMap` 的输出顺序？
6. 如何在保持原顺序的同时去重？
7. 字符串的 `len()` 返回什么单位？
8. `bytes()` 和 `chars()` 有何区别？
9. 迭代器为什么称为惰性？
10. `iter`、`iter_mut`、`into_iter` 的 `Item` 和所有权有何差异？
11. 适配器与消费器有何区别？
12. `map`、`filter`、`filter_map` 各表达什么关系？
13. `collect` 为什么经常需要类型标注？
14. 如何把 `Iterator<Item = Result<T, E>>` 收集为 `Result<Vec<T>, E>`？
15. `fold` 的初始值和累加器分别是什么？
16. 闭包可以通过哪几种方式捕获环境？
17. `move` 影响什么？
18. `Fn`、`FnMut`、`FnOnce` 如何区分？
19. 接收闭包时为什么应选择满足需求的最宽松约束？
20. `From` 与 `TryFrom` 如何选择？

---

## 二十、阶段验收清单

### 集合

- [ ] 能根据访问模式选择合适集合
- [ ] 能安全访问和修改 `Vec`
- [ ] 能使用 `HashMap::entry` 完成计数
- [ ] 能使用 `HashSet` 去重和执行集合运算
- [ ] 能使用 `VecDeque` 实现队列
- [ ] 不依赖哈希集合的迭代顺序

### 字符串与迭代器

- [ ] 能区分 UTF-8 字节、`char` 与用户感知字符
- [ ] 不使用不确定的字节边界切割字符串
- [ ] 能解释迭代器惰性求值
- [ ] 能准确选择 `iter`、`iter_mut`、`into_iter`
- [ ] 能使用常见适配器和消费器
- [ ] 能把错误序列收集为 `Result<Vec<_>, _>`
- [ ] 能在普通循环和迭代器之间按可读性选择

### 闭包与转换

- [ ] 能编写捕获共享引用、可变引用和所有权的闭包
- [ ] 能解释 `move` 捕获
- [ ] 能区分并使用 `Fn`、`FnMut`、`FnOnce`
- [ ] 能让函数接收闭包作为参数
- [ ] 能实现 `From` 和 `TryFrom`

### 项目

- [ ] 完成日志解析、筛选、统计和报告
- [ ] 格式错误行不会导致程序 panic
- [ ] 筛选结果借用原日志，避免不必要复制
- [ ] 无序统计在展示前执行稳定排序
- [ ] 至少完成三个扩展任务
- [ ] 能逐项说明项目中的集合和迭代方式为何这样选择

### 质量检查

```bash
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
cargo run -- sample.log --level ERROR
```

当你能读懂一条迭代器链中每一步的 `Item` 类型和所有权变化，并能根据业务访问模式选择集合、根据闭包行为判断 `Fn` 约束时，即可进入第五阶段：模块、泛型、Trait 与生命周期。

---

## 二十一、进入下一阶段前的预习

下一阶段将回答：

- 如何把日志分析器拆成库、模块和可执行入口？
- 如何让同一套算法适用于多种元素类型？
- 如何用 Trait 描述多个类型共有的行为？
- `impl Trait` 和 `dyn Trait` 有何区别？
- 为什么某些返回引用的函数需要生命周期标注？
- 迭代器的 `Item` 为什么是关联类型？

建议把本阶段项目拆分为 `model`、`parser`、`filter`、`report` 等模块，并记录哪些函数可以泛型化、哪些行为适合抽象为 Trait。这会自然引出第五阶段的内容。
