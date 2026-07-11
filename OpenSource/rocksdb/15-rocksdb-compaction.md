---
title: "RocksDB Compaction：文件选择、版本回收与放大权衡"
date: 2026-07-11
series: "从零开始读懂 RocksDB"
order: 15
tags:
  - RocksDB
  - Compaction
  - Leveled Compaction
  - Subcompaction
  - Write Amplification
  - Tombstone
summary: "从 Compaction Score 和文件选择进入后台合并主路径，理解多路归并、Snapshot 安全回收、输出切分、Subcompaction，以及读写空间放大的权衡。"
cover: "./assets/15-rocksdb-compaction/leveled-compaction-flow.png"
---

# RocksDB Compaction：文件选择、版本回收与放大权衡

前十四篇已经建立完整链路：

~~~text
Write -> WAL -> MemTable -> Flush -> SST
Read  -> MemTable -> Filter -> Index -> Data Block -> Block Cache
~~~

但如果 RocksDB 只 Flush、不整理，L0 文件会不断增加，同一 Key 的旧版本和 Tombstone 永远占用空间，每次读取也要检查越来越多的 Sorted Run。

Compaction 是 LSM Tree 的后台维护引擎：选择一组重叠 SST，按 Internal Key 归并，在 Snapshot 与下层重叠约束允许时回收旧版本和删除标记，再生成新的有序文件。

![Leveled Compaction 的文件选择、多路归并与并行输出](./assets/15-rocksdb-compaction/leveled-compaction-flow.png)

> 图 1：Compaction Score 触发文件选择，Source Level 的 Key Range 扩展到 Output Level 的全部重叠文件；多路归并在 Snapshot 保护下丢弃安全的旧版本和 Tombstone，并按边界拆成多个 Subcompaction，最终输出互不重叠的新 SST。无重叠时可直接 Trivial Move。

## 1. Compaction 解决什么问题

它同时承担：

1. 限制每次读取要检查的 Sorted Run；
2. 把 L0 重叠文件整理到非重叠 Level；
3. 合并同一 User Key 的多个 Internal Version；
4. 回收过期 Value、Delete 与 Range Tombstone；
5. 解析或缩短 Merge Operand 链；
6. 重写压缩、Filter、Index 与 SST Format；
7. 控制每层容量与文件大小；
8. 根据温度或生命周期放置数据。

Compaction 不是简单“合并小文件”，它是读取性能、空间回收与后台写入成本的共同控制点。

## 2. 三种放大

### 2.1 读放大

一次逻辑读取需要访问多少 MemTable、SST、Filter 或 Block：

~~~text
Read Amplification ≈ inspected sorted runs / files / blocks
~~~

### 2.2 写放大

底层实际写入字节与用户写入字节之比：

~~~text
Write Amplification =
  storage bytes written / user bytes written
~~~

### 2.3 空间放大

物理空间与逻辑活数据之比：

~~~text
Space Amplification =
  physical live files / logical live data
~~~

更积极的 Compaction 往往降低读放大和空间放大，却增加写放大与后台 CPU。

~~~mermaid
flowchart TD
  R["Lower read amplification"] --- W["Higher compaction write cost"]
  W --- S["Lower space amplification"]
  S --- R
~~~

不存在让三者同时最优的万能配置。

## 3. Leveled Compaction 的结构

默认 Level Compaction 下：

~~~text
L0: 文件 Key Range 可重叠，按新旧顺序读取
L1+: 同一 Level 内文件 Key Range 通常互不重叠
~~~

典型容量：

~~~text
L1 target = max_bytes_for_level_base
L2 target = L1 target * multiplier
L3 target = L2 target * multiplier
...
~~~

当前默认值包括：

| 选项 | 默认值 |
| --- | ---: |
| level0_file_num_compaction_trigger | 4 |
| level0_slowdown_writes_trigger | 20 |
| level0_stop_writes_trigger | 36 |
| max_bytes_for_level_base | 256 MiB |
| target_file_size_base | 64 MiB |
| target_file_size_multiplier | 1 |
| max_subcompactions | 1 |

默认值会演进，部署版本应查看当前 options.h 与 advanced_options.h。

## 4. L0 为什么特殊

每次 Flush 产生一个新的 L0 SST。不同 MemTable 覆盖的 Key Range 可以重叠：

~~~text
L0 file A: [a, m]
L0 file B: [d, z]
L0 file C: [b, f]
~~~

查 Key e 时三者都可能包含候选版本，必须按新旧顺序检查。

因此 L0 文件数直接影响：

- Point Lookup 文件探测；
- Iterator Child 数与归并堆；
- Bloom/Index CPU；
- Cache 压力；
- 写入 Stall 风险。

## 5. Compaction Score

VersionStorageInfo::ComputeCompactionScore 为各 Level 计算压力。

L0 常近似：

~~~text
score =
  eligible L0 file count
  / level0_file_num_compaction_trigger
~~~

L1+ 常近似：

~~~text
score =
  level compensated bytes
  / target bytes for level
~~~

Score >= 1 表示该 Level 达到自动 Compaction 条件。Picker 通常优先处理高 Score，但还会考虑正在 Compaction 的文件、Compaction Priority、冲突和动态 Level 债务。

## 6. 从 Score 到文件选择

LeveledCompactionPicker 的主线：

~~~text
ComputeCompactionScore()
  -> choose start level
  -> choose candidate file(s)
  -> find overlapping files in output level
  -> expand source inputs if safe
  -> compute grandparents
  -> create Compaction plan
~~~

选中 Source 文件后，必须加入 Output Level 中与其 Key Range 重叠的所有文件，否则输出会与未参与文件发生重叠，破坏 Level 不变量。

~~~mermaid
flowchart LR
  S["Pick source file range"] --> O["Find all overlapping output files"]
  O --> E{"Can expand source without extra output overlap?"}
  E -- Yes --> X["Expand source inputs"]
  E -- No --> P["Keep current set"]
  X --> G["Record grandparent overlap"]
  P --> G
  G --> C["Compaction plan"]
~~~

## 7. SetupOtherInputs 的闭包

假设选择 L1 文件 [c, h]，L2 中有：

~~~text
[a, d] [e, g] [h, k]
~~~

边界比较按 Internal/User Comparator 语义进行。所有重叠文件都要加入，输入范围可能扩大；扩大后又可能覆盖 Source Level 更多文件。

Picker 需要找到一个稳定输入闭包，在：

- 减少未来重叠；
- 避免一次任务过大；
- 不与其他 Compaction 冲突；
- 满足 max_compaction_bytes；

之间折中。

## 8. Grandparent Overlap

从 L1 Compact 到 L2 时，L3 是 Grandparent Level。

即使输出只写 L2，若一个输出文件与太多 L3 数据重叠，未来 L2 -> L3 会产生巨大写放大。Compaction 会跟踪 Grandparent Overlap，达到阈值时提前切分输出文件。

这说明 Output File Cut 不只看 target_file_size_base，还看未来 Compaction 边界。

## 9. Trivial Move

若 Source SST 与 Output Level 没有 Key Range 重叠，并满足其他约束，可以不重写数据：

~~~text
Manifest edit:
  delete file from L
  add same file to L+1
~~~

文件内容不变，成本主要是元数据更新，因此称 Trivial Move。

优点：

- 几乎无写放大；
- 无需解压/压缩；
- 快速释放上层压力。

但它不会回收文件内部旧版本或 Tombstone，也不能在有重叠时直接使用。

## 10. CompactionJob 的执行结构

计划确定后：

~~~text
CompactionJob::Prepare()
  -> create input iterators
  -> derive subcompaction boundaries

CompactionJob::Run()
  -> ProcessKeyValueCompaction()
  -> CompactionIterator
  -> output TableBuilder(s)

CompactionJob::Install()
  -> write VersionEdit to MANIFEST
  -> install new Version
  -> obsolete inputs become deletable
~~~

输出文件写成功并不表示立即对读可见。必须通过 VersionEdit 原子安装新文件集合。

## 11. 多路归并

每个输入 SST 提供有序 Internal Iterator，MergingIterator 生成全局有序流：

~~~text
Input L0-A ----+Input L0-B -----+-> MergingIterator -> CompactionIterator -> TableBuilder
Input L1-X -----+
Input L1-Y ----/
~~~

MergingIterator 负责排序；CompactionIterator 负责决定 Entry 是输出、变换还是丢弃。

## 12. CompactionIterator 的版本规则

同一 User Key：

~~~text
key@120 Value new
key@110 Value middle
key@90  Value old
~~~

如果不存在需要 110/90 的 Snapshot，较旧版本可以丢弃。

核心判断依赖：

- earliest_snapshot；
- Snapshot Stripe；
- ValueType；
- 是否存在下层重叠；
- CompactionFilter；
- MergeOperator；
- User-defined Timestamp；
- Preserve/History 策略。

不能只按“留下最新一条”实现，否则会破坏 Snapshot。

## 13. Snapshot 为什么增加空间

若活跃 Snapshot Sequence 为 100：

~~~text
key@120 Value new
key@90  Value old
~~~

当前读需要 new，但 Snapshot 100 需要 old。Compaction 必须保留两者。

长期 Snapshot 会：

- 阻止旧版本回收；
- 增加输出 SST；
- 增加空间放大；
- 让 Iterator 跳过更多版本；
- 延长 Tombstone 生命周期。

Snapshot 是逻辑一致性资源，也是一项 Compaction 债务。

## 14. Point Tombstone 何时可丢

~~~text
key@120 Deletion
key@80  Value
~~~

删除标记不能仅因“很旧”就丢弃。如果更深 Level 仍可能含 key 的旧 Value，删除 Tombstone 后旧值会复活。

通常要同时满足：

1. Tombstone 对所有活 Snapshot 都足够旧；
2. 当前 Compaction 已覆盖可能含旧版本的范围，或确认更深层无相关 Key；
3. 没有保留历史的其他约束。

这就是 Base Level 检查的重要性。

## 15. Range Tombstone 回收

Range Tombstone 覆盖一段 Key Range，处理更复杂：

- 与输出文件边界相交时需要 Fragment/截断；
- 不能越过仍含被覆盖 Point Key 的层级提前消失；
- Snapshot 仍可能需要旧数据；
- 多个 Range Tombstone 可合并或覆盖；
- Bottommost Compaction 才有更强回收机会。

Compaction 输出的 Range Deletion Block 只保存仍需传播的片段。

## 16. Merge Operand

~~~text
counter@120 Merge +1
counter@110 Merge +2
counter@100 Value 10
~~~

Compaction 可以调用 MergeOperator：

- PartialMerge 合并部分 Operand；
- FullMerge 遇到 Base Value 后产生最终 Value；
- 无法安全合并时继续保留 Operand。

Merge 减少前台写放大，却把部分 CPU 和读复杂度转移到读取/Compaction。

## 17. CompactionFilter

应用可以通过 CompactionFilter 删除或变换 Value，例如 TTL。

它运行在后台重写路径，不应：

- 依赖不稳定外部状态而无法重放；
- 阻塞太久；
- 违反 Snapshot/事务语义假设；
- 随意改变 Key 排序；
- 把失败静默当作成功。

CompactionFilter 的删除也可能需要 Tombstone 语义，具体行为应按 API 契约测试。

## 18. 输出文件切分

TableBuilder 持续写排序结果，遇到条件时关闭当前 SST：

- 接近目标文件大小；
- Grandparent Overlap 过大；
- SstPartitioner 请求边界；
- Compaction 输出范围边界；
- Blob/温度/特殊策略；
- target_file_size_is_upper_bound 相关估计。

新文件的 Key Range 在 Output Level 内保持有序且不重叠。

## 19. Subcompaction

一个大 Compaction 可按 Key Range 划成多个独立区间：

~~~text
[a, f)  -> worker 0
[f, p)  -> worker 1
[p, z]  -> worker 2
~~~

每个 Subcompaction 有自己的 Iterator 与 Output Builder，最终一起安装。

~~~mermaid
flowchart LR
  I["Selected input range"] --> B1["Boundary A"]
  B1 --> S1["Subcompaction 1"]
  B1 --> S2["Subcompaction 2"]
  B1 --> S3["Subcompaction 3"]
  S1 --> O["Ordered non-overlapping outputs"]
  S2 --> O
  S3 --> O
~~~

max_subcompactions 默认 1。提高它可以利用 CPU/I/O 并行，但也增加内存、读带宽与前台流量竞争。

## 20. Background Jobs 与 Subcompaction 不同

~~~text
max_background_jobs
  控制并行后台 Flush/Compaction Job 数

max_subcompactions
  控制一个 Compaction Job 内部的并行范围数
~~~

两者相乘可能制造很高并发。不要只调一个参数而忽略总设备 Queue Depth、CPU 与 Rate Limiter。

## 21. Level Dynamic Bytes

~~~cpp
options.level_compaction_dynamic_level_bytes = true;
~~~

RocksDB 根据实际数据规模选择 Base Level，让 L0 可以直接 Compact 到较深 Level，空的小数据库不必机械填满 L1、L2。

目标是：

- 保持 Level 大小倍率；
- 限制最坏空间放大；
- 避免无意义的中间层重写；
- 在数据增长时动态上移 Base Level。

生产 Level Compaction 常值得评估此选项。

## 22. Universal Compaction

Universal 把多个 Sorted Run 按大小与年龄组合：

~~~text
Run 0 newest
Run 1
Run 2
...
Run N oldest
~~~

特点：

- 写放大通常低于 Leveled；
- Sorted Run 多时读放大更高；
- Full Compaction 可能需要较大临时空间；
- 适合写入吞吐优先、空间余量足够场景。

触发依据包括 Sorted Run 数、Size Ratio 与 Size Amplification。

## 23. FIFO Compaction

FIFO 主要按文件年龄/总容量淘汰旧 SST：

~~~text
total size > max_table_files_size
  -> delete oldest eligible files
~~~

适合：

- 时间窗口数据；
- TTL/日志；
- 旧数据可整文件删除；
- 不要求像 Leveled 一样最小读放大。

若一个文件混合新旧数据，FIFO 不能细粒度删除其中一部分，因此文件边界和写入时间相关性很重要。

## 24. 手动 CompactRange

~~~cpp
rocksdb::CompactRangeOptions cro;
cro.change_level = true;
cro.target_level = 2;
cro.bottommost_level_compaction =
    rocksdb::BottommostLevelCompaction::kForce;

rocksdb::Status s =
    db->CompactRange(cro, nullptr, nullptr);
~~~

手动 Compaction 可能：

- 产生大量读写 I/O；
- 与自动 Compaction 竞争；
- 增加临时空间；
- 引发 Write Stall；
- 让 Cache 受到扫描污染。

它不是日常“数据库优化按钮”，应有明确范围、目标与监控。

## 25. 一个可运行实验

下面关闭自动 Compaction，Flush 多个 L0 文件，再手动 CompactRange，并输出前后 Level 文件分布。

~~~cpp
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/options.h"

void Check(const rocksdb::Status& s) {
  if (!s.ok()) {
    std::cerr << s.ToString() << "\n";
    std::abort();
  }
}

std::string Key(int n) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "key:%08d", n);
  return buf;
}

void PrintFiles(rocksdb::DB* db, const char* phase) {
  std::vector<rocksdb::LiveFileMetaData> files;
  db->GetLiveFilesMetaData(&files);
  std::cout << "-- " << phase << " --\n";
  for (const auto& f : files) {
    std::cout << "L" << f.level << " " << f.name
              << " bytes=" << f.size << "\n";
  }
}

int main() {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string path =
      "/tmp/rocksdb-compaction-" + std::to_string(suffix);

  rocksdb::Options options;
  options.create_if_missing = true;
  options.disable_auto_compactions = true;
  options.write_buffer_size = 64 * 1024;
  options.target_file_size_base = 64 * 1024;
  options.compression = rocksdb::kNoCompression;

  std::unique_ptr<rocksdb::DB> db;
  Check(rocksdb::DB::Open(options, path, &db));

  for (int round = 0; round < 6; ++round) {
    for (int i = 0; i < 1000; ++i) {
      Check(db->Put(rocksdb::WriteOptions(), Key(i),
                    "round-" + std::to_string(round) +
                        std::string(80, 'x')));
    }
    rocksdb::FlushOptions fo;
    fo.wait = true;
    Check(db->Flush(fo));
  }

  PrintFiles(db.get(), "before compaction");

  rocksdb::CompactRangeOptions cro;
  cro.change_level = true;
  cro.target_level = 1;
  Check(db->CompactRange(cro, nullptr, nullptr));

  PrintFiles(db.get(), "after compaction");

  db.reset();
  Check(rocksdb::DestroyDB(path, options));
}
~~~

实验中六轮覆盖相同 Key Range，制造多个重叠 L0 文件和同 Key 多版本。手动 Compaction 后通常会看到文件进入更深 Level，旧版本数量显著下降。

## 26. 编译与运行

~~~bash
g++ -std=c++17 -O2 compaction_demo.cc \
  -I./include -L. -lrocksdb \
  -lpthread -ldl -lz -lbz2 -llz4 -lzstd -lsnappy \
  -o compaction_demo

./compaction_demo
~~~

链接依赖按本地构建调整。不同版本和输入大小可能产生不同数量的输出 SST，这是正常的。

## 27. db_bench 观察 Compaction

~~~bash
./db_bench \
  --benchmarks=fillrandom,overwrite,stats \
  --num=1000000 \
  --value_size=256 \
  --write_buffer_size=67108864 \
  --level0_file_num_compaction_trigger=4 \
  --statistics
~~~

重点观察：

~~~text
Compaction Stats
Level files and size
Read(GB) / Write(GB)
Write Amplification
Stall micros
Pending Compaction Bytes
Compaction CPU
~~~

一次短基准可能还没进入稳定状态，应让数据量覆盖多个 Level。

## 28. 核心指标

| 指标/属性 | 说明 |
| --- | --- |
| rocksdb.estimate-pending-compaction-bytes | 待处理 Compaction 债务 |
| rocksdb.num-files-at-levelN | 每层文件数 |
| rocksdb.compaction-pending | 是否等待 Compaction |
| COMPACT_READ_BYTES | Compaction 读取字节 |
| COMPACT_WRITE_BYTES | Compaction 写入字节 |
| COMPACTION_CPU_TOTAL_TIME | Compaction CPU |
| STALL_MICROS | 写 Stall 时间 |
| WRITE_STALL | 写入遇到 Stall 的统计事件 |
| CompactionJobStats | 输入输出记录、字节、文件与丢弃统计 |

Pending Compaction Bytes 持续增长通常表示后台吞吐低于写入产生债务的速度。

## 29. 写 Stall 是保护机制

当 L0 文件数达到：

~~~text
level0_slowdown_writes_trigger
~~~

RocksDB 开始减速；达到：

~~~text
level0_stop_writes_trigger
~~~

可能停止写入等待后台追上。

Stall 不是孤立故障，而是防止 L0 无限制增长、读放大失控和磁盘空间耗尽。解决方法是定位债务来源，不只是调高阈值。

## 30. 常见 Compaction 瓶颈

- 存储写带宽不足；
- 读写共用设备互相争抢；
- 压缩 CPU 饱和；
- 单 Job 未启用合适 Subcompaction；
- Background Job 数不足或过高；
- L0 重叠范围过大；
- 大量 Delete/Overwrite 制造版本债务；
- Snapshot 长期持有；
- Rate Limiter 过严；
- 输出文件或 Level 目标不适合工作负载；
- Compaction Filter/Listener 过慢。

应先分辨 CPU-bound、read-bound、write-bound 还是调度/锁等待。

## 31. 调优顺序

1. 确认 Compaction Style 与业务匹配；
2. 量化写入速率和 Pending Bytes；
3. 检查 L0 文件数、Stall 与 Level Size；
4. 查看 Compaction Read/Write 和 CPU；
5. 调整 max_background_jobs 与 max_subcompactions；
6. 再考虑 Level Size、File Size、Compression；
7. 用稳定态基准验证读、写、空间三种放大。

不要一次改十个参数，否则很难知道改善来自哪里。

## 32. 常见误区

### 误区一：Compaction 只是合并小文件

错误。它还执行 MVCC 版本回收、删除传播、Merge 与格式重写。

### 误区二：旧 Tombstone 可以立即删除

错误。更深层旧 Value 可能复活。

### 误区三：L1+ 永远只参与两个文件

错误。Source Range 必须扩展到 Output Level 全部重叠文件。

### 误区四：提高后台线程总会更快

错误。可能争抢存储、CPU、Cache 和前台延迟。

### 误区五：Trivial Move 会清理旧版本

错误。它只改变文件 Level，不重写内容。

### 误区六：手动 CompactRange 没有副作用

错误。它可能产生巨大 I/O、空间峰值和 Stall。

### 误区七：长期 Snapshot 只影响读取

错误。它阻止 Compaction 回收旧版本。

### 误区八：只看写吞吐即可调优

错误。还必须看读放大、空间放大、Stall 和尾延迟。

## 33. 源码阅读顺序

~~~text
db/version_set.cc
  -> db/compaction/compaction_picker.cc
  -> db/compaction/compaction_picker_level.cc
  -> db/compaction/compaction.cc
  -> db/compaction/compaction_job.cc
  -> table/merging_iterator.cc
  -> db/compaction/compaction_iterator.cc
  -> table/block_based/block_based_table_builder.cc
  -> db/version_edit.cc
~~~

重点入口：

- [version_set.cc](../db/version_set.cc)：Level Size 与 Compaction Score；
- [compaction_picker.cc](../db/compaction/compaction_picker.cc)：通用输入选择；
- [compaction_picker_level.cc](../db/compaction/compaction_picker_level.cc)：Leveled Picker；
- [compaction_picker_universal.cc](../db/compaction/compaction_picker_universal.cc)：Universal Picker；
- [compaction_picker_fifo.cc](../db/compaction/compaction_picker_fifo.cc)：FIFO Picker；
- [compaction.cc](../db/compaction/compaction.cc)：Compaction Plan；
- [compaction_job.cc](../db/compaction/compaction_job.cc)：Prepare、Run、Install 与 Subcompaction；
- [compaction_iterator.cc](../db/compaction/compaction_iterator.cc)：版本、Delete、Merge 回收；
- [advanced_options.h](../include/rocksdb/advanced_options.h)：Level/File 参数；
- [options.h](../include/rocksdb/options.h)：Style、后台 Job 与手动 Compaction。

## 34. 本篇小结

~~~text
触发：Compaction Score 衡量 L0 文件数或 Level Size 压力
选择：Source Inputs + Output Level 全部重叠文件
扩展：在大小和冲突约束内形成稳定输入闭包
归并：MergingIterator 输出全局 Internal Key 顺序
回收：CompactionIterator 按 Snapshot/下层重叠决定保留
删除：Tombstone 只能在不会导致旧值复活时丢弃
输出：按大小、Grandparent Overlap、Partitioner 切 SST
并行：Subcompaction 按不重叠 Key Range 分工
安装：VersionEdit 原子替换文件集合
捷径：无重叠时 Trivial Move 避免重写
风格：Leveled、Universal、FIFO 面向不同放大取舍
保护：Slowdown/Stop Write 防止 Compaction 债务失控
~~~

Compaction 是 RocksDB 把写入速度“延期付款”的地方。MemTable 和 L0 让前台写入快速完成，后台则用 CPU、I/O 与临时空间把重叠数据整理成适合读取的形状。调优的目标不是让 Compaction 消失，而是让它以可预测的吞吐及时偿还债务，并在读、写、空间三种放大之间选择适合业务的平衡。

下一篇将深入删除语义：比较 Delete、SingleDelete 与 DeleteRange，追踪 Tombstone 从 WriteBatch、MemTable、SST、读取路径到 Compaction 回收的完整生命周期。

## 参考入口

- [Compaction Iterator](../db/compaction/compaction_iterator.cc)；
- [Compaction Job](../db/compaction/compaction_job.cc)；
- [Level Picker](../db/compaction/compaction_picker_level.cc)；
- [Universal Picker](../db/compaction/compaction_picker_universal.cc)；
- [FIFO Picker](../db/compaction/compaction_picker_fifo.cc)；
- [Compaction Options](../include/rocksdb/options.h)；
- [Advanced Options](../include/rocksdb/advanced_options.h)；
- [Tombstone Lifecycle](../docs/components/write_flow/08_tombstone_lifecycle.md)；
- [SuperVersion and Snapshots](../docs/components/read_flow/03_superversion_and_snapshots.md)。
