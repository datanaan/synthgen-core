# 元数据管理器 (MetadataManager)

> 类型：组件

## 定义

SynthGen Core 存储层的元数据管理中心。管理表注册信息、版本索引和 Snapshot 索引。使用内存数据结构 + JSON 持久化策略。

## 核心接口

```cpp
// src/storage/metadata.h
class MetadataManager {
public:
    Result<void> register_table(const std::string& table_id, const Schema& schema);
    Result<TableMetadata> get_table(const std::string& table_id) const;
    Result<void> add_version(const std::string& table_id, const VersionInfo& version);
    Result<void> add_snapshot(const std::string& table_id, const SnapshotInfo& snapshot);
    Result<void> flush();   // 序列化到 metadata.json
    Result<void> reload();  // 从 metadata.json 重新加载
};
```

## 持久化策略

- **原子写入**：先写临时文件，再 rename（POSIX 原子操作），避免写入中途崩溃导致数据损坏
- **flush**：将内存中的 TableMetadata 序列化为 JSON 并原子写入
- **reload**：从 metadata.json 重新加载到内存

## 校验规则

- 重复注册返回 `kTableAlreadyExists`
- 查询不存在的表返回 `kTableNotFound`

## 关联实体

- [[storage-engine]] — MetadataManager 是存储引擎的核心组件
- [[insert-only-base-table]] — 基表层通过 MetadataManager 管理版本索引
