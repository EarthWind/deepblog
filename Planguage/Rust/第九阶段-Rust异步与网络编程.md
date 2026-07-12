# 第九阶段：Rust 异步与网络编程

> 本阶段学习面向大量 I/O 等待的并发模型。重点不是给每个函数添加 `async`，而是理解 Future 如何被驱动、任务何时让出执行权，以及如何控制超时、取消、并发量、队列长度和优雅停机。

## 一、阶段目标

完成本阶段后，你应当能够：

- 解释 `Future`、`async`、`.await`、executor、task 和 waker 的关系
- 区分异步任务与操作系统线程
- 使用 Tokio runtime、任务、定时器和异步 I/O
- 使用 `join!`、`select!`、`spawn` 组合并发操作
- 正确处理 `Send + 'static` 任务约束
- 使用超时、取消和信号停止长时间任务
- 使用有界通道和信号量实现背压与并发限制
- 避免持有同步锁跨越 `.await`
- 编写 TCP 客户端与服务端
- 理解 HTTP 请求、响应、状态码和 JSON 边界
- 使用 Axum 构建带共享状态和统一错误响应的 REST API
- 为异步代码编写确定、可靠的测试
- 实现优雅停机并等待在途任务完成

---

## 二、什么时候使用异步

异步适合任务经常等待外部事件：

- 网络连接和 HTTP 请求
- 数据库查询
- 文件或设备异步 I/O（平台支持程度不同）
- 定时器和事件流
- 大量并发连接

异步不自动加速 CPU 密集计算。长时间压缩、加密、图像处理或复杂计算如果直接运行在异步 worker 上，会阻塞同一线程上的其他任务。

选择直觉：

| 工作负载 | 常见方案 |
|---|---|
| 少量阻塞任务 | 普通同步代码 |
| 大量 I/O 并发 | 异步 runtime |
| CPU 密集并行 | 工作线程池、数据并行 |
| 异步服务中的短 CPU 操作 | 直接执行 |
| 异步服务中的长阻塞/CPU 操作 | 专用线程池或 `spawn_blocking` |

不要为了“现代”而把纯计算库全面异步化。异步通常应从 I/O 边界向内传播。

---

## 三、`Future` 的执行模型

`Future` 表示“现在可能尚未完成、以后可能产生值”的计算。可简化理解为：

```rust,ignore
trait Future {
    type Output;

    fn poll(
        self: Pin<&mut Self>,
        context: &mut Context<'_>,
    ) -> Poll<Self::Output>;
}

enum Poll<T> {
    Ready(T),
    Pending,
}
```

- `Ready(value)`：计算完成
- `Pending`：当前无法继续，需要等待事件
- waker：事件就绪时通知 executor 再次 poll
- executor：调度并重复 poll Future

## 3.1 Future 是惰性的

```rust,ignore
let future = async_operation();
```

仅创建 Future 通常不会完成 I/O。它需要被 `.await`、组合或交给 executor。

## 3.2 `async fn`

```rust
async fn answer() -> u32 {
    42
}
```

调用 `answer()` 返回实现 `Future<Output = u32>` 的值。函数体在 Future 被驱动时执行。

## 3.3 `.await`

```rust,ignore
let value = answer().await;
```

若子 Future 未完成，当前任务让出执行权。让出的是异步任务，不一定阻塞操作系统线程。executor 可在同一线程继续运行其他就绪任务。

## 3.4 状态机直觉

编译器把 `async` 代码转换为保存局部变量和进度的状态机。跨越 `.await` 仍需使用的数据会成为状态机字段。这解释了：

- Future 大小与跨 await 状态有关
- `tokio::spawn` 时这些状态可能需要 `Send`
- 某些借用不能跨 await
- 持锁跨 await 会让守卫长期存活

---

## 四、Runtime 与 Tokio

标准库定义 Future 抽象，但不自带通用异步 runtime。Tokio 提供 executor、网络、定时器、同步原语和信号支持。

创建项目并添加依赖：

```bash
cargo new async-lab
cd async-lab
cargo add tokio --features full
```

入门示例：

```rust,ignore
use std::time::Duration;
use tokio::time::sleep;

#[tokio::main]
async fn main() {
    println!("开始");
    sleep(Duration::from_millis(200)).await;
    println!("完成");
}
```

`#[tokio::main]` 创建 runtime 并执行 async `main`。生产项目可按需求启用更小的 feature 集，而不是总用 `full`。

## 4.1 多线程与当前线程 runtime

- multi-thread：任务可在多个 worker 线程间调度
- current-thread：所有异步任务在当前线程调度

选择取决于工作负载、是否存在非 `Send` 任务和部署约束。异步并不等于多线程；单线程 executor 也能并发处理大量 I/O。

---

## 五、顺序与并发

顺序等待：

```rust,ignore
let first = load_first().await;
let second = load_second().await;
```

第二个操作要等第一个完成后才开始。

## 5.1 `join!`

```rust,ignore
let (first, second) = tokio::join!(load_first(), load_second());
```

两个 Future 在同一任务中并发推进，等待全部完成。若 Future 中有长时间不让出的同步工作，仍会妨碍另一个分支。

## 5.2 `try_join!`

```rust,ignore
let (user, orders) = tokio::try_join!(load_user(), load_orders())?;
```

用于输出为 `Result` 的多个操作，任何一个失败时提前返回错误。

## 5.3 `spawn`

```rust,ignore
let handle = tokio::spawn(async {
    do_work().await
});

let output = handle.await?;
```

任务由 runtime 独立调度。`JoinHandle.await` 的外层错误表示任务 panic 或被取消；内部返回值可能仍是业务 `Result`：

```rust,ignore
let value = handle.await??;
```

两个 `?` 分别处理 join 错误和任务业务错误。

## 5.4 不要丢弃重要任务句柄

“发射后不管”会让错误、完成状态和停机行为不可见。重要后台任务应：

- 保存 `JoinHandle`
- 记录失败
- 设计取消信号
- 停机时等待结束

---

## 六、`Send + 'static` 与任务所有权

Tokio 多线程 runtime 可能在任务暂停后把它移到另一个 worker，因此 `tokio::spawn` 的 Future 通常要求 `Send + 'static`。

```rust,ignore
let message = String::from("hello");

tokio::spawn(async move {
    println!("{message}");
});
```

`move` 把 `message` 所有权移入任务。这里 `'static` 不表示任务永久存在，而是任务不能借用可能提前失效的栈数据。

## 6.1 非 `Send` 值跨 await

```rust,compile_fail
use std::rc::Rc;

tokio::spawn(async {
    let value = Rc::new("hello");
    tokio::task::yield_now().await;
    println!("{value}");
});
```

`Rc` 跨越 `.await` 保存在任务状态中，使 Future 不是 `Send`。如果非 `Send` 值在 await 前离开作用域，则可能通过编译；真正需要非 `Send` 异步任务时可研究 `LocalSet`。

## 6.2 共享状态

跨任务共享所有权通常使用 `Arc<T>`。如果需要修改，再根据临界区性质选择锁或消息传递。

---

## 七、取消

异步取消通常是协作式的。常见方式：

- 丢弃 Future
- 中止 `JoinHandle`
- 使用 `select!` 等待工作或关闭信号
- 通过 `watch`、`broadcast` 或取消令牌传播信号

## 7.1 `select!`

```rust,ignore
tokio::select! {
    result = run_job() => {
        println!("任务结束：{result:?}");
    }
    _ = tokio::signal::ctrl_c() => {
        println!("收到停止信号");
    }
}
```

一个分支完成后，其他分支 Future 通常被丢弃。被取消的操作是否安全，取决于它的取消安全性。

## 7.2 取消安全

如果一个 Future 在任意 await 点被丢弃，已完成的外部副作用不会自动回滚。例如：

- 已写入一半的数据
- 已从队列取出但尚未确认的消息
- 已开始的数据库事务

设计策略：

- 把不可分割状态变更放入事务
- 将进度保存在 Future 外部
- 使用明确确认协议
- 查阅所用异步 API 的取消安全说明

`select!` 循环尤其要确认被反复取消的接收、读取或写入操作是否安全。

---

## 八、超时

```rust,ignore
use std::time::Duration;
use tokio::time::timeout;

let result = timeout(Duration::from_secs(2), fetch_data()).await;

match result {
    Ok(Ok(data)) => println!("{data:?}"),
    Ok(Err(error)) => eprintln!("业务失败：{error}"),
    Err(_) => eprintln!("操作超时"),
}
```

外层 `Result` 是超时结果，内层可能是业务结果。超时会取消被包装的 Future；如果 Future 长时间不让出执行权，timer 无法及时介入。

超时应分层设置：

- 连接超时
- 单次读写超时
- 完整请求 deadline
- 上游服务调用超时

不要无条件重试所有超时。重试应考虑幂等性、退避、抖动和总 deadline。

---

## 九、通道与消息传递

Tokio 常用通道：

| 类型 | 语义 |
|---|---|
| `mpsc` | 多生产者、单消费者 |
| `oneshot` | 单次响应 |
| `broadcast` | 每个订阅者接收广播消息 |
| `watch` | 只关心最新状态值 |

## 9.1 有界 `mpsc`

```rust,ignore
use tokio::sync::mpsc;

let (sender, mut receiver) = mpsc::channel::<String>(100);

let producer = tokio::spawn(async move {
    sender.send(String::from("work")).await
});

while let Some(message) = receiver.recv().await {
    println!("{message}");
}
```

容量满时 `send().await` 等待，形成背压。无界通道在生产持续快于消费时可能耗尽内存。

## 9.2 请求—响应 Actor 模式

```rust,ignore
use tokio::sync::{mpsc, oneshot};

enum Command {
    Get {
        key: String,
        reply: oneshot::Sender<Option<String>>,
    },
    Set {
        key: String,
        value: String,
        reply: oneshot::Sender<()>,
    },
}
```

一个专用任务独占资源，其他任务通过 `mpsc` 发命令、通过 `oneshot` 收响应。这很适合不能安全并发访问、操作本身包含 await 的客户端或连接。

---

## 十、背压与并发限制

并发系统必须回答：如果请求进入速度超过处理速度，会发生什么？

可用机制：

- 有界通道限制排队数量
- 信号量限制并发操作数
- 请求超时和 deadline
- 限流与拒绝策略
- 分批处理
- 负载卸载或降级

## 10.1 `Semaphore`

```rust,ignore
use std::sync::Arc;
use tokio::sync::Semaphore;

let limit = Arc::new(Semaphore::new(20));
let mut handles = Vec::new();

for item in items {
    let permit = Arc::clone(&limit)
        .acquire_owned()
        .await
        .map_err(|_| "信号量已关闭")?;

    handles.push(tokio::spawn(async move {
        let _permit = permit;
        process(item).await
    }));
}
```

permit 离开作用域时自动归还。限制值应通过容量测试确定，而不是复制任意数字。

## 10.2 不受控 spawn

```rust,ignore
for item in huge_stream {
    tokio::spawn(process(item));
}
```

即使任务轻量，输入无限或巨大时也会积累内存、连接和下游压力。spawn 前获取 permit，或使用有界队列和固定 worker 数量。

---

## 十一、异步锁与同步锁

## 11.1 `std::sync::Mutex`

临界区不包含 await、非常短时，在异步代码中可以使用同步 Mutex：

```rust,ignore
let value = {
    let mut state = state.lock().expect("锁已中毒");
    state.counter += 1;
    state.counter
};

send_value(value).await;
```

锁守卫在 await 前已释放。

## 11.2 `tokio::sync::Mutex`

确实必须在持锁期间 await，或锁竞争需要异步等待时使用异步 Mutex：

```rust,ignore
let mut resource = shared.lock().await;
resource.perform_async_operation().await?;
```

但这会串行化操作。对于单一异步资源，专用管理任务加消息通道常常更清晰、吞吐更好。

## 11.3 持同步锁跨 await 的危险

- 可能让 Future 不是 `Send`
- 阻塞 executor worker
- 增加死锁概率
- 临界区时间不可控

推荐步骤：

1. 锁内读取或更新最少状态。
2. 创建执行异步工作所需的独立快照。
3. 释放锁。
4. 执行 await。
5. 必要时重新加锁提交结果，并检查状态是否已变化。

---

## 十二、阻塞代码与 `spawn_blocking`

```rust,ignore
let result = tokio::task::spawn_blocking(move || {
    expensive_sync_operation(input)
})
.await
.map_err(|error| format!("阻塞任务失败：{error}"))??;
```

适用于：

- 没有异步接口的阻塞库
- 文件系统或压缩等可能长时间阻塞的操作
- 中等 CPU 工作

大量 CPU 密集工作应使用专门计算线程池，并控制并行度。`spawn_blocking` 队列也不是无限计算资源。

---

## 十三、TCP 服务端

```rust,ignore
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{TcpListener, TcpStream};

async fn handle(socket: TcpStream) -> std::io::Result<()> {
    let (reader, mut writer) = socket.into_split();
    let mut lines = BufReader::new(reader).lines();

    while let Some(line) = lines.next_line().await? {
        writer.write_all(line.as_bytes()).await?;
        writer.write_all(b"\n").await?;
    }
    Ok(())
}

#[tokio::main]
async fn main() -> std::io::Result<()> {
    let listener = TcpListener::bind("127.0.0.1:8080").await?;

    loop {
        let (socket, address) = listener.accept().await?;
        tokio::spawn(async move {
            if let Err(error) = handle(socket).await {
                eprintln!("连接 {address} 失败：{error}");
            }
        });
    }
}
```

生产化还需：

- 连接并发上限
- 读写超时
- 最大行/帧大小，防止内存攻击
- 半关闭处理
- 任务跟踪和优雅停机
- 结构化日志与指标

TCP 是字节流，没有天然消息边界。示例用换行作为帧边界；真实协议必须明确定义 framing。

---

## 十四、HTTP 与 JSON 基础

HTTP 请求主要包括：

- 方法：GET、POST、PUT、PATCH、DELETE 等
- 路径与查询参数
- 请求头
- 请求体

响应包括状态码、响应头和响应体。

常见 REST 状态码：

| 状态码 | 含义 |
|---|---|
| 200 | 成功并返回内容 |
| 201 | 资源创建成功 |
| 204 | 成功但无响应体 |
| 400 | 请求格式或参数错误 |
| 404 | 资源不存在 |
| 409 | 状态冲突 |
| 422 | 语法可解析但业务校验失败 |
| 500 | 未预期服务端错误 |
| 503 | 暂时不可用或过载 |

错误响应应保持统一结构：

```json
{
  "code": "todo_not_found",
  "message": "任务不存在"
}
```

不要把内部错误链、SQL、文件路径或密钥直接返回给客户端。

---

## 十五、阶段项目：REST 待办服务

本项目先用内存仓储完成异步 HTTP 边界，再扩展数据库持久化。这样可以先验证路由、状态、错误和测试，再替换存储实现。

## 15.1 添加依赖

```bash
cargo new todo-api
cd todo-api
cargo add axum
cargo add tokio --features full
cargo add serde --features derive
```

依赖 API 会演进，实践时应以项目锁定版本的文档为准。

## 15.2 数据模型

```rust,ignore
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize)]
struct Todo {
    id: u64,
    title: String,
    completed: bool,
}

#[derive(Debug, Deserialize)]
struct CreateTodo {
    title: String,
}

#[derive(Debug, Deserialize)]
struct UpdateTodo {
    title: Option<String>,
    completed: Option<bool>,
}
```

不要直接把数据库实体或内部领域类型无条件暴露为 API DTO。项目扩大后应分开 request、domain、persistence 类型。

## 15.3 状态与错误

```rust,ignore
use axum::{
    Json,
    http::StatusCode,
    response::{IntoResponse, Response},
};
use serde::Serialize;
use std::{collections::HashMap, sync::Arc};
use tokio::sync::RwLock;

#[derive(Clone, Default)]
struct AppState {
    todos: Arc<RwLock<HashMap<u64, Todo>>>,
    next_id: Arc<std::sync::atomic::AtomicU64>,
}

#[derive(Debug)]
enum ApiError {
    Validation(String),
    NotFound(u64),
}

#[derive(Serialize)]
struct ErrorBody {
    code: &'static str,
    message: String,
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (status, code, message) = match self {
            Self::Validation(message) => {
                (StatusCode::UNPROCESSABLE_ENTITY, "validation_error", message)
            }
            Self::NotFound(id) => (
                StatusCode::NOT_FOUND,
                "todo_not_found",
                format!("任务 {id} 不存在"),
            ),
        };
        (status, Json(ErrorBody { code, message })).into_response()
    }
}
```

示例使用 Tokio `RwLock`。因为内存 HashMap 操作不需要 await，实际也可使用短临界区的 `std::sync::RwLock`。选择应基于临界区性质，而不是“异步项目只能使用异步锁”。

## 15.4 Handler

```rust,ignore
use axum::{
    extract::{Path, State},
    Json,
};
use std::sync::atomic::Ordering;

async fn list_todos(State(state): State<AppState>) -> Json<Vec<Todo>> {
    let todos = state.todos.read().await;
    let mut values: Vec<_> = todos.values().cloned().collect();
    values.sort_by_key(|todo| todo.id);
    Json(values)
}

async fn get_todo(
    State(state): State<AppState>,
    Path(id): Path<u64>,
) -> Result<Json<Todo>, ApiError> {
    let todos = state.todos.read().await;
    todos
        .get(&id)
        .cloned()
        .map(Json)
        .ok_or(ApiError::NotFound(id))
}

async fn create_todo(
    State(state): State<AppState>,
    Json(input): Json<CreateTodo>,
) -> Result<(StatusCode, Json<Todo>), ApiError> {
    let title = input.title.trim();
    if title.is_empty() {
        return Err(ApiError::Validation(String::from("标题不能为空")));
    }
    if title.chars().count() > 200 {
        return Err(ApiError::Validation(String::from("标题不能超过 200 个字符")));
    }

    let id = state.next_id.fetch_add(1, Ordering::Relaxed) + 1;
    let todo = Todo {
        id,
        title: title.to_string(),
        completed: false,
    };
    state.todos.write().await.insert(id, todo.clone());
    Ok((StatusCode::CREATED, Json(todo)))
}

async fn update_todo(
    State(state): State<AppState>,
    Path(id): Path<u64>,
    Json(input): Json<UpdateTodo>,
) -> Result<Json<Todo>, ApiError> {
    let mut todos = state.todos.write().await;
    let todo = todos.get_mut(&id).ok_or(ApiError::NotFound(id))?;

    if let Some(title) = input.title {
        let title = title.trim();
        if title.is_empty() || title.chars().count() > 200 {
            return Err(ApiError::Validation(String::from("标题长度必须为 1～200 个字符")));
        }
        todo.title = title.to_string();
    }
    if let Some(completed) = input.completed {
        todo.completed = completed;
    }
    Ok(Json(todo.clone()))
}

async fn delete_todo(
    State(state): State<AppState>,
    Path(id): Path<u64>,
) -> Result<StatusCode, ApiError> {
    let removed = state.todos.write().await.remove(&id);
    removed
        .map(|_| StatusCode::NO_CONTENT)
        .ok_or(ApiError::NotFound(id))
}
```

锁内只有短暂同步 HashMap 操作，没有执行其他 `.await` 或慢 I/O。

## 15.5 路由与优雅停机

```rust,ignore
use axum::{
    Router,
    routing::{get, post},
};
use tokio::net::TcpListener;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let state = AppState::default();
    let app = Router::new()
        .route("/health", get(|| async { "ok" }))
        .route("/todos", get(list_todos).post(create_todo))
        .route(
            "/todos/{id}",
            get(get_todo).patch(update_todo).delete(delete_todo),
        )
        .with_state(state);

    let listener = TcpListener::bind("127.0.0.1:3000").await?;
    println!("listening on {}", listener.local_addr()?);

    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown_signal())
        .await?;
    Ok(())
}

async fn shutdown_signal() {
    if let Err(error) = tokio::signal::ctrl_c().await {
        eprintln!("监听关闭信号失败：{error}");
    }
}
```

优雅停机完整流程通常包括：

1. 检测 Ctrl+C、终止信号或内部致命错误。
2. 停止接受新请求。
3. 通知后台任务停止。
4. 等待在途请求和任务完成，设置最大等待时间。
5. 刷新必要缓冲并关闭资源。

## 15.6 数据库持久化扩展

不要让 handler 直接散布 SQL。定义仓储边界：

```rust,ignore
trait TodoRepository: Send + Sync {
    fn list(&self) -> impl Future<Output = Result<Vec<Todo>, RepositoryError>> + Send;
    fn get(&self, id: u64) -> impl Future<Output = Result<Option<Todo>, RepositoryError>> + Send;
    // create / update / delete
}
```

也可根据动态替换需求采用对象安全的异步抽象方案。具体数据库库、宏和迁移命令会随版本演进，应以锁定版本官方文档为准。

持久化步骤：

- 选择数据库并创建连接池
- 使用迁移建立表和索引
- 实现仓储
- 把数据库错误映射为领域/应用错误
- 在集成测试中使用隔离数据库
- 测试唯一约束、并发更新和事务回滚
- 对慢查询设置 timeout

表结构示意：

```sql
CREATE TABLE todos (
    id INTEGER PRIMARY KEY,
    title TEXT NOT NULL,
    completed BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

## 15.7 测试策略

```rust,ignore
#[tokio::test]
async fn creates_and_reads_todo() {
    let state = AppState::default();
    // 直接调用业务服务，或通过 Router 发测试请求
}
```

测试层次：

- 纯校验函数：普通同步单元测试
- 仓储实现：数据库集成测试
- handler：构造 Router，发送内存请求
- 端到端：启动临时服务，执行真实 HTTP 请求
- 超时和取消：使用 Tokio 时间控制能力，避免真实长时间等待

必须测试：

- 空标题和超长标题
- 不存在 ID
- 创建、查询、更新、删除完整流程
- 并发创建 ID 不重复
- 错误响应 JSON 结构稳定
- 关闭时不接受新请求并等待在途任务

## 15.8 项目扩展

1. 增加分页和完成状态筛选。
2. 增加数据库连接池与迁移。
3. 使用事务处理多步变更。
4. 增加请求追踪 ID 和结构化日志。
5. 对写接口增加并发限制。
6. 增加请求体大小限制。
7. 增加认证和权限检查。
8. 添加乐观并发版本号。
9. 增加指标：请求数、错误率、延迟、在途请求。
10. 实现带 deadline 的优雅停机。

---

## 十六、异步测试

```rust,ignore
#[tokio::test]
async fn operation_completes() {
    let value = async_operation().await;
    assert_eq!(value, 42);
}
```

测试原则：

- 避免依赖真实网络和公网服务
- 端口使用系统分配，而不是固定端口
- 使用有界 timeout 防止测试永久挂起
- 不用真实数秒 sleep 制造时序
- 把时钟、随机数和外部客户端设计为可替换依赖
- 断言任务已正确停止，不遗留后台任务

## 16.1 时间测试

Tokio 提供测试时间控制能力。使用时按当前版本文档启用所需 feature，并确保被测代码使用 Tokio timer，而不是标准线程 sleep。

## 16.2 并发测试不能证明无竞争

一次通过不代表所有调度都安全。应结合：

- 类型系统和锁不变量推理
- 多次压力测试
- 明确的状态机测试
- 专用并发模型检查工具（高级阶段）

---

## 十七、可观测性基础

异步问题经常与时序和跨任务调用链有关。至少记录：

- 请求/任务 ID
- 操作名称
- 开始与结束
- 延迟
- 错误分类
- timeout、取消和重试次数
- 队列深度、permit 使用量

不要在高频循环无节制输出日志。结构化字段比拼接长字符串更便于查询。避免记录口令、令牌和敏感请求体。

关键指标：

- 吞吐量
- 成功率和按类别错误率
- p50/p95/p99 延迟
- 当前连接与在途请求
- 队列等待时间和队列长度
- 数据库连接池使用率
- timeout 和限流数量

---

## 十八、常见错误

- 认为 async 自动让 CPU 计算并行
- 创建 Future 却没有 await 或 spawn
- 顺序 await 本可并发的独立 I/O
- 无限制 spawn 任务
- 使用无界队列承接不受控输入
- 丢弃重要 `JoinHandle`
- 持有同步锁跨越 `.await`
- 锁内调用慢 I/O 或未知外部代码
- 给每次请求创建新数据库连接而不使用池
- timeout 后无条件重试非幂等操作
- 在 `select!` 中使用取消不安全操作却不保存进度
- 把内部错误详情直接返回客户端
- 优雅停机只监听信号，不通知和等待后台任务
- 异步测试依赖真实 sleep，导致缓慢且不稳定

---

## 十九、针对性练习

1. 编写两个独立异步操作，分别比较顺序 await 与 `join!`。
2. 使用 `select!` 实现可取消倒计时。
3. 使用 `timeout` 包装可能挂起的操作。
4. 用有界 `mpsc` 实现生产者—消费者。
5. 用 `mpsc + oneshot` 实现资源管理任务。
6. 用 `Semaphore` 限制最多 10 个并发任务。
7. 编写 TCP echo server，并增加最大行长度。
8. 人为持有锁跨 await，分析问题并重构。
9. 为 REST API 添加分页和统一错误 JSON。
10. 为服务实现通知、等待、deadline 三阶段优雅停机。

---

## 二十、知识自测

1. Future 为什么是惰性的？
2. executor、task 和 waker 分别做什么？
3. `.await` 是否阻塞操作系统线程？
4. async 与多线程是什么关系？
5. `join!` 和 `spawn` 有何区别？
6. `JoinHandle.await` 为什么返回 join 结果？
7. `tokio::spawn` 为什么通常要求 `Send + 'static`？
8. `move` async 块解决什么所有权问题？
9. 取消一个 Future 会自动回滚副作用吗？
10. `timeout` 形成的嵌套 Result 如何理解？
11. 为什么优先使用有界通道？
12. 信号量和互斥锁的限制目标有何区别？
13. 什么时候应使用专用资源管理任务？
14. 为什么不能持同步锁跨 await？
15. `spawn_blocking` 适合什么工作？
16. TCP 为什么需要应用层 framing？
17. handler 为什么不应直接散布数据库细节？
18. 完整优雅停机包含哪几个阶段？
19. 如何让异步时间测试快速且确定？
20. 背压缺失会导致什么系统性后果？

---

## 二十一、阶段验收清单

### 异步模型

- [ ] 能解释 Future 的 poll 模型
- [ ] 能区分任务并发与线程并行
- [ ] 能使用 `async`、`.await`、`join!`、`select!` 和 `spawn`
- [ ] 能处理 `JoinHandle` 和任务错误
- [ ] 能解释 `Send + 'static` 约束

### 可靠性

- [ ] 能为外部 I/O 设置 timeout
- [ ] 能设计协作式取消
- [ ] 能使用有界通道实现背压
- [ ] 能用信号量限制并发量
- [ ] 不持同步锁跨越 `.await`
- [ ] 能把阻塞工作移出异步 worker
- [ ] 能实现完整优雅停机

### 网络与服务

- [ ] 能实现 TCP echo server
- [ ] 能解释消息边界和资源限制
- [ ] 能构建 CRUD REST API
- [ ] 参数校验与错误响应统一
- [ ] 共享状态的锁范围短且清楚
- [ ] 存储通过边界抽象，不散布到 handler
- [ ] 有异步单元、集成或端到端测试

### 项目

- [ ] 完成待办服务 CRUD
- [ ] 增加持久化仓储和迁移
- [ ] 支持配置、日志和优雅停机
- [ ] 对关键外部操作设置 timeout
- [ ] 对并发和队列设置明确上限
- [ ] 至少完成三个扩展任务

### 质量检查

```bash
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test --all-features
cargo build --release
```

当你能解释每个 `.await` 处保存了什么状态、哪些任务可被取消、系统如何限制排队和并发，并能交付可测试、可停机的网络服务时，即可进入第十阶段：宏、Unsafe、FFI 与底层原理。

---

## 二十二、延伸阅读方向

实践时优先查看项目锁定版本的官方文档：

- Tokio Tutorial：任务、共享状态、通道、`select!`
- Tokio Graceful Shutdown：检测、通知、等待三个阶段
- Tokio `Semaphore` 与 `timeout` API
- Axum `Router`、`State`、extractor 和 response 文档
- 所选数据库驱动的连接池、事务和迁移文档

生态 API 会演进，但核心原则相对稳定：让等待可并发，让资源有上限，让取消可推理，让错误可观察，让停机有秩序。
