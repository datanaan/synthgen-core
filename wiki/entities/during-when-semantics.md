# DURING/WHEN 语义

> 类型：概念
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

DURING/WHEN 是 SynthLang v2 引入的条件约束机制，约束只在特定条件下生效，产生非矩形约束域。

## 详情

**DURING 语义**：当指定列等于特定值时，约束生效
```
DURING status = "normal" THEN temperature BETWEEN -10 AND 45
// 仅在 status = "normal" 时，温度约束生效
```

**WHEN 语义**：当条件表达式为真时，约束生效
```
WHEN wind_speed > 15 THEN vibration < 2.0
// 仅在风速 >15 时，振动约束生效
```

**非矩形约束域**：
DURING/WHEN 产生的约束域不是简单的矩形空间（BETWEEN/MIN/MAX 定义的），而是条件性的。处理方式：
1. 在值域范围内采样
2. 检查 DURING/WHEN 条件
3. 不满足条件 → 拒绝（丢弃）
4. 满足条件 → 保留
5. 拒绝率 >90% 时尝试 MCMC 后备方案

**关键限制**：
- v2 仅支持简单等值条件（DURING）和比较条件（WHEN）
- 高维条件空间拒绝采样效率低，MCMC 作为后备
- 条件约束引擎独立于值域/行间/聚合引擎

## v2 范围

v2 Unit P #16 完整实现 DURING/WHEN 语义：
- ConditionalConstraintEngine 条件约束引擎
- DURING/WHEN 语法解析
- 非矩形约束域的拒绝采样/MCMC
- 至少 15 个测试用例

## 关联实体

- [[conditional-constraint]] — 条件约束引擎实现
- [[evidence-package]] — 条件约束信息写入证据包
- [[constraint-layering]] — 条件约束与三类约束体系的交互
- [[synthlang-parser]] — DURING/WHEN/THEN 语法扩展

## 来源

- [[source-v2-unit-p-spec]] — 二、#16 DURING/WHEN 语义
- [[source-v2-unit-p-plan]] — Part A：#16 DURING/WHEN 语义
