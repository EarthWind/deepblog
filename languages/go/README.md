# Go 源码实现详解

这是一套按章节组织、结合 golang/go 仓库源码的 Go 实现解析系列。主线按"源码树与构建 → 编译器 → 链接器 → 运行时核心（调度、栈、内存、GC）→ 语言特性的运行时实现（channel、同步、系统调用、接口、map、slice/defer/panic）→ cgo 与可观测性"展开，共 17 篇。

所有文章基于同一份源码快照撰写，文中路径均相对 Go 仓库根目录（例如 `src/runtime/proc.go`）：

- 仓库：https://github.com/golang/go.git
- 分支 / 提交：master / fdcd66b（2026-09-11，`src/internal/goversion` 中 `Version = 28`，即 Go 1.28 开发版）
- 资料核对日期：2026-09-12

> Go 运行时演进很快（Swiss Table map、Green Tea GC、defer/panic 重构、定时器通道语义等都是近几个版本的改动）。阅读时请以文中标注的提交为准，并对照你实际使用的 Go 版本核实差异。

## 章节目录

| 篇 | 标题 | 重点内容 | 主要源码入口 |
| --- | --- | --- | --- |
| 1 | [源码目录结构与构建引导](01-go-source-tree-and-bootstrap.md) | GOROOT 布局、cmd/dist 三阶段 bootstrap、cmd/go 构建流程、阅读源码的方法 | `src/cmd/dist/build.go`、`src/cmd/go/internal/work` |
| 2 | [程序启动与初始化](02-go-program-startup.md) | rt0_go、schedinit、runtime.main、包初始化顺序、退出路径 | `src/runtime/asm_amd64.s`、`src/runtime/proc.go` |
| 3 | [编译器前端：从源码到 Unified IR](03-compiler-frontend.md) | syntax 词法/语法分析、types2 类型检查与泛型推断、noder/Unified IR、ir 节点 | `src/cmd/compile/internal/{syntax,types2,noder,ir}` |
| 4 | [编译器中端：逃逸分析、内联与 walk](04-compiler-middle-end.md) | escape 位置图、inline 预算与启发式、devirtualize/PGO、rangefunc/loopvar、walk 降级为 runtime 调用 | `src/cmd/compile/internal/{escape,inline,devirtualize,walk}` |
| 5 | [编译器后端：SSA、优化与代码生成](05-compiler-backend-ssa.md) | SSA 构建、pass 流水线、规则重写、prove、regalloc、genssa 与寄存器 ABI | `src/cmd/compile/internal/{ssagen,ssa}`、`src/cmd/internal/obj` |
| 6 | [目标文件与链接器](06-linker-and-object-files.md) | goobj 格式、loader、死代码消除、重定位、pclntab、内部/外部链接 | `src/cmd/internal/goobj`、`src/cmd/link/internal/ld` |
| 7 | [GMP 调度器](07-scheduler-gmp.md) | g/m/p/schedt、schedule 与 findRunnable、工作窃取、自旋 M、sysmon、协作式与异步抢占 | `src/runtime/proc.go`、`src/runtime/preempt.go` |
| 8 | [goroutine 生命周期与栈管理](08-goroutine-and-stack.md) | newproc、gogo/mcall/systemstack、goexit、栈分配/扩容/复制/收缩、traceback | `src/runtime/proc.go`、`src/runtime/stack.go`、`src/runtime/asm_amd64.s` |
| 9 | [内存分配器](09-memory-allocator.md) | mallocgc 路径、mcache/mcentral/mheap、size class、堆位图、页分配器、scavenger | `src/runtime/malloc.go`、`src/runtime/mheap.go`、`src/runtime/mpagealloc.go` |
| 10 | [垃圾回收器](10-garbage-collector.md) | GC 阶段与触发、并发标记、混合写屏障、Pacer 与 GOMEMLIMIT、清扫、Green Tea GC | `src/runtime/mgc.go`、`src/runtime/mgcmark.go`、`src/runtime/mgcpacer.go` |
| 11 | [channel 与 select](11-channel-and-select.md) | hchan/sudog、chansend/chanrecv/closechan、selectgo 三段式、定时器通道 | `src/runtime/chan.go`、`src/runtime/select.go` |
| 12 | [同步原语与信号量](12-sync-primitives.md) | runtime 锁与 sema、Mutex 饥饿模式、RWMutex、WaitGroup、Once、Pool、sync.Map、atomic | `src/runtime/sema.go`、`src/sync`、`src/internal/sync` |
| 13 | [系统调用、netpoll 与定时器](13-syscall-netpoll-timers.md) | entersyscall/exitsyscall、pollDesc 与 epoll、internal/poll、每 P 定时器堆、信号处理 | `src/runtime/netpoll.go`、`src/runtime/time.go`、`src/internal/poll` |
| 14 | [接口、类型元数据与反射](14-interface-type-metadata-reflect.md) | abi.Type/ITab、iface/eface、getitab、类型断言缓存、reflectcall、alg 哈希与比较 | `src/internal/abi/type.go`、`src/runtime/iface.go`、`src/reflect` |
| 15 | [map 与 Swiss Table](15-map-swiss-table.md) | 控制字节与 H1/H2、SWAR 匹配、可扩展哈希目录、增量扩容、迭代语义 | `src/internal/runtime/maps`、`src/runtime/map.go` |
| 16 | [slice、string、defer 与 panic](16-slice-string-defer-panic.md) | growslice 容量策略、字符串转换与拼接、三种 defer、_panic 遍历、recover | `src/runtime/slice.go`、`src/runtime/string.go`、`src/runtime/panic.go` |
| 17 | [cgo 与运行时可观测性](17-cgo-and-runtime-tooling.md) | cmd/cgo 代码生成、cgocall/cgocallback、pprof 采样、trace 分代设计、race 插桩、GODEBUG | `src/cmd/cgo`、`src/runtime/cgocall.go`、`src/runtime/mprof.go`、`src/runtime/trace.go` |

## 建议阅读顺序

- **只关心运行时**：7 → 8 → 9 → 10 → 11 → 12 → 13，再回看 2。
- **只关心编译器**：1 → 3 → 4 → 5 → 6，配合 `GOSSAFUNC` 与 `-gcflags=-S` 实验。
- **排查性能问题**：9、10、12、17，重点看 Pacer、Mutex 饥饿模式和 pprof/trace 的采样原理。
- **完整通读**：按编号顺序，每篇末尾的「延伸阅读」给出了对应源码文件，建议边读边打开源码。

## 实验环境准备

```bash
git clone --depth 1 https://github.com/golang/go.git
cd go && git log -1 --format='%h %cd'      # 对照文中标注的提交

# 常用观察手段
go build -gcflags=-m=2 ./...                # 逃逸分析与内联决策
GOSSAFUNC=main go build .                   # 生成 ssa.html
go build -gcflags=-S . 2>&1 | less          # 汇编输出
GODEBUG=gctrace=1,schedtrace=1000 ./app     # GC 与调度器跟踪
go tool objdump -s main.main ./app          # 反汇编
```
