SynthGen Core v1 Unit C 实施计划：Data Import
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit C 设计规范 v1.0
估算：0.5 周
依赖：Unit A (Parser + Type System) + Unit B (Storage Engine)

---

## 概述

Unit C 实现数据导入功能。读取 Parquet 文件，校验 Schema 兼容性，写入基表层。

---

## Task 1：Schema 兼容性校验器

**目标**：实现 Parquet Schema 与 SynthGen Schema 的兼容性检查

### Step 1.1：Parquet Schema 读取

**做什么**：使用 ParquetReader 读取 Parquet 文件的 Schema

**产出**：`src/storage/schema_compatibility.h`, `src/storage/schema_compatibility.cpp`

**关键逻辑**：
- ParquetReader::read_schema() → Arrow Schema → SynthGen Schema 转换
- 列名映射：Parquet 列名 → SynthGen 列名（大小写敏感）
- 类型映射：Parquet 物理类型 → SynthGen DataType

**验收**：
- [ ] 能读取标准 Parquet 文件的 Schema
- [ ] 列名正确映射
- [ ] 类型正确映射

### Step 1.2：兼容性检查实现

**做什么**：实现 Schema 兼容性检查逻辑

**关键逻辑**：
- 检查 SynthGen Schema 的所有列在 Parquet 中是否存在
- 检查类型兼容性
- 生成 CompatibilityReport

**验收**：
- [ ] 列名缺失正确检测
- [ ] 类型不匹配正确检测
- [ ] 额外列正确识别（警告）
- [ ] 完全兼容返回 compatible=true

### Step 1.3：兼容性测试

**做什么**：编写兼容性检查单元测试

**产出**：`tests/unit/schema_compatibility_test.cpp`

**测试用例**（至少 12 个）：
- 完全兼容的 Schema
- 列名缺失
- 类型不匹配（FLOAT → STRING）
- 类型兼容（FLOAT → DOUBLE）
- 额外列（警告）
- 空 Parquet Schema
- 空 SynthGen Schema
- **错误测试**：大小写敏感的列名不匹配
- **错误测试**：Parquet INT32 → SynthGen FLOAT（不兼容）
- **边界测试**：1 列 Schema
- **边界测试**：1000 列 Schema
- **边界测试**：列名最大长度（256字符）

**验收**：12+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 2：值域范围校验

**目标**：对数值列进行值域范围校验

### Step 2.1：值域校验实现

**做什么**：实现 ArrowBatch 的值域范围检查

**产出**：`src/storage/value_range_checker.h`, `src/storage/value_range_checker.cpp`

**关键逻辑**：
- 遍历 batch 中所有行
- 对 FLOAT/INT 列检查值是否在 [range_min, range_max] 内
- 严格模式：任何越界 → 失败
- 宽松模式：记录越界行，跳过

**验收**：
- [ ] 值在范围内 → 通过
- [ ] 值超出范围 → 检测
- [ ] 边界值（min, max）→ 通过
- [ ] 无值域声明的列 → 跳过检查

### Step 2.2：NOT NULL 校验

**做什么**：检查 NOT NULL 列是否含空值

**验收**：
- [ ] 非空列含空值 → 检测
- [ ] 非空列不含空值 → 通过
- [ ] 可空列含空值 → 允许

### Step 2.3：值域校验测试

**做什么**：编写值域校验单元测试

**产出**：`tests/unit/value_range_checker_test.cpp`

**测试用例**（至少 10 个）：
- 所有值在范围内
- 值超出上限
- 值超出下限
- 边界值（刚好等于 min 和 max）
- 边界值（刚好 min-ε 和 max+ε）
- 无值域声明的列
- NOT NULL 列含空值
- NOT NULL 列不含空值
- **错误测试**：空 batch
- **边界测试**：100000 行 batch 校验性能（<1s）

**验收**：10+ 测试用例全通过

---

## Task 3：DataImporter 实现

**目标**：实现完整的数据导入流程

### Step 3.1：DataImporter 框架

**做什么**：实现 DataImporter 类框架

**产出**：`src/storage/data_importer.h`, `src/storage/data_importer.cpp`

**关键逻辑**：
- import()：完整导入流程
- check_compatibility()：预检查
- 错误处理：最多记录 100 个错误

**验收**：
- [ ] 框架可编译
- [ ] 接口符合设计规范

### Step 3.2：LOAD DATA 执行器

**做什么**：将 Parser 产生的 LoadDataStmt AST 转为 import() 调用

**产出**：`src/engine/load_data_executor.h`, `src/engine/load_data_executor.cpp`

```cpp
class LoadDataExecutor {
public:
    Result<ImportResult> execute(const ast::LoadDataStmt& stmt,
                                  SchemaRegistry& registry,
                                  StorageBackend& storage);
};
```

**验收**：
- [ ] 能执行 LOAD DATA 语句
- [ ] 返回 ImportResult

### Step 3.3：导入流程集成测试

**做什么**：编写端到端导入测试

**产出**：`tests/integration/data_import_test.cpp`

**测试用例**（至少 12 个）：
- 完整流程：注册 Schema → LOAD DATA → scan 验证
- 严格模式：不匹配 → 失败
- 宽松模式：不匹配 → 跳过
- 空文件导入
- 大数据量导入（100000 行）
- **错误测试**：未注册的 type
- **错误测试**：不存在的文件
- **错误测试**：损坏的 Parquet 文件
- **错误测试**：列名缺失
- **错误测试**：类型不匹配
- **错误测试**：值域超出
- **边界测试**：1 行导入

**验收**：12+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 4：脚手架集成

**目标**：为导入功能添加 Trace/Metrics

### Step 4.1：Trace span

**做什么**：为 import 操作添加 span

- import → span(component="import", operation="import", attributes={table_id, rows_imported, rows_skipped})
- 失败时 span.status = "error"

**验收**：每次 import 产生 span，失败时标记 error

### Step 4.2：Metrics 注册

**做什么**：注册导入相关 metrics

```
import_total        — 导入调用次数
import_errors       — 导入错误次数
import_duration_ms  — 导入耗时
import_rows_total   — 导入总行数
import_skipped_total — 跳过行数
```

**验收**：metrics 端点暴露上述指标

---

## Task 5：标准数据集验证

**目标**：用 Unit B 创建的标准数据集验证导入功能

### Step 5.1：sensor_1000.parquet 导入验证

**做什么**：使用 Unit B 的 sensor_1000.parquet 验证导入

**验收**：
- [ ] 1000 行全部导入成功
- [ ] Schema 兼容性校验通过
- [ ] scan 读回 1000 行，数据正确
- [ ] 值域范围校验通过（数据在 Schema 声明范围内）

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: Schema 兼容性 | 3 | 0.15w | ⬜ |
| Task 2: 值域校验 | 3 | 0.1w | ⬜ |
| Task 3: DataImporter | 3 | 0.15w | ⬜ |
| Task 4: 脚手架 | 2 | 0.05w | ⬜ |
| Task 5: 数据集验证 | 1 | 0.05w | ⬜ |
| **合计** | **12** | **0.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| Parquet 类型映射复杂 | 先支持核心类型，边缘类型返回 kTypeMismatch |
| 大文件内存不足 | 流式读取，batch 处理 |
| Schema 校验与 Unit A 的 Schema 定义不一致 | 依赖 Unit A 的 Schema 接口，不重复定义 |
