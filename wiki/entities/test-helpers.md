# 测试辅助库

> 类型：工具

## 定义

v1 工具线提供的测试宏和辅助函数集合。旨在减少测试代码重复，自动覆盖常见边界条件，提高测试编写效率和质量。

## 核心组件

### TEST_RANGE_VALIDATION 宏

自动测试值域边界的六个关键点：

```cpp
#define TEST_RANGE_VALIDATION(validator, column, min_val, max_val)
// 自动测试：
// 1. min - epsilon -> 失败
// 2. min -> 通过
// 3. min + epsilon -> 通过
// 4. max - epsilon -> 通过
// 5. max -> 通过
// 6. max + epsilon -> 失败
```

### SeedFixedTest 基类

提供固定种子的 Google Test fixture：

```cpp
class SeedFixedTest : public ::testing::Test {
protected:
    void SetUp() override { seed_ = 42; }
    uint64_t seed_ = 42;
};
```

### 比对宏

```cpp
#define EXPECT_BATCH_EQ(actual, expected)    // 逐行比对 ArrowBatch
#define EXPECT_SCHEMA_EQ(actual, expected)   // Schema 比对
```

## 文件位置

`src/scaffold/test_helpers.h`

## 设计原则

- 使用标准 Google Test 宏，避免内部 API
- 与 Google Test 版本兼容
- 宏可编译、可读性好

## 关联实体

- [[tool-line]] — 测试辅助库是工具线的组成部分
- [[scaffolding]] — 与脚手架设施配合
- [[seed-controller]] — SeedFixedTest 使用固定种子
