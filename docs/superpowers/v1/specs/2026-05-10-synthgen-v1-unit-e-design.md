SynthGen Core v1 Unit E 设计规范：Validation + tail_report
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0、Unit D 设计规范
下游文档：Unit E 实施计划
组件：#6 值域约束验证器 + #7 tail_report v1
估算：1 周
依赖：Unit D (Physics Engine v1)

---

## 一、本 Unit 交付什么

Unit E 实现 v1 的验证与报告系统：
1. **ValueRangeValidator**：逐行验证采样结果是否在矩形值域内（安全网）
2. **TailReportBuilder**：生成 tail_report（偏差声明 + 排除率 + data_grade）

**诚实声明**：v1 纯物理路径下，验证器应 100% 通过（因为采样器已在约束域内采样）。验证器作为安全网存在——即使物理引擎保证在值域内，验证器仍逐行检查。发现越界 → 记录到 tail_report（不应发生，但安全网不删）。

---

## 二、ValueRangeValidator

### 2.1 接口

```cpp
namespace synthgen::engine::constraint {

struct ValidationResult {
    int64_t rows_checked;
    int64_t rows_passed;
    int64_t rows_failed;      // v1 纯物理路径应为 0
    double pass_rate;         // v1 应为 1.0
    std::vector<ValidationFailure> failures;  // 最多前 100 个
};

struct ValidationFailure {
    int64_t row_index;
    std::string column_name;
    double actual_value;
    double expected_min;
    double expected_max;
    std::string constraint_name;
};

class ValueRangeValidator {
public:
    explicit ValueRangeValidator(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints);

    // 验证单 batch
    Result<ValidationResult> validate_batch(const ArrowBatch& batch);

    // 验证单行（用于调试）
    Result<bool> validate_row(const Row& row);

    // Explain
    ExplainInfo explain() const;

private:
    // 每列的验证规则
    std::vector<ColumnValidationRule> rules_;
};

struct ColumnValidationRule {
    std::string column_name;
    DataType type;
    std::optional<double> min_value;
    std::optional<double> max_value;
    std::vector<ConstraintOperator> operators;  // BETWEEN, >, <, etc.
};

}  // namespace synthgen::engine::constraint
```

### 2.2 验证逻辑

```cpp
// 对每行：
for each column with validation rule:
    if rule has min_value and value < min_value:
        record failure
    if rule has max_value and value > max_value:
        record failure
    if rule has operators:
        for each operator:
            if operator == kGreaterThan and value <= threshold: record failure
            if operator == kLessThan and value >= threshold: record failure
            // etc.

// 结果：
// - rows_failed = 0 → pass_rate = 1.0
// - rows_failed > 0 → pass_rate = rows_passed / rows_checked
// - failures 最多记录前 100 个
```

### 2.3 安全网原则

| 场景 | 行为 | 理由 |
|------|------|------|
| 物理引擎正常 | 验证器 100% 通过 | 预期行为 |
| 物理引擎 bug | 验证器发现越界 | 安全网生效 |
| 约束与 Schema 冲突 | 验证器按约束检查 | 约束优先 |
| 空 batch | 返回空结果 | 无数据可验证 |
| 约束引用不存在的列 | 返回 kUndefinedColumn | 前置检查 |

---

## 三、TailReportBuilder

### 3.1 接口

```cpp
namespace synthgen::engine::evidence {

struct TailReportV1 {
    // === 认识论偏差声明（必须）===
    std::string epistemological_bias = "physical_first";
    std::string tail_exclusion_statement =
        "Tail events systematically excluded by value range constraints. "
        "The generated data world's risk spectrum is narrower than the real physical world.";

    // === 排除率统计 ===
    std::vector<ConstraintExclusionRate> exclusion_rate_by_constraint;
    double total_exclusion_rate;  // v1 纯物理路径应为 0.0

    // === data_grade ===
    std::string data_grade = "physics_guaranteed";

    // === 物理引擎统计 ===
    int64_t rows_generated;
    int64_t rows_validated;
    int64_t rows_failed_validation;  // v1 应为 0
    std::string distribution_used;
    uint64_t seed_used;
};

struct ConstraintExclusionRate {
    std::string constraint_name;
    std::string column_name;
    double rate;           // 该约束的排除率
    double range_width;    // 约束范围宽度（max - min）
    double schema_range_width;  // Schema 声明范围宽度
};

class TailReportBuilder {
public:
    Result<TailReportV1> build(
        const GenerationResult& generation_result,
        const ValidationResult& validation_result,
        const GenerationRequest& request);

private:
    // 计算每个约束的排除率
    std::vector<ConstraintExclusionRate> calculate_exclusion_rates(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints);
};

}  // namespace synthgen::engine::evidence
```

### 3.2 排除率计算

```cpp
// 对值域约束，排除率 = 1 - (约束范围宽度 / Schema 声明范围宽度)
//
// 示例：
// Schema: temperature FLOAT [-50.0, 80.0] → 范围宽度 = 130.0
// 约束: temperature BETWEEN -10 AND 45 → 范围宽度 = 55.0
// 排除率 = 1 - (55.0 / 130.0) = 0.577 (57.7%)
//
// 纯物理路径下，采样器在约束范围内采样 → 实际排除率 = 0
// 但 tail_report 报告的是"理论排除率"——约束导致的尾部事件排除
```

### 3.3 偏差声明

**必须包含的声明**（来自路线图 v1.4 诚实声明）：

```
epistemological_bias: "physical_first"
声明：物理优先策略导致生成数据世界的风险谱比真实物理世界更窄。
      极端工况和尾部事件被系统性排除。这是理论选择的结果，不是功能缺陷。

tail_exclusion_statement:
声明：尾部事件被值域约束系统性排除。
      请求的数据仅在约束定义的矩形域内，域外事件不会出现。

data_grade: "physics_guaranteed"
声明：物理合法性无条件保证。所有生成值在约束定义的范围内。
```

---

## 四、错误处理

| 错误场景 | 错误码 | 行为 |
|---------|--------|------|
| 空 batch | kEmptyBatch | 返回空 ValidationResult |
| 约束引用不存在的列 | kUndefinedColumn | 失败 |
| 约束与 batch Schema 不匹配 | kSchemaMismatch | 失败 |
| 验证失败行超过 100 | （允许） | 只记录前 100 个 |
| 空约束列表 | （允许） | 返回全部通过 |

---

## 五、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `ValueRangeValidator::validate_batch()` | Unit F (EvidencePackage) | 验证结果纳入证据包 |
| `TailReportBuilder::build()` | Unit F (EvidencePackage) | tail_report 纳入证据包 |
| `ValidationResult` | Unit H (Metrics) | 暴露验证统计 |
| `TailReportV1` | Unit G (SDK) | 用户查看 tail_report |

---

## 六、Unit E 验收标准

### 6.1 功能验收

- [ ] 验证器逐行检查，值在约束范围内 → 通过
- [ ] 验证器发现越界值 → 记录失败（最多 100 个）
- [ ] 空 batch 返回空结果
- [ ] 空约束列表返回全部通过
- [ ] tail_report 包含认识论偏差声明
- [ ] tail_report 包含尾部排除声明
- [ ] tail_report 包含按约束的排除率
- [ ] tail_report 包含 data_grade = "physics_guaranteed"
- [ ] tail_report 包含物理引擎统计
- [ ] 排除率计算正确（1 - 约束宽度/Schema 宽度）

### 6.2 错误测试验收

- [ ] 空 batch 返回 kEmptyBatch
- [ ] 约束引用不存在的列返回 kUndefinedColumn
- [ ] 约束与 batch Schema 不匹配返回 kSchemaMismatch
- [ ] 验证失败 101 行 → 只记录前 100 个
- [ ] 验证失败 100 行 → 记录全部 100 个
- [ ] 空约束列表 → 全部通过
- [ ] 0 行 batch → 空结果

### 6.3 边界条件测试

- [ ] 值刚好等于 min → 通过
- [ ] 值刚好等于 max → 通过
- [ ] 值刚好 min-ε → 失败
- [ ] 值刚好 max+ε → 失败
- [ ] 1 行 batch 验证
- [ ] 100000 行 batch 验证
- [ ] 1000 列 batch 验证
- [ ] 范围宽度 = 0（min == max）→ 只有该值通过
- [ ] 范围宽度极大（DBL_MAX）→ 排除率 ≈ 0
- [ ] 范围宽度极小（ε）→ 排除率 ≈ 1

### 6.4 诚实声明验收

- [ ] tail_report.epistemological_bias = "physical_first"
- [ ] tail_report.tail_exclusion_statement 包含"风险谱更窄"
- [ ] tail_report.data_grade = "physics_guaranteed"
- [ ] 纯物理路径下 rows_failed_validation = 0
- [ ] 高斯分布截断后 rows_failed_validation = 0（截断在采样时完成）

### 6.5 脚手架验收

- [ ] validate_batch 产生 Trace span
- [ ] build 产生 Trace span
- [ ] /metrics 暴露 validation_total / validation_failures

### 6.6 测试验收

- [ ] 单元测试：验证逻辑 + tail_report 构建
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 每个 ErrorCode 至少 1 个测试用例触发
- [ ] 至少 25 个测试用例
- [ ] CI 自动运行
