# v2 一致性检查报告

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-consistency-check.md
> 编译日期：2026-05-14

## 摘要

v2 全部文档间一致性检查报告，覆盖接口定义比对、类型命名比对、错误码比对、文件路径比对、依赖关系比对五项检查。检查范围：v2 全部 Unit 的 spec + plan。结论：全部一致性检查通过，发现 3 个低严重度问题。

## 关键要点

- 接口定义一致性：InterRowEngine、AggregateEngine、ConstraintClassifier、ExecutionRouter、DataEngineV1、PostFilter、AuditLog、EvidencePackageV2 全部一致
- 类型命名一致性：17 个跨文档类型全部命名一致
- 错误码一致性：无冲突，全部使用 kPascalCase 规范
- 文件路径一致性：8 个产出文件路径无冲突
- 依赖关系一致性：K→J、M→J/K/L、N→M/O、P→M/N/O、O 独立并行
- 3 个低严重度问题：kClassificationError 语义差异、PhaseOneResult 跨 Unit 定义、post_filter_info 对齐（已解决）

## 提取的实体

- [[constraint-classifier]] — 分类结果跨文档一致性验证
- [[execution-router]] — 路由决策跨文档一致性
- [[inter-row-engine]] — 行间引擎跨文档一致性
- [[aggregate-engine]] — 聚合引擎跨文档一致性
