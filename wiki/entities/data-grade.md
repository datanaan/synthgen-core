# 数据等级 (data_grade)

> 类型：概念
> 首次编译：2026-05-11

## 定义

data_grade 不是"好/坏"标签，而是有条件的保证声明。每种等级对应不同的认识论承诺和允许用途。

## 详情

| data_grade | 含义 | 允许链路 |
|-----------|------|---------|
| physics_guaranteed | 物理合法性由约束卡片验证通过 | production/diagnostic/shadow/sandbox |
| statistics_guaranteed | 统计逼真性由数据驱动保证 | diagnostic/shadow/sandbox |
| limited_fidelity | 生成效率下降，统计逼真性不可保证 | sandbox |
| physics_unguaranteed | 约束缺失，物理合法性未验证 | sandbox（需豁免）/diagnostic 以下 |
| truncated | 超时/行数不足中断 | 需人工审核 |
| degraded | 部分维度约束未验证 | sandbox，标记未验证维度 |
| unqualified | 仅格式转换+扰动 | 不可进入任何链路 |

**排除率与 data_grade 联动**（含误差界）：

| 排除率 | data_grade | 双变量相关误差界 |
|-------|-----------|---------------|
| 0-30% | physics_guaranteed | ±0.2 |
| 30-70% | statistics_guaranteed | ±0.4 |
| 70-90% | limited_fidelity | 不保证 |
| >90% | 拒绝生成 | — |

**版本演进**：v1 仅提供 physics_guaranteed。统计签名条件保证从 v2 起随数据引擎参与而生效。

## 关联实体

- [[evidence-package]] — data_grade 作为必选字段
- [[identity-switch]] — 约束完备性决定 data_grade
- [[post-filter]] — 排除率与 data_grade 联动

## 来源

- [[source-theory-framework]] — §7.2 身份切换
- [[source-engineering-framework]] — §4.2 后筛选排除率联动表
- [[source-evidence-package-schema]] — §3.6 data_grade 定义
