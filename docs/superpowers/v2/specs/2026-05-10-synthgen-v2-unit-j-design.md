SynthGen Core v2 Unit J 设计规范：行间约束引擎
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit J 实施计划
组件：#10 行间约束引擎
估算：1.5 周
依赖：v1 #5 物理引擎 + v1 #6 值域验证器（Wave 1 起步组件）

---

## 一、本 Unit 交付什么

**Unit J 是 v2 的第一个新组件**——行间约束引擎是三类约束体系中"行间"层的核心实现。

交付物：
1. **InterRowEngine**：batch 有状态执行，跨 batch 状态传递
2. **Frame Buffer**：存储上一 batch 最后 N 行，用于跨 batch 约束检查
3. **ORDER 列绑定**：排序列来自 Schema ORDER 声明
4. **InterRowState**：batch 间传递的状态对象
5. **行间约束语法解析扩展**：Parser 识别行间约束语法

---

## 二、#10 行间约束引擎

### 2.1 核心语义

行间约束检查的是**相邻行之间**的关系，而非单行内部的值域。典型语义：
- `|vibration[t] - vibration[t-1]| < 5.0`（变化率约束）
- `temperature[t] > temperature[t-1]`（单调性约束）
- `status[t] != status[t-1] IMPLIES gap > 10s`（状态跳变约束）

**关键设计决策**：
1. **排序列绑定**：行间约束依赖行的顺序。顺序由 Schema 的 ORDER 列定义，不是用户在约束中指定的。
2. **batch 有状态**：数据按 batch 处理，行间约束跨 batch 传递状态（上一 batch 最后一行的值）。
3. **frame buffer**：存储上一 batch 最后 N 行，用于跨 batch 边界处的约束检查。

### 2.2 行间约束语法

```
// SynthLang v2 扩展：行间约束
DEFINE CONSTRAINT wind_safety ON sensor_log {
    temperature BETWEEN -10 AND 45,                          // 值域约束（v1 语法）
    vibration[t] - vibration[t-1] < 5.0,                    // 行间约束（v2 新增）
    ABS(pressure[t] - pressure[t-1]) <= 20.0,               // 绝对差约束
    vibration[t] > vibration[t-1],                           // 单调性约束
};

// Parser 识别 [t] 和 [t-1] 语法
// 约束分类器将行间约束标记为 PHASE_ONE
```

### 2.3 接口定义

```cpp
namespace synthgen::engine::constraint {

// 行间约束定义
struct InterRowConstraintDef {
    std::string column_name;           // 约束列
    std::string order_column;           // 排序列（来自 Schema ORDER 声明）

    // 约束类型
    enum class Type {
        kDeltaMax,         // |x[t] - x[t-1]| < delta_max
        kDeltaMin,         // |x[t] - x[t-1]| > delta_min
        kMonotoneIncrease, // x[t] > x[t-1]
        kMonotoneDecrease, // x[t] < x[t-1]
        kCustom,           // 自定义表达式（v2 不实现，预留）
    };

    Type type;
    std::optional<double> delta_max;   // kDeltaMax 时的阈值
    std::optional<double> delta_min;   // kDeltaMin 时的阈值
};

// batch 间传递的状态
struct InterRowState {
    std::optional<double> last_value;   // 上一 batch 最后一个有效值
    bool initialized = false;            // 是否有上一 batch 的状态
    std::string column_name;             // 状态所属的列名
};

// frame buffer：存储上一 batch 最后 N 行
struct FrameBuffer {
    static constexpr int kDefaultSize = 2;

    std::deque<Row> buffer;              // 缓冲的行数据
    int max_size;

    void push(const Row& row);           // 压入新行
    const Row& back() const;             // 最近一行
    bool empty() const;
    void clear();
};

// 行间约束执行结果
struct InterRowResult {
    ArrowBatch filtered_batch;          // 过滤后的数据（按 ORDER 列排序）
    std::vector<InterRowState> outgoing_states;  // 传递给下一 batch 的状态
    int64_t rows_passed;                // 通过的行数
    int64_t rows_filtered;              // 被过滤的行数
    double filter_rate;                 // 过滤率
};

// 行间约束引擎
class InterRowEngine {
public:
    explicit InterRowEngine(
        const Schema& schema,
        const std::vector<InterRowConstraintDef>& constraints);

    // 执行一个 batch 的行间约束检查
    Result<InterRowResult> execute_batch(
        const ArrowBatch& batch,
        const std::vector<InterRowState>& incoming_states);

    // ORDER 列查询
    const std::string& order_column() const { return order_column_; }

    // 帧缓冲区大小
    int frame_buffer_size() const { return frame_buffer_.max_size; }

    // Explain
    ExplainInfo explain() const;

private:
    Schema schema_;
    std::vector<InterRowConstraintDef> constraints_;
    std::string order_column_;              // ORDER 列名
    FrameBuffer frame_buffer_;              // 跨 batch 缓冲

    // 内部执行
    Result<bool> check_inter_row_constraint(
        const InterRowConstraintDef& constraint,
        double current_value,
        double previous_value);

    // 按 ORDER 列排序
    Result<ArrowBatch> sort_by_order_column(const ArrowBatch& batch);
};

}  // namespace synthgen::engine::constraint
```

### 2.4 Parser 扩展

v2 Parser 需要识别行间约束语法 `[t]` 和 `[t-1]`：

```cpp
// Parser v2 扩展
namespace synthgen::parser {

// 行间约束 AST 节点
struct InterRowRef {
    std::string column_name;
    int offset;              // 0 = [t], -1 = [t-1], -2 = [t-2]（预留）
};

// 行间约束项
struct InterRowConstraintItem {
    InterRowRef left_ref;     // 左侧引用（通常 [t]）
    ConstraintOperator op;     // <, <=, >, >=, !=
    InterRowRef right_ref;    // 右侧引用（通常 [t-1]）
    std::optional<double> constant;  // 常量值（如 < 5.0 中的 5.0）
};

}  // namespace synthgen::parser
```

### 2.5 错误处理

```cpp
enum class InterRowErrorCode {
    kOrderColumnRequired,     // 有行间约束但 Schema 无 ORDER 列
    kUndefinedColumn,         // 约束列不存在
    kTypeMismatch,            // 约束列类型非数值
    kInvalidDelta,            // delta_max ≤ 0
    kEmptyBatch,              // 空 batch 输入
    kStateNotInitialized,     // 状态未初始化但要求跨 batch 检查
    kOrderColumnNull,         // ORDER 列有 NULL 值
    kInvalidOffset,           // 行间引用偏移量不支持（如 [t-3]）
};
```

---

## 三、Unit J 验收标准

### 3.1 功能验收

- [ ] 行间约束引擎可检查 delta_max/delta_min/单调性约束
- [ ] 排序列来自 Schema ORDER 声明
- [ ] batch 间状态传递正确（跨 batch 边界的行间约束生效）
- [ ] frame buffer 存储上一 batch 最后 N 行
- [ ] 数据按 ORDER 列排序后检查行间约束
- [ ] 过滤率计算正确
- [ ] Parser 识别 [t]/[t-1] 语法
- [ ] 约束分类器正确标记行间约束为 PHASE_ONE

### 3.2 脚手架验收

- [ ] InterRowEngine 每次执行产生 Trace span（component="inter_row_engine", operation="execute_batch"）
- [ ] InterRowEngine 提供 explain() 方法（返回约束列表 + 执行模式 + ORDER 列信息）
- [ ] 后筛选实时排除率变化记录到 span

### 3.3 错误测试验收

**行间引擎错误测试**：
- [ ] 空 batch 输入返回空结果 + 空状态
- [ ] ORDER 列不存在返回 kOrderColumnRequired
- [ ] 约束列不存在于 Schema 返回 kUndefinedColumn
- [ ] 约束列类型非数值返回 kTypeMismatch
- [ ] delta_max ≤ 0 返回 kInvalidDelta
- [ ] batch 间状态传递在空状态时正确初始化
- [ ] ORDER 列有 NULL 值时行为确定
- [ ] 行间引用偏移量不支持（如 [t-3]）返回 kInvalidOffset
- [ ] 多个行间约束同时生效时过滤率计算正确

**Parser 扩展错误测试**：
- [ ] [t-3] 偏移量返回 kUnsupportedOffset
- [ ] 行间约束引用不存在的列返回 kUndefinedColumn
- [ ] 行间约束引用非数值列返回 kTypeMismatch
- [ ] 行间约束在无 ORDER 列的 Schema 上返回 kOrderColumnRequired

### 3.4 边界条件测试

- [ ] batch 大小 = 1（仅一行，无行间关系）
- [ ] batch 大小 = 2（最小行间关系）
- [ ] 第一个 batch 无 incoming_state（冷启动）
- [ ] 所有行都被过滤（filter_rate = 1.0）
- [ ] 没有行被过滤（filter_rate = 0.0）
- [ ] delta_max 极小（接近 0）时几乎全部过滤
- [ ] delta_max 极大（DBL_MAX）时几乎全部通过
- [ ] ORDER 列值重复时行为确定
- [ ] frame buffer 跨 batch 边界处行间约束正确

### 3.5 测试验收

- [ ] 单元测试覆盖：合法约束、非法约束、跨 batch 状态、frame buffer
- [ ] 错误测试用例占比 ≥ 30%（至少 9 个错误测试）
- [ ] 每个 ErrorCode 至少 1 个测试用例触发
- [ ] 至少 25 个测试用例（9 错误 + 16 正向/边界）
- [ ] CI 自动运行

---

## 四、与后续 Unit 的接口

Unit J 交付后，以下接口供后续 Unit 使用：

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `InterRowEngine::execute_batch()` | Unit K (聚合), Unit M (路由器) | 行间约束过滤 |
| `InterRowState` | Unit K (聚合) | batch 间状态传递 |
| `InterRowConstraintDef` | Unit L (分类器) | 约束分类 |
| `InterRowEngine::order_column()` | Unit M (路由器) | 执行模式判断 |
| `InterRowResult::filter_rate` | Unit N (后筛选), Unit P (EvidencePackage) | 排除率统计 |
