# 条件约束引擎 (ConditionalConstraintEngine)

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

条件约束引擎是实现 DURING/WHEN 语义的核心组件，处理非矩形约束域的条件约束过滤和生成。

## 详情

条件约束引擎提供两个核心能力：
1. **条件过滤**：apply() 方法，对已有数据应用条件约束过滤
2. **条件域生成**：generate_in_conditional_domain() 方法，在非矩形约束域内生成新数据

核心数据结构：
- `ConditionalConstraintDef`：条件约束定义，包含条件类型（kDuring/kWhen）、条件字段、条件生效时的约束列表
- 支持 DURING（列值等值条件）和 WHEN（布尔条件表达式）两种类型

非矩形约束域处理流程：
1. 识别 DURING/WHEN 约束
2. 确定条件生效的子空间
3. 在子空间内使用拒绝采样
4. 拒绝率 >90% 时尝试 MCMC

错误码：kUndefinedColumn（DURING 列不存在）、kTypeMismatch（值与列类型不匹配）、kInvalidCondition（WHEN 条件语法错误）、kRejectionSamplingFailed（拒绝采样不收敛）、kMCMCConvergenceFailed（MCMC 不收敛）、kConflictingConditions（冲突条件）

## v2 范围

v2 Unit P Part A 完整实现条件约束引擎：
- `src/engine/constraint/conditional_engine.h/.cpp`
- apply() 和 generate_in_conditional_domain() 方法
- 拒绝采样 + MCMC 后备方案
- 至少 10 个引擎测试

## 关联实体

- [[during-when-semantics]] — DURING/WHEN 语义定义
- [[execution-router]] — 路由器调用条件约束引擎
- [[evidence-package]] — 条件约束信息写入证据包
- [[synthlang-parser]] — DURING/WHEN/THEN 语法解析

## 来源

- [[source-v2-unit-p-spec]] — 二、#16 DURING/WHEN 语义
- [[source-v2-unit-p-plan]] — Part A Task A2：条件约束引擎
