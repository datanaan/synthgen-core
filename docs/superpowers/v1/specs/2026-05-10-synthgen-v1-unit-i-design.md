SynthGen Core v1 Unit I 设计规范：Tool Line v1
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0、Unit D/E 设计规范
下游文档：Unit I 实施计划
组件：组件模板引擎 v0.1 + 测试辅助库 v0.1
估算：0.5 周
依赖：Unit D (Physics Engine) + Unit E (Validation) 的脚手架代码作为素材

---

## 一、本 Unit 交付什么

Unit I 实现 v1 的两项开发辅助工具。工具线在第 4 周起引入，依赖已有组件的脚手架代码作为素材。

交付物：
1. **组件模板引擎 v0.1**：从 #5/#6 提炼模板，生成 #8 骨架
2. **测试辅助库 v0.1**：参数化值域测试宏 + 种子固定测试基类

---

## 二、组件模板引擎 v0.1

### 2.1 定位

**做什么**：给定组件接口描述，自动生成含 span 创建/写入、metrics 注册/暴露、Explain 接口占位、错误处理框架的 .cpp 和 .h 骨架。

**不是做什么**：
- ❌ 不生成核心逻辑代码（约束验证算法、采样策略等）
- ❌ 不生成业务逻辑

### 2.2 输入格式

```json
{
  "name": "ValueRangeValidator",
  "namespace": "synthgen::engine::constraint",
  "spans": ["validate_batch", "validate_row"],
  "metrics": ["rows_validated", "validation_errors"],
  "explain_fields": ["constraint_type", "execution_mode"],
  "dependencies": ["Schema", "ArrowBatch"],
  "methods": [
    {
      "name": "validate_batch",
      "return": "Result<ValidationResult>",
      "params": [{"name": "batch", "type": "const ArrowBatch&"}]
    }
  ]
}
```

### 2.3 输出格式

```cpp
// value_range_validator.h（自动生成）
#pragma once
#include "synthgen/common/result.h"
#include "synthgen/scaffold/trace.h"
#include "synthgen/scaffold/metrics.h"
#include "synthgen/scaffold/explain.h"

namespace synthgen::engine::constraint {

class ValueRangeValidator {
public:
    explicit ValueRangeValidator(const Schema& schema,
                                  const std::vector<ConstraintDef>& constraints);

    Result<ValidationResult> validate_batch(const ArrowBatch& batch);

    ExplainInfo explain() const;

private:
    // TODO: 核心逻辑由开发者填充
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace synthgen::engine::constraint

// value_range_validator.cpp（自动生成）
#include "value_range_validator.h"

namespace synthgen::engine::constraint {

ValueRangeValidator::ValueRangeValidator(
    const Schema& schema,
    const std::vector<ConstraintDef>& constraints) {
    // TODO: 初始化逻辑
}

Result<ValidationResult> ValueRangeValidator::validate_batch(
    const ArrowBatch& batch) {
    SpanGuard span("validator", "validate_batch", trace_id_);
    MetricsRegistry::increment("validation_total");

    // TODO: 核心验证逻辑

    span.set_attribute("rows_checked", std::to_string(result.rows_checked));
    return result;
}

ExplainInfo ValueRangeValidator::explain() const {
    return {
        .execution_mode = ExecutionMode::kRowByRow,
        .constraint_classification = {value_range: N, inter_row: 0, aggregate: 0}
    };
}

}  // namespace synthgen::engine::constraint
```

### 2.4 技术选型

- **模板引擎**：inja（C++ 头文件库）或 Jinja2（Python）
- **输入解析**：nlohmann/json
- **模板文件**：与代码库一起版本控制

### 2.5 验收标准

- [ ] 从 #5/#6 的脚手架代码提炼模板
- [ ] 模板生成的骨架代码能通过编译
- [ ] 骨架代码包含 span/metrics/explain 框架
- [ ] 核心逻辑处留 TODO 标记
- [ ] 生成的 #8 骨架通过基础 CI

---

## 三、测试辅助库 v0.1

### 3.1 定位

**做什么**：提供 C++ 测试宏和辅助函数，让开发者用一行宏定义即可生成边界/异常测试用例。

**不是做什么**：
- ❌ 不生成约束语义的深层测试（如"行间约束跨 batch 状态传递"需要理解业务语义）

### 3.2 核心宏

```cpp
// test_helpers.h

// 值域边界测试：自动生成 min-ε, min, min+ε, max-ε, max, max+ε 测试
#define TEST_RANGE_VALIDATION(validator, column, min_val, max_val) \
    TEST_F(SeedFixedTest, column##_range_validation) { \
        /* min-ε → 失败 */ \
        /* min → 通过 */ \
        /* min+ε → 通过 */ \
        /* max-ε → 通过 */ \
        /* max → 通过 */ \
        /* max+ε → 失败 */ \
    }

// 约束合规测试：给定生成器和约束，验证通过率
#define TEST_CONSTRAINT_COMPLIANCE(generator, constraint, expected_pass_rate) \
    TEST_F(SeedFixedTest, constraint##_compliance) { \
        auto result = generator.generate(/* ... */); \
        auto validation = validator.validate_batch(result.data); \
        EXPECT_NEAR(validation.pass_rate, expected_pass_rate, 0.01); \
    }

// 种子固定测试基类
class SeedFixedTest : public ::testing::Test {
protected:
    void SetUp() override {
        seed_ = 42;
    }
    uint64_t seed_ = 42;
};

// 参数化测试数据生成
class ParametrizedRangeTest : public ::testing::TestWithParam<std::tuple<double, double, bool>> {};

// 断言宏
#define EXPECT_BATCH_EQ(actual, expected) \
    /* 逐行比对两个 ArrowBatch */

#define EXPECT_SCHEMA_EQ(actual, expected) \
    /* 比对两个 Schema */
```

### 3.3 使用示例

```cpp
#include "synthgen/scaffold/test_helpers.h"

// 一行宏生成 6 个边界测试
TEST_RANGE_VALIDATION(validator, "temperature", -10.0, 45.0);

// 种子固定测试
TEST_F(SeedFixedTest, GenerateWithSeed42) {
    auto result = sampler.generate(request_with_seed(seed_));
    EXPECT_BATCH_EQ(result.data, load_snapshot("seed42.parquet"));
}

// 参数化测试
TEST_P(ParametrizedRangeTest, ValueInRange) {
    auto [value, min, max, expected] = GetParam();
    EXPECT_EQ(validator.validate_value(value, min, max), expected);
}
INSTANTIATE_TEST_SUITE_P(
    RangeTests,
    ParametrizedRangeTest,
    ::testing::Values(
        std::make_tuple(-10.1, -10.0, 45.0, false),  // min-ε
        std::make_tuple(-10.0, -10.0, 45.0, true),   // min
        std::make_tuple(-9.9, -10.0, 45.0, true),    // min+ε
        std::make_tuple(44.9, -10.0, 45.0, true),    // max-ε
        std::make_tuple(45.0, -10.0, 45.0, true),    // max
        std::make_tuple(45.1, -10.0, 45.0, false)    // max+ε
    )
);
```

### 3.4 技术选型

- **实现**：C++ 头文件库（test_helpers.h）
- **依赖**：Google Test
- **无外部依赖**：仅使用宏和模板

### 3.5 验收标准

- [ ] TEST_RANGE_VALIDATION 宏能自动检测越界值
- [ ] min-ε 和 max+ε 测试失败
- [ ] min 和 max 测试通过
- [ ] SeedFixedTest 基类可用
- [ ] 参数化测试宏可用
- [ ] 测试辅助库的宏在标准数据集上通过

---

## 四、Unit I 验收标准

### 4.1 模板引擎验收

- [ ] 从 #5/#6 提炼模板
- [ ] 生成 #8 骨架代码
- [ ] 骨架通过编译
- [ ] 骨架通过基础 CI
- [ ] 核心逻辑处留 TODO

### 4.2 测试辅助库验收

- [ ] TEST_RANGE_VALIDATION 宏可用
- [ ] 边界测试自动覆盖 min-ε/min/min+ε/max-ε/max/max+ε
- [ ] SeedFixedTest 基类可用
- [ ] 参数化测试宏可用
- [ ] 宏在标准数据集上通过

### 4.3 测试验收

- [ ] 模板引擎测试 ≥ 5 个
- [ ] 测试辅助库测试 ≥ 10 个
- [ ] CI 自动运行
