# conservative_tail_report

> 类型：组件
> 首次编译：2026-05-11

## 定义

物理优先策略导致的尾部裁剪报告。从 v1 起即声明，每次 EvidencePackage 中必须包含（当 include_tail_report=true 或后筛选路径时必选）。

## 详情

**字段结构**：
- excluded_by_physics_ratio：被物理约束排除的比例
- excluded_region_features：排除区域的分布特征（均值、方差、与保留区域 KL 散度）
- constraint_type_breakdown：按约束类型分别统计排除率（值域/行间/聚合）
- tail_skew_assessment：narrower_than_real_world / unknown
- disclaimer：物理优先偏差的诚实声明

**版本对应**：
- v1 #7：值域约束排除率 + 偏差声明 + data_grade
- v3 #22：**增强呈现版**——将 v2 已有的误差界数据与 data_grade 联动呈现，含 fidelity_mismatch 标记、代偿模型状态

**聚合约束的影响**：含聚合约束时，排除率增加 aggregation_window_discard_rate（窗口级抛弃率），需单独标注。

## 关联实体

- [[physics-first]] — tail_report 是偏差声明的载体
- [[constraint-layering]] — 排除率按约束类型分解
- [[evidence-package]] — 作为条件必选字段

## 来源

- [[source-theory-framework]] — §6.4 尾部裁剪报告
- [[source-evidence-package-schema]] — §3.9 conservative_tail_report
- [[source-roadmap]] — v1 #7 / v3 #22
