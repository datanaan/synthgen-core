# SynthGen Core 文档导航

> 最后更新：2026-05-10

## 目录结构

```
docs/
├── README.md              ← 你在这里
├── core/                  ← 核心框架文档
├── product/               ← 产品与商业文档
├── reference/             ← 参考与规范文档
├── archive/               ← 历史版本归档
└── superpowers/           ← 开发规划（specs + plans）
    ├── overall-design.md  ← 总体设计规范
    ├── v1/                ← v1 基础引擎
    ├── v2/                ← v2 约束完整
    ├── v3/                ← v3 时间智能
    └── v4/                ← v4 高级分析
```

---

## 一、核心框架 (`core/`)

| 文档 | 说明 | 状态 |
|------|------|------|
| [理论核心框架 v1.3](core/SynthGen%20Core%20理论核心框架%20v1.3.md) | 物理优先认识论、诚实梯度、约束完备性理论 | 当前版本 |
| [工程框架 v0.4](core/SynthGen%20Core%20工程框架%20v0.4.md) | 4层架构、组件接口、数据流、技术栈 | 通过审查 |
| [开发路线图 v1.4](core/SynthGen%20Core%20开发路线图%20v1.4.md) | 30个组件、3条线（明线+暗线+工具线）、4个版本 | 当前版本 |
| [路线图导读 v1.4](core/路线图导读%20v1.4.md) | 路线图阅读指南，v1.3→v1.4 变更说明 | 当前版本 |

## 二、产品与商业 (`product/`)

| 文档 | 说明 |
|------|------|
| [产品定位与协作关系 v2.0](product/SynthGen%20Core%20产品定位与协作关系%20v2.0.md) | SynthGen Core 与 Polymorphic-Twin 的边界、协议、协同 |
| [产品边界、定价维度、目标客户画像](product/产品边界、定价维度、目标客户画像.md) | 功能边界、定价模型、客户细分 |
| [五个独立维度](product/SynthGen%20Core%20的实际价值：五个独立维度.md) | SynthGen Core 的独立价值分析 |

## 三、参考与规范 (`reference/`)

| 文档 | 说明 |
|------|------|
| [EvidencePackage Schema v1.2](reference/EvidencePackage%20Schema%20定义%20v1.2.md) | 证据包数据结构定义 |
| [工程执行守则](reference/SynthGen%20Core%20工程执行守则.md) | 开发纪律、诚实声明、审查流程 |
| [关于团队](reference/aboutme.md) | 团队背景与技术栈 |

---

## 四、开发规划 (`superpowers/`)

### 总体设计

| 文档 | 说明 |
|------|------|
| [Overall Design](superpowers/2026-05-10-synthgen-overall-design.md) | 架构分层、技术栈、横切关注点、版本间依赖关系 |

### v1 — 基础引擎（Unit A~I, #1~#9）

**功能组件（明线）**：

| Unit | 组件 | Spec | Plan |
|------|------|------|------|
| A | #1 TaskReceiver | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-a-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-a-plan.md) |
| B | #2 ConstraintParser | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-b-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-b-plan.md) |
| C | #3 ValueRangeConstraint | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-c-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-c-plan.md) |
| D | #4 ExecutionRouter | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-d-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-d-plan.md) |
| E | #5 DataEngine (stub) | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-e-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-e-plan.md) |
| F | #6 PostFilter | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-f-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-f-plan.md) |
| G | #7 HashChainAudit | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-g-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-g-plan.md) |
| H | #8 EvidencePackage v1 | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-h-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-h-plan.md) |
| I | #9 IdentitySwitcher | [spec](superpowers/v1/specs/2026-05-10-synthgen-v1-unit-i-design.md) | [plan](superpowers/v1/plans/2026-05-10-synthgen-v1-unit-i-plan.md) |

**阶段文档**：

| 类型 | 文档 |
|------|------|
| 阶段设计 | [v1-design](superpowers/v1/specs/2026-05-10-synthgen-v1-design.md) |
| 一致性检查 | [v1-consistency-check](superpowers/v1/specs/2026-05-10-synthgen-v1-consistency-check.md) |
| 完整性检查 | [v1-completeness-check](superpowers/v1/specs/2026-05-10-synthgen-v1-completeness-check.md) |

### v2 — 约束完整（Unit J~P, #10~#17）

**功能组件（明线）**：

| Unit | 组件 | Spec | Plan | 备注 |
|------|------|------|------|------|
| J | #10 InterRowConstraint | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-unit-j-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-unit-j-plan.md) | |
| K | #11 AggregateConstraint | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-unit-k-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-unit-k-plan.md) | |
| L | #12 ConstraintClassifier | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-unit-l-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-unit-l-plan.md) | [COORDINATE] C3 |
| M | #13 ExecutionRouter Refactor | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-unit-m-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-unit-m-plan.md) | [COORDINATE] C2 |
| N | #14 PostFilter Complete | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-unit-n-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-unit-n-plan.md) | |
| O | #15 HashChain + #15b DataEngine KDE | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-unit-o-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-unit-o-plan.md) | [COORDINATE] C1, C8 |
| P | #16 DURING/WHEN + #17 EvidencePackage v2 | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-unit-p-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-unit-p-plan.md) | |

**脚手架（暗线）+ 工具线**：

| 类型 | Spec | Plan | 备注 |
|------|------|------|------|
| 脚手架 | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-scaffold-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-scaffold-plan.md) | |
| 工具线 | [spec](superpowers/v2/specs/2026-05-10-synthgen-v2-tool-design.md) | [plan](superpowers/v2/plans/2026-05-10-synthgen-v2-tool-plan.md) | [COORDINATE] C7 |

**阶段文档**：

| 类型 | 文档 |
|------|------|
| 阶段设计 | [v2-design](superpowers/v2/specs/2026-05-10-synthgen-v2-design.md) |
| 一致性检查 | [v2-consistency-check](superpowers/v2/specs/2026-05-10-synthgen-v2-consistency-check.md) |
| 完整性检查 | [v2-completeness-check](superpowers/v2/specs/2026-05-10-synthgen-v2-completeness-check.md) |

### v3 — 时间智能（Unit Q~T, #18~#24）

**功能组件（明线）**：

| Unit | 组件 | Spec | Plan | 备注 |
|------|------|------|------|------|
| Q | #18 ModelVersionChain | [spec](superpowers/v3/specs/2026-05-10-synthgen-v3-unit-q-design.md) | [plan](superpowers/v3/plans/2026-05-10-synthgen-v3-unit-q-plan.md) | |
| R | #19 GC Compaction | [spec](superpowers/v3/specs/2026-05-10-synthgen-v3-unit-r-design.md) | [plan](superpowers/v3/plans/2026-05-10-synthgen-v3-unit-r-plan.md) | |
| S | #20 TimeTravel + #21 ContinuousAlignment | [spec](superpowers/v3/specs/2026-05-10-synthgen-v3-unit-s-design.md) | [plan](superpowers/v3/plans/2026-05-10-synthgen-v3-unit-s-plan.md) | [COORDINATE] C4, C5 |
| T | #22 TailReport + #23 StorageModel + #24 BiasReport | [spec](superpowers/v3/specs/2026-05-10-synthgen-v3-unit-t-design.md) | [plan](superpowers/v3/plans/2026-05-10-synthgen-v3-unit-t-plan.md) | |

**脚手架（暗线）+ 工具线**：

| 类型 | Spec | Plan |
|------|------|------|
| 脚手架 | [spec](superpowers/v3/specs/2026-05-10-synthgen-v3-scaffold-design.md) | [plan](superpowers/v3/plans/2026-05-10-synthgen-v3-scaffold-plan.md) |
| 工具线 | [spec](superpowers/v3/specs/2026-05-10-synthgen-v3-tool-design.md) | [plan](superpowers/v3/plans/2026-05-10-synthgen-v3-tool-plan.md) |

**阶段文档**：

| 类型 | 文档 |
|------|------|
| 阶段设计 | [v3-design](superpowers/v3/specs/2026-05-10-synthgen-v3-design.md) |
| 一致性检查 | [v3-consistency-check](superpowers/v3/specs/2026-05-10-synthgen-v3-consistency-check.md) |
| 完整性检查 | [v3-completeness-check](superpowers/v3/specs/2026-05-10-synthgen-v3-completeness-check.md) |

### v4 — 高级分析（Unit U~X, #25~#30）

**功能组件（明线）**：

| Unit | 组件 | Spec | Plan | 备注 |
|------|------|------|------|------|
| U | #25 ROWS + #26 PARTITION BY | [spec](superpowers/v4/specs/2026-05-10-synthgen-v4-unit-u-design.md) | [plan](superpowers/v4/plans/2026-05-10-synthgen-v4-unit-u-plan.md) | |
| V | #27 SESSION Window | [spec](superpowers/v4/specs/2026-05-10-synthgen-v4-unit-v-design.md) | [plan](superpowers/v4/plans/2026-05-10-synthgen-v4-unit-v-plan.md) | |
| W | #28 Completeness Scoring | [spec](superpowers/v4/specs/2026-05-10-synthgen-v4-unit-w-design.md) | [plan](superpowers/v4/plans/2026-05-10-synthgen-v4-unit-w-plan.md) | |
| X | #29 Counter-example + #30 EvidencePackage v3 | [spec](superpowers/v4/specs/2026-05-10-synthgen-v4-unit-x-design.md) | [plan](superpowers/v4/plans/2026-05-10-synthgen-v4-unit-x-plan.md) | [COORDINATE] C5, C6 |

**脚手架（暗线）+ 工具线**：

| 类型 | Spec | Plan |
|------|------|------|
| 脚手架 | [spec](superpowers/v4/specs/2026-05-10-synthgen-v4-scaffold-design.md) | [plan](superpowers/v4/plans/2026-05-10-synthgen-v4-scaffold-plan.md) |
| 工具线 | [spec](superpowers/v4/specs/2026-05-10-synthgen-v4-tool-design.md) | [plan](superpowers/v4/plans/2026-05-10-synthgen-v4-tool-plan.md) |

**阶段文档**：

| 类型 | 文档 |
|------|------|
| 阶段设计 | [v4-design](superpowers/v4/specs/2026-05-10-synthgen-v4-design.md) |
| 一致性检查 | [v4-consistency-check](superpowers/v4/specs/2026-05-10-synthgen-v4-consistency-check.md) |
| 完整性检查 | [v4-completeness-check](superpowers/v4/specs/2026-05-10-synthgen-v4-completeness-check.md) |

---

## 五、归档 (`archive/`)

### 历史版本

| 文档 | 版本 |
|------|------|
| 理论核心框架 v1.0 | 初版 |
| 理论核心框架 v1.1 | 第二版 |
| 理论核心框架 v1.2 | 第三版 |
| 工程设计 v0.1 | 初版（未通过审查） |
| 工程设计 v0.2 | 6项补完 |
| 工程框架 v0.3 | 条件通过 |
| 开发路线图 v1.0~v1.3 | 4个历史版本 |
| 路线图导读 v1.0~v1.3 | 4个历史版本 |
| Agent 辅助开发建议书 | 已合并入路线图 v1.4 工具线 |
| 脚手架工程建议书 | 已合并入路线图 v1.4 暗线 |

### 审查报告 (`archive/repo/`)

| 文档 | 说明 |
|------|------|
| review_eng_r1~r4 | 工程审查 4 轮（v0.1→v0.4） |
| review_full_chain_r1 | 全链路回头望 |
| review_r2, review_r3_final | 补充审查 |
| resp_1 | 审查回应 |
| gatekeeper_knowledge_base | 守门员知识库 |

### 思考记录 (`archive/some-think/`)

| 文档 | 说明 |
|------|------|
| ff.md | v0.1 工程框架自评报告 |

---

## 六、协调项汇总

需团队决策后回填的 `[COORDINATE]` 项：

| 编号 | 主题 | 影响范围 | 当前状态 |
|------|------|----------|----------|
| C1 | KDE 技术选型 | v2-unit-o | 待决策 |
| C2 | v1 接口兼容策略 | v2-unit-m | 待决策 |
| C3 | Parser 扩展范围 | v2-unit-l | 待决策 |
| C4 | 持续对齐与数据引擎接口 | v3-unit-s | 待决策 |
| C5 | 待测模型接入协议 | v3-unit-s, v4-unit-x | 待决策 |
| C6 | 反例搜索理论基础 | v4-unit-x | 研究项 |
| C7 | Python 工具链接受度 | v2-tool | 待决策 |
| C8 | WORM 存储选型 | v2-unit-o | 待决策 |

---

## 七、文档统计

| 类别 | 数量 |
|------|------|
| 核心框架文档 | 4 |
| 产品与商业文档 | 3 |
| 参考与规范文档 | 3 |
| 归档文档 | 26 |
| 开发规划 - specs | 43 |
| 开发规划 - plans | 30 |
| **总计** | **109** |
