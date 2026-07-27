# SynthGen Core Round 5 测试报告：覆盖空白填补 + 并发安全修复

> 日期：2026-05-23
> 范围：测试覆盖空白填补 + 源码级并发安全修复
> 执行方式：4 个并行 Subagent（R5A–R5D）

---

## 1. 执行概要

| 指标 | 之前 (R4 结束) | 之后 (R5 结束) | 变化 |
|------|----------------|----------------|------|
| **测试总数** | 1170 | **1453** | +283 (+24%) |
| **通过率（并行）** | 100% | **100%** | 0 失败 |
| **测试文件数** | 61 | **66** | +5 |
| **源文件修改** | — | **3 个** | 并发安全修复 |
| **发现的 Bug** | — | **3** | 全部修复 |
| **并行执行时间** | 3.96s | **9.81s** | +5.85s（含性能测试） |

---

## 2. 新增测试文件 (5个)

### 单元测试 (3个文件, 206 个测试)

| 文件 | 测试数 | 类型 | 覆盖范围 |
|------|--------|------|----------|
| `tests/unit/common_test.cpp` | 55 | 单元测试 | Result\<T\> 全路径、Types 枚举、Hash 函数 |
| `tests/unit/physics_deep_test.cpp` | 78 | 单元测试 | Gaussian/Uniform/Random/RangeExtractor/RectangularSampler 深度边界 |
| `tests/unit/scaffold_deep_test.cpp` | 73 | 单元测试 | Explain/Trace/Metrics 并发、嵌套、极限值 |

### E2E 测试 (2个文件, 77 个测试)

| 文件 | 测试数 | 类型 | 覆盖范围 |
|------|--------|------|----------|
| `tests/e2e/performance_stress_test.cpp` | 27 | 性能/压力 | 百万行、200列宽表、500版本链、10K审计、100K聚合 |
| `tests/e2e/concurrency_fault_test.cpp` | 50 | 并发/故障 | 12并发 + 9故障注入 + 11错误传播 + 18恢复韧性 |

---

## 3. 发现并修复的 Bug (3个)

### 并发安全 Bug（严重）

| # | Bug | 文件 | 描述 |
|---|-----|------|------|
| 1 | AuditLog 并发写崩溃 | `audit_log.h/.cpp` | `std::deque<AuditRecord>` 无内部锁，多线程并发 `push_back()` 导致堆损坏 |
| 2 | ModelStorageLayer LRU 缓存并发崩溃 | `model_storage_layer.h/.cpp` | `std::list` + `std::unordered_map` 无同步，并发访问导致 segfault |
| 3 | MetadataManager 元数据竞争 | `metadata.h/.cpp` | `next_part_` 计数器和 `unordered_map` 无锁，并发追加导致分区号重复 |

### 修复方案

为三个类添加 `std::mutex` 成员，在所有公共方法入口加 `std::lock_guard`：

- **AuditLog**: `mutable std::mutex mutex_` — 保护 `records_`、`genesis_created_`、`next_id_`
- **ModelStorageLayer**: `mutable std::mutex cache_mutex_` — 保护 `lru_list_`、`lru_map_`（文件 I/O 不持锁）
- **MetadataManager**: `mutable std::mutex mutex_` — 保护 `tables_`、`next_part_`

---

## 4. 修改的源文件 (3个, 6个文件)

| 文件 | 修改内容 |
|------|----------|
| `src/storage/audit/audit_log.h` | 新增 `#include <mutex>` + `mutable std::mutex mutex_` 成员 |
| `src/storage/audit/audit_log.cpp` | 7 个公共方法入口加 `lock_guard`：`create_genesis`、`append`、`verify_chain`、`daily_verification`、`detect_forks`、`get_latest`、`scan` |
| `src/storage/model/model_storage_layer.h` | 新增 `#include <mutex>` + `mutable std::mutex cache_mutex_` 成员 |
| `src/storage/model/model_storage_layer.cpp` | `update_cache`、`load_model`（缓存检查段）加锁 |
| `src/storage/metadata.h` | 新增 `#include <mutex>` + `mutable std::mutex mutex_` 成员 |
| `src/storage/metadata.cpp` | 7 个方法加 `lock_guard`：`create_table`、`get_table`、`add_version`、`add_snapshot`、`next_part_number`、`flush`、`reload` |

---

## 5. 各 Agent 详细报告

### R5A: Common + Physics 模块 (133 个测试)

**结论：未发现 Bug。** 模块实现健壮。

- **common_test.cpp (55 个测试)**
  - ResultTest (36): ok/error 状态、值访问、移动/复制语义、所有 ErrorCode 枚举值、Result\<void\>
  - ResultVoidTest (3): 默认构造、错误构造
  - TypesTest (5): ColumnDef 默认值、DataType 枚举
  - HashTest (11): SHA-256 已知输入、确定性、抗碰撞、线程安全、文件 I/O

- **physics_deep_test.cpp (78 个测试)**
  - GaussianDeepTest (9): stddev=0、负范围、极小范围、统计均值验证
  - UniformDeepTest (15): 精确边界、min==max、INT64_MIN/MAX、统计均匀性、datetime 范围
  - RandomDeepTest (12): uniform_01 范围、高斯方差、负 stddev (UB)、跨线程确定性
  - SeedControllerDeepTest (6): 层间无冲突、1000 请求 ID 无冲突
  - RangeExtractorDeepTest (17): 重叠约束收紧、矛盾约束错误、仅 STRING 模式
  - RectangularDeepTest (18): 空模式、单列各类型、批量计数精确性

### R5B: Scaffold 模块 (73 个测试)

**结论：未发现 Bug。** 线程安全设计良好。

- **scaffold_deep_test.cpp (73 个测试)**
  - ExplainDeepTest (14): 所有 ExecutionMode、极长字符串、Unicode、边界值
  - TraceDeepTest (18): 计时验证、100 层嵌套、父子链、并发追踪（thread_local 隔离验证）
  - MetricsDeepTest (31): 负增量、原子 counter、histogram CAS loop、8 线程并发压力
  - ScaffoldCombinedTest (4): 跨模块交互
  - ScaffoldStressTest (4): 16 线程×100 span、10K 快速创建/销毁、16 线程×50K counter

**关键发现：** SpanGuard 使用 `thread_local` 存储（天然线程安全），MetricsRegistry 使用 `std::atomic` (fetch_add / compare_exchange_weak)，ExplainInfo 是纯 POD 结构体。

### R5C: 性能/压力测试 (27 个测试)

**结论：未发现 Bug。** 系统在大规模下表现稳定。

- **performance_stress_test.cpp (27 个测试)**
  - 大数据量 (4): 1M 行生成 (1s)、10M 单列 (9.8s)、100 批次×10K、500K+5 约束
  - 宽表 (2): 200 列混合类型 (900ms)、200 列+50 约束 (340ms)
  - 版本链压力 (3): 500 版本链 (35ms)、时间旅行 v250 (72ms)、GC 压缩 500 版本 (36ms)
  - 审计日志压力 (2): 10K 条目哈希链完整性 (210ms)、性能无退化验证
  - 约束压力 (3): 100 并发约束 (450ms)、50K 行 inter-row (7ms)、100K 行 AVG 精度 (1e-10)
  - 证据包压力 (2): 100K 行 JSON 往返、850K 行全字段填充
  - 其他规模 (7): 50K 点 KS 检验、50 次连续对齐、100 检查点、原子写入恢复、分类器 65 约束、路由 100 配置、完整管道 50K 行

### R5D: 并发 + 故障注入测试 (50 个测试)

**结论：发现 3 个并发安全 Bug。**

- **concurrency_fault_test.cpp (50 个测试)**
  - 并发测试 (12): MetricsRegistry 10 线程、AuditLog 5 线程并发写、ModelStorage 8 线程、SpanGuard 8 线程、SchemaRegistry 10 线程、SeedController 8 线程、ParquetIO 6 线程、ObjectStore 4 线程
  - 故障注入 (9): 截断 Parquet、文本文件替代、不存在的目录、只读目录、空文件、/dev/full、无效路径
  - 错误传播 (11): Result\<T\> 链、约束管道错误、大批量拒绝、不可变违规、极端范围 (-1e300)
  - 恢复韧性 (18): 部分 Parquet 恢复、原子写入中断清理、审计链篡改检测、LRU 缓存行为、并发版本链、混合值 histogram 并发、周期检测

---

## 6. 测试覆盖总结

| 模块 | R4 状态 | R5 状态 | 变化 |
|------|---------|---------|------|
| API | EXCELLENT | EXCELLENT | 无变化 |
| **Common** | **无测试** | **EXCELLENT** | +55 测试 |
| Parser | EXCELLENT | EXCELLENT | 无变化 |
| Schema | GOOD | GOOD | 无变化 |
| Engine - Constraint | EXCELLENT | EXCELLENT | 无变化 |
| Engine - Evidence | EXCELLENT | EXCELLENT | 无变化 |
| **Engine - Physics** | **GOOD** | **EXCELLENT** | +78 测试 |
| Engine - Router | EXCELLENT | EXCELLENT | 无变化 |
| **Scaffold** | **LIMITED** | **EXCELLENT** | +73 测试 |
| Storage - Audit | EXCELLENT | EXCELLENT | +并发测试 |
| Storage - Model | GOOD | GOOD | +并发修复 |
| Storage - 其他 | GOOD | GOOD | +并发修复 |
| SDK | 无测试 | 无测试 | — |

---

## 7. 累计状态

| 指标 | 值 |
|------|------|
| 总测试数 | **1453** |
| 通过率 | **100%** (1 跳过：权限测试) |
| 测试文件数 | **66** |
| 并行执行时间 | **9.81s** |
| 累计 Bug 修复 | **17** (R1-R4: 14, R5: 3) |
| 累计源文件修改 | **13** |
| 总提交数 | 待提交 |

---

## 8. 已知设计限制（非 Bug）

| 限制 | 说明 |
|------|------|
| InterRow 约束拒绝 DATETIME 列 | `validate_constraints()` 仅允许 kFloat/kInt |
| DeltaMax=0 被拒绝 | 验证要求 `delta_max > 0` |
| ValueRangeValidator 跳过 STRING/ENUM | 非数值列的范围约束被静默忽略 |
| ORDER 列不保证排序 | Physics 引擎生成随机时间戳 |
| MetadataManager flush() 持锁期间做文件 I/O | 简单实现，高并发下可能成为瓶颈 |
| SDK bindings 无测试 | pybind11 绑定未覆盖 |

---

## 9. 待提交文件

```
修改：
  src/storage/audit/audit_log.h
  src/storage/audit/audit_log.cpp
  src/storage/metadata.h
  src/storage/metadata.cpp
  src/storage/model/model_storage_layer.h
  src/storage/model/model_storage_layer.cpp
  tests/CMakeLists.txt

新增：
  tests/unit/common_test.cpp
  tests/unit/physics_deep_test.cpp
  tests/unit/scaffold_deep_test.cpp
  tests/e2e/performance_stress_test.cpp
  tests/e2e/concurrency_fault_test.cpp
```
