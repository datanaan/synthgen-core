SynthGen Core v1 Unit I 实施计划：Tool Line v1
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit I 设计规范 v1.0
估算：0.5 周
依赖：Unit D (Physics Engine) + Unit E (Validation) 的脚手架代码

---

## 概述

Unit I 实现 v1 的两项开发辅助工具。在第 4-5 周起引入，依赖 #5/#6 的脚手架代码作为素材。

---

## Task 1：组件模板引擎 v0.1

**目标**：从 #5/#6 提炼模板，生成组件骨架

### Step 1.1：模板素材收集

**做什么**：分析 #5 物理引擎和 #6 验证器的脚手架代码

**产出**：`tools/scaffold_templates/` 目录

**收集内容**：
- span 创建/写入模式
- metrics 注册/暴露模式
- Explain 接口模式
- 错误处理框架
- 头文件结构

**验收**：
- [ ] 收集 #5 的脚手架代码
- [ ] 收集 #6 的脚手架代码
- [ ] 识别共同模式

### Step 1.2：模板定义

**做什么**：定义组件骨架模板

**产出**：`tools/scaffold_templates/component.h.inja`
`tools/scaffold_templates/component.cpp.inja`

**模板变量**：
- {{name}}：组件名
- {{namespace}}：命名空间
- {{spans}}：span 列表
- {{metrics}}：metrics 列表
- {{explain_fields}}：Explain 字段
- {{methods}}：方法列表

**验收**：
- [ ] 模板语法正确
- [ ] 变量覆盖所有需求
- [ ] 模板可解析

### Step 1.3：模板引擎实现

**做什么**：实现模板展开工具

**产出**：`tools/scaffold_generator/main.cpp`

**关键逻辑**：
- 读取 JSON 接口描述
- 加载模板
- 展开模板
- 输出 .h 和 .cpp

**验收**：
- [ ] 读取 JSON 正确
- [ ] 模板展开正确
- [ ] 输出文件正确

### Step 1.4：生成 #8 骨架

**做什么**：用模板引擎生成 EvidencePackage 构建器骨架

**产出**：`src/engine/evidence/_generated_builder.h`
`src/engine/evidence/_generated_builder.cpp`

**验收**：
- [ ] 骨架可编译
- [ ] 骨架通过 CI
- [ ] 核心逻辑处留 TODO

### Step 1.5：模板引擎测试

**做什么**：编写模板引擎测试

**产出**：`tools/scaffold_generator/test_generator.cpp`

**测试用例**（至少 5 个）：
- 简单组件生成
- 多 span 组件
- 多 metrics 组件
- **错误测试**：非法 JSON 输入
- **边界测试**：空方法列表

**验收**：5+ 测试用例全通过

---

## Task 2：测试辅助库 v0.1

**目标**：实现测试宏和辅助函数

### Step 2.1：TEST_RANGE_VALIDATION 宏

**做什么**：实现值域边界测试宏

**产出**：`src/scaffold/test_helpers.h`

```cpp
#define TEST_RANGE_VALIDATION(validator, column, min_val, max_val) \
    TEST_F(SeedFixedTest, column##_range_validation) { \
        auto v = validator; \
        /* min-ε */ \
        EXPECT_FALSE(v.validate_value(min_val - 0.001, min_val, max_val)); \
        /* min */ \
        EXPECT_TRUE(v.validate_value(min_val, min_val, max_val)); \
        /* min+ε */ \
        EXPECT_TRUE(v.validate_value(min_val + 0.001, min_val, max_val)); \
        /* max-ε */ \
        EXPECT_TRUE(v.validate_value(max_val - 0.001, min_val, max_val)); \
        /* max */ \
        EXPECT_TRUE(v.validate_value(max_val, min_val, max_val)); \
        /* max+ε */ \
        EXPECT_FALSE(v.validate_value(max_val + 0.001, min_val, max_val)); \
    }
```

**验收**：
- [ ] 宏可编译
- [ ] min-ε 失败
- [ ] min 通过
- [ ] max+ε 失败
- [ ] max 通过

### Step 2.2：SeedFixedTest 基类

**做什么**：实现种子固定测试基类

**产出**：`src/scaffold/test_helpers.h`

```cpp
class SeedFixedTest : public ::testing::Test {
protected:
    void SetUp() override {
        seed_ = 42;
    }
    uint64_t seed_ = 42;
};
```

**验收**：
- [ ] 基类可用
- [ ] seed_ = 42
- [ ] 可继承

### Step 2.3：参数化测试宏

**做什么**：实现参数化测试辅助

**产出**：`src/scaffold/test_helpers.h`

```cpp
#define EXPECT_BATCH_EQ(actual, expected) \
    /* 逐行比对 */

#define EXPECT_SCHEMA_EQ(actual, expected) \
    /* Schema 比对 */
```

**验收**：
- [ ] EXPECT_BATCH_EQ 可用
- [ ] EXPECT_SCHEMA_EQ 可用

### Step 2.4：测试辅助库测试

**做什么**：编写测试辅助库测试

**产出**：`tests/unit/test_helpers_test.cpp`

**测试用例**（至少 10 个）：
- TEST_RANGE_VALIDATION 宏（正常范围）
- TEST_RANGE_VALIDATION 宏（边界值）
- SeedFixedTest 基类
- EXPECT_BATCH_EQ（相同 batch）
- EXPECT_BATCH_EQ（不同 batch）
- EXPECT_SCHEMA_EQ（相同 Schema）
- EXPECT_SCHEMA_EQ（不同 Schema）
- **错误测试**：非法范围（min > max）
- **边界测试**：范围宽度 = 0
- **边界测试**：范围宽度 = DBL_MAX

**验收**：10+ 测试用例全通过

---

## Task 3：工具验证

**目标**：验证工具可用性

### Step 3.1：模板引擎验证

**做什么**：用模板引擎生成一个测试组件，验证通过编译

**产出**：测试组件代码

**验收**：
- [ ] 生成代码可编译
- [ ] 生成代码通过 CI
- [ ] 模板不生成核心逻辑

### Step 3.2：测试辅助库验证

**做什么**：用测试辅助库编写 #8 的测试

**产出**：`tests/unit/evidence_package_test_with_helpers.cpp`

**验收**：
- [ ] 宏减少测试代码量
- [ ] 边界测试自动覆盖
- [ ] 测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 模板引擎 | 5 | 0.3w | ⬜ |
| Task 2: 测试辅助库 | 4 | 0.15w | ⬜ |
| Task 3: 工具验证 | 2 | 0.05w | ⬜ |
| **合计** | **11** | **0.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 模板素材不足 | 确保 #5/#6 脚手架代码规范 |
| 模板过时 | 与代码库一起版本控制 |
| 测试宏与 Google Test 版本冲突 | 使用标准宏，避免内部 API |
| 工具维护成本 | 每个版本更新模板 |
