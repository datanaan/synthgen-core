# DataImporter

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

SynthGen Core 的数据导入组件，负责将外部 Parquet 文件加载到基表层，包含 Schema 兼容性校验与导入模式控制。

## 详情

DataImporter 读取 Parquet 文件，校验 Schema 兼容性（列名、类型、值域范围），按指定模式写入基表层：

- **Strict 模式**：任何不匹配导致整体失败
- **Lenient 模式**：不匹配行跳过，继续导入

导入结果返回 ImportResult（成功行数、跳过行数、table_id、snapshot_id、错误列表）。错误最多记录前 100 个。

支持预检查（check_compatibility）：不实际导入，只返回 CompatibilityReport。

## v1 范围

v1 仅支持 Parquet 格式导入，导入后自动创建快照。

## 关联实体

- [[schema-compatibility-checker]] — Schema 兼容性比对工具
- [[insert-only-base-table]] — 基表层 INSERT ONLY 语义
- [[synthlang-parser]] — LOAD DATA 语句解析

## 来源

- [[source-v1-unit-c-spec]] — Unit C 设计规范
