# EvidencePackage

> 类型：组件
> 首次编译：2026-05-11

## 定义

SynthGen Core 产出的唯一标准封装格式。.evidence 文件（Zip 归档），自描述、不可变、可独立验证。是 SynthGen Core 与 Polymorphic-Twin 之间的唯一正式耦合点。

## 详情

**文件结构**：
```
evidence_<package_id>.evidence
├── manifest.json          # 全部元信息
├── data/                  # Parquet 分片数据
├── constraints/           # 约束自检报告
├── models/                # 生成器签名（可选）
└── semantic/              # 语义标注（可选）
```

**核心字段**：schema_version, package_id, generator_identity, data_grade, generation_request, data_profile, statistical_fidelity, conservative_tail_report, provenance, data_origin, drift_detection, checksum

**字段适用性标注**：每个字段标记 always / data_engaged / aggregation_present / drift_available / not_applicable

**版本演进**：
- v1（#8）：基础版，audit_immutability/statistical_fidelity/drift_detection/constraint_type_breakdown 均 not_applicable
- v2（#17）：新增 statistical_fidelity、constraint_type_breakdown、身份声明、audit_immutability: verified
- v3（#30）：新增模型版本链 provenance、完备度评分、偏差报告引用

**设计原则**：
- 自描述：包含解析自身所需的全部信息
- 不可变：构建完成后不可修改
- 可独立验证：无 SynthGen Core 运行时环境也可验证

## 关联实体

- [[polymorphic-twin]] — 检疫端点消费方
- [[data-grade]] — 必选字段
- [[tail-report]] — 条件必选字段
- [[audit-log]] — 审计可验证性

## 来源

- [[source-evidence-package-schema]] — 完整 Schema 定义
- [[source-roadmap]] — v1 #8 / v2 #17 / v4 #30 组件定义
