# Polymorphic-Twin

> 类型：项目
> 首次编译：2026-05-11

## 定义

数字孪生可信治理基础设施。SynthGen Core 的姊妹系统，两者通过 EvidencePackage 标准协议形成"生成-审判"协作。

## 详情

**与 SynthGen Core 的关系**：
- SynthGen Core：证据提供者（制造候选数据）
- Polymorphic-Twin Core：证据审判者（验证、定级、赋予链路权限）
- 两者是独立系统，可独立部署、独立销售、独立服务客户

**协作协议**：
- EvidencePackage Schema — Polymorphic-Twin 定义，SynthGen Core 遵守
- CallerIdentity 格式 — Polymorphic-Twin 定义
- 约束卡片格式 — Polymorphic-Twin 定义，SynthGen Core 在生成阶段引用
- 数据版本溯源码 — 双方协商

**检疫端点**：`/api/v1/core/quarantine/*` 接收 EvidencePackage，执行 6 项检查（格式完整性、Schema版本、哈希完整性、data_origin合法性、data_grade合规、约束自检一致性）

**SynthGen Core 侧约束**：
- 不硬编码 Polymorphic-Twin 内部逻辑
- 提供两种输出模式：标准 EvidencePackage + 通用开放格式（Parquet/HDF5）
- data_origin = derived_from_validation 的包禁止进入任何链路

## 关联实体

- [[synthgen-core]] — 姊妹系统
- [[evidence-package]] — 唯一正式耦合点

## 来源

- [[source-product-positioning]] — §1-6 产品定位与协作关系
