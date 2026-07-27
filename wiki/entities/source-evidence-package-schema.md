# EvidencePackage Schema v1.2

> 来源：docs/reference/EvidencePackage Schema 定义 v1.2.md
> 编译日期：2026-05-11

## 摘要

SynthGen Core 与 Polymorphic-Twin 之间的数据接口规范。EvidencePackage 是 SynthGen Core 产出的唯一标准封装格式，为 .evidence 文件（Zip 归档），自描述、不可变、可独立验证。定义了必选/条件必选/可选三类字段，以及 Polymorphic-Twin Core 检疫端点的 6 项检查清单。

## 关键要点

- **文件格式**：.evidence = Zip 归档，含 manifest.json + data/ + constraints/ + models/ + semantic/
- **字段分类**：必选（schema_version, package_id, data_grade 等）、条件必选（statistical_fidelity）、可选（semantic_annotations）
- **data_grade 枚举**：physics_guaranteed / statistics_guaranteed / limited_fidelity / physics_unguaranteed / truncated / degraded / unqualified
- **检疫检查 6 项**：格式完整性、Schema 版本兼容、哈希完整性、data_origin 合法性、data_grade 合规、约束自检一致性
- **版本兼容**：MAJOR.MINOR，消费者接受 MINOR 升级，拒绝 MAJOR 不匹配
- **data_origin**：production_raw / experiment / public_dataset / archive；derived_from_validation 硬拒绝

## 提取的实体

- [[evidence-package]] — EvidencePackage 数据包协议
- [[data-grade]] — 数据等级枚举
- [[polymorphic-twin]] — 检疫端点消费方
- [[tail-report]] — conservative_tail_report 字段
