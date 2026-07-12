# 第二阶段：Rust 所有权、借用与切片

> 所有权是 Rust 最核心的语言机制。本阶段不以“背规则”为目标，而是通过内存模型、代码执行过程和编译错误，建立对值、变量、引用及资源生命周期的准确直觉。

## 一、阶段目标

完成本阶段后，你应当能够：

- 解释栈与堆在 Rust 程序中的基本作用
- 准确判断赋值、传参和返回时是否发生 move
- 区分 move、copy 和 clone
- 使用不可变引用和可变引用编写函数
- 理解并遵守借用规则
- 通过缩小作用域解决常见借用冲突
- 理解悬垂引用为何会被编译器阻止
- 熟练区分 `String`、`&String` 和 `&str`
- 使用字符串切片和数组切片表达对连续数据的借用
- 理解 `Copy`、`Clone` 和 `Drop` 的基本语义
- 在不滥用 `clone()` 的前提下完成文本统计项目

## 二、为什么 Rust 需要所有权

程序运行时必须管理内存。常见方案包括：

- 由程序员显式申请和释放内存
- 使用垃圾回收器定期查找并释放不再使用的对象
- 通过编译期规则确定资源何时释放

Rust 采用第三种方式：每个值都有明确的所有者；当所有者离开作用域时，值所占有的资源自动释放。

所有权不仅管理堆内存，也管理文件、网络连接、锁和其他资源。其目标是同时获得：

- 无需垃圾回收器的可预测性能
- 不需要手动释放资源的便利性
- 编译期阻止悬垂引用、重复释放和数据竞争

---

## 三、栈与堆的基础模型

## 3.1 栈

栈按后进先出的顺序存储数据。函数调用时创建栈帧，函数结束时整个栈帧被移除。

适合直接放在栈上的值通常具有编译期已知的固定大小，例如：

```rust
let age: u32 = 20;
let position: (i32, i32) = (10, 20);
let flags: [bool; 4] = [true, false, true, false];
```

栈上分配和释放通常很快，因为只需要移动栈指针。

## 3.2 堆

当数据大小在编译期不能确定，或需要动态增长时，数据通常存放在堆上。

```rust
let message = String::from("hello");
```

可以用一个简化模型理解 `String`：变量本身在栈上保存三个字段，实际文本字节存放在堆上。

```text
栈上的 String
┌──────────┐
│ ptr ───────────────┐
│ len = 5  │         │
│ cap = 5  │         │
└──────────┘         │
                     ▼
                 堆上字节
                 h e l l o
```

- `ptr`：指向堆内存
- `len`：当前有效字节数
- `cap`：已分配容量

这个模型是理解移动语义的关键。

## 3.3 栈与堆不是所有权规则本身

初学时不要把所有权简单理解为“栈数据复制、堆数据移动”。是否实现 `Copy` 取决于类型语义，而不只是值位于哪里。后续自定义类型也可能全部由固定大小字段组成，却仍然不自动实现 `Copy`。

---

## 四、所有权三条规则

所有权规则可以概括为：

1. Rust 中每个值都有一个所有者。
2. 同一时刻一个值只有一个所有者。
3. 所有者离开作用域时，值会被释放。

观察作用域：

```rust
fn main() {
    {
        let message = String::from("hello");
        println!("{message}");
    } // message 离开作用域，String 的堆内存被释放
}
```

Rust 在作用域结束时自动调用清理逻辑。对于 `String`，清理逻辑会释放它拥有的堆内存。

## 4.1 资源获取即初始化

这种让资源生命周期绑定到对象生命周期的思想通常称为 RAII。它适用于多种资源：

```rust
use std::fs::File;

fn main() {
    let file = File::open("example.txt");
    println!("{file:?}");
} // 成功打开的文件对象离开作用域时，文件句柄自动关闭
```

不论函数是正常结束还是提前返回，已经构造成功的局部值都会按规则清理。

---

## 五、移动、复制与克隆

## 5.1 简单值的复制

```rust
fn main() {
    let x = 5;
    let y = x;

    println!("x={x}, y={y}");
}
```

整数实现了 `Copy`。`let y = x` 会复制整数值，两个变量之后都可使用。

## 5.2 `String` 的移动

```rust,compile_fail
fn main() {
    let first = String::from("hello");
    let second = first;

    println!("{first}");
    println!("{second}");
}
```

`let second = first` 之后，`first` 不再有效，所有权移动到 `second`。

如果 Rust 只是复制 `ptr`、`len`、`cap`，两个变量就会指向同一块堆内存；离开作用域时二者都尝试释放它，会造成重复释放。Rust 通过让旧变量失效来避免这个问题。

```text
移动前：first ──> 堆数据
移动后：second ─> 堆数据
        first 已失效
```

移动通常只复制栈上的少量元数据，不复制堆中的全部内容，因此成本较低。

## 5.3 深拷贝 `clone()`

确实需要两份独立数据时使用 `clone()`：

```rust
fn main() {
    let first = String::from("hello");
    let second = first.clone();

    println!("first={first}, second={second}");
}
```

此时两份 `String` 各自拥有独立的堆内存。

`clone()` 可能执行堆分配和完整数据复制。它不是错误，但应该出于明确的所有权需求使用，而不是为了绕过编译错误。

## 5.4 `Copy` 常见类型

通常实现 `Copy` 的类型包括：

- 整数、浮点数
- `bool`
- `char`
- 仅包含 `Copy` 元素的元组
- 仅包含 `Copy` 元素的固定长度数组
- 共享引用 `&T`

```rust
fn main() {
    let point = (10, 20);
    let copied = point;

    println!("{point:?}, {copied:?}");
}
```

持有资源并需要自定义清理的类型不能实现 `Copy`。

## 5.5 判断赋值行为

看到 `let b = a;` 时依次判断：

1. `a` 的类型是否实现 `Copy`？
2. 实现了：复制后 `a`、`b` 都有效。
3. 未实现：所有权移动到 `b`，`a` 不再可用。
4. 如果写 `let b = a.clone();`，则调用该类型的克隆逻辑。

### 练习：判断能否编译

```rust
let a = 10;
let b = a;
println!("{a} {b}");
```

```rust,compile_fail
let a = String::from("rust");
let b = a;
println!("{a} {b}");
```

```rust
let a = String::from("rust");
let b = a.clone();
println!("{a} {b}");
```

---

## 六、函数调用与所有权

传参在所有权层面类似于赋值。

## 6.1 传入拥有所有权的值

```rust,compile_fail
fn consume(text: String) {
    println!("收到：{text}");
}

fn main() {
    let message = String::from("hello");
    consume(message);

    println!("{message}");
}
```

调用 `consume(message)` 会把所有权移动给函数参数 `text`。函数结束后 `text` 被清理，调用方不能再使用 `message`。

## 6.2 传入 `Copy` 值

```rust
fn print_number(value: i32) {
    println!("{value}");
}

fn main() {
    let number = 42;
    print_number(number);
    println!("仍可使用：{number}");
}
```

`i32` 实现 `Copy`，传参复制了值。

## 6.3 通过返回值转移所有权

```rust
fn create_message() -> String {
    String::from("created")
}

fn add_suffix(mut text: String) -> String {
    text.push_str(" by Rust");
    text
}

fn main() {
    let message = create_message();
    let message = add_suffix(message);
    println!("{message}");
}
```

返回值可以把所有权移交给调用方。

## 6.4 用元组归还多个值

```rust
fn length_of(text: String) -> (String, usize) {
    let length = text.len();
    (text, length)
}

fn main() {
    let message = String::from("hello");
    let (message, length) = length_of(message);

    println!("{message} 的长度为 {length}");
}
```

这种方式可行，却很繁琐。更自然的办法是让函数借用值。

---

## 七、引用与借用

引用允许使用一个值，但不取得它的所有权。创建引用称为借用。

```rust
fn length_of(text: &String) -> usize {
    text.len()
}

fn main() {
    let message = String::from("hello");
    let length = length_of(&message);

    println!("{message} 的长度为 {length}");
}
```

```text
message ──────────────> String ──> 堆数据
             ▲
             │
text 引用 ───┘
```

- `&message` 创建对 `message` 的引用
- 参数类型 `&String` 表示借用一个 `String`
- 函数结束时只清理引用本身，不释放被引用的数据
- `message` 始终拥有原值，因此调用后仍可使用

## 7.1 解引用

`*` 是解引用运算符：

```rust
fn main() {
    let number = 10;
    let reference = &number;

    assert_eq!(number, *reference);
}
```

许多方法调用中 Rust 会自动解引用，所以经常可以直接写 `reference.len()`，不必手动写 `(*reference).len()`。

## 7.2 共享引用默认不可修改

```rust,compile_fail
fn change(text: &String) {
    text.push_str(" world");
}
```

`&String` 是共享引用，只能读取，不能通过它修改原值。

---

## 八、可变引用

需要通过引用修改值时，使用 `&mut T`。

```rust
fn append_world(text: &mut String) {
    text.push_str(" world");
}

fn main() {
    let mut message = String::from("hello");
    append_world(&mut message);

    println!("{message}");
}
```

三个位置都要表达可变性：

- 所有者：`let mut message`
- 创建可变引用：`&mut message`
- 参数类型：`&mut String`

## 8.1 核心借用规则

对同一个值，在同一段有效使用范围内，只允许以下两种状态之一：

- 任意数量的共享引用 `&T`
- 恰好一个可变引用 `&mut T`

另外，引用必须始终有效。

可以记为：“多读，或单写；引用不能悬空。”

## 8.2 多个共享引用

```rust
fn main() {
    let message = String::from("hello");

    let first = &message;
    let second = &message;
    let third = &message;

    println!("{first}, {second}, {third}");
}
```

只读不会互相干扰，所以多个共享引用可以同时存在。

## 8.3 不能同时使用两个可变引用

```rust,compile_fail
fn main() {
    let mut message = String::from("hello");

    let first = &mut message;
    let second = &mut message;

    first.push('!');
    second.push('?');
}
```

如果多个可变引用能同时修改同一数据，操作顺序和观察结果可能不明确。Rust 在编译期禁止这种情况。

## 8.4 共享引用与可变引用不能交叠使用

```rust,compile_fail
fn main() {
    let mut message = String::from("hello");

    let reader = &message;
    let writer = &mut message;

    println!("{reader}");
    writer.push('!');
}
```

如果读者正在读取，而写者改变了数据，读者观察到的状态可能失效。

## 8.5 引用的有效范围以最后一次使用为准

现代 Rust 会分析引用的实际使用范围：

```rust
fn main() {
    let mut message = String::from("hello");

    let first = &message;
    let second = &message;
    println!("{first} {second}");

    // first 和 second 后续不再使用，可变借用可以从这里开始
    let writer = &mut message;
    writer.push('!');

    println!("{writer}");
}
```

这常被称为非词法生命周期的效果。不要机械地认为引用一定持续到花括号结束；编译器关注它最后一次可能被使用的位置。

## 8.6 用作用域明确分隔借用

```rust
fn main() {
    let mut message = String::from("hello");

    {
        let writer = &mut message;
        writer.push_str(" rust");
    }

    let another_writer = &mut message;
    another_writer.push('!');

    println!("{message}");
}
```

缩小作用域是解决借用冲突的常用方法，但应先理解哪些引用发生了交叠。

---

## 九、典型借用冲突分析

## 9.1 修改集合时仍持有元素引用

```rust,compile_fail
fn main() {
    let mut values = vec![10, 20, 30];
    let first = &values[0];

    values.push(40);
    println!("{first}");
}
```

`push` 可能导致动态数组重新分配，原先指向元素的引用可能失效。因此 Rust 阻止在 `first` 后续仍会使用时修改 `values`。

如果只需要元素值，并且元素实现 `Copy`，可以复制出来：

```rust
fn main() {
    let mut values = vec![10, 20, 30];
    let first = values[0];

    values.push(40);
    println!("{first}");
}
```

## 9.2 同一表达式中的读写借用

某些看似简单的代码涉及多个借用。处理错误时可先拆开：

```rust
fn main() {
    let mut values = vec![10, 20, 30];
    let length = values.len();
    values.push(length);

    println!("{values:?}");
}
```

先计算只读结果，再执行修改，能让借用边界清楚。

## 9.3 通过索引取得两个可变引用

```rust,compile_fail
fn main() {
    let mut values = [10, 20, 30];
    let first = &mut values[0];
    let second = &mut values[1];

    *first += 1;
    *second += 1;
}
```

人能看出两个索引不同，但普通索引表达式不足以让编译器在所有情况下证明它们不重叠。可使用标准库提供的安全分割方法：

```rust
fn main() {
    let mut values = [10, 20, 30];
    let (left, right) = values.split_at_mut(1);

    let first = &mut left[0];
    let second = &mut right[0];

    *first += 1;
    *second += 1;

    println!("{values:?}");
}
```

`split_at_mut` 的 API 保证两个切片不重叠。

---

## 十、悬垂引用

悬垂引用指向已经被释放或失效的数据。Rust 编译器会阻止安全代码创建它。

```rust,compile_fail
fn dangling() -> &String {
    let message = String::from("hello");
    &message
}
```

函数返回时 `message` 被释放，若返回其引用，调用方会拿到无效地址，因此无法编译。

正确方式是返回拥有所有权的值：

```rust
fn create_message() -> String {
    String::from("hello")
}

fn main() {
    let message = create_message();
    println!("{message}");
}
```

返回所有权时，堆数据不会在被移动的过程中释放；新的调用方变量成为所有者。

## 10.1 不要通过泄漏解决生命周期问题

把普通数据强行变成全局存活，或故意泄漏内存，通常不是解决借用错误的正确方案。先考虑：

- 返回拥有所有权的值
- 让调用者提供数据并返回对调用者数据的引用
- 缩小引用作用域
- 调整数据结构，使所有权关系更明确

---

## 十一、字符串：`String` 与 `&str`

## 11.1 `String`

`String` 是拥有所有权、可增长、UTF-8 编码的字符串类型。

```rust
fn main() {
    let mut text = String::from("Rust");
    text.push(' ');
    text.push_str("语言");

    println!("{text}");
}
```

`String` 通常管理堆上的字节缓冲区，离开作用域时释放该缓冲区。

## 11.2 字符串切片 `&str`

字符串字面量的类型是 `&str`：

```rust
fn main() {
    let language: &str = "Rust";
    println!("{language}");
}
```

`&str` 是对一段有效 UTF-8 字节的借用视图。它不拥有所指向的字符串数据。

`&str` 可以引用：

- 程序中的字符串字面量
- 一个 `String` 的全部内容
- 一个 `String` 或其他字符串数据的一部分

```rust
fn main() {
    let text = String::from("hello rust");

    let all: &str = &text;
    let hello: &str = &text[0..5];

    println!("{all}, {hello}");
}
```

## 11.3 为什么参数通常优先接收 `&str`

```rust
fn print_text(text: &str) {
    println!("{text}");
}

fn main() {
    let owned = String::from("owned string");
    let literal = "string literal";

    print_text(&owned);
    print_text(literal);
}
```

`&str` 参数既能接受字符串字面量，也能接受 `String` 的切片，因此比 `&String` 更通用。

只有函数确实需要 `String` 类型特有的整体信息，或 API 约束明确要求时，才考虑 `&String`。只读取文本内容时优先 `&str`。

## 11.4 何时接收 `String`

当函数需要取得文本所有权时接收 `String`：

```rust
fn store_message(message: String) {
    println!("存储消息：{message}");
    // 实际项目中可能把 message 放入结构体或集合
}
```

典型场景：

- 函数要长期保存传入字符串
- 函数要把字符串转交给另一个所有者
- 函数要消耗并转换原值

## 11.5 参数选择表

| 需求 | 常用参数类型 |
|---|---|
| 只读取文本 | `&str` |
| 修改已有 `String` | `&mut String` |
| 取得并保存文本所有权 | `String` |
| 只读取任意元素序列 | `&[T]` |
| 修改任意元素序列 | `&mut [T]` |

---

## 十二、字符串切片的边界

Rust 字符串采用 UTF-8 编码，切片范围使用字节索引，而不是字符序号。

```rust
fn main() {
    let text = String::from("hello");
    let slice = &text[0..2];
    println!("{slice}");
}
```

ASCII 字符每个占 1 字节，因此结果是 `he`。

中文字符通常占多个 UTF-8 字节：

```rust
fn main() {
    let text = String::from("你好Rust");
    let first = &text[0..3];
    println!("{first}");
}
```

`0..3` 恰好覆盖第一个汉字的字节范围。如果边界落在一个字符编码内部，程序会 panic。

因此：

- 不要把字符串字节索引当作字符索引
- 只有确定 UTF-8 字符边界时才直接切片
- 遍历 Unicode 标量值可使用 `.chars()`
- 遍历原始字节可使用 `.bytes()`

```rust
fn main() {
    let text = "你好Rust";

    for character in text.chars() {
        println!("{character}");
    }

    println!("字节数：{}", text.len());
    println!("字符数：{}", text.chars().count());
}
```

注意：用户眼中的一个“可见字符”有时可能由多个 Unicode 标量值组成。`.chars().count()` 也不等同于所有语言环境中的可见字符数量。

---

## 十三、切片解决什么问题

考虑返回字符串第一个单词的位置：

```rust
fn first_word_end(text: &String) -> usize {
    for (index, byte) in text.bytes().enumerate() {
        if byte == b' ' {
            return index;
        }
    }

    text.len()
}
```

这个函数返回索引，但索引与字符串之间没有类型层面的联系：

```rust
fn main() {
    let mut text = String::from("hello world");
    let end = first_word_end(&text);

    text.clear();
    println!("旧索引仍是 {end}，但字符串已经为空");
}
```

更好的设计是返回切片：

```rust
fn first_word(text: &str) -> &str {
    for (index, byte) in text.bytes().enumerate() {
        if byte == b' ' {
            return &text[..index];
        }
    }

    text
}

fn main() {
    let text = String::from("hello world");
    let word = first_word(&text);

    println!("第一个单词：{word}");
}
```

返回的 `&str` 与原字符串存在借用关系。如果切片后续仍被使用，编译器会阻止原字符串被清空：

```rust,compile_fail
fn main() {
    let mut text = String::from("hello world");
    let word = first_word(&text);

    text.clear();
    println!("{word}");
}
```

切片不仅携带位置，也在类型系统中保持了与原数据的关系。

---

## 十四、数组切片

数组切片类型为 `&[T]`，可借用数组或动态数组中的连续元素。

```rust
fn sum(values: &[i32]) -> i32 {
    let mut total = 0;

    for value in values {
        total += value;
    }

    total
}

fn main() {
    let array = [10, 20, 30, 40];
    let vector = vec![1, 2, 3, 4, 5];

    println!("{}", sum(&array));
    println!("{}", sum(&array[1..3]));
    println!("{}", sum(&vector));
}
```

接收 `&[i32]` 比接收 `&[i32; 4]` 或 `&Vec<i32>` 更通用。

## 14.1 可变切片

```rust
fn double_all(values: &mut [i32]) {
    for value in values {
        *value *= 2;
    }
}

fn main() {
    let mut values = [1, 2, 3, 4];
    double_all(&mut values[1..]);

    println!("{values:?}");
}
```

函数只能修改切片覆盖的元素，不能改变原集合的长度。

## 14.2 常见切片方法

```rust
fn main() {
    let values = [10, 20, 30, 40];
    let slice = &values[..];

    println!("长度：{}", slice.len());
    println!("是否为空：{}", slice.is_empty());
    println!("首元素：{:?}", slice.first());
    println!("末元素：{:?}", slice.last());
    println!("是否包含 20：{}", slice.contains(&20));
}
```

安全获取元素：

```rust
fn get_value(values: &[i32], index: usize) -> Option<&i32> {
    values.get(index)
}
```

返回值借用自 `values`，因此不能比原切片活得更久。生命周期的显式标注会在第五阶段系统学习。

---

## 十五、重新借用

可变引用本身不是 `Copy`。但把它传给函数时，编译器经常会创建一次更短暂的重新借用。

```rust
fn append_mark(text: &mut String) {
    text.push('!');
}

fn main() {
    let mut message = String::from("hello");
    let reference = &mut message;

    append_mark(reference);
    append_mark(reference);

    println!("{reference}");
}
```

每次调用会临时重新借用 `*reference`，因此原来的可变引用在调用结束后还能继续使用。

如果显式把可变引用移动给另一个绑定，情况可能不同：

```rust,compile_fail
fn main() {
    let mut message = String::from("hello");
    let first = &mut message;
    let second = first;

    second.push('!');
    first.push('?');
}
```

现阶段不必记忆所有自动重新借用细节。遇到错误时，关注编译器指出的“value moved here”和“borrow later used here”。

---

## 十六、`Copy`、`Clone` 与 `Drop`

## 16.1 `Clone`

`Clone` 表示可以显式创建一个副本：

```rust
#[derive(Clone, Debug)]
struct Profile {
    name: String,
    score: u32,
}

fn main() {
    let first = Profile {
        name: String::from("Alice"),
        score: 100,
    };

    let second = first.clone();
    println!("{first:?}");
    println!("{second:?}");
}
```

派生的 `Clone` 会依次克隆每个字段。由于 `String::clone()` 会复制堆数据，整个操作可能有明显成本。

## 16.2 `Copy`

`Copy` 表示按位复制后两个值都可独立安全使用。它继承自 `Clone`。

```rust
#[derive(Copy, Clone, Debug)]
struct Point {
    x: i32,
    y: i32,
}

fn main() {
    let first = Point { x: 3, y: 4 };
    let second = first;

    println!("{first:?} {second:?}");
}
```

结构体所有字段都实现 `Copy` 时，结构体才可能实现 `Copy`。

以下结构体不能实现 `Copy`：

```rust,compile_fail
#[derive(Copy, Clone)]
struct User {
    name: String,
}
```

因为 `String` 不实现 `Copy`。

## 16.3 `Drop`

`Drop` 允许类型在离开作用域时执行自定义清理逻辑：

```rust
struct Resource {
    name: String,
}

impl Drop for Resource {
    fn drop(&mut self) {
        println!("释放资源：{}", self.name);
    }
}

fn main() {
    let _first = Resource {
        name: String::from("first"),
    };

    {
        let _second = Resource {
            name: String::from("second"),
        };
        println!("内部作用域即将结束");
    }

    println!("main 即将结束");
}
```

通常变量按声明的相反顺序清理，嵌套作用域中的变量会更早离开作用域。

## 16.4 提前释放

不能直接调用 `value.drop()`，但可以使用标准库函数 `drop`：

```rust
use std::mem::drop;

fn main() {
    let message = String::from("temporary");
    println!("{message}");

    drop(message);
    // message 的所有权已移动给 drop，之后不能再使用
}
```

提前释放在锁等资源上尤其有用，但将在并发阶段深入讨论。

## 16.5 `Copy` 与 `Drop` 互斥

实现 `Drop` 的类型不能实现 `Copy`。如果复制一个需要清理资源的值，会导致不清楚应该由哪个副本执行资源释放。

---

## 十七、设计函数参数的实用方法

编写函数前，先问三个问题：

1. 函数只需要读取，还是需要修改？
2. 函数调用后，调用方是否还需要这个值？
3. 函数是否需要长期保存这个值？

对应选择：

```rust
fn inspect(text: &str) {
    println!("{text}");
}

fn modify(text: &mut String) {
    text.make_ascii_uppercase();
}

fn consume(text: String) {
    println!("取得所有权：{text}");
}
```

## 17.1 不要为了省字符牺牲语义

接收 `String` 不一定错误，接收引用也不一定总是正确。关键是函数的所有权契约：

- `String`：调用方把值交给函数
- `&str`：调用方允许函数临时读取文本
- `&mut String`：调用方允许函数临时修改字符串

让函数签名准确表达意图，调用者才能知道调用之后值是否还能使用。

## 17.2 返回拥有值还是引用

- 函数内部新创建的数据：通常返回拥有所有权的类型
- 返回输入中的一部分：可以返回引用或切片
- 引用必须明确来源于仍然有效的输入数据

```rust
fn build_label() -> String {
    String::from("new label")
}

fn first_item(values: &[i32]) -> Option<&i32> {
    values.first()
}
```

---

## 十八、避免滥用 `clone()`

下面写法能通过编译，但可能隐藏设计问题：

```rust
fn print_message(message: String) {
    println!("{message}");
}

fn main() {
    let message = String::from("hello");
    print_message(message.clone());
    println!("{message}");
}
```

函数只读取内容，签名更适合改为：

```rust
fn print_message(message: &str) {
    println!("{message}");
}

fn main() {
    let message = String::from("hello");
    print_message(&message);
    println!("{message}");
}
```

## 18.1 合理使用 `clone()` 的场景

- 业务上确实需要两个独立所有者
- 需要保存数据快照
- 需要把数据交给生命周期更长的任务，同时原处仍需保留
- 克隆的是轻量值，成本明确且可接受
- 简化代码的收益经过衡量，且不在性能关键路径

## 18.2 代码审查问题

每看到一次 `clone()`，问：

1. 谁需要拥有这份数据？
2. 能否只借用？
3. 能否移动原值，并调整后续代码不再使用它？
4. 克隆成本多大？
5. 是真正的业务需求，还是为了快速消除编译错误？

不要为了实现“零 clone”写出过度复杂的代码。目标是明确且高效的所有权设计。

---

## 十九、常见错误与解决思路

## 19.1 use of moved value

```rust,compile_fail
fn main() {
    let message = String::from("hello");
    let another = message;
    println!("{message}");
}
```

解决方向：

- 如果原变量不再需要：接受移动，改用新变量
- 如果只需临时读取：改为借用
- 如果确需两份独立值：显式克隆
- 如果函数应归还值：通过返回值转移所有权

## 19.2 cannot borrow as mutable

```rust,compile_fail
fn main() {
    let message = String::from("hello");
    let reference = &mut message;
    reference.push('!');
}
```

所有者必须声明为 `mut`。

## 19.3 cannot borrow more than once

查看第一个可变引用最后在哪里使用，判断两个借用是否交叠。可通过以下方式解决：

- 调整代码顺序
- 缩短第一个引用的使用范围
- 使用代码块明确分隔阶段
- 使用 `split_at_mut` 等能证明不重叠的 API
- 重新设计数据操作流程

## 19.4 cannot borrow as mutable because it is also borrowed as immutable

共享引用后续仍被使用时不能修改原值。先完成读取、保存真正需要的独立结果，再执行修改。

```rust
fn main() {
    let mut text = String::from("hello world");
    let word_length = text.split_whitespace().next().map(str::len);

    text.clear();
    println!("先前第一个单词长度：{word_length:?}");
}
```

这里保存的是独立的数字，不再保存指向 `text` 的切片。

## 19.5 returns a reference to data owned by the current function

局部数据将在函数结束时释放，不能返回其引用。改为返回拥有类型，或让数据由调用方提供。

---

## 二十、阶段项目：文本统计器

## 20.1 项目目标

编写一个命令行文本统计器，读取文本文件并输出：

- 字节数
- Unicode 标量值数量
- 非空字符数量
- 行数
- 单词数
- 最长一行及其字符数量
- 指定关键词出现的次数
- 出现频率最高的若干单词

本阶段先使用标准库。词频统计需要 `HashMap`，第四阶段才会系统学习集合，本项目仅使用其基础 API。

## 20.2 创建项目

```bash
cargo new text-stats
cd text-stats
```

建议结构：

```text
text-stats/
├── Cargo.toml
└── src/
    └── main.rs
```

## 20.3 完整参考实现

```rust
use std::collections::HashMap;
use std::env;
use std::fs;
use std::process;

fn count_non_whitespace(text: &str) -> usize {
    text.chars().filter(|character| !character.is_whitespace()).count()
}

fn count_keyword(text: &str, keyword: &str) -> usize {
    if keyword.is_empty() {
        return 0;
    }

    text.match_indices(keyword).count()
}

fn longest_line(text: &str) -> Option<(&str, usize)> {
    text.lines()
        .map(|line| (line, line.chars().count()))
        .max_by_key(|(_, character_count)| *character_count)
}

fn normalize_word(word: &str) -> String {
    word.trim_matches(|character: char| !character.is_alphanumeric())
        .to_lowercase()
}

fn word_frequencies(text: &str) -> HashMap<String, usize> {
    let mut frequencies = HashMap::new();

    for raw_word in text.split_whitespace() {
        let word = normalize_word(raw_word);

        if !word.is_empty() {
            *frequencies.entry(word).or_insert(0) += 1;
        }
    }

    frequencies
}

fn top_words(text: &str, limit: usize) -> Vec<(String, usize)> {
    let frequencies = word_frequencies(text);
    let mut entries: Vec<(String, usize)> = frequencies.into_iter().collect();

    entries.sort_by(|left, right| {
        right
            .1
            .cmp(&left.1)
            .then_with(|| left.0.cmp(&right.0))
    });
    entries.truncate(limit);
    entries
}

fn print_report(path: &str, text: &str, keyword: Option<&str>) {
    println!("文件：{path}");
    println!("字节数：{}", text.len());
    println!("字符数：{}", text.chars().count());
    println!("非空字符数：{}", count_non_whitespace(text));
    println!("行数：{}", text.lines().count());
    println!("单词数：{}", text.split_whitespace().count());

    match longest_line(text) {
        Some((line, length)) => {
            println!("最长行字符数：{length}");
            println!("最长行：{line}");
        }
        None => println!("文件中没有文本行"),
    }

    if let Some(keyword) = keyword {
        println!("关键词“{keyword}”出现 {} 次", count_keyword(text, keyword));
    }

    println!("词频前 10：");
    for (index, (word, count)) in top_words(text, 10).iter().enumerate() {
        println!("{:>2}. {:<20} {}", index + 1, word, count);
    }
}

fn main() {
    let arguments: Vec<String> = env::args().collect();

    if arguments.len() < 2 || arguments.len() > 3 {
        eprintln!("用法：{} <文件路径> [关键词]", arguments[0]);
        process::exit(2);
    }

    let path = &arguments[1];
    let keyword = arguments.get(2).map(String::as_str);

    let text = match fs::read_to_string(path) {
        Ok(content) => content,
        Err(error) => {
            eprintln!("无法读取文件“{path}”：{error}");
            process::exit(1);
        }
    };

    print_report(path, &text, keyword);
}
```

运行：

```bash
cargo run -- sample.txt
cargo run -- sample.txt Rust
```

`--` 后的参数传递给程序，而不是 Cargo。

## 20.4 所有权分析

### 文件内容

```rust
let text = fs::read_to_string(path)?;
```

概念上，`read_to_string` 返回一个新的 `String`，调用方变量 `text` 获得所有权。程序结束时它自动释放。

### 报告函数借用内容

```rust
fn print_report(path: &str, text: &str, keyword: Option<&str>)
```

函数只读取路径和内容，因此接收 `&str`，不取得所有权。调用后 `text` 仍属于 `main`。

### 最长行返回切片

```rust
fn longest_line(text: &str) -> Option<(&str, usize)>
```

返回的行切片直接借用自 `text`，没有复制整行。字符数量是 `usize`，它实现 `Copy`。

### 词频为何拥有单词

词频表需要在遍历结束后继续保存键。规范化操作会生成新的小写字符串，因此使用 `HashMap<String, usize>` 让映射拥有这些键。

### 排序为何消费映射

```rust
frequencies.into_iter().collect()
```

`top_words` 后续不再需要映射，因此直接消费它并把键值移动到 `Vec`，避免再次克隆每个字符串。

## 20.5 项目改进任务

1. 将统计逻辑和命令行逻辑拆分到库与二进制入口。
2. 为每个纯函数编写单元测试。
3. 支持忽略关键词大小写。
4. 支持通过参数设置词频输出数量。
5. 跳过纯数字“单词”。
6. 对空文件给出清晰报告。
7. 统计平均每行字符数。
8. 支持只分析指定行范围。
9. 检查所有 `String` 参数，能用 `&str` 的地方进行调整。
10. 检查全部 `clone()`，写明保留或删除的理由。

---

## 二十一、针对性练习

## 21.1 移动分析

对每段代码画出所有权转移过程，并判断能否编译：

```rust
let first = String::from("a");
let second = first;
let third = second;
println!("{third}");
```

```rust,compile_fail
fn identity(text: String) -> String {
    text
}

let first = String::from("a");
let second = identity(first);
println!("{first} {second}");
```

```rust
fn identity(text: String) -> String {
    text
}

let first = String::from("a");
let first = identity(first);
println!("{first}");
```

## 21.2 借用改写

把下列函数改成不取得所有权：

```rust
fn starts_with_rust(text: String) -> bool {
    text.starts_with("Rust")
}
```

参考目标签名：

```rust
fn starts_with_rust(text: &str) -> bool
```

## 21.3 可变借用

编写函数：

```rust
fn trim_in_place(text: &mut String)
```

要求去掉两端空白并修改原字符串。思考 `trim()` 返回的切片与清空原字符串之间会产生什么借用关系。可先把需要保留的内容创建为独立 `String`，再修改原值。

## 21.4 切片函数

实现：

```rust
fn last_word(text: &str) -> &str
```

要求：

- 空字符串返回空切片
- 忽略末尾空白
- 不克隆单词
- 对中文文本不在字符中间切开

可以研究 `trim_end`、`rsplit_once` 或 `split_whitespace`。

## 21.5 数组切片

实现：

```rust
fn middle(values: &[i32]) -> Option<&i32>
```

- 空切片返回 `None`
- 奇数长度返回中间元素
- 偶数长度自行定义返回左中位或右中位，并写入文档

## 21.6 消除不必要的克隆

重构：

```rust
fn count_characters(text: String) -> usize {
    text.chars().count()
}

fn main() {
    let text = String::from("Rust 语言");
    let count = count_characters(text.clone());
    println!("{text}: {count}");
}
```

要求不使用 `clone()`，且调用后仍能输出原文本。

---

## 二十二、知识自测

尝试不查资料回答：

1. 所有权三条规则是什么？
2. `String` 变量在栈上大致保存哪些信息？
3. 为什么两个 `String` 不能默认共享同一堆指针并分别释放？
4. `let second = first;` 对 `i32` 和 `String` 分别发生什么？
5. move 与 clone 的成本有何不同？
6. 函数按值接收 `String` 表达了什么契约？
7. 借用和取得所有权有什么区别？
8. `&T` 和 `&mut T` 分别允许哪些操作？
9. 核心借用规则是什么？
10. 为什么不能同时使用两个指向同一值的可变引用？
11. 为什么 `Vec::push` 可能与已有元素引用冲突？
12. 引用的有效使用范围一定持续到花括号结束吗？
13. 什么是悬垂引用？Rust 如何阻止它？
14. `String` 和 `&str` 的所有权差异是什么？
15. 为什么只读字符串参数通常优先写成 `&str`？
16. 为什么不能随意按数字下标切割 UTF-8 字符串？
17. 切片相比返回索引提供了什么额外保证？
18. `&[T]` 与固定数组引用有什么差别？
19. `Copy` 和 `Clone` 的调用方式有何不同？
20. 为什么实现 `Drop` 的类型不能实现 `Copy`？
21. 什么情况下使用 `clone()` 是合理的？
22. 函数内部创建的 `String` 为什么不能以 `&String` 返回？

如果不能用自己的语言解释其中四项以上，请返回对应章节，并通过修改示例验证理解。

---

## 二十三、常见误区

- 把所有借用错误都用 `clone()` 消除
- 认为移动会复制整个堆数据
- 认为“在栈上”就必然实现 `Copy`
- 把 `&String` 当成所有只读字符串参数的默认类型
- 为了避免移动而让每个函数都接收引用
- 把引用作用域机械理解为花括号范围
- 在仍使用切片时修改其原始字符串
- 把字符串字节长度当作字符数量
- 使用不确定的字节索引切割 UTF-8 字符串
- 看到 `mut` 就认为可以无视可变借用唯一性
- 为返回局部变量引用而尝试添加 `'static`
- 盲目追求零克隆，导致 API 难用或设计过度复杂

---

## 二十四、阶段验收清单

### 概念能力

- [ ] 能解释栈与堆的基础差异
- [ ] 能准确陈述所有权三条规则
- [ ] 能区分 move、copy 和 clone
- [ ] 能判断常见赋值和传参是否移动所有权
- [ ] 能解释变量离开作用域时如何释放资源
- [ ] 能解释引用为何不会取得值的所有权
- [ ] 能陈述“多读或单写”的借用规则
- [ ] 能解释悬垂引用为何危险

### 编码能力

- [ ] 能使用 `&T` 编写只读函数
- [ ] 能使用 `&mut T` 编写修改函数
- [ ] 能通过调整使用顺序或作用域解决借用冲突
- [ ] 能合理选择 `String`、`&str` 和 `&mut String`
- [ ] 能安全使用字符串切片和数组切片
- [ ] 能使用 `split_at_mut` 取得不重叠的可变切片
- [ ] 能为自定义简单值类型合理派生 `Copy` 和 `Clone`
- [ ] 能说明 `Drop` 与资源清理的关系

### 项目能力

- [ ] 完成文本统计器基本功能
- [ ] 文本读取后只有清晰的所有者
- [ ] 只读函数优先接收切片
- [ ] 最长行等结果尽量返回借用切片
- [ ] 没有为绕过编译器而随意添加 `clone()`
- [ ] 能逐个说明项目中拥有类型和借用类型的选择理由
- [ ] 能处理空文件、无效路径和 UTF-8 文本

### 质量检查

```bash
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test
cargo run -- sample.txt Rust
```

当你能够拿到一段带有 `String`、函数调用和多个引用的代码，准确画出所有权转移与借用范围，并能独立完成文本统计器时，即可进入第三阶段：结构体、枚举与模式匹配。

---

## 二十五、进入下一阶段前的重构任务

回到第一阶段的四个项目，执行一次所有权审查：

1. 标记每个 `String` 的创建位置和最终所有者。
2. 标记每次函数调用是 move、copy 还是 borrow。
3. 把只读 `&String` 参数改为更通用的 `&str`。
4. 删除没有业务必要的 `clone()`。
5. 把接收固定数组或 `Vec`、但只需读取元素的函数改为切片参数。
6. 缩小可变变量和可变引用的作用域。
7. 检查字符串切片是否处于合法 UTF-8 边界。
8. 为每个函数写一句所有权契约说明。

第三阶段将使用结构体把相关数据组织在一起，使用枚举和 `Option<T>`、`Result<T, E>` 表达状态与失败。所有权知识会贯穿这些类型的构造、匹配和方法调用。
