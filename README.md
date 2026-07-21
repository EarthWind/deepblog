# DeepBlog 技术学习笔记

这里记录个人在算法、编程语言、分布式系统和开源项目源码阅读过程中的学习笔记。内容以 Markdown 文档为主，部分主题附有论文、示意图和示例代码。

> 本仓库是持续整理中的个人知识库，文章内容会随着学习和实践不断补充、修订。

## 内容导航

| 主题 | 主要内容 | 阅读入口 |
| --- | --- | --- |
| 算法与数据结构 | 线性表、树、图、字符串、散列表，以及查找和排序算法 | [算法笔记](Algorithm/算法.md) |
| 分布式算法 | 分布式一致性、Paxos、Raft、Chubby 及相关论文资料 | [分布式一致性与共识算法](DistributedAlgorithm/分布式一致性和共识算法.md) |
| 编程语言 | C、C++、Dart、Rust 的系统学习路线，以及少量 Python 示例 | [编程语言目录](Planguage/) |
| 开源项目 | Flutter、Ceph、DAOS、RocksDB、SGLang、vLLM 的原理与源码分析 | [开源项目目录](OpenSource/) |

## 专题系列

### 算法与分布式系统

- [数据结构与算法](Algorithm/)：涵盖基础数据结构、常见查找算法和排序算法。
- [Paxos](DistributedAlgorithm/Paxos/)：包含 Multi-Paxos 笔记、经典论文和流程图。
- [Raft](DistributedAlgorithm/Raft/)：包含算法流程、论文译文及原始论文。
- [Chubby](DistributedAlgorithm/chubby/)：收录 Chubby 相关论文资料。

### 编程语言

- [C 语言](Planguage/C/0.目录.md)：从开发环境、语法和指针，到内存、编译链接、测试与工程实践。
- [C++](Planguage/C++/C++学习列表.md)：覆盖现代 C++ 基础、STL、模板、并发、协程及 C++20/23/26 特性。
- [Dart](<Planguage/Dart/1.1 简介.md>)：覆盖类型系统、模式匹配、面向对象、异步编程、包管理、空安全与 FFI。
- [Rust](Planguage/Rust/Rust由浅入深学习路线.md)：从所有权和类型系统逐步深入异步、宏、Unsafe、FFI 与性能优化。

### 开源项目与源码阅读

- [Flutter](<OpenSource/Flutter/1.0 Flutter概览.md>)：框架架构、Dart 入门、Widget、布局、状态管理、网络与本地数据。
- [Ceph](OpenSource/ceph/ceph-architecture-blog-series.md)：从整体架构深入 MON、MGR、OSD、BlueStore、RBD、CephFS 和 RGW。
- [DAOS](OpenSource/daos/README.md)：围绕控制面、数据面、核心服务和底层存储实现展开源码阅读。
- [RocksDB](OpenSource/rocksdb/blogs.md)：覆盖核心 API、LSM Tree、读写路径、WAL、SST、Compaction 和事务。
- [SGLang](OpenSource/sglang/)：梳理服务入口、运行时、调度、缓存、并行和高级推理能力。
- [vLLM](OpenSource/vllm/vllm_blog_series_roadmap.md)：从 V1 架构和请求链路深入调度、KV Cache、Worker 与扩展机制。

## 目录结构

```text
deepblog/
├── Algorithm/             # 算法与数据结构
├── DistributedAlgorithm/  # 分布式一致性与共识算法
├── OpenSource/            # 开源项目原理与源码分析
├── Planguage/             # 编程语言学习笔记
└── README.md
```

## 阅读方式

可以直接通过上面的导航在线阅读，也可以将仓库克隆到本地，使用任意支持 Markdown 的编辑器浏览。建议先从各专题的目录或学习路线开始，再按顺序阅读具体文章。

部分早期笔记可能对应特定的软件版本或学习阶段；用于实际项目之前，请结合当前官方文档和源码进行验证。

## 反馈与交流

如果发现内容错误、失效链接或表述不清，欢迎提交 Issue 或 Pull Request。也欢迎补充示例、参考资料和实践经验。
