# SynthGen Core 测试增强与 Bug 修复报告

> 日期：2026-05-20  
> 范围：全系统测试增强 + Bug 修复  
> 执行方式：5 轮 Subagent 自动化测试（计划 + 4 轮混沌测试）

---

## 1. 执行概要

| 指标 | 之前 | 之后 | 变化 |
|------|------|------|------|
| **测试总数** | 772 | **1170** | +398 (+52%) |
| **通过率（并行）** | ~99% (11 flaky) | **100%** | 0 失败 |
| **测试文件数** | 42 | **61** | +19 |
| **测试代码行数** | ~12,400 | **~25,100** | +12,700 |
| **E2E 测试文件** | 0 | **17** | +17 |
| **发现并修复的 Bug** | — | **14** | 全部修复 |
| **并行执行时间** | 2.5s | 3.96s | +1.46s |
| **源文件修改** | — | **10 个** | — |

---

## 2. Bug 修复清单

### 严重 (Segfault/Crash)

| # | Bug | 文件 | 发现轮次 | 描述 |
|---|-----|------|----------|------|
| 1 | InterRowEngine TIMESTAMP 列崩溃 | `inter_row_engine.cpp` | R4 | 重建过滤批次时未处理 Arrow TIMESTAMP 类型，static_cast 到 StringArray 导致空指针 |
| 2 | ValueRangeValidator 空指针崩溃 | `value_range_validator.cpp` | R3 | 传入 nullptr 的 Arrow Table 时直接解引用，无空值检查 |

### 高 (数据正确性)

| # | Bug | 文件 | 发现轮次 | 描述 |
|---|-----|------|----------|------|
| 3 | 解析器静默忽略缺失语法元素 | `parser.cpp` (4处) | R1 | `expect()` 返回值被丢弃，缺失 `{`、`}`、`:` 时不报错 |
| 4 | RangeExtractor 同列多约束覆盖 | `range_extractor.cpp` | R1 | 两个 BETWEEN 约束在同一列时后者覆盖前者，而非取交集 |
| 5 | EvidenceV2 JSON 丢 6 个字段 | `evidence_package_v2_builder.cpp` | R1 | `to_json()` 序列化了但 `from_json()` 未反序列化 6 个关键字段 |
| 6 | Schema::validate 接受 NaN | `schema.cpp` | R2 | NaN 范围值通过验证，因为 NaN >= x 在 C++ 中为 false |
| 7 | ModelStorageLayer 缓存过期 | `model_storage_layer.cpp` | R2 | 文件删除后 LRU 缓存仍返回旧数据，影响 GC 压缩正确性 |
| 8 | V1/V2 schema hash 格式不一致 | `evidence_package_v2_builder.cpp` | R3 | V1 hash 包含列范围信息，V2 hash 不包含，破坏跨版本数据溯源 |
| 9 | ValueRangeValidator `>` 当 `>=` | `value_range_validator.h/.cpp` | R4 | kGreaterThan 和 kGreaterEqual 使用相同逻辑，边界值错误通过 |

### 中 (精度/语义)

| # | Bug | 文件 | 发现轮次 | 描述 |
|---|-----|------|----------|------|
| 10 | DriftDetector 相同分布 KS≠0 | `drift_detector.cpp` | R1 | 合并排序中等值只推进一个索引，导致 CDF 暂时不同步 |
| 11 | AggregateEngine 精度丢失 | `aggregate_engine.cpp` | R4 | 朴素累加求和导致浮点误差累积，1000 个相同值的 AVG 丢失精度 |

### 基础设施

| # | Bug | 文件 | 发现轮次 | 描述 |
|---|-----|------|----------|------|
| 12 | storage_integration_test 竞态 | `storage_integration_test.cpp` | 计划 | 硬编码临时目录路径，并行测试互相删除数据 |
| 13 | v3 单元测试竞态 | 5 个 v3 测试文件 | 计划 | `./test_v3_*` 相对路径，并行运行时冲突 |
| 14 | API 测试端口冲突 | `api_test.cpp` | 计划 | `rand() % 1000` 多进程产生相同端口 |

---

## 3. 新增测试文件 (19个)

### E2E 测试 (17个文件)

| 文件 | 测试数 | 类型 | 覆盖范围 |
|------|--------|------|----------|
| `e2e/smoke_test.cpp` | 5 | 冒烟测试 | Parser→Schema→Generate→Evidence→Storage→Audit 全链路 |
| `e2e/service_integration_test.cpp` | 18 | Service 集成 | SynthGenService 所有方法 + 错误路径 |
| `e2e/pipeline_stress_test.cpp` | 4 | 压力测试 | 50列宽表、20约束、100次追加、5000行状态序列 |
| `e2e/parser_fuzz_test.cpp` | 32 | Parser 模糊 | 边界输入、畸形语法、关键字冲突、特殊字符 |
| `e2e/data_quality_test.cpp` | 15 | 数据质量 | 分布统计、种子确定性、边界约束、多类型列 |
| `e2e/storage_chaos_test.cpp` | 15 | Storage 混沌 | 错误路径、零行/单行、宽表、审计日志规模 |
| `e2e/v2_v3_chaos_test.cpp` | 14 | v2/v3 管道 | 所有约束类型、所有聚合函数、跨版本存储 |
| `e2e/schema_evidence_chaos_test.cpp` | 24 | Schema+Evidence | NaN/空值/重复、JSON 往返、SchemaValidator |
| `e2e/constraint_edge_test.cpp` | 15 | 约束边界 | DeltaMin、Monotone、跨批次状态、精确边界 |
| `e2e/service_scaffold_test.cpp` | 15 | Scaffold | 嵌套 Span、Metrics 规模、1M 行生成、Explain |
| `e2e/v3_deep_test.cpp` | 23 | v3 深度 | 100版本链、全锚定压缩、交替漂移、10MB 写入 |
| `e2e/e2e_pipeline_chaos_test.cpp` | 29 | 全链路 E2E | 多组件交互、V1/V2 比较、并发 Metrics、审计篡改 |
| `e2e/negative_destructive_test.cpp` | 31 | 负面测试 | 空输入、null 指针、无效参数、畸形 JSON |
| `e2e/realworld_simulation_test.cpp` | 6 | 真实场景 | 气象站、电机监控、电网、多表工厂、质量审计 |
| `e2e/numeric_precision_test.cpp` | 20 | 数值精度 | 极小/极大范围、DBL_EPSILON、Kahan 求和验证 |
| `e2e/type_system_test.cpp` | 15 | 类型系统 | STRING/DATETIME/ENUM 生成、存储往返、跨类型 |
| `e2e/composition_matrix_test.cpp` | 105 | 组合矩阵 | Schema×约束×引擎×Evidence 参数化组合 |

### 集成测试 (2个文件)

| 文件 | 测试数 | 覆盖范围 |
|------|--------|----------|
| `integration/storage_engine_test.cpp` | 6 | 生成→存储→读回→过滤→投影→重启持久化 |
| `integration/constraint_pipeline_test.cpp` | 6 | Classify→Route→InterRow→Aggregate→PostFilter→EvidenceV2 |

---

## 4. 修改的源文件 (10个)

| 文件 | 修改内容 |
|------|----------|
| `src/parser/parser.cpp` | 4 处 `expect()` 返回值检查，缺失语法元素现在返回错误 |
| `src/engine/physics/range_extractor.cpp` | 同列多约束取交集（max of mins, min of maxs） |
| `src/engine/constraint/value_range_validator.cpp` | nullptr 空值检查 + 严格/非严格运算符区分 |
| `src/engine/constraint/value_range_validator.h` | 新增 `min_strict` / `max_strict` 字段 |
| `src/engine/constraint/inter_row_engine.cpp` | TIMESTAMP 类型处理器 + 批次重建修复 |
| `src/engine/constraint/aggregate_engine.cpp` | Kahan 补偿求和替代朴素累加 |
| `src/engine/evidence/evidence_package_v2_builder.cpp` | 补全 6 个字段序列化/反序列化 + hash 格式对齐 V1 |
| `src/engine/alignment/drift_detector.cpp` | 等值时双索引推进，修正 KS 统计量 |
| `src/schema/schema.cpp` | NaN 范围值检查 |
| `src/storage/model/model_storage_layer.cpp` | 缓存命中前验证文件存在性 |

---

## 5. 已知设计限制（非 Bug）

| 限制 | 说明 |
|------|------|
| InterRow 约束拒绝 DATETIME 列 | `validate_constraints()` 仅允许 kFloat/kInt，时间戳差值约束需扩展 |
| DeltaMax=0 被拒绝 | 验证要求 `delta_max > 0`，阻止了"相同连续值"约束 |
| ValueRangeValidator 跳过 STRING/ENUM | 非数值列的范围约束被静默忽略 |
| ORDER 列不保证排序 | Physics 引擎生成随机时间戳，ORDER 语义由执行层保证 |
| AggregateEngine TimestampArray 转型 | 使用 Int64Array 而非 TimestampArray，依赖继承关系 |

---

## 6. 提交记录

```
01ce564 fix: DriftDetector KS statistic for identical distributions (missed in round 1 commit)
3499a4f test: chaos/fuzz testing round 4 — 146 new tests, 3 bugs found and fixed
12ef425 test: chaos/fuzz testing round 3 — 60 new tests, 2 bugs found and fixed
446f34a test: chaos/fuzz testing round 2 — 77 new tests, 2 bugs found and fixed
4853f4f test: chaos/fuzz testing round 1 — 76 new tests, 7 bugs found and fixed
2be0b3f test: test hardening — add 39 E2E/integration/stress tests, fix parallel race conditions
```

---

## 7. 最终状态

```
1170 tests, 100% pass rate, 3.96s execution
61 test files, ~25,100 lines of test code
10 source files modified with bug fixes
6 commits, all on main branch
```
