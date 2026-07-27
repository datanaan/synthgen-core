# INSERT ONLY 基表层

> 类型：设计约束

## 定义

SynthGen Core 基表层的核心设计约束。基表层只允许 append（追加写入），不支持修改（UPDATE）和删除（DELETE）。这是数据库内核级约束，不是应用层模拟。

## 设计原则

- **数据库内核原生特性**：Schema enforcement、immutable audit、version time-travel 必须是内核原生功能
- **"以后迁移到内核"不可接受**：一旦在应用层实现，几乎不可能迁移
- **审计友好**：INSERT ONLY 保证数据不可篡改，是哈希链审计日志的基础

## v1 实现

- `ObjectStoreBackend::append()` -> ParquetWriter.append -> MetadataManager.add_version
- 多次 append 产生多个 part 文件
- scan 按 version 顺序读取全部 part 文件
- 数据顺序正确（按 append 顺序）

## 验证要点

- 多次 append 产生多个 part 文件
- scan 读回全部 append 的数据
- 数据顺序正确
- 0 行 batch append + scan 正确处理

## 关联实体

- [[storage-engine]] — 存储引擎实现 INSERT ONLY 约束
- [[audit-log]] — 哈希链审计日志依赖数据不可变性
