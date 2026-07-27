# v1 Unit C Spec — Data Import

> 来源：raw/specs/v1-unit-c-design.md
> 编译日期：2026-05-14

## 摘要

Unit C 实现数据导入功能——将外部 Parquet 文件的数据加载到 SynthGen 的基表层。核心交付物包括 DataImporter（读取 Parquet、校验 Schema 兼容性、写入基表层）、Schema 兼容性校验器、LOAD DATA 执行器。估算 0.5 周，依赖 Unit A（Parser + Type System）和 Unit B（Storage Engine）。

## 关键要点

- DataImporter 支持 Strict（不匹配即失败）和 Lenient（跳过不匹配行）两种导入模式
- Schema 兼容性校验比对列名、类型、值域范围，生成 CompatibilityReport
- 导入结果生成快照 ID，遵循 INSERT ONLY 语义
- 错误最多记录前 100 个，避免内存溢出

## 提取的实体

- [[data-importer]] — Parquet 数据导入器，负责 Schema 校验与数据写入
- [[schema-compatibility-checker]] — Schema 兼容性比对工具
- [[insert-only-base-table]] — 基表层 INSERT ONLY 语义
