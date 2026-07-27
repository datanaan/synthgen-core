SynthGen Core v2 Unit K 设计规范：聚合约束引擎
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit K 实施计划
组件：#11 聚合约束引擎
估算：1.5 周
依赖：v1 #6 值域验证器 + #10 行间引擎

---

## 一、本 Unit 交付什么

**Unit K 是 v2 两阶段执行的核心**——聚合约束引擎实现阶段二的窗口聚合验证。

交付物：
1. **AggregateEngine**：两阶段执行（阶段一值域+行间 → 阶段二聚合）
2. **AggregationWindow**：时间窗口定义和行索引管理
3. **WindowExclusionRate**：窗口排除率计算 + partial_window_excluded 标记
4. **聚合约束语法解析扩展**：Parser 识别 OVER/INTERVAL 语法

---

## 二、#11 聚合约束引擎

### 2.1 核心语义

聚合约束检查的是**一组行**（窗口）的统计特征，而非单行值域或行间关系。典型语义：
- `AVG(temperature) OVER (INTERVAL 1 HOUR) < 40.0`（时间窗口聚合）
- `MAX(vibration) OVER (INTERVAL 5 MINUTES) <= 3.0`（最大值约束）
- `COUNT(*) OVER (INTERVAL 1 HOUR) >= 10`（频次约束）

**两阶段执行模型**：

```
阶段一（PHASE_ONE）：逐行过滤
  → 值域约束检查（v1 ValueRangeValidator）
  → 行间约束检查（v2 InterRowEngine）
  → 输出：逐行过滤后的数据

阶段二（PHASE_TWO）：窗口聚合验证
  → 在阶段一输出上按窗口分组
  → 计算每个窗口的聚合值
  → 验证聚合值是否满足约束
  → 标记不满足约束的窗口（partial_window_excluded）
```

### 2.2 聚合约束语法

```
// SynthLang v2 扩展：聚合约束
DEFINE CONSTRAINT wind_safety ON sensor_log {
    temperature BETWEEN -10 AND 45,                         // 值域（PHASE_ONE）
    vibration[t] - vibration[t-1] < 5.0,                    // 行间（PHASE_ONE）
    AVG(temperature) OVER (INTERVAL 1 HOUR) < 40.0,         // 聚合（PHASE_TWO）
    MAX(vibration) OVER (INTERVAL 5 MINUTES) <= 3.0,       // 聚合（PHASE_TWO）
};
```

### 2.3 接口定义

```cpp
namespace synthgen::engine::constraint {

// 聚合函数类型
enum class AggregateFunction {
    kAvg,
    kSum,
    kMin,
    kMax,
    kCount,
};

// 窗口类型（v2 仅支持 INTERVAL）
enum class WindowType {
    kInterval,     // 时间窗口：INTERVAL 1 HOUR
    // v4 扩展
    // kRows,      // 行数窗口：ROWS 100
    // kSession,   // 会话窗口：SESSION BY col GAP 5 MINUTES
};

// 聚合约束定义
struct AggregateConstraintDef {
    std::string column_name;          // 聚合列
    AggregateFunction function;        // 聚合函数
    WindowType window_type;            // 窗口类型
    std::string window_spec;           // 窗口规格（如 "1 HOUR", "5 MINUTES"）
    std::optional<double> min_val;     // 聚合结果下限
    std::optional<double> max_val;     // 聚合结果上限
    std::string constraint_name;       // 约束名称
};

// 聚合窗口
struct AggregationWindow {
    int64_t start_row;                 // 窗口起始行索引
    int64_t end_row;                   // 窗口结束行索引
    Timestamp window_start;            // 窗口起始时间
    Timestamp window_end;              // 窗口结束时间
    std::vector<int64_t> included_rows;  // 窗口内包含的行索引
    std::vector<int64_t> excluded_rows;  // 被排除的行索引
    bool is_partial = false;            // 窗口不完整标记（开头/结尾）
};

// 窗口排除率
struct WindowExclusionRate {
    std::string constraint_name;
    std::string window_spec;
    double exclusion_rate;              // 窗口内排除率
    bool is_partial;                    // partial_window_excluded 标记
};

// 阶段一结果
struct PhaseOneResult {
    ArrowBatch filtered_batch;          // 逐行过滤后的数据
    InterRowResult inter_row_result;    // 行间约束结果
    int64_t rows_filtered;             // 阶段一过滤的行数
};

// 阶段二结果
struct PhaseTwoResult {
    std::vector<AggregationWindow> windows;      // 聚合窗口列表
    std::vector<WindowExclusionRate> window_exclusion_rates;  // 窗口排除率
    int64_t windows_violated;                     // 违反约束的窗口数
    int64_t total_windows;                        // 总窗口数
};

// 两阶段执行结果
struct TwoPhaseResult {
    PhaseOneResult phase_one;
    PhaseTwoResult phase_two;
    double total_exclusion_rate;                  // 整体排除率
};

// 聚合约束引擎
class AggregateEngine {
public:
    explicit AggregateEngine(
        const Schema& schema,
        const std::vector<AggregateConstraintDef>& constraints);

    // 两阶段执行
    Result<TwoPhaseResult> execute(
        const ArrowBatch& batch,
        const ValueRangeValidator& range_validator,
        InterRowEngine& inter_row_engine,
        const std::vector<InterRowState>& inter_row_states);

    // 单独执行阶段二（阶段一由调用方执行）
    Result<PhaseTwoResult> execute_phase_two(
        const ArrowBatch& phase_one_output);

    // 窗口划分
    Result<std::vector<AggregationWindow>> compute_windows(
        const ArrowBatch& batch);

    // 聚合计算
    Result<double> compute_aggregate(
        const ArrowBatch& batch,
        const AggregationWindow& window,
        const AggregateConstraintDef& constraint);

    // Explain
    ExplainInfo explain() const;

private:
    Schema schema_;
    std::vector<AggregateConstraintDef> constraints_;
    std::string order_column_;         // ORDER 列（时间窗口需要）

    // 时间窗口划分：按 ORDER 列（DATETIME 类型）划分
    Result<std::vector<AggregationWindow>> compute_time_windows(
        const ArrowBatch& batch,
        const std::string& interval_spec);
};

}  // namespace synthgen::engine::constraint
```

### 2.4 错误处理

```cpp
enum class AggregateErrorCode {
    kUnsupportedAggregateFunction,   // 不支持的聚合函数
    kInvalidWindowSpec,               // 窗口语法错误
    kEmptyWindow,                     // 空窗口（0行数据）
    kOverflow,                        // 聚合结果溢出
    kWindowColumnNotDatetime,         // 时间窗口要求 ORDER 列为 DATETIME
    kUndefinedColumn,                 // 聚合列不存在
    kTypeMismatch,                    // 聚合列类型不匹配
    kInvalidRange,                    // min_val > max_val
    kPartialWindowNotHandled,         // partial window 未处理
};
```

---

## 三、Unit K 验收标准

### 3.1 功能验收

- [ ] 两阶段执行正确：阶段一（值域+行间）→ 阶段二（聚合）
- [ ] 时间窗口（INTERVAL）划分正确
- [ ] AVG/SUM/MIN/MAX/COUNT 聚合函数计算正确
- [ ] 窗口排除率计算正确
- [ ] partial_window_excluded 标记在窗口不完整时正确设置
- [ ] 阶段一过滤后 0 行数据进入阶段二时正确处理
- [ ] Parser 识别 OVER/INTERVAL 语法
- [ ] 约束分类器正确标记聚合约束为 PHASE_TWO

### 3.2 脚手架验收

- [ ] AggregateEngine 每次执行产生 Trace span（component="aggregate_engine", operation="execute"）
- [ ] 两阶段执行分别产生子 span（phase_one, phase_two）
- [ ] AggregateEngine 提供 explain() 方法

### 3.3 错误测试验收

**聚合引擎错误测试**：
- [ ] 空窗口（0行数据）的聚合结果行为确定
- [ ] 不支持的聚合函数返回 kUnsupportedAggregateFunction
- [ ] 窗口语法错误返回 kInvalidWindowSpec
- [ ] 阶段一过滤后 0 行数据进入阶段二时正确处理
- [ ] partial_window 标记在窗口不完整时正确设置
- [ ] 聚合结果溢出返回 kOverflow
- [ ] 时间窗口要求 ORDER 列为 DATETIME 类型
- [ ] 聚合列不存在返回 kUndefinedColumn
- [ ] 聚合列类型不匹配返回 kTypeMismatch
- [ ] min_val > max_val 返回 kInvalidRange

**Parser 扩展错误测试**：
- [ ] OVER 关键字后缺少括号返回 kSyntaxError
- [ ] INTERVAL 语法错误返回 kInvalidWindowSpec
- [ ] 不支持的窗口类型返回 kUnsupportedWindowType
- [ ] 聚合列不存在返回 kUndefinedColumn

### 3.4 边界条件测试

- [ ] 窗口大小 = 1（仅一行数据）
- [ ] 窗口大小 = 数据总量（一个窗口覆盖全部数据）
- [ ] 所有窗口都违反约束
- [ ] 没有窗口违反约束
- [ ] 极大窗口（覆盖 1 年数据）
- [ ] 极小窗口（1 秒）
- [ ] 阶段一过滤率极高时阶段二的窗口变化
- [ ] 多个聚合约束同时生效时的交互

### 3.5 测试验收

- [ ] 单元测试覆盖：聚合函数、窗口划分、两阶段执行、排除率计算
- [ ] 错误测试用例占比 ≥ 30%（至少 10 个错误测试）
- [ ] 每个 ErrorCode 至少 1 个测试用例触发
- [ ] 至少 30 个测试用例（10 错误 + 20 正向/边界）
- [ ] CI 自动运行

---

## 四、与后续 Unit 的接口

Unit K 交付后，以下接口供后续 Unit 使用：

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `AggregateEngine::execute()` | Unit M (路由器) | 两阶段约束执行 |
| `TwoPhaseResult` | Unit N (后筛选), Unit P (EvidencePackage) | 排除率统计 |
| `WindowExclusionRate` | Unit N (后筛选), Unit P (tail_report) | 窗口排除率 |
| `AggregateConstraintDef` | Unit L (分类器) | 约束分类 |
| `PhaseOneResult/PhaseTwoResult` | Unit P (EvidencePackage) | 证据包构建 |
