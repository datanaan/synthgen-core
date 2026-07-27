SynthGen Core v4 Unit U 设计规范：行数窗口 + 分组时间窗口
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v4 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit U 实施计划
组件：#25 行数窗口 + #26 分组时间窗口
估算：2 周
依赖：v2 #11 聚合约束引擎

---

## 一、本 Unit 交付什么

**Unit U 是 v4 窗口扩展的基础**——行数窗口和分组时间窗口为会话窗口提供聚合原语。

交付物：
1. **RowsWindowEngine**：基于行数的滑动/翻滚窗口聚合
2. **PartitionWindowEngine**：基于分组的区间窗口聚合
3. **WindowTypeV2 枚举扩展**：kRows / kPartitionBy
4. **与聚合引擎的集成**：复用 v2 #11 AggregateConstraintEngine 的两阶段执行框架

---

## 二、#25 行数窗口

### 2.1 核心语义

行数窗口（ROWS）基于物理行号而非时间区间：

- `OVER (ROWS 100)` —— 最近 100 行的滑动窗口
- `OVER (ROWS BETWEEN 50 PRECEDING AND CURRENT ROW)` —— 标准 SQL 行数窗口
- 行数窗口与时间无关，按物理行号计算

**与 v2 区间窗口的区别**：

| 维度 | v2 区间窗口 | v4 行数窗口 |
|------|-----------|-----------|
| 划分依据 | 时间区间 | 物理行号 |
| 窗口大小 | 固定时间跨度 | 固定行数 |
| 空窗口 | 区间内无数据 = 空 | 行数不足 = 部分窗口 |
| 排序要求 | 按 timestamp | 按用户指定列或默认顺序 |

### 2.2 接口定义

（定义见 v4 阶段设计规范 3.1 节）

### 2.3 错误处理

```cpp
enum class RowsWindowErrorCode {
    kInvalidRowCount,           // row_count <= 0
    kUndefinedColumn,           // column_name 不存在
    kEmptyBatch,               // 空批次
    kAggregateFunctionMismatch, // 聚合函数与列类型不匹配
    kWindowOverflow,            // 窗口行数超过批次大小
    kMissingSortColumn,         // 排序列未指定且无法推断
};
```

---

## 三、#26 分组时间窗口

### 3.1 核心语义

分组时间窗口（PARTITION BY + INTERVAL）按分组键拆分后，在每个分组内应用区间窗口：

- `OVER (PARTITION BY region, INTERVAL 1 HOUR)` —— 按地区分组，每组 1 小时窗口
- 分组列必须是 Schema 中存在的列
- 区间部分复用 v2 #11 的 IntervalWindow 逻辑

**语义层次**：

```
1. 按 partition_column 分组 → N 个子 batch
2. 每个子 batch 按 interval_spec 划分时间窗口
3. 每个时间窗口内应用聚合函数
4. 合并所有分组的结果
```

### 3.2 接口定义

（定义见 v4 阶段设计规范 3.1 节）

### 3.3 错误处理

```cpp
enum class PartitionWindowErrorCode {
    kUndefinedPartitionColumn,   // 分组列不存在
    kEmptyPartition,             // 分组后某组无数据
    kInvalidIntervalSpec,        // 区间规格无效
    kAggregateFunctionMismatch,  // 聚合函数与列类型不匹配
    kTooManyPartitions,          // 分组数超过阈值（默认 10000）
    kIntervalParseError,         // 区间字符串解析失败
};
```

---

## 四、Unit U 验收标准

### 4.1 功能验收

**#25 行数窗口**：
- [ ] ROWS N 正确计算最近 N 行的聚合
- [ ] ROWS BETWEEN M PRECEDING AND N FOLLOWING 正确计算范围
- [ ] 空批次返回空结果
- [ ] 行数超过批次大小时返回 kWindowOverflow
- [ ] 排序列指定后按指定列排序

**#26 分组时间窗口**：
- [ ] PARTITION BY 正确按列值分组
- [ ] 每个分组内区间窗口正确计算
- [ ] 多分组结果正确合并
- [ ] 分组列不存在返回 kUndefinedPartitionColumn
- [ ] 空分组跳过不报错

### 4.2 错误测试验收

- [ ] kInvalidRowCount（row_count = 0 / -1）
- [ ] kUndefinedColumn
- [ ] kEmptyBatch
- [ ] kAggregateFunctionMismatch
- [ ] kWindowOverflow
- [ ] kUndefinedPartitionColumn
- [ ] kEmptyPartition
- [ ] kInvalidIntervalSpec
- [ ] kTooManyPartitions
- [ ] kIntervalParseError

### 4.3 边界条件测试

- [ ] ROWS 1（最小行数窗口）
- [ ] ROWS = batch_size（窗口等于批次）
- [ ] ROWS > batch_size（溢出）
- [ ] 单分组 vs 多分组
- [ ] 分组数为 0（所有值相同）
- [ ] 分组数 > 10000（超阈值）
- [ ] 分组列含 NULL 值
- [ ] 空字符串区间规格

### 4.4 测试验收

- [ ] 至少 20 个测试用例
- [ ] 错误测试占比 ≥ 30%

---

## 五、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `RowsWindowEngine::compute()` | Unit V (会话窗口) | 行数窗口聚合结果 |
| `PartitionWindowEngine::compute()` | Unit V | 分组窗口聚合结果 |
| `WindowTypeV2::kRows` | Unit V, 路由器 | 窗口类型判断 |
| `WindowTypeV2::kPartitionBy` | Unit V, 路由器 | 窗口类型判断 |
