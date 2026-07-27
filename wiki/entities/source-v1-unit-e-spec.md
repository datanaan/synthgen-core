# v1 Unit E Spec — Validation + tail_report

> 来源：raw/specs/v1-unit-e-design.md
> 编译日期：2026-05-14

## 摘要

Unit E 实现 v1 的验证与报告系统。ValueRangeValidator 作为安全网逐行验证采样结果是否在矩形值域内（v1 纯物理路径应 100% 通过），TailReportBuilder 生成 tail_report（偏差声明 + 排除率 + data_grade）。估算 1 周，依赖 Unit D。

## 关键要点

- ValueRangeValidator：逐行验证，返回 ValidationResult（通过率、失败详情）
- v1 诚实声明：纯物理路径下验证器应 100% 通过，越界记录到 tail_report
- TailReportBuilder：组装 conservative_tail_report，含偏差声明与排除率
- data_grade 在 v1 固定为 physics_guaranteed
- 验证器不删除——即使物理引擎保证在值域内，安全网仍保留

## 提取的实体

- [[value-range-validator]] — 值域约束验证器（安全网）
- [[tail-report]] — conservative_tail_report 尾部裁剪报告
- [[exclusion-rate]] — 排除率指标
- [[data-grade]] — 数据等级枚举体系
