# 第七阶段：Rust 智能指针与内部可变性

> 智能指针不是“更高级的引用”，而是拥有数据并附带特定所有权语义的类型。本阶段学习如何表达间接存储、共享所有权、内部可变性和父子关系，并理解这些便利背后的运行时成本与不变量。

## 一、阶段目标

完成本阶段后，你应当能够：

- 使用 `Box<T>` 分配堆数据和定义递归类型
- 理解 `Deref`、自动解引用与解引用强制转换
- 使用 `Drop` 和 RAII 管理资源
- 根据所有权关系选择 `Box<T>`、`Rc<T>` 或 `Arc<T>`
- 使用 `Cell<T>` 和 `RefCell<T>` 实现单线程内部可变性
- 理解 `RefCell<T>` 将借用检查推迟到运行时的代价
- 使用 `Mutex<T>`、`RwLock<T>` 表达线程安全内部可变性
- 理解锁中毒、死锁和锁作用域
- 使用 `Weak<T>` 表达不拥有资源的关系并避免引用循环
- 阅读 `Rc<RefCell<T>>` 与 `Arc<Mutex<T>>` 等组合类型
- 完成可修改、无引用环的树形数据结构

---

## 二、什么是智能指针

普通引用 `&T` 借用数据，不拥有数据。智能指针通常是结构体，它：

- 拥有所指向的数据或参与共享所有权
- 实现 `Deref`，可像引用一样访问内部值
- 通常实现 `Drop`，离开作用域时执行清理或减少引用计数
- 可能维护引用计数、借用状态或锁状态

常见类型：

| 类型 | 所有权 | 可变性检查 | 线程安全 | 典型用途 |
|---|---|---|---|---|
| `Box<T>` | 单一 | 编译期 | 取决于 `T` | 堆分配、递归类型、Trait 对象 |
| `Rc<T>` | 单线程共享 | 编译期 | 否 | 单线程共享只读数据 |
| `Arc<T>` | 多线程共享 | 编译期 | 是，仍取决于 `T` | 跨线程共享所有权 |
| `Cell<T>` | 单一外壳 | 运行时操作无借用 | 否 | 小型 `Copy` 值内部可变 |
| `RefCell<T>` | 单一外壳 | 运行期借用规则 | 否 | 单线程内部可变性 |
| `Mutex<T>` | 共享时配合 `Arc` | 运行时互斥锁 | 是 | 多线程独占修改 |
| `RwLock<T>` | 共享时配合 `Arc` | 运行时读写锁 | 是 | 多读少写共享数据 |
| `Weak<T>` | 不拥有 | 跟随 `Rc`/`Arc` | 分版本 | 父指针、缓存观察者、避免环 |

“线程安全”不是单个类型名称保证一切。例如 `Arc<RefCell<T>>` 仍不能安全跨线程共享，因为 `RefCell<T>` 不是 `Sync`。

---

## 三、`Box<T>`：单一所有权的堆分配

```rust
fn main() {
    let number = Box::new(42);
    println!("number = {number}");
    println!("value = {}", *number);
}
```

`number` 本身是固定大小指针，整数存放在堆上。`Box<T>` 离开作用域时，指针和堆数据一起释放。

## 3.1 何时使用 `Box<T>`

- 类型大小在编译期未知，但需要放入固定大小结构
- 转移较大值所有权而避免移动大量内联字节
- 拥有 Trait 对象：`Box<dyn Trait>`
- 构建递归数据结构

不要为了“堆更高级”而把所有值装箱。`Box` 有分配和指针间接访问成本。

## 3.2 递归类型为什么需要间接层

下面无法确定大小：

```rust,compile_fail
enum List {
    Cons(i32, List),
    Nil,
}
```

一个 `List` 包含另一个完整 `List`，编译器无法计算有限大小。使用 `Box`：

```rust
#[derive(Debug)]
enum List<T> {
    Cons(T, Box<List<T>>),
    Nil,
}

impl<T> List<T> {
    fn prepend(self, value: T) -> Self {
        Self::Cons(value, Box::new(self))
    }

    fn len(&self) -> usize {
        match self {
            Self::Cons(_, tail) => 1 + tail.len(),
            Self::Nil => 0,
        }
    }
}

fn main() {
    let list = List::Nil.prepend(3).prepend(2).prepend(1);
    println!("{list:?}, len={}", list.len());
}
```

`Box<List<T>>` 大小固定，因此枚举大小可计算。

## 3.3 Box 与移动

```rust
fn main() {
    let first = Box::new(String::from("hello"));
    let second = first;
    // println!("{first}"); // 所有权已移动
    println!("{second}");
}
```

装箱不会改变单一所有权规则。

---

## 四、`Deref` 与自动解引用

`Deref` 允许智能指针表现得像目标类型的引用。

```rust
use std::ops::Deref;

struct MyBox<T>(T);

impl<T> MyBox<T> {
    fn new(value: T) -> Self {
        Self(value)
    }
}

impl<T> Deref for MyBox<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

fn main() {
    let value = MyBox::new(5);
    assert_eq!(*value, 5);
}
```

`*value` 概念上相当于 `*(value.deref())`。

## 4.1 解引用强制转换

```rust
fn greet(name: &str) {
    println!("你好，{name}");
}

fn main() {
    let name = Box::new(String::from("Ferris"));
    greet(&name);
}
```

编译器可沿 `&Box<String>` → `&String` → `&str` 自动转换。

## 4.2 `DerefMut`

类型实现 `DerefMut` 后，可从 `&mut Wrapper<T>` 转换为 `&mut T`。提供它意味着调用者能够修改目标值，应确保这符合封装不变量。

不要滥用 `Deref` 模拟继承或隐藏业务转换。`Deref` 最适合真正具有智能指针语义的类型。

---

## 五、`Drop` 与 RAII

```rust
struct Connection {
    name: String,
}

impl Drop for Connection {
    fn drop(&mut self) {
        println!("关闭连接：{}", self.name);
    }
}

fn main() {
    let _connection = Connection {
        name: String::from("database"),
    };
    println!("工作中");
}
```

作用域结束时自动调用 `drop`。这就是 RAII：资源和拥有它的值共享生命周期。

## 5.1 提前释放

```rust
use std::mem::drop;

fn main() {
    let connection = Connection {
        name: String::from("temporary"),
    };
    drop(connection);
    println!("连接已经提前释放");
}
```

不能直接调用 `connection.drop()`，应使用 `std::mem::drop` 消费该值。

锁守卫也是 RAII：守卫离开作用域时自动解锁。需要尽早解锁时，可缩小作用域或显式 `drop(guard)`。

## 5.2 panic 时的清理

采用栈展开的 panic 策略时，已构造值通常会依次执行 `Drop`；采用直接终止策略时不会展开。不要把必须保证执行的持久化逻辑只依赖于 `Drop`，进程被强制终止、断电时它也无法执行。

---

## 六、`Rc<T>`：单线程共享所有权

当多个所有者需要共享同一值，且无法明确指定唯一所有者时，可使用引用计数 `Rc<T>`。

```rust
use std::rc::Rc;

fn main() {
    let shared = Rc::new(String::from("shared"));
    println!("count={}", Rc::strong_count(&shared));

    let first = Rc::clone(&shared);
    println!("count={}", Rc::strong_count(&shared));

    {
        let second = Rc::clone(&shared);
        println!("{first}, {second}");
        println!("count={}", Rc::strong_count(&shared));
    }

    println!("count={}", Rc::strong_count(&shared));
}
```

`Rc::clone` 只增加引用计数，不深拷贝内部数据。使用这个写法能明确表达“克隆共享所有权句柄”。

当强引用计数降为 0，内部值被释放。

## 6.1 共享链表

```rust
use std::rc::Rc;

#[derive(Debug)]
enum List {
    Cons(i32, Rc<List>),
    Nil,
}

fn main() {
    let tail = Rc::new(List::Cons(10, Rc::new(List::Nil)));
    let left = List::Cons(1, Rc::clone(&tail));
    let right = List::Cons(2, Rc::clone(&tail));
    println!("{left:?}\n{right:?}");
}
```

两个列表共享同一尾部。

## 6.2 `Rc<T>` 默认只读

`Rc<T>` 允许多个所有者，因此不能直接获得普通 `&mut T`。如果强引用只有一个，可尝试：

```rust
let mut value = Rc::new(String::from("hello"));
if let Some(inner) = Rc::get_mut(&mut value) {
    inner.push('!');
}
```

需要多个所有者共同修改时，单线程通常组合 `Rc<RefCell<T>>`。

## 6.3 不可跨线程

`Rc` 的引用计数更新不是线程安全的，因此不实现相应跨线程标记。跨线程共享使用 `Arc`。

---

## 七、`Cell<T>`：复制式内部可变性

`Cell<T>` 允许通过共享引用修改内部值，但不返回内部值的普通引用。

```rust
use std::cell::Cell;

struct Counter {
    calls: Cell<u64>,
}

impl Counter {
    fn record(&self) {
        self.calls.set(self.calls.get() + 1);
    }

    fn calls(&self) -> u64 {
        self.calls.get()
    }
}
```

即使方法接收 `&self`，也能更新计数。

适合：

- 小型 `Copy` 值
- 缓存标志、计数器
- 不需要向外借出内部引用

常用操作：`get`、`set`、`replace`、`take`、`update`（可用性视工具链而定）。对于非 `Copy` 值，可用 `replace` 或 `take` 移出，但 API 选择应保持清晰。

---

## 八、`RefCell<T>`：运行期借用检查

`RefCell<T>` 允许在只有共享引用的情况下修改内部值。

```rust
use std::cell::RefCell;

fn main() {
    let messages = RefCell::new(Vec::new());

    messages.borrow_mut().push(String::from("first"));
    messages.borrow_mut().push(String::from("second"));

    println!("{:?}", messages.borrow());
}
```

借用规则没有消失，只是从编译期移到运行时：

- 任意多个 `borrow()`
- 或一个 `borrow_mut()`
- 冲突时 panic

## 8.1 运行期借用冲突

```rust,should_panic
use std::cell::RefCell;

fn main() {
    let value = RefCell::new(String::from("hello"));
    let reader = value.borrow();
    let mut writer = value.borrow_mut();
    writer.push('!');
    println!("{reader}");
}
```

`reader` 守卫仍存活时请求可变借用，会 panic。

缩小作用域：

```rust
use std::cell::RefCell;

fn main() {
    let value = RefCell::new(String::from("hello"));
    {
        let reader = value.borrow();
        println!("{reader}");
    }
    value.borrow_mut().push('!');
}
```

## 8.2 非 panic API

```rust
match value.try_borrow_mut() {
    Ok(mut writer) => writer.push('!'),
    Err(error) => eprintln!("暂时无法修改：{error}"),
}
```

当冲突可能来自正常控制流时，使用 `try_borrow` / `try_borrow_mut` 比 panic 更合适。

## 8.3 使用场景

- 单线程图结构或树结构
- mock 对象在 `&self` 方法中记录调用
- 外部 Trait 只给 `&self`，实现需要维护内部状态
- 编译器无法证明安全，但设计者能维护借用时序

`RefCell` 不是“关闭借用检查器”。它引入运行时检查和 panic 风险，应尽量限制在清晰边界内。

---

## 九、`Rc<RefCell<T>>`：共享且可修改

```rust
use std::cell::RefCell;
use std::rc::Rc;

fn main() {
    let shared = Rc::new(RefCell::new(vec![1, 2, 3]));
    let first = Rc::clone(&shared);
    let second = Rc::clone(&shared);

    first.borrow_mut().push(4);
    second.borrow_mut().push(5);

    println!("{:?}", shared.borrow());
}
```

从外到内读类型：

- `Rc`：多个单线程所有者
- `RefCell`：运行期借用控制
- `Vec`：实际数据

组合很方便，也容易产生问题：

- 借用冲突直到运行时才暴露
- 数据流难追踪
- 可能形成强引用环
- API 容易把内部实现泄露给所有调用者

尽量用封装方法隐藏 `borrow_mut()`，不要让业务代码到处操作嵌套智能指针。

---

## 十、引用循环与内存泄漏

两个 `Rc` 节点互相持有强引用时，即使外部变量消失，强计数仍不为 0，数据无法释放。

```text
节点 A --强引用--> 节点 B
节点 A <--强引用-- 节点 B
```

安全 Rust 允许内存泄漏；它不会造成悬垂引用，但资源一直占用。

应判断关系中的所有权方向：

- 父拥有子：父到子使用强引用
- 子只观察父：子到父使用弱引用

---

## 十一、`Weak<T>`：不拥有数据的引用

`Weak<T>` 不增加强引用计数，不阻止值释放。

```rust
use std::rc::{Rc, Weak};

fn main() {
    let strong = Rc::new(String::from("hello"));
    let weak: Weak<String> = Rc::downgrade(&strong);

    match weak.upgrade() {
        Some(value) => println!("仍存在：{value}"),
        None => println!("已经释放"),
    }

    drop(strong);
    assert!(weak.upgrade().is_none());
}
```

`upgrade()` 返回 `Option<Rc<T>>`，因为原值可能已经释放。

引用计数：

```rust
Rc::strong_count(&value);
Rc::weak_count(&value);
```

计数适合学习和诊断，不应成为脆弱业务逻辑的核心依据。

---

## 十二、树形结构：父弱子强

```rust
use std::cell::RefCell;
use std::rc::{Rc, Weak};

#[derive(Debug)]
struct Node {
    name: String,
    parent: RefCell<Weak<Node>>,
    children: RefCell<Vec<Rc<Node>>>,
}

impl Node {
    fn new(name: impl Into<String>) -> Rc<Self> {
        Rc::new(Self {
            name: name.into(),
            parent: RefCell::new(Weak::new()),
            children: RefCell::new(Vec::new()),
        })
    }

    fn add_child(parent: &Rc<Self>, child: Rc<Self>) -> Result<(), String> {
        if Rc::ptr_eq(parent, &child) {
            return Err(String::from("节点不能成为自己的子节点"));
        }

        if child.parent.borrow().upgrade().is_some() {
            return Err(format!("节点 {} 已经有父节点", child.name));
        }

        *child.parent.borrow_mut() = Rc::downgrade(parent);
        parent.children.borrow_mut().push(child);
        Ok(())
    }

    fn parent(&self) -> Option<Rc<Node>> {
        self.parent.borrow().upgrade()
    }

    fn child_names(&self) -> Vec<String> {
        self.children
            .borrow()
            .iter()
            .map(|child| child.name.clone())
            .collect()
    }
}

fn main() -> Result<(), String> {
    let root = Node::new("root");
    let docs = Node::new("docs");
    let guide = Node::new("guide.md");

    Node::add_child(&root, Rc::clone(&docs))?;
    Node::add_child(&docs, Rc::clone(&guide))?;

    println!("root children: {:?}", root.child_names());
    println!("guide parent: {}", guide.parent().unwrap().name);
    println!("root strong={}, weak={}",
        Rc::strong_count(&root), Rc::weak_count(&root));
    Ok(())
}
```

关系：

```text
root --Rc--> docs --Rc--> guide
root <--Weak-- docs <--Weak-- guide
```

父节点拥有子节点；子节点不拥有父节点，因此没有强引用环。

## 12.1 更严格的环检测

示例只阻止自引用和重复父节点。如果支持移动子树，还需沿目标父节点的祖先链向上检查，确保子节点不是其祖先，否则会形成逻辑环。

## 12.2 为什么字段使用 `RefCell`

节点通过 `Rc` 被多个位置共享，普通方式无法获得 `&mut Node`。`RefCell` 允许在已共享的节点中修改父子关系，但调用方必须维持运行期借用规则。

---

## 十三、`Arc<T>`：线程安全共享所有权

`Arc` 使用原子引用计数，可以跨线程共享所有权。

```rust
use std::sync::Arc;
use std::thread;

fn main() {
    let message = Arc::new(String::from("shared"));
    let mut handles = Vec::new();

    for index in 0..3 {
        let message = Arc::clone(&message);
        handles.push(thread::spawn(move || {
            println!("{index}: {message}");
        }));
    }

    for handle in handles {
        handle.join().expect("线程不应 panic");
    }
}
```

原子计数有额外成本。单线程共享使用 `Rc`，不要无条件选择 `Arc`。

`Arc<T>` 只保证引用计数更新安全，不自动让 `T` 可修改。共享修改常用 `Arc<Mutex<T>>` 或 `Arc<RwLock<T>>`。

---

## 十四、`Mutex<T>`：互斥内部可变性

```rust
use std::sync::{Arc, Mutex};
use std::thread;

fn main() {
    let counter = Arc::new(Mutex::new(0_u64));
    let mut handles = Vec::new();

    for _ in 0..10 {
        let counter = Arc::clone(&counter);
        handles.push(thread::spawn(move || {
            let mut value = counter.lock().expect("锁已中毒");
            *value += 1;
        }));
    }

    for handle in handles {
        handle.join().expect("线程不应 panic");
    }

    println!("{}", *counter.lock().expect("锁已中毒"));
}
```

`lock()` 返回守卫，守卫实现 `DerefMut`。守卫离开作用域时解锁。

## 14.1 缩小锁作用域

```rust
let snapshot = {
    let data = shared.lock().map_err(|_| "锁已中毒")?;
    data.clone()
}; // 此处解锁

perform_slow_io(snapshot);
```

不要在持锁时执行慢 I/O、长时间计算或调用未知外部代码。

## 14.2 锁中毒

线程持锁期间 panic 时，标准库锁会被标记为中毒，提醒共享数据可能处于不完整状态。应用应根据不变量决定：

- 传播错误并停止相关操作
- 检查并修复状态后恢复
- 在明确安全时取出内部守卫

不要机械 `unwrap()` 后又声称系统具有容错能力。

## 14.3 死锁

常见死锁来源：

- 两个线程以相反顺序获取两把锁
- 同一线程持锁时再次请求同一非重入锁
- 持锁发送消息并等待另一方，而另一方也在等锁

预防：

- 全局规定锁顺序
- 缩小锁作用域
- 减少同时持有多把锁
- 使用 `try_lock` 或超时策略（若使用的同步工具支持）
- 优先考虑消息传递或重新划分数据所有权

---

## 十五、`RwLock<T>`：多读单写

```rust
use std::sync::RwLock;

fn main() {
    let config = RwLock::new(String::from("v1"));

    {
        let first = config.read().expect("锁已中毒");
        let second = config.read().expect("锁已中毒");
        println!("{first} {second}");
    }

    {
        let mut writer = config.write().expect("锁已中毒");
        *writer = String::from("v2");
    }
}
```

适合读取明显多于写入、且读临界区足够有价值的场景。它不一定比 `Mutex` 快，实际表现受实现、争用和工作负载影响，应基准验证。

---

## 十六、内部可变性组合选择

| 场景 | 常见选择 |
|---|---|
| 单一所有者、普通修改 | `T` + `&mut T` |
| 单一所有者、小型 Copy 字段内部修改 | `Cell<T>` |
| 单线程共享所有权、只读 | `Rc<T>` |
| 单线程共享所有权、可修改 | `Rc<RefCell<T>>` |
| 多线程共享所有权、只读 | `Arc<T>` |
| 多线程共享、互斥修改 | `Arc<Mutex<T>>` |
| 多线程共享、多读少写 | `Arc<RwLock<T>>` |
| 不应延长目标生命的关系 | `Weak<T>` |

优先使用最简单的静态所有权：

```text
拥有值 / &mut T
    ↓ 确实需要单线程共享
Rc<T>
    ↓ 确实需要共享修改
Rc<RefCell<T>>
```

跨线程同理逐步引入 `Arc` 和锁。复杂智能指针组合应当是需求推导的结果。

---

## 十七、阶段项目：可修改目录树

在前面的 `Node` 基础上实现完整目录树。

## 17.1 功能要求

- 创建目录和文件节点
- 父节点强拥有子节点
- 子节点弱引用父节点
- 添加、删除、重命名节点
- 输出绝对路径
- 深度优先遍历
- 阻止重复名称和逻辑环
- 删除子树后，外部无强引用时自动释放
- 为引用计数和父子关系编写测试

## 17.2 推荐模型

```rust
enum NodeKind {
    Directory,
    File { size: u64 },
}

struct Node {
    name: RefCell<String>,
    kind: NodeKind,
    parent: RefCell<Weak<Node>>,
    children: RefCell<Vec<Rc<Node>>>,
}
```

可进一步把可变状态集中到一个 `RefCell<NodeState>`：

```rust
struct NodeState {
    name: String,
    parent: Weak<Node>,
    children: Vec<Rc<Node>>,
}

struct Node {
    kind: NodeKind,
    state: RefCell<NodeState>,
}
```

集中借用能简化一致性更新，但也可能扩大借用范围。选择后记录理由。

## 17.3 核心 API

```rust,ignore
impl Node {
    fn new_directory(name: impl Into<String>) -> Rc<Self>;
    fn new_file(name: impl Into<String>, size: u64) -> Rc<Self>;
    fn add_child(parent: &Rc<Self>, child: Rc<Self>) -> Result<(), TreeError>;
    fn remove_child(parent: &Rc<Self>, name: &str) -> Result<Rc<Self>, TreeError>;
    fn rename(&self, name: impl Into<String>) -> Result<(), TreeError>;
    fn absolute_path(&self) -> String;
    fn walk(&self, visitor: &mut impl FnMut(&Node, usize));
}
```

`remove_child` 返回 `Rc<Node>`，把从树中移除的强所有权交给调用者。调用者丢弃它且没有其他强引用时，子树自动释放。

## 17.4 错误枚举

```rust
#[derive(Debug, PartialEq, Eq)]
enum TreeError {
    EmptyName,
    InvalidName(String),
    DuplicateName(String),
    AlreadyHasParent,
    NotDirectory,
    NotFound(String),
    WouldCreateCycle,
    BorrowConflict,
}
```

公共操作可使用 `try_borrow`，把意外借用冲突转成 `BorrowConflict`，避免库因正常调用时序直接 panic。

## 17.5 环检测思路

添加 `child` 到 `parent` 前：

1. 若 `Rc::ptr_eq(parent, &child)`，拒绝。
2. 从 `parent` 沿 `Weak::upgrade()` 向祖先遍历。
3. 如果任何祖先与 `child` 指向同一分配，拒绝。
4. 确认名称不重复。
5. 设置弱父引用，再加入强子列表。

## 17.6 必写测试

- 新节点没有父节点
- 添加后父子双方关系正确
- 父强计数不因子节点的弱引用增加
- 重复名称被拒绝
- 节点不能添加为自己的子节点
- 祖先不能成为后代的子节点
- 删除后父引用失效或被清空
- 外部最后一个 `Rc` 释放后 `Weak::upgrade()` 返回 `None`
- 遍历深度和顺序正确
- 文件节点不能添加子节点

## 17.7 扩展任务

1. 实现移动子树并保持事务式一致性。
2. 使用闭包访问者统计文件数和总大小。
3. 实现按路径查找。
4. 添加只读快照。
5. 隐藏所有智能指针字段，仅暴露领域 API。
6. 用 arena/索引方案重写并比较复杂度。
7. 实现 `Display` 树形打印。
8. 添加属性测试生成随机树操作序列。

---

## 十八、常见错误与排查

## 18.1 `Rc` 不是深拷贝

`Rc::clone` 增加强计数；`(*rc).clone()` 才可能克隆内部值。两者语义和成本不同。

## 18.2 `Arc` 不等于内部可变

`Arc<Vec<T>>` 可跨线程共享只读数据，但不能直接 `push`。需要修改时重新设计所有权或加入同步原语。

## 18.3 `RefCell` 借用守卫活得太久

避免把 `borrow()` 结果保存在大作用域变量中。尽量：

```rust
let length = value.borrow().len();
value.borrow_mut().push(new_item);
```

第一个临时借用在语句结束后释放。复杂情况下使用代码块或显式 `drop`。

## 18.4 临时借用与返回值

不能返回指向 `RefCell` 临时借用守卫内部的普通引用，因为守卫离开后运行期借用就结束。可返回拥有值、映射后的守卫，或让调用者通过闭包访问。

## 18.5 引用环难发现

绘制所有权图，为每条边标注“强”或“弱”。任何强引用有向环都值得审查。

## 18.6 锁粒度过大

症状包括吞吐下降、长尾延迟、线程长期阻塞。测量持锁时间，并把慢工作移出临界区。

---

## 十九、针对性练习

1. 用 `Box` 实现泛型链表及迭代方法。
2. 实现自定义智能指针并为其实现 `Deref`。
3. 用 `Cell<u64>` 实现只读方法中的调用计数。
4. 用 `RefCell<Vec<String>>` 实现记录调用的 mock。
5. 人为制造一次 `RefCell` 借用 panic，再用作用域修复。
6. 构建两个共享尾部的 `Rc` 链表并观察强计数。
7. 构造强引用环并用 `Weak` 重构。
8. 用 `Arc<Mutex<u64>>` 实现并发计数器。
9. 比较 `Mutex` 与 `RwLock` API，并说明何时不应使用后者。
10. 为目录树实现绝对路径和环检测。

---

## 二十、知识自测

1. 智能指针与普通引用最核心的差别是什么？
2. 递归类型为什么需要 `Box` 等间接层？
3. `Deref` 和解引用强制转换解决什么问题？
4. 为什么不能直接调用值的 `drop` 方法？
5. `Rc::clone` 与内部值的 `clone` 有何区别？
6. `Rc` 为什么不能跨线程？
7. `Cell` 与 `RefCell` 的使用差异是什么？
8. `RefCell` 是否取消了借用规则？
9. `borrow_mut` 冲突时发生什么？
10. `Rc<RefCell<T>>` 每一层分别提供什么能力？
11. 强引用环为什么导致泄漏？
12. `Weak::upgrade` 为什么返回 `Option`？
13. 父子树中哪条边应为强引用，哪条应为弱引用？
14. `Arc<T>` 是否自动让 `T` 可安全修改？
15. `MutexGuard` 如何保证自动解锁？
16. 什么是锁中毒？
17. 如何缩小锁作用域？
18. `RwLock` 一定比 `Mutex` 快吗？
19. 如何从嵌套智能指针类型读出所有权语义？
20. 什么情况下索引或 arena 可能比 `Rc<RefCell<T>>` 更合适？

---

## 二十一、阶段验收清单

### 智能指针基础

- [ ] 能使用 `Box<T>` 定义递归类型
- [ ] 能解释 `Deref` 和自动解引用
- [ ] 能通过 `Drop` 理解 RAII
- [ ] 能区分 `Rc::clone` 与深拷贝
- [ ] 能根据共享范围选择 `Rc` 或 `Arc`

### 内部可变性

- [ ] 能使用 `Cell` 管理小型 Copy 状态
- [ ] 能使用 `RefCell` 并控制借用守卫作用域
- [ ] 能解释运行期借用冲突
- [ ] 能合理封装 `Rc<RefCell<T>>`
- [ ] 能使用 `Mutex` 与 `RwLock`
- [ ] 能解释锁中毒、锁粒度和死锁风险

### 引用关系

- [ ] 能画出强引用和弱引用所有权图
- [ ] 能识别强引用环
- [ ] 能使用 `Weak` 表达非拥有关系
- [ ] 能正确处理 `upgrade()` 的 `None`

### 项目

- [ ] 完成可修改目录树
- [ ] 父强拥有子、子弱引用父
- [ ] 阻止重复名称、自引用和祖先环
- [ ] 删除节点时正确转移所有权
- [ ] 公共 API 不泄露不必要的 `RefCell` 操作
- [ ] 为释放、弱引用和借用冲突编写测试
- [ ] 至少完成三个扩展任务

### 质量检查

```bash
cargo fmt --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test
cargo doc --no-deps
```

当你能为一张对象关系图明确指出每条边的所有者，能根据单线程/多线程和只读/可写需求选择组合，并能识别运行期借用与死锁风险时，即可进入第八阶段：并发编程。

---

## 二十二、进入下一阶段前的预习

下一阶段将系统学习：

- 如何创建线程并安全转移数据？
- `Send` 和 `Sync` 分别意味着什么？
- 消息传递与共享状态如何选择？
- 通道如何表达所有权转移？
- 如何实现线程池和生产者—消费者？
- 原子类型适合哪些共享状态？

建议保留本阶段的 `Arc<Mutex<T>>` 示例。下一阶段将加入通道、线程池和原子类型，并比较“共享可变状态”与“通过消息转移所有权”的设计差异。
