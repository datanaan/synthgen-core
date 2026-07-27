# v1 Unit C Plan — Data Import

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-c-plan.md
> 编译日期：2026-05-14

## 摘要

Unit C 实现数据导入功能，估算 0.5 周，依赖 Unit A（Parser + Type System）和 Unit B（Storage Engine）。包含 5 个 Task、12 个步骤：Schema 兼容性校验器（Parquet Schema <-> SynthGen Schema 类型映射与兼容性检查）、值域范围校验（含严格/宽松模式、NOT NULL 检查）、DataImporter 框架 + LOAD DATA 执行器、脚手架集成、标准数据集验证。核心是读取 Parquet 文件并校验后写入基表层。

## 关键要点

- Schema 兼容性检查：列名映射（大小写敏感）、类型映射（Parquet 物理类型 -> SynthGen DataType）
- 两种值域校验模式：严格模式（越界即失败）、宽松模式（记录越界行并跳过）
- NOT NULL 校验：检查 NOT NULL 列是否含空值
- LoadDataExecutor 将 Parser 的 LoadDataStmt AST 转为 import() 调用
- 错误处理：最多记录 100 个错误

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `SchemaCompatibility` | `src/storage/schema_compatibility.h/.cpp` | Parquet Schema 与 SynthGen Schema 的兼容性检查 |
| `ValueRangeChecker` | `src/storage/value_range_checker.h/.cpp` | ArrowBatch 值域范围校验（严格/宽松模式） |
| `DataImporter` | `src/storage/data_importer.h/.cpp` | 完整导入流程编排 |
| `LoadDataExecutor` | `src/engine/load_data_executor.h/.cpp` | LOAD DATA AST -> import() 调用的桥接 |

### Schema 类型映射

- Parquet 物理类型 -> SynthGen DataType
- FLOAT -> DOUBLE 兼容，但 INT32 -> FLOAT 不兼容
- 列名大小写敏感
- 额外列（Parquet 有但 SynthGen 无）产生警告，不报错

### 测试策略

- Schema 兼容性测试 12+ 用例（错误测试 >= 30%）
- 值域校验测试 10+ 用例
- 导入集成测试 12+ 用例（错误测试 >= 30%）
- 边界测试：1 行导入、100000 行 batch 性能（< 1s）、1000 列 Schema

## 提取的实体

- [[storage-engine]] — 存储引擎（已存在）
- [[schema-registry]] — Schema 注册表（已创建）
- [[insert-only-base-table]] — 导入数据写入 INSERT ONLY 基表层（已创建）
- [[schema-compatibility-checker]] — Parquet Schema 与 SynthGen Schema 兼容性校验器（新实体）
