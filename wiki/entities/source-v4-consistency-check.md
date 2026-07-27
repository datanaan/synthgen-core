# v4 一致性检查 — 全部 Unit 与上游文档一致性验证

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-consistency-check.md
> 编译日期：2026-05-14

## 摘要

v4 一致性检查验证 v4 全部 Unit（U-X）与上游设计规范的接口一致性、依赖一致性、诚实声明一致性和协调项一致性。检查范围涵盖 Unit U（WindowTypeV2 枚举、RowsWindowDef/PartitionWindowDef 字段）、Unit V（SessionWindowDef 字段、SessionWindowEngine 接口）、Unit W（CompletenessScore 字段、CompletenessScorer::score 签名、should_allow_full_function 签名）、Unit X（CounterExampleResult/EvidencePackageV3/ModelVersionProvenance 字段）。所有依赖关系（U<-v2#11、V<-U、W<-v2#13、X<-W/v2#13）、诚实声明（完备度非布尔/布尔是1.0特例/反例搜索research）和协调项（C5/C6）均通过一致性验证。

## 关键要点

- 接口一致性：4 个 Unit 全部接口定义与设计规范完全一致
- 依赖一致性：5 条依赖关系全部正确
- 诚实声明一致性：完备度非布尔（0.0-1.0）、布尔是 1.0 特例（is_fully_constrained）、反例搜索 research 三态
- 协调项一致性：C5（待测模型接入协议）和 C6（反例搜索理论基础）均正确标注
- 检查结论：全部通过

## 提取的实体

- [[source-v4-unit-u-spec]] — Unit U 设计规范
- [[source-v4-unit-v-spec]] — Unit V 设计规范
- [[source-v4-unit-w-spec]] — Unit W 设计规范
- [[source-v4-unit-x-spec]] — Unit X 设计规范
