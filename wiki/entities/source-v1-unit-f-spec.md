# v1 Unit F Spec — EvidencePackage Builder v1

> 来源：raw/specs/v1-unit-f-design.md
> 编译日期：2026-05-14

## 摘要

Unit F 实现 EvidencePackage 构建器——将生成结果、验证结果、tail_report 组装成完整的证据包。核心职责包括 Schema 验证、诚实声明传递、字段适用性标注。估算 1 周，依赖 Unit E。

## 关键要点

- EvidencePackage v1 Schema：含 schema_version、constraint_summary、exclusion_rate、data_grade、row_count、provenance、trace_spans
- Provenance 记录完整溯源：数据源、约束、生成参数
- trace_spans 数组记录每个组件的执行 span
- v1 诚实声明：exclusion_rate = 0.0, data_grade = physics_guaranteed
- 构建后自动验证字段完整性（Schema validation）
- 不适用字段标记为 not_applicable

## 提取的实体

- [[evidence-package]] — EvidencePackage 数据包协议
- [[span-guard]] — Trace span RAII 守卫
