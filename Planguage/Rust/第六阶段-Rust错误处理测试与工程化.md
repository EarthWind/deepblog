# 第六阶段：Rust 错误处理、测试与工程化

> 能运行的程序只是起点。生产代码还需要明确失败语义、稳定测试、可维护的项目结构和自动化质量门禁。本阶段将把前五阶段的语言能力转化为可交付工程能力。

## 一、阶段目标

完成本阶段后，你应当能够：

- 判断何时使用 `panic!`，何时返回 `Result`
- 使用 `?` 传播并转换错误
- 设计可匹配、可展示、可追踪来源的错误类型
- 保留错误链，而不是把所有失败压扁为字符串
- 编写单元测试、集成测试和文档测试
- 测试正常路径、边界条件和失败路径
- 理解 `Cargo.toml`、`Cargo.lock` 与语义化版本
- 使用 Cargo feature 和 workspace 组织工程
- 分离 I/O、配置解析和纯业务逻辑
- 建立格式化、Clippy、测试、文档和 release 构建门禁
- 完成具有完善错误处理和测试的迷你 grep

---

## 二、错误分类

程序中的“错误”并不完全相同，至少可分为：

1. **可恢复错误**：文件不存在、用户参数无效、网络暂时失败、权限不足。
2. **程序缺陷**：数组越界、不变量被破坏、不可能分支被执行。
3. **资源耗尽或环境故障**：内存不足、磁盘写满、系统调用失败。

Rust 的主要表达方式：

- `Result<T, E>`：调用者可以处理或传播的失败
- `Option<T>`：值可能正常缺失，但不强调错误原因
- panic：当前执行无法安全继续，通常表示缺陷或无法恢复的启动条件

设计 API 时，先问“调用者能否采取有意义的行动”。能重试、换路径、提示用户或选择降级时，通常应返回 `Result`。

---

## 三、`panic!` 的边界

```rust,should_panic
fn main() {
    panic!("程序无法继续");
}
```

可能触发 panic 的常见操作：

- 显式 `panic!`
- 对 `None` 调用 `unwrap`
- 对 `Err` 调用 `unwrap`
- 数组或切片越界索引
- debug 构建中的整数溢出
- 断言失败

## 3.1 适合 panic 的情况

- 代码违反内部不变量
- 进入理论上不可达的分支
- 示例或测试中希望快速暴露失败
- 程序启动所需的静态配置由开发者完全控制，缺失意味着部署缺陷
- 继续执行可能产生错误结果或破坏数据

```rust
fn percentage(value: u8) -> u8 {
    assert!(value <= 100, "百分比必须在 0..=100 内");
    value
}
```

如果输入来自用户，上述 API 更适合返回 `Result`；如果调用前已由类型或内部流程保证范围，断言可用于检测程序缺陷。

## 3.2 不适合 panic 的情况

- 用户输入格式错误
- 文件可能不存在
- 远程服务可能不可用
- 数据库查询可能失败
- 查找可能无结果
- 库的调用者能够选择处理策略

库代码尤其应谨慎 panic，因为库不应轻易替应用决定整个进程退出。

## 3.3 `unwrap` 与 `expect`

```rust
let port: u16 = "8080"
    .parse()
    .expect("硬编码端口必须是有效 u16");
```

`expect` 比 `unwrap` 多一条上下文信息。如果某个值确实由代码不变量保证成功，`expect` 的说明应解释“为什么不可能失败”，而不是重复“解析失败”。

---

## 四、`Result<T, E>` 基础

```rust
use std::fs::File;
use std::io;

fn open_config() -> Result<File, io::Error> {
    File::open("config.txt")
}
```

调用者可匹配错误：

```rust
use std::fs::File;
use std::io::ErrorKind;

fn main() {
    match File::open("config.txt") {
        Ok(file) => println!("打开成功：{file:?}"),
        Err(error) if error.kind() == ErrorKind::NotFound => {
            println!("配置文件不存在");
        }
        Err(error) => eprintln!("打开失败：{error}"),
    }
}
```

不要通过错误文本判断类型，应使用结构化变体或 `kind()` 等 API。

## 4.1 常用组合方法

```rust
fn parse_port(input: Option<&str>) -> Result<u16, String> {
    input
        .ok_or_else(|| String::from("缺少端口"))?
        .parse::<u16>()
        .map_err(|error| format!("端口格式错误：{error}"))
}
```

- `map`：转换成功值
- `map_err`：转换错误值
- `and_then`：成功后继续可能失败的操作
- `or_else`：失败后尝试恢复
- `unwrap_or` / `unwrap_or_else`：提供默认值
- `ok`：把 `Result<T, E>` 转为 `Option<T>`，会丢弃错误

丢弃错误前必须确认原因确实不重要。

---

## 五、`?` 运算符与错误传播

手动传播：

```rust
use std::fs;
use std::io;

fn read_username(path: &str) -> Result<String, io::Error> {
    let content = match fs::read_to_string(path) {
        Ok(value) => value,
        Err(error) => return Err(error),
    };
    Ok(content.trim().to_string())
}
```

使用 `?`：

```rust
fn read_username(path: &str) -> Result<String, std::io::Error> {
    let content = std::fs::read_to_string(path)?;
    Ok(content.trim().to_string())
}
```

`?` 的作用：

- `Ok(value)`：取出 `value` 并继续
- `Err(error)`：将错误转换为函数返回错误类型并提前返回

## 5.1 链式写法

```rust
use std::fs::File;
use std::io::{self, Read};

fn read_text(path: &str) -> Result<String, io::Error> {
    let mut content = String::new();
    File::open(path)?.read_to_string(&mut content)?;
    Ok(content)
}
```

## 5.2 `?` 使用位置

函数或闭包必须返回与 `?` 兼容的类型。`main` 也可以返回 `Result`：

```rust
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let text = std::fs::read_to_string("input.txt")?;
    println!("{text}");
    Ok(())
}
```

教学小程序这样很方便；生产 CLI 通常还需要统一错误输出和退出码，可在 `run()` 返回 `Result`、`main()` 中处理。

---

## 六、自定义错误类型

字符串错误简单，却难以让调用者可靠匹配，也无法自然保留来源。

```rust
use std::fmt;
use std::io;
use std::num::ParseIntError;

#[derive(Debug)]
enum ConfigError {
    Read {
        path: String,
        source: io::Error,
    },
    MissingField(&'static str),
    InvalidPort(ParseIntError),
    PortOutOfRange(u16),
}

impl fmt::Display for ConfigError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Read { path, source } => {
                write!(formatter, "无法读取配置文件 {path}：{source}")
            }
            Self::MissingField(field) => write!(formatter, "缺少配置字段：{field}"),
            Self::InvalidPort(source) => write!(formatter, "端口格式无效：{source}"),
            Self::PortOutOfRange(port) => write!(formatter, "端口不允许为 {port}"),
        }
    }
}

impl std::error::Error for ConfigError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Read { source, .. } => Some(source),
            Self::InvalidPort(source) => Some(source),
            _ => None,
        }
    }
}
```

## 6.1 错误类型设计原则

- 变体应表达调用者可能采取的不同处理策略
- 展示文本面向人，枚举变体面向程序
- 保存必要上下文，例如路径、参数名、行号
- 使用 `source` 保留底层错误链
- 不把敏感信息直接写入错误文本
- 不为每一行失败代码创建一个无意义变体

## 6.2 `From` 自动转换

```rust
impl From<ParseIntError> for ConfigError {
    fn from(error: ParseIntError) -> Self {
        Self::InvalidPort(error)
    }
}
```

之后在返回 `Result<_, ConfigError>` 的函数中可直接对解析结果使用 `?`。

## 6.3 应用错误与库错误

- 库错误：应结构化、稳定、便于调用者匹配
- 应用顶层错误：可以汇总多个库错误，并添加业务上下文
- 最终 UI 层决定如何展示、记录以及使用什么退出码

不要在底层函数中既打印错误又返回错误，否则上层可能重复输出。底层负责描述失败，顶层负责呈现。

---

## 七、错误上下文与来源链

只有底层错误往往缺少“正在做什么”：

```text
系统找不到指定的文件
```

更有用：

```text
加载用户配置 C:\app\config.toml 失败：系统找不到指定的文件
```

添加上下文时保留原始错误，而不是只拼接后丢弃：

```rust
fn load(path: &str) -> Result<String, ConfigError> {
    std::fs::read_to_string(path).map_err(|source| ConfigError::Read {
        path: path.to_string(),
        source,
    })
}
```

打印错误链：

```rust
use std::error::Error;

fn print_error(error: &(dyn Error + 'static)) {
    eprintln!("错误：{error}");
    let mut source = error.source();
    while let Some(cause) = source {
        eprintln!("  原因：{cause}");
        source = cause.source();
    }
}
```

---

## 八、测试基础

Rust 测试函数使用 `#[test]`：

```rust
fn add(left: i32, right: i32) -> i32 {
    left + right
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn adds_two_positive_numbers() {
        assert_eq!(add(2, 3), 5);
    }
}
```

运行：

```bash
cargo test
cargo test adds_two
cargo test -- --show-output
cargo test -- --test-threads=1
```

测试名称应描述行为和场景，而不是只写 `test1`。

## 8.1 断言宏

```rust
assert!(value > 0);
assert_eq!(actual, expected);
assert_ne!(actual, unexpected);
```

可添加失败说明：

```rust
assert_eq!(actual, expected, "用户 {} 的余额不正确", user_id);
```

被比较类型通常需实现 `PartialEq + Debug`。

## 8.2 测试返回 `Result`

```rust
#[test]
fn parses_valid_number() -> Result<(), Box<dyn std::error::Error>> {
    let value: i32 = "42".parse()?;
    assert_eq!(value, 42);
    Ok(())
}
```

这让测试可以使用 `?`。但需要测试具体错误变体时，应显式匹配结果。

## 8.3 测试 panic

```rust
#[test]
#[should_panic(expected = "必须为正数")]
fn rejects_zero() {
    require_positive(0);
}
```

尽量加 `expected`，避免因无关 panic 误判通过。

## 8.4 忽略慢测试

```rust
#[test]
#[ignore = "需要大型测试数据"]
fn large_dataset() {}
```

运行忽略测试：

```bash
cargo test -- --ignored
```

---

## 九、测试组织

## 9.1 单元测试

通常放在被测试模块同文件的 `#[cfg(test)] mod tests` 中。

优点：

- 可测试私有实现
- 与代码位置接近
- 适合纯函数和模块内部边界

不要过度测试私有实现细节，否则重构会导致大量无意义测试失败。优先验证可观察行为。

## 9.2 集成测试

位于 package 根的 `tests/`：

```text
tests/
├── cli.rs
└── common/
    └── mod.rs
```

每个顶层 `.rs` 文件是独立 crate，只能使用 library crate 的公共 API。

```rust,ignore
use mini_grep::{search, Config};

#[test]
fn public_search_api_works() {
    let result = search("rust", "Rust\nrust", true);
    assert_eq!(result, vec!["rust"]);
}
```

集成测试验证使用者视角的模块组合和公开契约。

## 9.3 文档测试

公共 API 的 `///` 示例会被 `cargo test --doc` 编译和运行：

```rust
/// 返回文本中包含关键词的行。
///
/// ```
/// let lines = mini_grep::search("rust", "learn rust\nlearn go", true);
/// assert_eq!(lines, vec!["learn rust"]);
/// ```
pub fn search<'a>(query: &str, content: &'a str, case_sensitive: bool) -> Vec<&'a str> {
    content
        .lines()
        .filter(|line| {
            if case_sensitive {
                line.contains(query)
            } else {
                line.to_lowercase().contains(&query.to_lowercase())
            }
        })
        .collect()
}
```

文档测试既是说明，也是防止示例过时的测试。

---

## 十、测试什么

一个功能至少考虑：

- 典型成功路径
- 空输入
- 最小和最大边界
- 非法格式
- 缺失数据
- 重复数据
- Unicode 文本
- 大小写规则
- I/O 失败
- 状态转换失败

不要只追求覆盖率数字。高价值测试应保护业务行为、边界和曾经出现的缺陷。

## 10.1 Arrange、Act、Assert

```rust
#[test]
fn removes_completed_task() {
    // Arrange
    let mut list = TaskList::with_completed_task();

    // Act
    let removed = list.remove_completed();

    // Assert
    assert_eq!(removed, 1);
    assert!(list.is_empty());
}
```

阶段清楚即可，不必机械写注释。

## 10.2 避免不稳定测试

- 不依赖测试执行顺序
- 不共享可变全局状态
- 不依赖真实当前时间
- 不访问不受控外部网络
- 使用唯一临时路径
- 明确排序无序集合后再断言
- 将随机数生成器或时钟作为依赖注入

---

## 十一、纯逻辑与 I/O 分离

难测试：

```rust,ignore
fn search_file() {
    // 读取参数、打开文件、搜索、打印全部混在一起
}
```

易测试：

```rust
fn search<'a>(query: &str, content: &'a str) -> Vec<&'a str> {
    content.lines().filter(|line| line.contains(query)).collect()
}
```

边界层负责读取和打印，核心函数只处理输入并返回结果。纯函数无需真实文件、终端或网络，测试更快、更稳定。

建议分层：

```text
输入边界 -> 解析/校验 -> 纯业务逻辑 -> 输出边界
```

---

## 十二、Cargo 依赖与锁文件

`Cargo.toml` 声明允许的依赖版本范围，`Cargo.lock` 记录实际解析的精确版本。

一般惯例：

- 二进制应用提交 `Cargo.lock`，保证可重复构建
- 库项目通常也可提交锁文件用于开发和 CI，但下游依赖解析仍由下游决定
- 不手工随意编辑锁文件
- 使用 Cargo 命令更新依赖并审查变更

语义化版本通常形如 `主版本.次版本.修订号`：

- 修订号：兼容性缺陷修复
- 次版本：兼容性新功能
- 主版本：可能有不兼容变化

实际兼容范围由 Cargo 版本要求语法决定。升级后必须运行完整测试，而不是假设“同主版本必然安全”。

---

## 十三、Cargo Feature

Feature 用于条件启用代码或可选依赖。

`Cargo.toml`：

```toml
[features]
default = []
json = []
verbose = []
```

代码：

```rust
#[cfg(feature = "verbose")]
pub fn print_diagnostics() {
    println!("详细诊断已启用");
}
```

构建：

```bash
cargo build --features verbose
cargo test --all-features
cargo test --no-default-features
```

Feature 设计原则：

- feature 通常应是可加性的
- 不要让两个 feature 形成无法组合的互斥状态
- 默认 feature 应克制
- 测试默认、无默认和全 feature 组合
- 公共类型不应因 feature 组合产生令人意外的不兼容变化

---

## 十四、Cargo Workspace

workspace 管理多个相关 package：

```text
project/
├── Cargo.toml
├── crates/
│   ├── core/
│   │   └── Cargo.toml
│   └── cli/
│       └── Cargo.toml
└── Cargo.lock
```

根 `Cargo.toml`：

```toml
[workspace]
members = ["crates/core", "crates/cli"]
resolver = "3"
```

用途：

- 核心库与多个入口分离
- 共享一个锁文件
- 从根目录统一构建和测试
- 控制 package 边界，减少不必要依赖

常用命令：

```bash
cargo check --workspace
cargo test --workspace
cargo clippy --workspace --all-targets --all-features
```

不要因为目录多就过早拆 workspace。只有 package 需要独立发布、独立依赖或明确编译边界时再拆分。

---

## 十五、配置、环境变量与日志

配置优先级可设计为：

```text
命令行参数 > 环境变量 > 配置文件 > 默认值
```

解析后尽早转换为强类型：

```rust
#[derive(Debug)]
struct Config {
    query: String,
    path: String,
    case_sensitive: bool,
}
```

不要让未经验证的字符串在业务层四处传播。

敏感信息：

- 不提交密钥到版本控制
- 不在错误和日志中输出完整令牌
- 环境变量缺失应给出变量名和配置指导
- 日志记录事件和上下文，不重复打印同一错误

小程序可使用 `eprintln!`；大型项目通常接入结构化日志生态，但应以实际依赖文档为准。

---

## 十六、阶段项目：迷你 grep

## 16.1 功能需求

- 搜索一个或多个文件
- 支持大小写敏感和不敏感
- 显示文件名和行号
- `--invert` 输出不匹配行
- `--count` 只显示匹配数量
- 错误输出到 stderr
- 使用明确退出码
- 核心搜索逻辑有单元测试
- 公共 API 有文档测试
- CLI 有集成测试设计

## 16.2 项目结构

```text
mini-grep/
├── Cargo.toml
├── src/
│   ├── lib.rs
│   ├── main.rs
│   ├── config.rs
│   ├── error.rs
│   └── search.rs
└── tests/
    └── public_api.rs
```

## 16.3 错误类型

```rust,ignore
// src/error.rs
use std::error::Error;
use std::fmt;
use std::io;

#[derive(Debug)]
pub enum GrepError {
    InvalidArguments(String),
    ReadFile { path: String, source: io::Error },
}

impl fmt::Display for GrepError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::InvalidArguments(message) => write!(f, "参数错误：{message}"),
            Self::ReadFile { path, source } => {
                write!(f, "无法读取文件 {path}：{source}")
            }
        }
    }
}

impl Error for GrepError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::ReadFile { source, .. } => Some(source),
            Self::InvalidArguments(_) => None,
        }
    }
}
```

## 16.4 配置解析

```rust,ignore
// src/config.rs
use crate::GrepError;

#[derive(Debug, PartialEq, Eq)]
pub struct Config {
    pub query: String,
    pub paths: Vec<String>,
    pub case_sensitive: bool,
    pub invert: bool,
    pub count_only: bool,
}

impl Config {
    pub fn parse(arguments: impl IntoIterator<Item = String>) -> Result<Self, GrepError> {
        let mut query = None;
        let mut paths = Vec::new();
        let mut case_sensitive = true;
        let mut invert = false;
        let mut count_only = false;

        for argument in arguments {
            match argument.as_str() {
                "-i" | "--ignore-case" => case_sensitive = false,
                "-v" | "--invert" => invert = true,
                "-c" | "--count" => count_only = true,
                value if value.starts_with('-') => {
                    return Err(GrepError::InvalidArguments(format!(
                        "未知选项：{value}"
                    )));
                }
                value if query.is_none() => query = Some(value.to_string()),
                value => paths.push(value.to_string()),
            }
        }

        let query = query.ok_or_else(|| {
            GrepError::InvalidArguments(String::from("缺少搜索关键词"))
        })?;

        if paths.is_empty() {
            return Err(GrepError::InvalidArguments(String::from(
                "至少需要一个文件路径",
            )));
        }

        Ok(Self {
            query,
            paths,
            case_sensitive,
            invert,
            count_only,
        })
    }
}
```

## 16.5 纯搜索逻辑

```rust,ignore
// src/search.rs
#[derive(Debug, PartialEq, Eq)]
pub struct Match<'a> {
    pub line_number: usize,
    pub line: &'a str,
}

/// 搜索包含关键词的行。
///
/// ```
/// use mini_grep::search;
/// let matches = search("rust", "Rust\nrust", true, false);
/// assert_eq!(matches.len(), 1);
/// assert_eq!(matches[0].line_number, 2);
/// ```
pub fn search<'a>(
    query: &str,
    content: &'a str,
    case_sensitive: bool,
    invert: bool,
) -> Vec<Match<'a>> {
    let normalized_query = (!case_sensitive).then(|| query.to_lowercase());

    content
        .lines()
        .enumerate()
        .filter_map(|(index, line)| {
            let matched = if let Some(query) = normalized_query.as_deref() {
                line.to_lowercase().contains(query)
            } else {
                line.contains(query)
            };

            (matched != invert).then_some(Match {
                line_number: index + 1,
                line,
            })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEXT: &str = "Rust is fast\nrust is safe\nGo is simple\n";

    #[test]
    fn case_sensitive_search() {
        let result = search("rust", TEXT, true, false);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].line_number, 2);
    }

    #[test]
    fn case_insensitive_search() {
        let result = search("RUST", TEXT, false, false);
        assert_eq!(result.len(), 2);
    }

    #[test]
    fn invert_search() {
        let result = search("rust", TEXT, false, true);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].line, "Go is simple");
    }

    #[test]
    fn empty_content_has_no_matches() {
        assert!(search("rust", "", true, false).is_empty());
    }

    #[test]
    fn unicode_query_works() {
        let result = search("语言", "Rust 语言\n系统编程", true, false);
        assert_eq!(result.len(), 1);
    }
}
```

返回行切片借用文件内容，没有为每个结果复制字符串。

## 16.6 库入口

```rust,ignore
// src/lib.rs
mod config;
mod error;
mod search;

pub use config::Config;
pub use error::GrepError;
pub use search::{search, Match};

pub fn run(config: &Config) -> Result<String, GrepError> {
    let mut output = String::new();

    for path in &config.paths {
        let content = std::fs::read_to_string(path).map_err(|source| {
            GrepError::ReadFile {
                path: path.clone(),
                source,
            }
        })?;
        let matches = search(
            &config.query,
            &content,
            config.case_sensitive,
            config.invert,
        );

        if config.count_only {
            output.push_str(&format!("{path}:{}\n", matches.len()));
        } else {
            for matched in matches {
                output.push_str(&format!(
                    "{path}:{}:{}\n",
                    matched.line_number, matched.line
                ));
            }
        }
    }

    Ok(output)
}
```

这里为了教学简单，一旦某个文件失败就停止。扩展版可以收集每个文件的独立结果，做到部分成功。

## 16.7 CLI 入口

```rust,ignore
// src/main.rs
use std::env;
use std::process;

use mini_grep::{Config, GrepError};

fn run() -> Result<(), GrepError> {
    let config = Config::parse(env::args().skip(1))?;
    let output = mini_grep::run(&config)?;
    print!("{output}");
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        let code = match error {
            GrepError::InvalidArguments(_) => 2,
            GrepError::ReadFile { .. } => 1,
        };
        process::exit(code);
    }
}
```

## 16.8 运行

```bash
cargo run -- rust README.md
cargo run -- --ignore-case rust README.md src/lib.rs
cargo run -- --invert TODO README.md
cargo run -- --count rust README.md
```

## 16.9 项目测试补充

为 `Config::parse` 添加：

- 缺少关键词
- 缺少路径
- 未知选项
- 多文件
- 各 flag 组合

集成测试重点：

- 公共 `search` API
- `run` 读取临时测试文件
- 输出包含文件名和行号
- 文件不存在返回 `GrepError::ReadFile`

如果测试真实二进制，可使用标准库启动 `CARGO_BIN_EXE_*` 指定的构建产物，或选择成熟测试工具；采用第三方工具时以其当前文档为准。

## 16.10 扩展任务

1. 无匹配时使用独立退出码。
2. 支持从标准输入读取。
3. 支持递归目录搜索。
4. 支持固定字符串和简单模式。
5. 每个文件失败时继续处理其他文件，并汇总错误。
6. 支持上下文行。
7. 支持颜色，但非终端输出时自动关闭。
8. 对大文件使用缓冲逐行读取。
9. 增加基准测试比较大小写处理策略。
10. 将核心库与 CLI 拆为 workspace 成员。

---

## 十七、工程质量门禁

本地与 CI 推荐执行：

```bash
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test --all-features
cargo test --doc
cargo build --release
```

workspace：

```bash
cargo fmt --all --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace --all-features
cargo build --workspace --release
```

门禁失败应阻止合并，但规则要稳定、可在本地复现。不要在 CI 中偷偷执行开发者无法复现的魔法步骤。

## 17.1 `cargo check` 与完整构建

- 开发循环：频繁 `cargo check`
- 提交前：Clippy 与测试
- 发布前：release 构建和制品验证

## 17.2 警告作为错误

`-D warnings` 能保持代码整洁，但工具链升级可能新增 lint。团队应固定或有计划地升级工具链，避免无预警破坏 CI。

---

## 十八、常见误区

- 库代码遇到可恢复错误直接 panic
- 对用户输入大量使用 `unwrap`
- 把所有错误转换为同一个字符串，丢失分类和来源
- 底层函数打印错误后又返回错误
- 只测试成功路径
- 测试依赖顺序、真实网络或共享全局状态
- 为提高覆盖率测试大量私有实现细节
- 把文件 I/O、参数解析和搜索逻辑写在同一个函数
- feature 之间互斥且组合后无法编译
- 拆分过多 crate，增加构建和依赖复杂度
- 升级依赖后不运行完整测试
- 只在 CI 运行格式化和测试，本地无法快速验证
- 错误信息包含密码、令牌或完整敏感配置

---

## 十九、针对性练习

1. 将命令框架的 `String` 错误改为自定义枚举。
2. 为错误实现 `Display`、`Error` 和 `source`。
3. 给第三阶段待办项目补充单元测试与集成测试。
4. 为一个公共函数编写可运行文档示例。
5. 测试 UTF-8、空输入和文件不存在。
6. 将环境变量解析为强类型配置。
7. 为可选输出格式增加 Cargo feature。
8. 把核心库和 CLI 拆成 workspace。
9. 设计不会依赖真实当前时间的测试接口。
10. 为曾出现的缺陷先写回归测试，再修复实现。

---

## 二十、知识自测

1. 可恢复错误和程序缺陷应分别如何表达？
2. 库代码为何应谨慎 panic？
3. `expect` 的信息应说明什么？
4. `?` 在成功和失败时分别做什么？
5. `map_err` 与 `From` 转换各适合什么场景？
6. 自定义错误为何优于字符串错误？
7. `Error::source` 的价值是什么？
8. 为什么底层函数不应同时打印并返回错误？
9. 单元测试、集成测试、文档测试分别验证什么？
10. `#[should_panic(expected = ...)]` 为何比无条件形式更好？
11. 如何避免测试依赖真实外部系统？
12. 为什么纯业务逻辑应与 I/O 分离？
13. `Cargo.toml` 与 `Cargo.lock` 分别记录什么？
14. Cargo feature 应尽量满足什么组合性质？
15. 什么情况下值得拆 workspace？
16. 为什么哈希集合断言前通常需要稳定排序？
17. `cargo check`、`cargo test`、release 构建分别解决什么问题？
18. 为什么工具链升级可能让 `-D warnings` CI 失败？

---

## 二十一、阶段验收清单

### 错误处理

- [ ] 能判断 panic 与 `Result` 的边界
- [ ] 不对不可信输入草率 `unwrap`
- [ ] 能使用 `?` 传播错误
- [ ] 能设计结构化自定义错误
- [ ] 能实现 `Display`、`Error` 和来源链
- [ ] 错误包含有用上下文且不泄露敏感信息
- [ ] 输出和退出码由应用边界决定

### 测试

- [ ] 能编写单元、集成和文档测试
- [ ] 覆盖成功、边界和失败路径
- [ ] 测试不依赖执行顺序和真实网络
- [ ] 能测试具体错误变体
- [ ] 能为缺陷添加回归测试
- [ ] 核心逻辑与 I/O 分离

### Cargo 与工程化

- [ ] 理解 manifest、锁文件和依赖范围
- [ ] 能定义和测试 Cargo feature
- [ ] 能判断是否需要 workspace
- [ ] 配置尽早解析为强类型
- [ ] 能建立本地与 CI 一致的质量命令

### 项目

- [ ] 迷你 grep 支持多文件、行号和大小写选项
- [ ] 支持反向匹配与计数
- [ ] 错误输出到 stderr，并使用合理退出码
- [ ] 搜索逻辑拥有单元测试
- [ ] 公共 API 拥有文档测试
- [ ] 文件错误保留路径和底层来源
- [ ] 至少完成三个扩展任务

### 最终质量检查

```bash
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test --all-features
cargo test --doc
cargo build --release
```

当你能够交付一个失败行为清晰、测试稳定、结构合理，并能通过自动质量门禁的项目时，即可进入第七阶段：智能指针与内部可变性。

---

## 二十二、进入下一阶段前的预习

下一阶段将回答：

- 递归数据结构为什么需要 `Box<T>`？
- 何时需要多个所有者？
- `Rc<T>` 与 `Arc<T>` 有何区别？
- 为什么 `RefCell<T>` 能在不可变外壳中修改数据？
- 编译期借用检查与运行期借用检查如何取舍？
- 如何使用 `Weak<T>` 避免引用循环？

建议保留迷你 grep 的错误类型和测试框架。后续学习智能指针时，可以尝试共享配置、构建树形查询表达式，并观察不同所有权方案对 API 和测试的影响。
