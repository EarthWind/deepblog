# Linux 内核 CephFS 客户端实现：内核态挂载、页缓存与回写路径

## 这篇文章要解决什么问题

前面几篇我们已经把 CephFS 的用户态世界讲得比较完整了：

- `MDS` 如何处理元数据请求
- `MDCache`、`Locker`、`Capability`、`SessionMap` 怎样一起维持一致性
- `src/client` 里的通用客户端语义如何工作
- `ceph-fuse` 如何把 FUSE 请求桥接到 CephFS 客户端

但 CephFS 还有另一条同样非常重要、而且在生产里极常见的客户端路径：

- **Linux 内核 CephFS 客户端**

也就是我们平时常说的：

- kernel client
- kclient
- `mount -t ceph ...`

很多人第一次接触它时，容易把它想成：

- “把用户态 CephFS 客户端搬进内核”

这个理解不够准确。

更准确地说，内核 CephFS 客户端是一套：

- 接入 Linux VFS
- 依赖内核页缓存和 writeback 机制
- 通过 MDS capability 协议维持缓存一致性
- 数据面直接连到底层对象存储

的专门实现。

所以真正值得问的问题是：

- 内核客户端到底怎样挂载 CephFS？
- 它和 Linux VFS 是怎样对接的？
- page cache 在 CephFS kernel client 里扮演什么角色？
- writeback 是怎样和 cap、一致性、错误上报绑在一起的？
- 为什么 CephFS 能同时说自己支持较强 cache coherency，但又要特别提醒 `mmap`、断连和 eviction 的边界？

如果只允许用一句话先给结论，那就是：

**Linux 内核 CephFS 客户端不是“把 FUSE 换成内核态”这么简单，而是一套直接嵌入 Linux VFS 的远端文件系统实现：挂载时通过 `mount.ceph` 和 kernel client 建立到 Ceph 集群的连接，元数据操作通过 MDS 协议完成，文件内容读写则建立在内核 page cache、writeback 和 capability 协议之上，因此它既能像本地文件系统一样接入页缓存与回写框架，又必须持续和 MDS 协商 cache 与写缓冲的一致性边界。**

这一篇的目标，就是把这条线真正讲清楚。

## 先建立第一条边界：内核 CephFS 客户端主体不在 Ceph 仓库里

这是理解整篇文章的第一步。

必须先把一个现实讲清楚：

- Ceph 仓库里主要能直接看到 CephFS 的用户态客户端、FUSE 适配层和大量文档
- Linux 内核态 CephFS 客户端主体，则主要在 Linux 内核源码树的 `fs/ceph`

也就是说，这一篇如果要讲“内核 CephFS 客户端实现”，就不能只把注意力放在当前 Ceph 仓库源码里，而要同时建立一个正确边界：

- **Ceph 仓库负责文档、协议背景和用户态实现视角**
- **内核源码树负责真正的 kernel client 实现**

### 为什么这很重要

因为这会直接影响你如何阅读和理解：

- Ceph 文档会告诉你挂载方式、cap 语义、一致性和 writeback 边界
- Linux `fs/ceph` 才会告诉你 superblock、inode、dentry、address_space、writeback 是怎样接到 VFS 的

所以这篇文章的写法也会遵循这条边界：

- 先用 Ceph 仓库文档立整体模型
- 再用 Linux kernel client 的典型主线去建立实现心智图

## 第 1 层：先把总结构图立住

如果整篇只记一张总图，我建议先记下面这张：

```text
用户进程
  ->
Linux VFS
  ->
fs/ceph (kernel client)
  |    \
  |     \-> MDS 协议：lookup/open/create/caps/session
  |
  \-> page cache / writeback / address_space
         \
          \-> OSD 数据面读写
```

这张图最重要的意义是：

- 它把内核客户端明确放在 Linux VFS 和 Ceph 集群之间

也就是说，kernel client 的真正任务不是：

- “发几个网络请求”

而是：

- **把 Linux 本地文件系统抽象和 Ceph 远端文件系统协议稳定对接起来。**

## 第 2 层：为什么说 `mount.ceph` 只是 helper，真正干活的是 kernel client

很多人第一次挂载 CephFS，接触到的是：

- `mount -t ceph ...`
- 或 `mount.ceph`

这很容易让人以为：

- `mount.ceph` 本身实现了很多核心逻辑

其实不是。

更准确地说：

- `mount.ceph` 更像一个 mount helper
- 它负责整理和传递挂载参数
- 真正把 CephFS 文件系统接进 Linux 内核的，仍然是 kernel client

### 这意味着什么

意味着内核态 CephFS 挂载这件事的核心不是：

- 用户态 mount 命令多复杂

而是：

- 内核侧如何建立 superblock、会话、MDS/OSD 连接与文件系统实例

这点很值得先立住。

## 第 3 层：挂载阶段真正建立了哪些东西

如果从架构层面看，kernel client 挂载 CephFS 时，真正要完成的事至少包括：

- 解析 monitor / fs / path / auth 等挂载参数
- 建立到 Ceph 集群的基础连接
- 初始化 MDS client 状态
- 初始化 superblock 与根 dentry / 根 inode
- 把 CephFS 文件系统实例接到 Linux VFS 上

### 这说明什么

说明挂载并不是：

- “远端问一句能不能挂”

而是：

- 在内核里真正生成一个远端文件系统实例，并把它注册成 VFS 可用对象

所以这一步的关键词应该是：

- `superblock`
- `root dentry`
- `root inode`
- `mds client`

而不是只盯着 mount 命令本身。

## 第 4 层：为什么理解内核 CephFS 客户端，一定要先理解 VFS 对接

这是整篇最重要的切换点之一。

如果你已经读过前面用户态客户端和 Ceph-FUSE 那几篇，会很容易带着一个思维惯性：

- CephFS 客户端主要是“路径解析 + 会话 + cap + 请求发送”

这当然没错，但到了内核客户端，这个视角还不够。

因为内核客户端必须首先回答：

- 它如何对接 Linux VFS？

也就是：

- superblock 放在哪里
- inode 用哪套 inode operations
- dentry 用哪套 dentry operations
- file 用哪套 file operations
- address_space 怎样接 page cache 与 writeback

### 所以最准确的理解方式是

- kernel client 不是“把用户态客户端搬进内核”
- 它首先是一套 Linux VFS 文件系统实现

这点非常关键。

## 第 5 层：为什么 `dir.c`、`inode.c`、`file.c`、`addr.c` 这几个名字就已经能说明主线

虽然内核源码主体不在当前仓库，但只要知道 Linux `fs/ceph` 的典型文件分工，其实已经足够建立很强的阅读框架。

最值得优先关注的大概就是：

- `super.c`
- `dir.c`
- `inode.c`
- `file.c`
- `addr.c`
- `caps.c`
- `mds_client.c`

### 这几个名字分别意味着什么

## `super.c`

- 挂载、superblock、文件系统实例建立

## `dir.c`

- lookup、create、rename、unlink、readdir 等目录和路径语义

## `inode.c`

- inode 生命周期、属性、元数据更新

## `file.c`

- open、read、write、fsync、file operations

## `addr.c`

- page cache、address_space、writeback、页级 IO

## `caps.c`

- capability 协议与一致性控制

## `mds_client.c`

- 和 MDS 的元数据协议交互

### 这说明什么

说明内核客户端的阅读方式，天然就应该按：

- 挂载
- VFS 对接
- 元数据路径
- page cache / writeback
- cap 一致性

这条线来展开。

## 第 6 层：为什么说内核客户端的元数据面仍然是“客户端 -> MDS”

这一点很容易被新读者误解。

因为一旦听到“内核态文件系统实现”，大家会下意识想到：

- 是不是很多元数据都在本地了？

其实不是这个意思。

和前面用户态客户端一样，CephFS kernel client 在元数据面上仍然遵循同一套根设计：

- 目录、路径、inode 元数据、caps 仍然由 MDS 主导

### 这意味着什么

像：

- `lookup`
- `getattr`
- `create`
- `rename`
- `unlink`

这些动作，在内核客户端里虽然表现成：

- `dentry_operations`
- `inode_operations`
- `file_operations`

但它们背后仍然要通过：

- MDS 协议

完成真正的远端元数据协商。

也就是说，VFS 只是本地入口形态变了，CephFS 元数据面的架构原则并没有变。

## 第 7 层：为什么内核客户端的目录项缓存同样离不开 lease 和 cap

这一点和用户态客户端那篇高度呼应。

CephFS kernel client 不是把 Linux dcache 当成一个完全本地、自说自话的名字缓存。

更准确地说：

- dentry 的可信度仍然要受到 CephFS 协议控制

这背后最关键的两个词仍然是：

- dentry lease
- cap

### 这说明什么

说明 Linux VFS 层的：

- dentry cache
- inode cache

虽然提供了本地缓存壳子，但 CephFS 是否能放心命中它们，仍然要服从：

- MDS 协议下发的 lease/cap 语义

这就是为什么 CephFS kernel client 不只是一个普通网络文件系统壳层，而是：

- 一个把 Linux 本地缓存体系和远端一致性协议拧在一起的实现

## 第 8 层：为什么 page cache 是理解 kernel client 和 Ceph-FUSE 最大区别的第一关键词

如果要从架构角度总结：

- kernel client 和 Ceph-FUSE 最大的实现差别是什么？

我会先给出一个词：

- `page cache`

### 为什么

因为 Ceph-FUSE 虽然也能享受到页缓存带来的好处，但它毕竟是：

- 用户态文件系统

而内核 CephFS 客户端则是：

- 直接把远端文件系统接进 Linux VFS 和 address_space

这意味着：

- 页缓存不再只是“外面还有一层”
- 而是 kernel client 自己数据路径的一部分

### 所以最重要的区别是什么

- Ceph-FUSE 更像“FUSE + 用户态客户端 + 内核页缓存外壳”
- kernel client 则是“直接成为 VFS/page cache/writeback 体系的一员”

这就是为什么第 29 篇一定要单独讲。

## 第 9 层：为什么 read 路径不能只理解成“内核发请求到 OSD”

到了数据路径，最常见的误解就是：

- 内核读文件，不就是发一个读请求到后端吗？

这个理解太粗糙。

更准确地说，内核 CephFS read 路径至少要经过下面几层思考：

- 当前页是否已经在 page cache 里
- 当前客户端是否持有足够的 file read / cache 相关 capability
- 如果命中页缓存，是否可以直接满足
- 如果 miss，是否需要从后端对象存储拉取数据页

### 这意味着什么

意味着在 kernel client 里，读路径真正的直觉应该是：

- **先问 page cache 和 cap 是否允许本地命中，再决定要不要访问远端对象数据。**

这点非常重要。

## 第 10 层：为什么 write 路径真正难在 writeback，而不是“把数据写远端”

写路径同样很容易被低估。

很多人会想：

- 写文件，不就是写远端对象吗？

实际上，kernel client 最关键的事情往往不是“立刻远端写”，而是：

- 怎样把 Linux 页缓存里的脏页和 CephFS 的 capability 协议正确绑起来

### 这里最值得建立的直觉

在 CephFS 里，写并不总是：

- 每次 `write()` 立刻同步落远端

而经常是：

- 先进入 page cache
- 再通过 writeback 路径异步推进

但前提是客户端必须持有允许：

- buffer file write

这类能力。

### 所以 writeback 路径在回答什么

- 哪些页已经脏了
- 哪些页可以继续缓存在本地
- 什么时候必须下刷
- 下刷时如何报错
- 断连或 eviction 时这些脏页会发生什么

这就是 kernel client 真正复杂的地方。

## 第 11 层：为什么 `Fb` 这类 capability 对内核 writeback 特别关键

前面几篇我们已经讲过：

- CephFS cap 不只是权限位

到了 kernel client，这一点会体现得更直接。

比如：

- `Fc` 可以直观对应读缓存能力
- `Fb` 可以直观对应写缓冲能力

### 为什么 `Fb` 特别重要

因为它几乎直接决定：

- 当前客户端能不能把写先 buffer 在本地

而这正是 page cache + writeback 能成立的前提之一。

### 这说明什么

说明 kernel client 的 writeback 不是“纯 Linux 本地机制”，而是：

- Linux 页缓存/回写框架
- 加上 CephFS capability 协议

两者共同作用的结果。

这就是 CephFS 很有代表性的地方。

## 第 12 层：为什么 CephFS 能说自己有 strong cache coherency，但又必须反复提醒边界

这是讲内核客户端时特别容易说偏的话题。

CephFS 文档会强调：

- strong cache coherency

这确实是理解 CephFS 的关键优势之一。

但如果只说这句话，不补边界，就很容易误导。

### 为什么

因为 kernel client 的缓存一致性虽然很强，但它依赖的是：

- MDS capability 协议
- lease 失效与回收
- flush / writeback / revoke / reconnect

这一整套持续运行的机制。

而且文档也会明确提醒一些边界，比如：

- 跨主机共享 writable mmap 的页缓存失效并不是完全自动同步

### 所以更准确的说法应该是

- CephFS 提供较强的缓存一致性
- 但它不是“所有共享内存语义自动完美透明”的魔法系统

这对理解内核客户端尤其重要。

## 第 13 层：为什么 `write()` 成功不等于数据已经真正 durable

这是 writeback 相关里最容易踩坑的一点。

很多人会本能觉得：

- `write()` 成功就说明已经写好了

但只要你接受了：

- page cache
- writeback
- 异步刷回

这套模型，就应该立刻意识到：

- `write()` 成功往往只代表数据已经成功进入客户端侧缓冲路径

而不必然代表：

- 已经稳定落到远端并可持久保证

### 所以为什么 `fsync()` 很重要

因为错误比如：

- ENOSPC
- 连接中断后的回写失败

往往更可靠地会在：

- flush / fsync / writeback 收敛阶段

暴露出来。

### 这说明什么

说明 CephFS kernel client 的写语义，和本地支持 writeback 的文件系统一样，必须结合：

- page cache
- writeback
- fsync

一起理解，而不能只盯着 `write()` 返回值。

## 第 14 层：为什么断连、eviction 和 `recover_session` 这几个词应该放在同一节讲

这一点非常适合从 writeback 风险角度串起来。

如果客户端只是只读命中 cache，断连的后果相对简单一些。

但如果客户端当前持有：

- dirty page cache
- buffered write
- 尚未完全 flush 的状态

那一旦发生：

- session reset
- eviction
- reconnect 失败

问题就会立刻复杂起来。

### 为什么

因为这里已经不只是“我还能不能继续访问文件”，而是：

- 那些本地尚未完全稳定写出的脏数据怎么办

这也是为什么文档会特别提醒：

- 客户端被 eviction 时，未 flush 的 buffered IO 可能丢失
- 某些场景下需要关注 `recover_session`

### 所以这些词其实在讲同一个主题

- **当 page cache/writeback 遇到会话失效时，系统如何保住一致性边界。**

这点非常关键。

## 第 15 层：为什么内核客户端调试通常离不开 `dmesg` 和 debugfs

如果从排障视角讲 kernel client，就不能只讲“挂载怎么配”。

还必须明确：

- 内核客户端出了问题，很多关键信息根本不在用户态日志里

而是在：

- `dmesg`
- `/sys/kernel/debug/ceph/...`

这类位置。

### 这说明什么

说明 kernel client 的排障思维，也必须和用户态客户端不同。

用户态客户端你可能更容易看：

- 自己进程日志
- admin socket

而 kernel client 则更多要看：

- 内核日志
- debugfs 状态
- caps stale / session reset / writeback 错误

这点很适合在文章末尾作为“实现与运维连接点”提出来。

## 第 16 层：把整个内核客户端主线压缩成一张图

如果这一篇只记一张图，我建议记下面这张：

```text
mount.ceph / mount -t ceph
  ->
Linux kernel fs/ceph 建立 superblock 与会话
  ->
VFS inode/dentry/file/address_space 接入 CephFS
  ->
元数据操作走 MDS 协议
  ->
数据读写走 page cache + cap 协议 + OSD 数据面
  ->
脏页通过 writeback 刷回
  ->
flush/fsync/reconnect/eviction 决定最终一致性与错误暴露边界
```

这张图里最关键的一句话是：

- **内核 CephFS 客户端是“VFS + page cache + Ceph 协议”三者的结合体。**

## 第 17 层：为什么说 kernel client 真正难的不是“网络文件系统”，而是“远端协议和本地内核语义的精确缝合”

这是整篇最想让读者留下的认知。

如果只是把 CephFS kernel client 看成：

- 一个网络文件系统驱动

你当然没有说错，但还是太平了。

它真正困难、也真正精彩的地方在于：

- 一边要遵守 Linux VFS、inode、dentry、address_space、writeback 的本地语义
- 一边又要遵守 CephFS 的 MDS、cap、session、一致性协议

换句话说，它最难的地方不是：

- “怎么把包发到远端”

而是：

- **怎么把 Linux 本地文件系统抽象和 Ceph 远端文件系统协议精确地缝在一起。**

这就是为什么它必须单独写一篇。

## 用一句话重新概括这篇

如果把这篇全部内容压缩成一句尽量准确的话，我会这样说：

**Linux 内核 CephFS 客户端是一套直接嵌入 `VFS` 的远端文件系统实现：挂载时由 `mount.ceph` 辅助内核建立 CephFS 实例，元数据路径通过 MDS 协议和 capability 体系维持名字空间与缓存一致性，数据路径则建立在 page cache、writeback 与后端对象读写之上，因此它的核心不只是“内核里发 Ceph 请求”，而是把 Linux 本地文件系统语义和 CephFS 远端协议稳定缝合起来。**

## 把整篇压缩成一条最短骨架

如果你只想记一条骨架，可以记成下面这样：

```text
mount helper 负责传参
  ->
kernel client 建立 superblock / root / MDS 会话
  ->
VFS 操作映射到 fs/ceph
  ->
元数据操作走 MDS
  ->
读先看 page cache 和 caps
  ->
写先进入 page cache，再走 writeback
  ->
flush/fsync/reconnect/eviction 决定最终一致性与错误边界
```

只要这条骨架记住了，Linux 内核 CephFS 客户端的总体结构就不会乱。

## 初学者最容易混淆的 10 个点

### 1. 认为内核客户端代码主体在 Ceph 仓库里

不对。主体主要在 Linux `fs/ceph`。

### 2. 认为 `mount.ceph` 本身实现了核心文件系统逻辑

不对。它主要是 mount helper。

### 3. 认为 kernel client 只是把用户态客户端搬进内核

不对。它首先是一套 VFS 文件系统实现。

### 4. 认为 metadata path 因为在内核里就不需要 MDS 了

不对。元数据面仍然由 MDS 主导。

### 5. 认为 dentry/inode cache 在内核里天然可信

不对。它们仍受 lease/cap 协议约束。

### 6. 认为 read 就是直接远端拉数据

不对。先要看 page cache 和 capability。

### 7. 认为 write 成功就等于已经稳定落盘

不对。writeback 和 fsync 边界同样重要。

### 8. 认为 writeback 只是 Linux 本地机制，和 Ceph 协议无关

不对。它和 `Fb` 等 capability 紧密绑定。

### 9. 认为 strong cache coherency 等于所有共享页缓存场景都自动完美同步

不对。仍然有明确边界，比如 writable mmap。

### 10. 认为 kernel client 排障主要看用户态日志

不对。很多关键线索在 `dmesg` 和 debugfs。

## 这一篇最应该留下的 5 个直觉

### 直觉一：kernel client 首先是 VFS 文件系统实现

这是第一原则。

### 直觉二：metadata path 仍然是客户端到 MDS 的协议交互

内核态不改变这一点。

### 直觉三：page cache 是 kernel client 最关键的实现特征之一

这也是它和 Ceph-FUSE 最大的差别。

### 直觉四：writeback 必须和 capability 协议一起理解

这点非常关键。

### 直觉五：内核 CephFS 客户端真正难的是缝合本地内核语义和远端 Ceph 协议

这就是它的技术价值。

## 下一篇看什么

既然这一篇已经把：

- kernel mount
- VFS 对接
- page cache
- writeback
- cap 一致性边界

这条 Linux 内核 CephFS 客户端主线讲清楚了，下一步最自然的事情，就是转到更偏实践的一篇：

**既然客户端和缓存语义都讲清楚了，CephFS 到底该怎么测，元数据压测和数据压测分别看什么？**

所以下一篇建议接：

**《CephFS 性能测试实战：元数据压测、数据压测与关键指标解读》**
