SynthGen Core v4 Unit U 实施计划：行数窗口 + 分组时间窗口
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit U 设计规范 v1.0
估算：2 周
依赖：v2 #11 聚合约束引擎

---

## 概述

Unit U 交付行数窗口和分组时间窗口——v4 窗口类型扩展的基础聚合原语。

---

## Task 1：WindowTypeV2 枚举与数据结构

### Step 1.1：WindowTypeV2 枚举扩展

**产出**：`src/window/window_type_v2.h`

**验收**：kInterval / kRows / kPartitionBy 三种类型定义，与 v2 kInterval 兼容

### Step 1.2：RowsWindowDef 数据结构

**产出**：`src/window/rows_window_def.h`, `src/window/rows_window_def.cpp`

**验收**：RowsWindowDef 构造和序列化正确，row_count=0/-1 返回 kInvalidRowCount

### Step 1.3：PartitionWindowDef 数据结构

**产出**：`src/window/partition_window_def.h`, `src/window/partition_window_def.cpp`

**验收**：PartitionWindowDef 构造和序列化正确，空 partition_column 返回 kUndefinedPartitionColumn

---

## Task 2：RowsWindowEngine 实现

### Step 2.1：行数窗口核心计算

**产出**：`src/window/rows_window_engine.h`, `src/window/rows_window_engine.cpp`

**验收**：ROWS N 正确计算最近 N 行聚合，ROWS BETWEEN 正确计算范围

### Step 2.2：排序和溢出处理

**验收**：排序列指定后正确排序，ROWS > batch_size 返回 kWindowOverflow

### Step 2.3：空批次和边界处理

**验收**：空批次返回空结果，ROWS 1 最小窗口正确

---

## Task 3：PartitionWindowEngine 实现

### Step 3.1：分组和区间窗口计算

**产出**：`src/window/partition_window_engine.h`, `src/window/partition_window_engine.cpp`

**验收**：PARTITION BY 正确分组，每个分组内区间窗口正确计算

### Step 3.2：结果合并和阈值控制

**验收**：多分组结果正确合并，分组数 > 10000 返回 kTooManyPartitions

### Step 3.3：NULL 值和空分组处理

**验收**：分组列含 NULL 正确处理，空分组跳过不报错

---

## Task 4：与聚合引擎集成

### Step 4.1：AggregateConstraintEngine 适配

**产出**：修改 `src/constraint/aggregate_constraint_engine.h/cpp`

**验收**：v2 聚合引擎正确处理 WindowTypeV2::kRows 和 kPartitionBy

### Step 4.2：两阶段执行框架复用

**验收**：行数窗口和分组窗口都走两阶段执行（预扫描 + 约束检查）

---

## Task 5：错误处理和测试

### Step 5.1：错误路径测试清单

**验收**：以下 10 个 ErrorCode 全部有对应测试用例：

| ErrorCode | 测试场景 | 用例数 |
|-----------|---------|--------|
| kInvalidRowCount | row_count=0, row_count=-1 | 2 |
| kUndefinedPartitionColumn | partition_column 为空字符串 | 1 |
| kWindowOverflow | ROWS > batch_size | 1 |
| kTooManyPartitions | 分组数 > 10000 | 1 |
| kNullPartitionKey | 分组列含 NULL | 1 |
| kEmptyBatch | 空批次输入 | 1 |
| kMissingOrderColumn | 排序列未定义但窗口要求排序 | 1 |
| kIncompatibleWindowType | 聚合引擎收到不支持的新窗口类型 | 1 |
| kWindowRangeInvalid | ROWS BETWEEN 范围非法（start > end） | 1 |
| kSerializationError | 序列化/反序列化失败 | 1 |

### Step 5.2：功能测试

**产出**：`tests/unit/rows_window_test.cpp`, `tests/unit/partition_window_test.cpp`

**验收**：28+ 测试通过（行数窗口 14+，分组窗口 14+），错误测试占比 ≥ 30%

**测试分类**：
- 单元测试：20+（正常路径 14，边界路径 6）
- 错误测试：8+（对应上述 ErrorCode）
- 性能测试：2（大数据集分组，行数窗口溢出检测）

### Step 5.3：集成测试

**产出**：`tests/integration/v4_window_aggregate_test.cpp`

**验收**：与 v2 聚合引擎集成正确，v2 区间窗口不受影响

**集成场景**：
- v2 区间窗口 + v4 行数窗口混合查询
- v2 区间窗口 + v4 分组窗口混合查询
- 三种窗口类型共存回归测试

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1 | 0.25w | ⬜ |
| Task 2 | 0.5w | ⬜ |
| Task 3 | 0.5w | ⬜ |
| Task 4 | 0.375w | ⬜ |
| Task 5 | 0.375w | ⬜ |
| **合计** | **2w** | — |

---

## 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| v2 聚合引擎接口不兼容 | Task 4 阻塞 | 提前对齐接口定义 |
| 分组数爆炸（内存） | 性能问题 | kTooManyPartitions 阈值保护 |
| 行数窗口排序歧义 | 语义不清 | 强制指定排序列或使用默认顺序 |
