# v1 Unit B Spec — Storage Engine

> 来源：raw/specs/v1-unit-b-design.md
> 编译日期：2026-05-14

## 摘要
Unit B 是 v1 的存储地基，交付 StorageBackend 抽象接口及其默认实现 ObjectStoreBackend。采用对象存储 + Parquet 列式格式 + 自研元数据层的架构。基表层 INSERT ONLY（写入后不可变），支持版本索引和 Snapshot 索引。包含 ParquetReader/ParquetWriter 读写器、MetadataManager 元数据管理器。估算 1.5 周，无前置依赖，与 Unit A 并行。

## 关键要点
- StorageBackend 抽象接口定义表管理（register_table/has_table）、写入（append INSERT ONLY）、读取（scan + 列裁剪）、版本查询（list_versions/get_snapshot）四大类操作
- ObjectStoreBackend 默认实现使用文件系统目录布局：tables/<table_id>/ 下含 schema.json、metadata.json、base/（Parquet part 文件）、snapshots/
- 元数据层 v1 包含版本索引、Snapshot 索引和表注册，支持 flush/reload 持久化
- 至少 25 个测试用例，错误测试占比 >= 30%

## 提取的实体
- [[storage-engine]] — 基础存储引擎，包含 StorageBackend 接口和 ObjectStoreBackend 实现
- [[parquet-reader-writer]] — Parquet 文件读写器，支持流式读写和列裁剪
- [[metadata-manager]] — 元数据管理器，维护版本索引、Snapshot 索引和表注册信息
