# v4 功能完整检查 — 版本级组件覆盖度验证

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-completeness-check.md
> 编译日期：2026-05-14

## 摘要

v4 功能完整检查验证 v4 全部组件的覆盖度，涵盖五大维度。功能组件（明线）6/6 覆盖率 100%：#25 行数窗口、#26 分组时间窗口、#27 会话窗口、#28 完备度连续化评分、#29 反例搜索(research)、#30 EvidencePackage v3，每个组件均有设计规范+Unit spec+Unit plan。脚手架（暗线）3/3 活跃项覆盖率 100%：Explain 增强、Trace 增强、可观测性。工具线 1/1 活跃项覆盖率 100%：Trace 分析 v0.3。诚实声明 5/5 覆盖率 100%。协调项 2/2 覆盖率 100%。全部检查通过。

## 关键要点

- 功能组件覆盖率 6/6（100%）：每个组件均有设计规范、spec、plan 三层文档
- 脚手架覆盖率 3/3（100%）：Explain/Trace/可观测性，N/A 项（确定性测试/CI/CD/错误注入）按路线图不在 v4 范围
- 工具线覆盖率 1/1（100%）：仅 Trace 分析活跃，模板引擎/测试辅助/Schema 校验器已在 v2 交付
- 诚实声明覆盖率 5/5（100%）：完备度非布尔、布尔是1.0特例、反例搜索research、高评分!=高质量、fidelity_score是参考值
- 协调项覆盖率 2/2（100%）：C5 待测模型接入协议、C6 反例搜索理论基础

## 提取的实体

- [[source-v4-unit-u-spec]] — Unit U 设计规范
- [[source-v4-unit-v-spec]] — Unit V 设计规范
- [[source-v4-unit-w-spec]] — Unit W 设计规范
- [[source-v4-unit-x-spec]] — Unit X 设计规范
