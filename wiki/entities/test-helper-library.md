# TestHelperLibrary（测试辅助库）

> 类型：工具
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

v1 开发辅助工具，提供参数化值域测试宏和种子固定测试基类，支持确定性可复现的测试。

## 详情

两项核心能力：

1. **参数化值域测试宏**（SYNTHGEN_VALUE_RANGE_TEST）：
   - 自动生成多组值域参数的测试用例
   - 验证采样结果是否在指定值域内

2. **种子固定测试基类**（SeededTestFixture）：
   - 继承 Google Test 的 ::testing::Test
   - 构造时固定全局种子
   - 提供参考快照比对方法
   - Schema 验证辅助方法

## v1 范围

v0.1 版本，仅支持值域参数化测试和种子固定测试。

## 关联实体

- [[tool-line]] — 所属工具线
- [[seed-controller]] — 种子固定依赖种子控制器
- [[value-range-validator]] — 测试目标之一

## 来源

- [[source-v1-unit-i-spec]] — Unit I 设计规范
