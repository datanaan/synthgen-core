EvidencePackage Schema 定义 v1.2
文档性质：SynthGen Core 与 Polymorphic-Twin 之间的数据接口规范
当前版本：v1.2
上一版本：v1.1（理论框架 v1.2 中隐式定义，本版本正式独立成文）
适用对象：Polymorphic-Twin Core 检疫端点、第三方验证系统、数据归档层
Schema 状态：核心字段定稿，扩展字段预留机制已定义

一、设计原则
1.1 协议定位
EvidencePackage 是 SynthGen Core 产出的唯一标准封装格式。任何外部系统（Polymorphic-Twin Core、第三方验证工具、数据归档层）通过解析此格式获取生成数据及其完整元数据。

核心要求：

自描述：包内包含解析自身所需的全部信息，不依赖外部上下文。

不可变：包一旦构建完成，其内容不可修改。任何修正需生成新包。

可独立验证：包的完整性和数据质量声明可在无 SynthGen Core 运行时的环境中验证。

1.2 字段分类
EvidencePackage 的所有字段分为三类：

字段类别	含义	示例
必选字段	所有 EvidencePackage 必须包含。未提供则包无效，Polymorphic-Twin Core 拒绝接收	schema_version, generator_identity, data_grade
条件必选	取决于生成模式，特定条件下必须提供	统计生成时必须提供 statistical_fidelity
可选字段	提供额外信息，不影响包的有效性	semantic_annotations, custom_metadata
1.3 版本兼容策略
schema_version 遵循语义化版本（MAJOR.MINOR）。

MAJOR 变更：字段删除、必选字段新增、字段语义不兼容变更。

MINOR 变更：可选字段新增、字段长度扩展、新增枚举值。

消费者必须接受 MINOR 版本升级后的包（前向兼容）。拒绝 MAJOR 版本高于自身支持的包，返回明确的版本不匹配错误。

二、EvidencePackage 顶级结构
2.1 文件格式
EvidencePackage 为单个 .evidence 文件，其内部为 Zip 归档。最大单包体积由生成数据量表决定，无固定上限，但建议单包不超过 10GB 以便网络传输和端点处理。

2.2 内部目录结构
text
evidence_<package_id>.evidence
├── manifest.json              # 顶级清单，包含除数据本体外的所有元信息
├── data/                      # 数据本体目录
│   ├── part-00000.parquet     # 列式数据文件（可多个分片）
│   ├── part-00001.parquet
│   └── ...
├── constraints/               # 约束自检报告
│   └── constraint_report.yaml
├── models/                    # 生成器签名（可选）
│   └── generator_signature.json
└── semantic/                  # 语义标注（可选）
    └── annotations.json
2.3 manifest.json 结构
json
{
  "schema_version": "1.2",
  "package_id": "evp_20260510_a3f2b8c1",
  "created_at": "2026-05-10T14:32:17Z",
  "generator_identity": { /* 见2.4节 */ },
  "data_grade": "physics_guaranteed",
  "generation_request": { /* 见2.5节 */ },
  "data_profile": { /* 见2.6节 */ },
  "statistical_fidelity": { /* 见2.7节 */ },
  "conservative_tail_report": { /* 见2.8节 */ },
  "constraint_report_ref": "constraints/constraint_report.yaml",
  "provenance": { /* 见2.9节 */ },
  "data_origin": "production_raw",
  "drift_detection": { /* 见2.10节 */ },
  "semantic_annotations_ref": "semantic/annotations.json",
  "checksum": {
    "algorithm": "SHA256",
    "manifest_hash": "b4c5d6e7...",
    "data_parts_hash": ["a1b2c3...", "d4e5f6..."]
  }
}
三、核心字段定义
3.1 schema_version
类型：string (MAJOR.MINOR)

类别：必选

说明：EvidencePackage 格式版本。消费者据此决定解析策略。

示例："1.2"

3.2 package_id
类型：string

类别：必选

说明：全局唯一标识符。建议格式：evp_<YYYYMMDD>_<8位随机hex>

示例："evp_20260510_a3f2b8c1"

3.3 created_at
类型：string (ISO 8601 UTC)

类别：必选

说明：EvidencePackage 构建完成的时间戳。

示例："2026-05-10T14:32:17Z"

3.4 generator_identity
类别：必选

说明：生成此包的 SynthGen Core 实例和模型的身份信息。

json
{
  "product": "SynthGen Core",
  "product_version": "1.0.0",
  "instance_id": "synthgen-prod-01",
  "model": {
    "model_id": "gen_model_v2",
    "model_version": 2,
    "model_hash": "SHA256:9f8e7d6c...",
    "training_data_range": {
      "start": "2026-01-01T00:00:00Z",
      "end": "2026-05-01T00:00:00Z"
    }
  },
  "generation_engine": {
    "physics_engine": "builtin_v1",
    "data_engine": "builtin_v1",
    "constraint_engine": "builtin_v1",
    "execution_mode": "two_phase"
  }
}
字段约束：

model.model_version：与 SynthGen Core 内部版本链中的版本号一致。

model.model_hash：生成模型参数的哈希，用于后续追溯和验证。

generation_engine.execution_mode：枚举值 row_by_row / stateful_batch / two_phase，由约束类型决定。

3.5 generation_request
类别：必选

说明：触发此包生成的完整请求快照，用于复现和审计。

json
{
  "from_table": "sensor_log",
  "constraints": ["wind_safety", "temporal_safety"],
  "mode": "constrained",
  "fallback": "physics_only",
  "limit": 1000,
  "seed": 42,
  "include_tail_report": true,
  "request_id": "req_a1b2c3d4"
}
3.6 data_grade
类型：string (枚举)

类别：必选

说明：此包的数据等级。Polymorphic-Twin Core 据此决定允许进入的链路等级。

枚举值与含义：

值	含义	允许进入的链路
physics_guaranteed	物理合法性已由约束卡片验证通过	production / diagnostic / shadow / sandbox
statistics_guaranteed	统计逼真性由数据驱动保证，物理合法性部分验证或不可验证	diagnostic / shadow / sandbox
limited_fidelity	生成效率显著下降，统计逼真性不可保证	sandbox
physics_unguaranteed	约束缺失，物理合法性未验证。非安全关键场景可用	sandbox（需豁免）/ diagnostic以下
truncated	超时或行数不足导致生成中断，已有合法行在此包中	需人工审核后决定
degraded	包内部分数据的特定维度的束未经验证	仅 sandbox，且需标记未验证维度
unqualified	仅格式转换+扰动，物理和统计合法性均不可保证	不可进入任何链路
3.7 data_profile
类别：必选

说明：生成数据的统计概要，供消费者快速判断数据适用性。

json
{
  "row_count": 1000,
  "column_count": 5,
  "columns": [
    {
      "name": "timestamp",
      "type": "datetime",
      "min": "2026-01-01T00:00:00Z",
      "max": "2026-05-01T00:00:00Z",
      "null_count": 0
    },
    {
      "name": "wind_speed",
      "type": "float64",
      "min": 0.0,
      "max": 24.8,
      "mean": 12.3,
      "std": 6.7,
      "null_count": 0
    }
  ],
  "data_format": "parquet",
  "data_parts": ["data/part-00000.parquet"]
}
字段约束：

column.type 使用 Apache Arrow 类型系统命名。

data_format 固定为 "parquet"。

data_parts 列出数据目录下的所有分片文件。读取方按顺序拼接。

3.8 statistical_fidelity
类别：条件必选（当数据引擎参与生成时必选）

说明：统计签名忠实性的条件保证与实际测量值。与理论框架 v1.3 第4.2节对齐。

json
{
  "guarantee_tier": {
    "unconditional": {
      "value_range": true,
      "mean": true,
      "variance": true
    },
    "conditional": {
      "bivariate_correlation": {
        "applicable": true,
        "condition_met": true,
        "theoretical_error_bound": 0.2,
        "measured_error": 0.12,
        "fidelity_mismatch": false
      },
      "autocorrelation": {
        "applicable": true,
        "condition_met": false,
        "reason": "time_series_too_short",
        "theoretical_error_bound": null,
        "measured_error": null,
        "status": "not_guaranteed"
      }
    },
    "unguaranteed": ["higher_order_dependencies", "causal_structure"]
  }
}
字段约束：

条件保证中的每个维度，若 condition_met 为 false，则该维度自动标记为 not_guaranteed。

fidelity_mismatch 为 true 时，表示实测误差超出理论误差界，需用户关注。

3.9 conservative_tail_report
类别：条件必选（当 include_tail_report 为 true 或后筛选路径时必选）

说明：物理优先策略导致的尾部裁剪报告。与理论框架 v1.3 第6.4节对齐。

json
{
  "excluded_by_physics_ratio": 0.12,
  "excluded_region_features": {
    "mean_shift": {"wind_speed": 18.3, "temperature": -15.2},
    "kl_divergence_from_retained": 0.45
  },
  "constraint_type_breakdown": {
    "value_range": {"excluded_ratio": 0.06, "top_offending_constraint": "wind_speed > 25"},
    "inter_row": {"excluded_ratio": 0.04, "top_offending_constraint": "vibration_diff > 5.0"},
    "aggregation": {"excluded_ratio": 0.02, "discarded_window_count": 3}
  },
  "tail_skew_assessment": "narrower_than_real_world",
  "disclaimer": "此数据世界因物理优先选择而拥有比真实物理世界更窄的尾部风险谱。此偏差是理论选择的结果，非工程缺陷。用户有权在知情后自行判断数据用于下游任务的适宜性。"
}
字段约束：

constraint_type_breakdown：按约束类型（值域/行间/聚合）分别统计排除率。含聚合约束时，aggregation.discarded_window_count 报告被丢弃的窗口数。

excluded_region_features 中的统计量基于被排除的样本计算，用于对比。

3.10 constraint_report_ref
类型：string (相对路径)

类别：必选

说明：指向包内约束自检报告的路径。

约束自检报告格式（constraints/constraint_report.yaml）：

yaml
constraint_report:
  package_id: "evp_20260510_a3f2b8c1"
  constraints_loaded: ["wind_safety", "temporal_safety"]
  per_constraint:
    - name: "wind_safety"
      status: "applied"
      violations: 87
      violation_rate: 0.087
      unverified_dimensions: []
    - name: "temporal_safety"
      status: "partially_applied"
      violations: 42
      violation_rate: 0.042
      unverified_dimensions: ["temperature"]  # 约束真空
  overall:
    total_rows_generated: 1087
    total_rows_retained: 958
    overall_exclusion_rate: 0.119
3.11 provenance
类别：必选

说明：完整的生成溯源链，用于审计和复现。

json
{
  "generation_parameters": {
    "seed": 42,
    "limit": 1000,
    "mode": "constrained",
    "fallback": "physics_only",
    "batch_size": 128,
    "timeout_seconds": 30
  },
  "model_versions": [
    {
      "model_id": "gen_model_v2",
      "training_data_sources": [
        {"source": "/data/sensors/2026-01.parquet", "data_origin": "production_raw"},
        {"source": "/data/sensors/2026-02.parquet", "data_origin": "production_raw"}
      ],
      "constraint_card_versions": {
        "wind_safety": "v3",
        "temporal_safety": "v1"
      }
    }
  ],
  "generator_invocation": {
    "generator_version": "SynthGen Core 1.0.0",
    "execution_id": "exec_7g8h9i0j",
    "started_at": "2026-05-10T14:30:00Z",
    "completed_at": "2026-05-10T14:32:17Z",
    "execution_path": "two_phase",
    "exclusion_rate_estimate": 0.08,
    "exclusion_rate_actual": 0.119
  }
}
3.12 data_origin
类型：string (枚举)

类别：必选

说明：用于持续对齐的训练数据的来源标记。Polymorphic-Twin Core 在检疫时检查此字段，确保数据来源符合协议边界。与理论框架 v1.3 第5.4节对齐。

值	含义
production_raw	生产系统原始传感器数据
experiment	独立测试台或实验采集数据
public_dataset	公开或行业标准数据集
archive	历史归档数据
derived_from_validation	包含验证结论的衍生数据（硬拒绝用于更新）
若此字段为 derived_from_validation，Polymorphic-Twin Core 拒绝此包进入任何链路，并记录协议违规。

3.13 drift_detection
类别：可选（当生成过程中有漂移检测行为时提供）

说明：漂移检测的状态报告。

json
{
  "drift_detection_mode": "joint",
  "drift_detected": false,
  "drift_score": {
    "mmd": 0.03,
    "ks_pvalue": 0.42
  },
  "baseline_distribution": "core_model",
  "compensatory_active": false
}
若 compensatory_active 为 true，则此包的数据由代偿通道生成，distribution_basis 为 compensatory。

3.14 semantic_annotations_ref
类型：string (相对路径，可选)

类别：可选

说明：指向语义标注文件的路径。语义标注为 AI 生成，未经人工确认。

json
{
  "annotations": [
    {
      "target_rows": [0, 150],
      "content": "风速在样本 50-120 之间持续上升，突破 20m/s 后温度下降至 -35°C，标记为潜在结冰工况。",
      "confidence": "low",
      "generated_by": "llm_compiler_v1"
    }
  ],
  "disclaimer": "此语义标注由自动化系统生成，未经人工确认。仅供参考。"
}
3.15 checksum
类别：必选

说明：包的完整性校验信息。

json
{
  "algorithm": "SHA256",
  "manifest_hash": "b4c5d6e7...",
  "data_parts_hash": ["a1b2c3...", "d4e5f6..."]
}
验证流程：

读取 manifest.json，计算 manifest_hash 并与声明值比较。

对 data/ 目录下每个分片文件计算哈希，依次与 data_parts_hash 比较。

任意哈希不匹配 → 包已损坏或被篡改，Polymorphic-Twin Core 拒绝接收。

四、Polymorphic-Twin Core 的检疫检查清单
Polymorphic-Twin Core 的 /api/v1/core/quarantine/validate 端点对此包进行以下检查：

检查项	判断逻辑	失败行为
格式完整性	manifest.json 存在且可解析；必选字段齐全	拒绝接收，返回字段缺失列表
Schema 版本兼容	schema_version MAJOR ≤ 端点支持的最大 MAJOR	拒绝接收，返回版本不匹配
哈希完整性	checksum 全部匹配	拒绝接收，返回哈希不匹配详情
data_origin 合法性	data_origin ≠ derived_from_validation	拒绝接收，记录协议违规
data_grade 合规	data_grade 在允许的链路等级内	降级至允许的最高链路，标记降级原因
约束自检一致性	constraint_report 中的 unverified_dimensions 与 data_grade 一致	要求 SynthGen Core 重新构建包
五、示例：最小有效 EvidencePackage
json
{
  "schema_version": "1.2",
  "package_id": "evp_20260510_minimal",
  "created_at": "2026-05-10T14:00:00Z",
  "generator_identity": {
    "product": "SynthGen Core",
    "product_version": "1.0.0",
    "instance_id": "dev-01",
    "model": {
      "model_id": "gen_model_v1",
      "model_version": 1,
      "model_hash": "SHA256:abc...",
      "training_data_range": {
        "start": "2026-01-01T00:00:00Z",
        "end": "2026-01-31T00:00:00Z"
      }
    },
    "generation_engine": {
      "physics_engine": "builtin_v1",
      "data_engine": "builtin_v1",
      "constraint_engine": "builtin_v1",
      "execution_mode": "row_by_row"
    }
  },
  "data_grade": "physics_guaranteed",
  "generation_request": {
    "from_table": "sensor_log",
    "constraints": ["wind_safety"],
    "mode": "constrained",
    "limit": 100,
    "seed": 42
  },
  "data_profile": {
    "row_count": 100,
    "column_count": 2,
    "columns": [
      {"name": "timestamp", "type": "datetime", "min": "...", "max": "...", "null_count": 0},
      {"name": "wind_speed", "type": "float64", "min": 0.0, "max": 24.8, "mean": 12.3, "std": 6.7, "null_count": 0}
    ],
    "data_format": "parquet",
    "data_parts": ["data/part-00000.parquet"]
  },
  "constraint_report_ref": "constraints/constraint_report.yaml",
  "provenance": {
    "generation_parameters": {"seed": 42, "limit": 100, "mode": "constrained"},
    "model_versions": [{
      "model_id": "gen_model_v1",
      "training_data_sources": [{"source": "/data/sensors/2026-01.parquet", "data_origin": "production_raw"}],
      "constraint_card_versions": {"wind_safety": "v1"}
    }]
  },
  "data_origin": "production_raw",
  "checksum": {
    "algorithm": "SHA256",
    "manifest_hash": "e5f6a7b8...",
    "data_parts_hash": ["c9d0e1f2..."]
  }
}
最小包中，statistical_fidelity 和 conservative_tail_report 省略（因为纯物理生成，无数据驱动参与，无后筛选排除率报告需求）。其余必选字段必须存在。

文档结束

本 Schema 定义了 EvidencePackage 的精确结构。Polymorphic-Twin Core 检疫端点、第三方验证系统、数据归档层均以此为准进行解析和验证。