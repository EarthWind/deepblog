# 第十阶段：Rust 宏、Unsafe、FFI 与底层原理

> 本阶段进入 Rust 的“可信计算基”。宏在编译期生成代码，`unsafe` 允许执行编译器无法证明安全的操作，FFI 连接 Rust 与其他语言。真正的目标不是多写不安全代码，而是把无法避免的不安全性压缩到小而可审计的边界中。

## 一、阶段目标

完成本阶段后，你应当能够：

- 使用 `macro_rules!` 编写实用声明宏
- 理解派生宏、属性宏和函数式过程宏的用途
- 知道过程宏为何需要独立 crate
- 准确列出 `unsafe` 允许的五类操作
- 区分 unsafe 语法许可与安全不变量证明
- 创建、读取和修改裸指针
- 理解空指针、悬垂指针、对齐、初始化与别名风险
- 使用 `#[repr(C)]` 建立稳定的 C 布局边界
- 使用 `extern "C"` 声明外部函数
- 安全转换 C 字符串和 Rust 字符串
- 明确跨 FFI 的所有权、分配器、错误和 panic 边界
- 为每个 `unsafe` 块写出可验证的 `SAFETY` 说明
- 理解 `Pin` 与自引用 Future 的基本问题
- 为一小段 C API 构建安全 Rust 包装层

---

## 二、宏与函数的区别

函数接收运行时值；宏接收 Rust 语法片段，在编译期生成代码。

宏适合：

- 可变参数语法，例如 `println!`
- 重复生成相似实现
- 创建领域专用语法
- 派生 Trait 实现
- 在编译期检查输入结构

不适合：

- 普通运行时计算
- 仅为了少写几行简单函数
- 隐藏复杂控制流和副作用
- 生成难以理解、难以定位错误的代码

优先级建议：

```text
普通函数 / 泛型 / Trait
        ↓ 确实无法清晰表达
声明宏
        ↓ 需要分析或生成复杂语法
过程宏
```

---

## 三、声明宏 `macro_rules!`

## 3.1 最小示例

```rust
macro_rules! say_hello {
    () => {
        println!("hello");
    };
}

fn main() {
    say_hello!();
}
```

宏由一个或多个“匹配器 => 展开模板”组成。

## 3.2 捕获表达式

```rust
macro_rules! twice {
    ($value:expr) => {{
        let temporary = $value;
        temporary + temporary
    }};
}

fn main() {
    let result = twice!(10 + 2);
    assert_eq!(result, 24);
}
```

使用局部变量保证传入表达式只求值一次。错误写法 `$value + $value` 可能让带副作用的表达式执行两次。

双层花括号让展开结果形成独立表达式作用域。

## 3.3 常见片段说明符

| 说明符 | 匹配内容 |
|---|---|
| `expr` | 表达式 |
| `ident` | 标识符 |
| `ty` | 类型 |
| `path` | 路径 |
| `pat` | 模式 |
| `stmt` | 语句 |
| `item` | 函数、结构体、impl 等条目 |
| `block` | 代码块 |
| `literal` | 字面量 |
| `tt` | 单个 token tree |

选择最具体的片段类型能得到更清楚的错误。

## 3.4 重复匹配

```rust
macro_rules! string_vec {
    ($($value:expr),* $(,)?) => {{
        let mut output = Vec::new();
        $(output.push($value.to_string());)*
        output
    }};
}

fn main() {
    let values = string_vec!["rust", 42, true,];
    assert_eq!(values, ["rust", "42", "true"]);
}
```

- `$()`：重复区域
- `,*`：用逗号分隔，重复零次或多次
- `$(,)?`：允许末尾可选逗号
- `+`：至少一次
- `?`：零次或一次

## 3.5 多个匹配分支

```rust
macro_rules! calculate {
    (add $left:expr, $right:expr) => { $left + $right };
    (mul $left:expr, $right:expr) => { $left * $right };
}
```

分支按顺序尝试。应尽量让语法无歧义，并用 `compile_error!` 提供清晰失败说明。

## 3.6 生成条目

```rust
macro_rules! define_id {
    ($name:ident) => {
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        struct $name(u64);

        impl From<u64> for $name {
            fn from(value: u64) -> Self {
                Self(value)
            }
        }
    };
}

define_id!(UserId);
define_id!(OrderId);
```

## 3.7 宏卫生

宏展开中的局部标识符通常不会意外捕获调用方同名变量。对外部路径，公共宏常使用 `$crate` 指向定义宏的 crate：

```rust,ignore
#[macro_export]
macro_rules! make_error {
    ($message:expr) => {
        $crate::Error::new($message)
    };
}
```

`#[macro_export]` 会把宏导出到 crate 根，应谨慎使用以避免公共命名空间污染。

## 3.8 调试宏

方法：

- 从最小调用开始
- 将复杂宏拆成内部辅助规则
- 使用编译器错误位置观察匹配失败
- 使用稳定的宏展开工具或 IDE 功能查看生成代码
- 为成功和编译失败场景分别测试

宏的错误体验是 API 质量的一部分。

---

## 四、过程宏概览

过程宏接收 token stream，返回新的 token stream。通常单独放在 `proc-macro = true` 的 library crate 中。

三类：

## 4.1 自定义派生宏

```rust,ignore
#[derive(MySerialize)]
struct User {
    name: String,
}
```

适合根据结构体或枚举定义生成 Trait 实现。

## 4.2 属性宏

```rust,ignore
#[route(GET, "/users")]
fn list_users() {}
```

可接收属性参数并替换所修饰条目。

## 4.3 函数式过程宏

```rust,ignore
let query = sql!(SELECT * FROM users);
```

外观类似函数调用，但输入是 Rust token。

## 4.4 过程宏工程结构

```text
workspace/
├── Cargo.toml
├── my-macro/
│   ├── Cargo.toml
│   └── src/lib.rs
└── app/
    ├── Cargo.toml
    └── src/main.rs
```

过程宏 crate 通常依赖语法解析和 token 生成工具。具体 API 会演进，应以项目锁定版本文档为准。

## 4.5 过程宏质量要求

- 保留并使用正确源码 span
- 对无效输入给出精确编译错误
- 不 panic 于普通用户输入
- 对泛型、生命周期、where 子句和属性进行完整处理
- 使用专门编译测试覆盖应通过和应失败的调用
- 不生成依赖调用方偶然导入的未限定名称

初学阶段重点是理解用途和边界，不必立即实现大型 derive 框架。

---

## 五、`unsafe` 的含义

`unsafe` 不会关闭借用检查器，也不会让任何代码自动变快。它只允许执行编译器无法验证的特定操作，并把证明义务交给程序员。

## 5.1 五类 unsafe 能力

在 unsafe 上下文中可以：

1. 解引用裸指针。
2. 调用 unsafe 函数或方法。
3. 访问或修改可变静态变量。
4. 实现 unsafe Trait。
5. 访问 `union` 字段。

创建裸指针本身通常安全，解引用才需要 unsafe。

## 5.2 unsafe 函数与 unsafe 块

```rust
unsafe fn read_raw(pointer: *const i32) -> i32 {
    // 即使函数本身是 unsafe，也建议把具体不安全操作放入小块中。
    unsafe { *pointer }
}
```

调用方必须满足 unsafe 函数文档中的安全前置条件。

在现代 Rust 工程中，推荐即使位于 `unsafe fn` 内也显式标出 unsafe 操作，这使审计边界清楚，并配合相应 lint。

## 5.3 安全性是整体属性

一个 unsafe 块可能只有一行，但其安全性依赖周围安全代码维护的不变量。审计时要查看：

- 裸指针从哪里来
- 所指内存由谁拥有
- 有效期多长
- 是否正确对齐和初始化
- 是否存在并发写入或别名冲突
- 长度和容量来自哪里
- 谁负责释放，使用哪个分配器

---

## 六、安全不变量与 `SAFETY` 注释

不合格注释：

```rust,ignore
unsafe { *pointer } // 这里没问题
```

合格注释应说明为什么满足前置条件：

```rust
fn read_first(values: &[i32]) -> Option<i32> {
    if values.is_empty() {
        return None;
    }

    let pointer = values.as_ptr();
    // SAFETY: values 非空，所以 pointer 指向至少一个已初始化、正确对齐的 i32；
    // 读取期间 values 保持共享借用，不会发生可变访问或释放。
    Some(unsafe { *pointer })
}
```

每个 unsafe 块至少回答：

- 哪个 API 前置条件需要证明？
- 哪些检查或类型关系保证它？
- 该保证在未来重构中如何保持？

注释不能替代验证，但它让代码审查者知道应验证什么。

---

## 七、裸指针

裸指针类型：

- `*const T`：常用于只读访问
- `*mut T`：常用于可写访问

裸指针：

- 可以为空
- 可以悬垂
- 不自动遵守借用规则
- 不保证所指内存已初始化或对齐
- 没有自动生命周期跟踪

## 7.1 从引用创建裸指针

```rust
fn main() {
    let mut value = 42;
    let const_pointer = &raw const value;
    let mut_pointer = &raw mut value;

    // SAFETY: 两个指针来自仍存活且正确对齐的局部变量；
    // 此处按顺序访问，且没有与写入重叠的其他读取。
    unsafe {
        println!("{}", *const_pointer);
        *mut_pointer = 100;
    }

    println!("{value}");
}
```

也常见引用转换：

```rust
let pointer = &value as *const i32;
```

## 7.2 指针算术

```rust
fn get_unchecked_copy(values: &[i32], index: usize) -> Option<i32> {
    if index >= values.len() {
        return None;
    }

    // SAFETY: index < len，as_ptr 指向切片第一个元素；
    // add(index) 仍位于同一分配中的已初始化元素。
    Some(unsafe { *values.as_ptr().add(index) })
}
```

标准索引已足够安全高效。此例只用于理解证明义务，不应替换普通代码。

## 7.3 空指针

```rust
use std::ptr;

let pointer: *const i32 = ptr::null();
assert!(pointer.is_null());
```

解引用空指针是未定义行为。FFI 接收指针时必须在解引用前明确检查是否允许为空。

---

## 八、未定义行为的常见来源

未定义行为（UB）意味着编译器不再保证程序行为。可能表现为崩溃、错误结果、似乎正常或被优化成意外代码。

常见风险：

- 解引用空指针或悬垂指针
- 访问越界内存
- 读取未初始化内存
- 使用未对齐指针执行要求对齐的访问
- 构造无效值，例如非法 `bool` 或引用
- 违反别名规则，同时存在不允许重叠的读写
- 数据竞争
- 使用错误布局解释字节
- 重复释放或使用错误分配器释放
- 从错误长度/容量重建 `Vec`
- C 字符串缺少终止零字节
- FFI 函数签名或 ABI 声明错误
- panic 穿越不允许展开的 FFI 边界

“程序在我的机器上运行正常”不是安全证明。

---

## 九、安全封装示例：切片分割

安全 Rust 难以用两个可变切片直接表达手工分割，实现底层可借助裸指针：

```rust
fn split_at_mut<T>(values: &mut [T], middle: usize) -> (&mut [T], &mut [T]) {
    assert!(middle <= values.len());

    let length = values.len();
    let pointer = values.as_mut_ptr();

    // SAFETY:
    // 1. middle <= length，因此两个范围都位于原分配内；
    // 2. [0, middle) 与 [middle, length) 不重叠；
    // 3. pointer 来自有效可变切片，元素均已初始化且正确对齐；
    // 4. 原切片的独占借用持续覆盖返回切片的生命周期。
    unsafe {
        (
            std::slice::from_raw_parts_mut(pointer, middle),
            std::slice::from_raw_parts_mut(pointer.add(middle), length - middle),
        )
    }
}
```

安全包装的原则：

- 在进入 unsafe 前完成边界检查
- unsafe 块尽量小
- 返回安全类型
- 不把调用者无法验证的隐含责任泄露到安全 API
- 为边界、空输入和重叠条件写测试

实际项目直接使用标准库 `split_at_mut`。

---

## 十、内存布局、大小与对齐

```rust
use std::mem::{align_of, size_of};

fn main() {
    println!("u8: size={}, align={}", size_of::<u8>(), align_of::<u8>());
    println!("u64: size={}, align={}", size_of::<u64>(), align_of::<u64>());
}
```

结构体可能包含填充字节，以满足字段对齐。默认 Rust 布局不承诺适合作为稳定 FFI 格式。

## 10.1 `repr(C)`

```rust
#[repr(C)]
#[derive(Debug, Clone, Copy)]
struct Point {
    x: f64,
    y: f64,
}
```

`repr(C)` 使用适合 C 互操作的字段布局规则，但不自动保证：

- 所有字段在目标 C 平台类型完全一致
- 类型可以安全从任意字节构造
- 指针字段有效
- 二进制格式可跨平台持久化

## 10.2 `repr(transparent)`

单字段新类型可使用透明布局与内部字段保持 ABI 兼容：

```rust
#[repr(transparent)]
struct FileHandle(*mut std::ffi::c_void);
```

仍需满足该属性的结构限制和目标 ABI 契约。

## 10.3 `repr(packed)` 风险

压缩布局可能导致字段未对齐。创建指向未对齐字段的普通引用本身就可能无效。除非处理明确外部二进制格式且完全理解未对齐访问，否则不要使用。

二进制解析更适合逐字段读取并显式处理字节序，而不是把任意字节强制转换为结构体引用。

---

## 十一、FFI 基础

FFI 允许 Rust 调用遵循外部 ABI 的函数，或向外部语言导出函数。

## 11.1 调用外部函数

现代 edition 中 extern 块需要标记为 unsafe：

```rust,ignore
use std::ffi::{c_char, c_int};

unsafe extern "C" {
    fn external_function(name: *const c_char) -> c_int;
}
```

声明正确性由程序员保证：函数名称、ABI、参数、返回类型和链接库必须与外部定义完全一致。

调用：

```rust,ignore
let result = unsafe { external_function(pointer) };
```

## 11.2 导出 Rust 函数

```rust,ignore
use std::ffi::c_int;

#[unsafe(no_mangle)]
pub extern "C" fn rust_add(left: c_int, right: c_int) -> c_int {
    left.saturating_add(right)
}
```

`extern "C"` 指定 C ABI。导出名称、属性语法和链接方式应按所用 Rust edition、crate 类型和目标平台验证。

## 11.3 FFI 安全类型

边界常用：

- `std::ffi::c_*` C 标量别名
- 固定宽度整数，如 `u32`
- `#[repr(C)]` 结构体
- 裸指针
- 长度与指针配对
- 明确 ABI 的函数指针

不应直接跨通用 C ABI 传递：

- `String`
- `Vec<T>`
- `&str`
- Rust Trait 对象
- 未标注布局的 Rust 枚举或结构体
- `Result<T, E>`

这些类型的布局和所有权语义不是 C ABI 契约。

---

## 十二、C 字符串

C 字符串通常是以 `\0` 结尾的字节序列；Rust `String` 是带长度的 UTF-8 数据。

## 12.1 Rust 到 C

```rust
use std::ffi::CString;

fn main() -> Result<(), std::ffi::NulError> {
    let text = CString::new("hello")?;
    let pointer = text.as_ptr();

    // pointer 只在 text 存活且未被修改/释放期间有效。
    println!("{pointer:p}");
    Ok(())
}
```

`CString::new` 会拒绝内部零字节，因为它会让 C 字符串提前结束。

如果 C API 保存该指针供调用后使用，不能只传临时 `CString::as_ptr()`；必须按 API 约定延长存储寿命或转移所有权。

## 12.2 C 到 Rust

```rust,ignore
use std::ffi::{c_char, CStr};

unsafe fn copy_c_string(pointer: *const c_char) -> Result<String, String> {
    if pointer.is_null() {
        return Err(String::from("收到空字符串指针"));
    }

    // SAFETY: 调用方必须保证 pointer 指向可读、以 NUL 结尾的字节序列，
    // 且该序列在本次调用期间保持有效。
    let text = unsafe { CStr::from_ptr(pointer) };
    text.to_str()
        .map(str::to_owned)
        .map_err(|error| format!("字符串不是有效 UTF-8：{error}"))
}
```

C 字符串不一定是 UTF-8。根据领域需求选择：

- `to_str()`：无效 UTF-8 返回错误
- `to_string_lossy()`：替换无效序列
- `to_bytes()`：保留原始字节

不要无条件假设外部文本是 UTF-8。

---

## 十三、FFI 所有权与分配器

最危险的问题通常不是函数调用，而是谁拥有内存。

每个指针 API 必须回答：

- 指针可否为空？
- 指向单个值还是数组？
- 数组长度是多少？
- 数据由谁分配？
- 谁负责释放？
- 使用哪个释放函数和分配器？
- 调用后指针是否仍有效？
- 函数是否保存指针？保存多久？
- 是否允许并发访问？

## 13.1 Rust 分配、Rust 释放

```rust,ignore
#[unsafe(no_mangle)]
pub extern "C" fn create_value(value: i32) -> *mut i32 {
    Box::into_raw(Box::new(value))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn destroy_value(pointer: *mut i32) {
    if pointer.is_null() {
        return;
    }

    // SAFETY: 调用方必须保证 pointer 由 create_value 返回，尚未释放，
    // 且本函数只调用一次。Box::from_raw 恢复所有权并在作用域结束时释放。
    unsafe { drop(Box::from_raw(pointer)) };
}
```

不能把任意指针交给 `Box::from_raw`。它必须来自兼容的 `Box::into_raw` 分配，类型、对齐和所有权都正确。

## 13.2 字符串返回

若 Rust 返回字符串给 C，必须同时提供 Rust 侧释放函数，或让调用方提供输出缓冲区。绝不能让 C 的 `free` 随意释放 Rust `String` 缓冲区，除非 ABI 明确约定同一分配器和表示。

推荐接口风格：

- 句柄创建/销毁成对
- `pointer + length` 表达字节缓冲区
- 查询所需长度后由调用方分配
- 错误码加独立错误消息获取函数

---

## 十四、错误与 panic 边界

C ABI 不理解 Rust 的 `Result` 和 panic。

## 14.1 错误码

```rust,ignore
#[repr(C)]
pub enum Status {
    Ok = 0,
    NullPointer = 1,
    InvalidInput = 2,
    InternalError = 3,
}
```

对外枚举应固定表示，并确保外部声明一致。也可使用明确整数常量。

## 14.2 防止 panic 穿越边界

导出 API 应避免 panic。必要时在边界捕获：

```rust,ignore
use std::panic::{catch_unwind, AssertUnwindSafe};

pub extern "C" fn ffi_entry() -> i32 {
    match catch_unwind(AssertUnwindSafe(|| internal_work())) {
        Ok(Ok(())) => 0,
        Ok(Err(_)) => 1,
        Err(_) => 2,
    }
}
```

`catch_unwind` 不是通用异常机制，也不捕获直接终止进程的 panic 策略。被捕获后仍需确保内部状态满足不变量。

不要让 panic 穿过不支持展开的外部 ABI 边界，否则可能导致未定义行为或进程终止，具体取决于 ABI 和策略。

---

## 十五、回调与上下文指针

C 常用函数指针加 `void* user_data`：

```rust,ignore
use std::ffi::c_void;

type Callback = unsafe extern "C" fn(value: i32, context: *mut c_void);
```

安全包装需要处理：

- 回调的调用线程
- 调用次数和时机
- context 生命周期
- 是否并发调用
- 外部库停止回调的同步保证
- panic 不穿越回调边界

将闭包装箱并转为裸指针后，必须在外部不再回调时准确恢复并释放。过早释放导致悬垂，忘记释放导致泄漏，重复恢复导致重复释放。

回调包装是高风险区域，应使用成熟模式、集中实现并进行压力与线程测试。

---

## 十六、Opaque Handle 模式

不向 C 暴露 Rust 结构布局，而只暴露不透明句柄：

```rust,ignore
pub struct Engine {
    // Rust 私有实现
}

#[unsafe(no_mangle)]
pub extern "C" fn engine_new() -> *mut Engine {
    Box::into_raw(Box::new(Engine::new()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn engine_run(engine: *mut Engine) -> i32 {
    let Some(engine) = (unsafe { engine.as_mut() }) else {
        return 1;
    };
    match engine.run() {
        Ok(()) => 0,
        Err(_) => 2,
    }
}
```

C 头文件只声明不完整类型：

```c
typedef struct Engine Engine;
Engine *engine_new(void);
int engine_run(Engine *engine);
void engine_free(Engine *engine);
```

优点：

- Rust 内部布局可改变
- ABI 表面更小
- 不变量集中在 Rust API
- 调用者无法直接修改字段

仍需明确句柄线程安全、空指针和释放规则。

---

## 十七、Unsafe Trait

unsafe Trait 表示实现者必须满足编译器无法验证、且其他安全代码会依赖的不变量。

```rust,ignore
unsafe trait TrustedBuffer {
    fn pointer(&self) -> *const u8;
    fn length(&self) -> usize;
}
```

实现需要 `unsafe impl`：

```rust,ignore
unsafe impl TrustedBuffer for MyBuffer {
    // 必须证明 pointer/length 始终描述有效可读区域
}
```

标准库中的 `Send`、`Sync` 是典型 unsafe auto Trait。手工实现它们风险极高：错误实现可能让安全代码产生数据竞争。

安全要求必须写在 Trait 文档的 `# Safety` 小节中，实现处写证明依据。

---

## 十八、`union`

```rust
#[repr(C)]
union Number {
    integer: u32,
    float: f32,
}
```

访问 union 字段需要 unsafe，因为编译器不知道当前实际存储的是哪个变体。

```rust
let number = Number { integer: 0x3f80_0000 };
// SAFETY: 此处有意按相同 32 位表示读取为 f32，且两字段大小布局已知。
let value = unsafe { number.float };
println!("{value}");
```

实际 FFI 中常有独立 tag 字段。安全包装必须先验证 tag，再读取对应 union 字段。tag 与内容不一致可能产生无效值。

---

## 十九、Pin 的基本问题

普通 Rust 值可以移动：赋值、返回、放入集合都可能改变其内存地址。大多数类型不关心地址。

某些状态机或底层结构可能包含指向自身字段的指针。一旦移动，内部指针会失效。

`Pin<P>` 表达“通过这个指针访问的值不能再被安全移动”，前提是类型不实现 `Unpin`。

## 19.1 `Unpin`

多数普通类型自动实现 `Unpin`，即使被 Pin 包装也可安全移动。真正地址敏感的类型会是 `!Unpin`，常借助 `PhantomPinned` 表达。

## 19.2 Pin 不保证所有内存安全

Pin 只约束移动，不自动保证：

- 指针有效
- 字段初始化顺序正确
- 对象不会提前释放
- 自引用构造正确
- 并发访问安全

## 19.3 与 Future 的关系

编译器生成的异步状态机可能在某些状态中持有对自身字段的引用，因此 `Future::poll` 接收 `Pin<&mut Self>`。日常 async Rust 通常由 `.await`、`Box::pin` 和 runtime 处理这些细节。

不要手写自引用结构作为入门练习。优先使用索引、拥有数据、Pin 投影工具或成熟库。

---

## 二十、阶段项目：安全 FFI 包装层

## 20.1 项目目标

假设一个 C 库提供文本缓冲区 API：

```c
typedef struct text_buffer text_buffer;

text_buffer *text_buffer_new(const char *text);
int text_buffer_append(text_buffer *buffer, const char *text);
const char *text_buffer_data(const text_buffer *buffer);
size_t text_buffer_len(const text_buffer *buffer);
void text_buffer_free(text_buffer *buffer);
```

Rust 侧目标：

- 原始绑定集中在 `sys` 模块
- 提供拥有句柄的安全 `TextBuffer`
- 构造时处理内部 NUL
- 空指针和错误码转成 `Result`
- `Drop` 中只释放一次
- 返回文本时复制或提供生命周期正确的借用
- 安全 API 不暴露裸指针
- 每个 unsafe 块有 `SAFETY` 说明

## 20.2 原始绑定层

```rust,ignore
mod sys {
    use std::ffi::{c_char, c_int, c_void};

    pub type TextBuffer = c_void;

    unsafe extern "C" {
        pub fn text_buffer_new(text: *const c_char) -> *mut TextBuffer;
        pub fn text_buffer_append(
            buffer: *mut TextBuffer,
            text: *const c_char,
        ) -> c_int;
        pub fn text_buffer_data(buffer: *const TextBuffer) -> *const c_char;
        pub fn text_buffer_len(buffer: *const TextBuffer) -> usize;
        pub fn text_buffer_free(buffer: *mut TextBuffer);
    }
}
```

原始层保持接近 C，不承担安全承诺。

## 20.3 错误类型

```rust
#[derive(Debug)]
pub enum BufferError {
    InteriorNul(std::ffi::NulError),
    AllocationFailed,
    AppendFailed(i32),
    NullData,
    InvalidUtf8(std::str::Utf8Error),
}
```

实现 `Display`、`Error` 和来源链。

## 20.4 安全包装

```rust,ignore
use std::ffi::{CStr, CString};
use std::ptr::NonNull;

pub struct TextBuffer {
    handle: NonNull<sys::TextBuffer>,
}

impl TextBuffer {
    pub fn new(text: &str) -> Result<Self, BufferError> {
        let text = CString::new(text).map_err(BufferError::InteriorNul)?;

        // SAFETY: text.as_ptr() 指向以 NUL 结尾且在调用期间有效的 C 字符串；
        // 外部函数签名与链接的 C 头文件一致。
        let pointer = unsafe { sys::text_buffer_new(text.as_ptr()) };
        let handle = NonNull::new(pointer).ok_or(BufferError::AllocationFailed)?;
        Ok(Self { handle })
    }

    pub fn append(&mut self, text: &str) -> Result<(), BufferError> {
        let text = CString::new(text).map_err(BufferError::InteriorNul)?;

        // SAFETY: self.handle 来自成功的构造函数且尚未释放；
        // &mut self 保证本次调用期间没有通过安全 API 的并发可变访问；
        // text 在调用期间保持有效并以 NUL 结尾。
        let status = unsafe {
            sys::text_buffer_append(self.handle.as_ptr(), text.as_ptr())
        };

        if status == 0 {
            Ok(())
        } else {
            Err(BufferError::AppendFailed(status))
        }
    }

    pub fn to_string(&self) -> Result<String, BufferError> {
        // SAFETY: handle 有效；函数按 C API 契约返回内部以 NUL 结尾的只读指针。
        let pointer = unsafe { sys::text_buffer_data(self.handle.as_ptr()) };
        if pointer.is_null() {
            return Err(BufferError::NullData);
        }

        // SAFETY: 已检查非空；C API 保证指针在 self 存活且未修改期间有效，
        // 并指向以 NUL 结尾的字节。立即复制为拥有的 String，避免泄露借用风险。
        let text = unsafe { CStr::from_ptr(pointer) };
        Ok(text.to_str().map_err(BufferError::InvalidUtf8)?.to_owned())
    }

    pub fn len(&self) -> usize {
        // SAFETY: handle 在 self 存活期间有效，查询函数不修改或释放对象。
        unsafe { sys::text_buffer_len(self.handle.as_ptr()) }
    }
}

impl Drop for TextBuffer {
    fn drop(&mut self) {
        // SAFETY: handle 只由本对象拥有，来自 text_buffer_new，且尚未释放；
        // Drop 对每个成功构造的 TextBuffer 只执行一次。
        unsafe { sys::text_buffer_free(self.handle.as_ptr()) }
    }
}
```

## 20.5 `Send` 和 `Sync`

不要自动为句柄包装器实现 `Send` 或 `Sync`。必须查清 C 库：

- 对象是否可跨线程移动？
- 多线程能否同时读取？
- 是否有线程亲和性？
- 库是否使用全局可变状态？

只有契约明确且能证明时，才考虑 `unsafe impl Send/Sync`，并记录安全依据。

## 20.6 构建脚本

实际项目可：

- 链接系统已安装动态/静态库
- 使用 `build.rs` 编译 bundled C 源码
- 使用绑定生成工具生成 `sys` 声明

构建脚本和工具 API 会演进，应以项目版本文档为准。生成后的绑定仍不等于安全，必须在上层包装。

## 20.7 测试清单

- 空字符串
- 普通 ASCII
- 有效 UTF-8
- 内部 NUL 被拒绝
- 超长输入
- append 成功与错误码
- C 返回空指针
- C 返回无效 UTF-8
- 多次创建与销毁
- 销毁函数只调用一次
- 在内存检查工具下运行
- 不允许 safe API 制造悬垂借用

## 20.8 扩展任务

1. 返回借用 `&CStr`，正确绑定到 `&self` 生命周期，并说明修改后失效规则。
2. 支持非 UTF-8 字节接口。
3. 为 C 调用增加状态码到错误枚举映射。
4. 生成 C 头文件并编写 C 调用示例。
5. 导出 Rust opaque handle API 给 C。
6. 捕获导出边界 panic。
7. 增加回调注册，并正确管理上下文生命周期。
8. 使用动态分析工具验证泄漏和越界。

---

## 二十一、Unsafe 代码审查清单

### 指针

- [ ] 非空要求是否检查
- [ ] 指针对齐是否满足
- [ ] 指向内存是否已初始化
- [ ] 长度是否正确且没有溢出
- [ ] 指针是否仍在原分配范围内
- [ ] 生命周期内是否不会释放或重分配

### 别名与并发

- [ ] 可变访问是否独占
- [ ] 是否存在同时读写
- [ ] 是否可能跨线程数据竞争
- [ ] `Send` / `Sync` 实现是否有外部契约支持

### 所有权

- [ ] 谁分配、谁释放
- [ ] 分配器是否匹配
- [ ] 是否可能重复释放
- [ ] 是否可能泄漏
- [ ] 从裸指针恢复所有权是否只发生一次

### FFI

- [ ] ABI、符号、参数和返回类型是否一致
- [ ] `repr(C)` 和整数宽度是否正确
- [ ] 字符串编码和 NUL 规则是否明确
- [ ] 错误码是否完整处理
- [ ] panic 是否被限制在 Rust 边界内
- [ ] 外部库调用线程和回调规则是否明确

### 文档与测试

- [ ] 每个 unsafe 块有具体 `SAFETY` 说明
- [ ] unsafe 函数有 `# Safety` 文档
- [ ] 安全包装无法被普通调用者轻易破坏
- [ ] 边界、空值、错误和释放路径有测试
- [ ] 使用适当工具进行动态检查

---

## 二十二、常见误区

- 认为 unsafe 会关闭所有编译器检查
- 为了性能在没有基准时使用裸指针
- unsafe 块包围整个大函数
- `SAFETY` 注释只写“这里安全”
- 从运行正常推断没有未定义行为
- 把任意字节转换为引用或结构体
- 用 `repr(C)` 误以为类型对任意字节都有效
- 让 C 释放 Rust 分配的内存，或反过来
- 把临时 `CString::as_ptr()` 保存到调用结束之后
- 假设 C 文本一定是 UTF-8
- 让 panic 穿越 FFI 边界
- 随意手工实现 `Send` 或 `Sync`
- 用强制 `'static` 或泄漏内存解决回调生命周期
- 为减少重复代码编写难以理解的宏
- 过程宏对普通错误输入直接 panic
- 手写自引用结构却没有完整 Pin 不变量证明

---

## 二十三、针对性练习

1. 编写支持尾逗号的 `hashmap!` 声明宏。
2. 编写宏生成若干新类型 ID 及转换实现。
3. 为宏添加无效输入编译测试。
4. 用裸指针实现教学版 `get_unchecked_copy`，先做边界检查。
5. 手工实现教学版 `split_at_mut` 并写安全证明。
6. 检查几个类型的 `size_of` 和 `align_of`，解释填充。
7. 用 `CString` 和 `CStr` 完成双向字符串转换。
8. 设计 opaque handle 的创建、调用和销毁 API。
9. 为一个导出函数设计错误码和 panic 边界。
10. 审查一段 unsafe 代码，列出所有未被证明的前置条件。

---

## 二十四、知识自测

1. 宏和函数分别在什么阶段工作？
2. 声明宏为什么应避免重复求值输入表达式？
3. 三类过程宏是什么？
4. unsafe 允许哪五类操作？
5. unsafe 块是否自动让代码正确？
6. 裸指针与引用有哪些关键差异？
7. 什么是未定义行为？
8. `SAFETY` 注释应说明什么？
9. `repr(C)` 保证什么、不保证什么？
10. 为什么压缩结构体字段可能不能创建普通引用？
11. C 字符串与 Rust 字符串的表示有何差异？
12. `CString::new` 为什么可能失败？
13. `CStr::from_ptr` 需要哪些前置条件？
14. 为什么跨 FFI 释放必须匹配分配器？
15. `Box::from_raw` 为什么只能调用一次？
16. 为什么不能直接跨 C ABI 传 `String` 或 `Vec`？
17. 如何阻止 panic 穿越 FFI 边界？
18. opaque handle 有什么好处？
19. `Weak`、裸指针和 opaque handle 的所有权语义有何区别？
20. `Pin` 主要约束什么？`Unpin` 又表示什么？

---

## 二十五、阶段验收清单

### 宏

- [ ] 能编写表达式、标识符和重复匹配声明宏
- [ ] 能避免输入表达式被重复求值
- [ ] 理解宏卫生和 `$crate`
- [ ] 能区分三类过程宏
- [ ] 能为宏设计清晰编译错误

### Unsafe

- [ ] 能列出五类 unsafe 能力
- [ ] 能写出具体安全不变量
- [ ] 能把 unsafe 缩小到最小边界
- [ ] 能识别空、悬垂、越界、未初始化和未对齐指针
- [ ] 能识别别名冲突与数据竞争
- [ ] 不以性能猜测作为 unsafe 理由

### FFI

- [ ] 能声明和调用 C ABI 函数
- [ ] 能使用 `repr(C)` 和 C 标量类型
- [ ] 能安全转换 `CString` / `CStr`
- [ ] 能明确跨边界所有权与释放函数
- [ ] 能设计 opaque handle API
- [ ] 能阻止 panic 穿越外部边界
- [ ] 不随意实现 `Send` / `Sync`

### 项目

- [ ] 原始绑定和安全包装分层
- [ ] 安全 API 不暴露不必要裸指针
- [ ] 所有空指针和错误码得到处理
- [ ] `Drop` 准确释放一次
- [ ] 每个 unsafe 块有可审计说明
- [ ] 覆盖无效 UTF-8、内部 NUL、空指针和释放测试
- [ ] 至少完成三个扩展任务

### 质量检查

```bash
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test --all-features
cargo doc --no-deps
```

对于 unsafe 和 FFI 项目，还应在适用平台使用解释器、内存检查器、地址消毒器或其他动态分析工具。工具支持和命令随平台与工具链变化，应查阅当前官方说明。

当你能逐条证明 unsafe 前置条件、让安全调用者无法破坏这些条件，并能清楚定义跨语言的布局、所有权、错误与释放契约时，即可进入第十一阶段：性能优化与生产实践。

---

## 二十六、进入下一阶段前的预习

下一阶段将重点回答：

- 如何建立可信的性能基线？
- 如何区分 CPU、内存、锁和 I/O 瓶颈？
- 如何使用 profiler 和火焰图定位热点？
- 零拷贝何时真正有效？
- release profile、LTO 和 codegen units 如何影响产物？
- 如何建立日志、指标、追踪、CI 和供应链安全？

请保留本阶段的安全包装与普通安全实现。下一阶段应先测量，再决定是否需要优化或引入 unsafe；任何优化都必须同时验证性能收益和安全不变量。
