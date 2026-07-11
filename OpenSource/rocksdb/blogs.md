下面是一套“由浅入深、结合源码”的 RocksDB 博客系列清单，可以写成 18 篇左右。主线建议按：使用入门 -> LSM 原理 -> 写流程 -> 读流程 -> 存储格式 -> 高级特性 -> 性能调优 -> 源码贡献。

**RocksDB 博客系列**
| 篇 | 标题 | 重点内容 | 代码入口 |
|---|---|---|---|
| 1 | RocksDB 是什么：从一个 KV 示例开始 | RocksDB 定位、适用场景、基本 Put/Get/Delete | [examples/simple_example.cc](E:/projects/rocksdb/examples/simple_example.cc) |
| 2 | 编译与运行 RocksDB 示例 | 构建静态库、运行 examples、理解项目结构 | [examples/README.md](E:/projects/rocksdb/examples/README.md) |
| 3 | RocksDB API 入门：DB、Options、Status | `DB::Open`、`Options`、`ReadOptions`、`WriteOptions` | [include/rocksdb/db.h](E:/projects/rocksdb/include/rocksdb/db.h) |
| 4 | Column Family：一个 DB 里的多张逻辑表 | CF 的使用场景、打开/关闭、句柄生命周期 | [examples/column_families_example.cc](E:/projects/rocksdb/examples/column_families_example.cc) |
| 5 | LSM Tree 入门：为什么 RocksDB 写入快 | MemTable、WAL、SST、Level、Compaction 的整体模型 | [docs/components/index.md](E:/projects/rocksdb/docs/components/index.md) |
| 6 | 写入路径一：Put/Delete 最终去了哪里 | `Put`、`Delete`、`WriteBatch`、写入入口 | [docs/components/write_flow/01_write_apis.md](E:/projects/rocksdb/docs/components/write_flow/01_write_apis.md) |
| 7 | 写入路径二：WriteBatch 与批量写 | 批量编码、原子性、批量提交 | [db/write_batch.cc](E:/projects/rocksdb/db/write_batch.cc) |
| 8 | 写入路径三：WAL 如何保证崩溃恢复 | WAL-before-memtable、日志格式、replay | [docs/components/write_flow/03_wal.md](E:/projects/rocksdb/docs/components/write_flow/03_wal.md) |
| 9 | 写入路径四：MemTable 与 SkipList | 内存写入、InternalKey、flush 触发 | [db/memtable.cc](E:/projects/rocksdb/db/memtable.cc) |
| 10 | Sequence Number：RocksDB 如何实现可见性 | 单调序列号、snapshot、并发读写可见性 | [docs/components/write_flow/05_sequence_numbers.md](E:/projects/rocksdb/docs/components/write_flow/05_sequence_numbers.md) |
| 11 | 读取路径一：Get 的完整源码流程 | SuperVersion、MemTable、SST 分层查找 | [docs/components/read_flow/01_point_lookup.md](E:/projects/rocksdb/docs/components/read_flow/01_point_lookup.md) |
| 12 | 读取路径二：Iterator 如何扫描数据 | DBIter、MergingIterator、正反向扫描 | [docs/components/read_flow/07_iterator_scan.md](E:/projects/rocksdb/docs/components/read_flow/07_iterator_scan.md) |
| 13 | SST 文件与 BlockBasedTable | Data block、Index block、Filter block、TableReader | [table/block_based/block_based_table_reader.cc](E:/projects/rocksdb/table/block_based/block_based_table_reader.cc) |
| 14 | Bloom Filter 与 Block Cache | 如何减少磁盘 I/O，cache key 与缓存层级 | [docs/components/read_flow/06_block_cache.md](E:/projects/rocksdb/docs/components/read_flow/06_block_cache.md) |
| 15 | Compaction：LSM 的核心成本 | Level 选择、文件合并、读写放大、空间放大 | [db/version_set.cc](E:/projects/rocksdb/db/version_set.cc) |
| 16 | 删除语义：Delete、SingleDelete、RangeDelete | tombstone 生命周期、读路径和 compaction 清理 | [docs/components/write_flow/08_tombstone_lifecycle.md](E:/projects/rocksdb/docs/components/write_flow/08_tombstone_lifecycle.md) |
| 17 | 事务 RocksDB：悲观与乐观事务 | TransactionDB、OptimisticTransactionDB、冲突检测 | [examples/transaction_example.cc](E:/projects/rocksdb/examples/transaction_example.cc) |
| 18 | 备份、恢复与生产运维 | BackupEngine、checkpoint、故障恢复 | [examples/rocksdb_backup_restore_example.cc](E:/projects/rocksdb/examples/rocksdb_backup_restore_example.cc) |
| 19 | 性能调优：从 Options 到 db_bench | block cache、write buffer、compaction、benchmark | [tools/db_bench_tool.cc](E:/projects/rocksdb/tools/db_bench_tool.cc) |
| 20 | 如何读 RocksDB 源码与贡献代码 | 目录结构、测试、benchmark、review 注意点 | [CLAUDE.md](E:/projects/rocksdb/CLAUDE.md) |

第一篇可以从这个最小代码切入：

```cpp
#include "rocksdb/db.h"

int main() {
  rocksdb::DB* db;
  rocksdb::Options options;
  options.create_if_missing = true;

  rocksdb::Status s = rocksdb::DB::Open(options, "/tmp/testdb", &db);
  assert(s.ok());

  s = db->Put(rocksdb::WriteOptions(), "hello", "rocksdb");
  assert(s.ok());

  std::string value;
  s = db->Get(rocksdb::ReadOptions(), "hello", &value);
  assert(s.ok());

  delete db;
}
```

建议每篇文章都固定用这个结构：先给一个可运行例子，再画出数据流，最后带读者跳到 2 到 4 个源码文件。这样不会一上来陷进源码细节，但读完一轮后能真正理解 RocksDB 的核心路径。