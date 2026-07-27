# v1 Unit I Plan — Tool Line v1

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-i-plan.md
> 编译日期：2026-05-14

## 摘要

Unit I 实现 v1 的两项开发辅助工具，估算 0.5 周，依赖 Unit D（Physics Engine）和 Unit E（Validation）的脚手架代码。在第 4-5 周起引入。包含 3 个 Task、11 个步骤：组件模板引擎 v0.1（从 #5/#6 提炼脚手架模板 -> Inja 模板定义 -> 模板展开工具 -> 生成 #8 骨架）、测试辅助库 v0.1（TEST_RANGE_VALIDATION 宏、SeedFixedTest 基类、EXPECT_BATCH_EQ/EXPECT_SCHEMA_EQ 宏）、工具验证（模板引擎验证、测试辅助库验证）。

## 关键要点

- 模板引擎从 Unit D/E 的脚手架代码提炼共同模式：span 创建/写入、metrics 注册/暴露、Explain 接口、错误处理框架、头文件结构
- 模板使用 Inja 格式，变量包括 name、namespace、spans、metrics、explain_fields、methods
- TEST_RANGE_VALIDATION 宏自动覆盖 min-epsilon/min/min+epsilon/max-epsilon/max/max+epsilon 六个边界点
- SeedFixedTest 基类提供固定种子（seed=42）的测试 fixture
- 工具线是一等公民，模板素材质量直接影响后续版本开发效率

## 实现细节

### 关键产出

| 产出 | 文件路径 | 职责 |
|------|---------|------|
| 模板素材目录 | `tools/scaffold_templates/` | 从 #5/#6 收集的脚手架模式 |
| 组件头文件模板 | `tools/scaffold_templates/component.h.inja` | .h 文件模板 |
| 组件实现模板 | `tools/scaffold_templates/component.cpp.inja` | .cpp 文件模板 |
| 模板引擎 | `tools/scaffold_generator/main.cpp` | JSON 接口描述 -> 模板展开 -> .h/.cpp |
| 测试辅助库 | `src/scaffold/test_helpers.h` | TEST_RANGE_VALIDATION、SeedFixedTest、EXPECT_BATCH_EQ 等 |

### 模板变量

| 变量 | 说明 |
|------|------|
| `{{name}}` | 组件名 |
| `{{namespace}}` | 命名空间 |
| `{{spans}}` | span 列表 |
| `{{metrics}}` | metrics 列表 |
| `{{explain_fields}}` | Explain 字段 |
| `{{methods}}` | 方法列表 |

### TEST_RANGE_VALIDATION 宏

自动测试六个边界点：
1. min - epsilon -> 失败
2. min -> 通过
3. min + epsilon -> 通过
4. max - epsilon -> 通过
5. max -> 通过
6. max + epsilon -> 失败

### SeedFixedTest 基类

```cpp
class SeedFixedTest : public ::testing::Test {
protected:
    void SetUp() override { seed_ = 42; }
    uint64_t seed_ = 42;
};
```

### 测试策略

- 模板引擎测试 5+ 用例
- 测试辅助库测试 10+ 用例
- 验证：生成代码可编译、通过 CI、核心逻辑处留 TODO

## 提取的实体

- [[tool-line]] — 开发辅助工具（已存在）
- [[component-template-engine]] — 组件模板引擎，从脚手架代码提炼模式生成组件骨架（新实体）
- [[test-helpers]] — 测试辅助库，TEST_RANGE_VALIDATION 宏 + SeedFixedTest 基类 + 比对宏（新实体）
