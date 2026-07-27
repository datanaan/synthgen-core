SynthGen Core v1 Unit B 设计规范：Storage Engine
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit B 实施计划
组件：#4 基础存储引擎
估算：1.5 周
依赖：无（Wave 1 起步组件，与 Unit A 并行）

---

## 一、本 Unit 交付什么

Unit B 是 v1 的存储地基——所有数据的持久化都依赖它。

交付物：
1. **StorageBackend 抽象接口**：定义读写、版本查询的统一接口
2. **ObjectStoreBackend**：v1 默认实现（对象存储 + Parquet + 自研元数据层）
3. **Parquet 读写**：列式数据的高效读写
4. **元数据层 v1**：版本索引 + Snapshot 索引 + 表注册
5. **基表层（INSERT ONLY）**：原始数据的不可变存储

---

## 二、StorageBackend 抽象接口

```cpp
namespace synthgen::storage {

// 写入返回的引用
struct SnapshotRef {
    std::string snapshot_id;  // UUID
    int64_t row_count;
    Timestamp created_at;
};

// 读取的迭代器
class ArrowBatchIterator {
public:
    virtual ~ArrowBatchIterator() = default;
    virtual Result<std::optional<ArrowBatch>> next() = 0;
};

// 读取参数
struct ScanPredicate {
    std::optional<std::string> column;
    std::optional<double> min_value;
    std::optional<double> max_value;
};

// 版本元数据
struct VersionMeta {
    std::string version_id;
    std::string table_id;
    Timestamp created_at;
    int64_t row_count;
    std::string schema_hash;  // Schema 定义时的 hash
};

// 抽象接口
class StorageBackend {
public:
    virtual ~StorageBackend() = default;

    // === 表管理 ===
    virtual Result<void> register_table(const std::string& table_id,
                                         const Schema& schema) = 0;
    virtual Result<bool> has_table(const std::string& table_id) const = 0;

    // === 写入（INSERT ONLY，写入后不可变）===
    virtual Result<SnapshotRef> append(const std::string& table_id,
                                        const ArrowBatch& batch) = 0;

    // === 读取 ===
    virtual Result<ArrowBatchIterator> scan(
        const std::string& table_id,
        const std::string& snapshot_id,
        const std::vector<std::string>& columns = {},
        const std::optional<ScanPredicate>& pred = std::nullopt) = 0;

    // === 版本 ===
    virtual Result<std::vector<VersionMeta>> list_versions(
        const std::string& table_id) const = 0;
    virtual Result<SnapshotRef> get_snapshot(
        const std::string& table_id,
        const std::string& version_tag) const = 0;

    // === 元数据 ===
    virtual Result<const Schema*> get_schema(
        const std::string& table_id) const = 0;
};

}  // namespace synthgen::storage
```

---

## 三、ObjectStoreBackend（v1 默认实现）

### 3.1 存储布局

```
<data_root>/
├── tables/
│   └── <table_id>/
│       ├── schema.json          # Schema 定义
│       ├── metadata.json        # 版本索引 + Snapshot 索引
│       ├── base/                # 基表层（INSERT ONLY）
│       │   ├── part-00001.parquet
│       │   ├── part-00002.parquet
│       │   └── ...
│       └── snapshots/          # 快照层（不可变）
│           ├── <snapshot_id>/
│           │   ├── data.parquet
│           │   └── provenance.json
│           └── ...
└── metadata/
    └── global.json              # 全局元数据
```

### 3.2 元数据层 v1

```cpp
namespace synthgen::storage {

struct TableMetadata {
    std::string table_id;
    Schema schema;
    std::string schema_hash;
    Timestamp created_at;
    std::vector<VersionMeta> versions;
    std::vector<SnapshotRef> snapshots;
};

class MetadataManager {
public:
    Result<void> create_table(const std::string& table_id, const Schema& schema);
    Result<const TableMetadata*> get_table(const std::string& table_id) const;
    Result<void> add_version(const std::string& table_id, VersionMeta version);
    Result<void> add_snapshot(const std::string& table_id, SnapshotRef snapshot);

    // 持久化
    Result<void> flush();    // 写入 metadata.json
    Result<void> reload();   // 从 metadata.json 重新加载
};

}  // namespace synthgen::storage
```

### 3.3 Parquet 读写

```cpp
namespace synthgen::storage {

class ParquetReader {
public:
    // 读取整个文件
    Result<ArrowBatch> read_all(const std::string& path);

    // 流式读取（大文件）
    Result<ArrowBatchIterator> read_streaming(const std::string& path);

    // 读取 Schema
    Result<Schema> read_schema(const std::string& path);

    // 列裁剪读取
    Result<ArrowBatch> read_columns(const std::string& path,
                                      const std::vector<std::string>& columns);
};

class ParquetWriter {
public:
    // 写入整个 batch
    Result<void> write(const std::string& path, const ArrowBatch& batch);

    // 追加写入（创建新 part 文件）
    Result<std::string> append(const std::string& dir, const ArrowBatch& batch);
};

}  // namespace synthgen::storage
```

### 3.4 基表层 INSERT ONLY 语义

**规则**：
1. 基表层只支持 INSERT（append），不支持 UPDATE/DELETE
2. 每个 append 创建新的 Parquet part 文件
3. 写入后的 part 文件不可变
4. 读取时按 part 文件顺序扫描

**实现**：
- `append()` → 创建新 part-NNNNN.parquet，更新 metadata
- `scan()` → 按序读取所有 part 文件

---

## 四、错误处理

| 错误场景 | 错误码 | 行为 |
|---------|--------|------|
| 表已存在 | kTableAlreadyExists | register_table 失败 |
| 表不存在 | kTableNotFound | 操作失败 |
| Parquet 文件损坏 | kDataCorruption | 读取失败 |
| 磁盘空间不足 | kStorageFull | 写入失败 |
| Schema 不匹配 | kSchemaMismatch | 导入失败（v1 由 Unit C 处理） |

---

## 五、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `StorageBackend::register_table()` | Unit C (Import) | 注册新表 |
| `StorageBackend::append()` | Unit C (Import), Unit D (Physics) | 写入数据 |
| `StorageBackend::scan()` | Unit D (Physics) | 读取基表数据 |
| `StorageBackend::list_versions()` | Unit F (Evidence) | 查询版本 |
| `ParquetReader::read_schema()` | Unit C (Import) | 读取文件 Schema |
| `MetadataManager` | Unit B 内部 | 元数据管理 |

---

## 六、v2+ 预留

| 能力 | v1 状态 | v2+ 预留 |
|------|---------|---------|
| atomic_write 事务 | ❌ 不实现 | v3 StorageBackend 接口扩展 |
| 快照层写入 | ✅ 基础版 | v2 增强（不可变 + provenance） |
| 审计日志 | ❌ 不实现 | v2 新增模块 |
| 模型层 | ❌ 不实现 | v3 新增模块 |
| WORM 合规 | ❌ 不实现 | v2 审计日志引入时 |
| 流控/背压 | ❌ 不实现 | v2+ 按需 |

---

## 七、Unit B 验收标准

### 7.1 功能验收

- [ ] StorageBackend 接口可编译，ObjectStoreBackend 可实例化
- [ ] register_table + has_table + get_schema 工作正确
- [ ] append 写入 Parquet 文件，scan 可读回全部数据
- [ ] 列裁剪读取正确
- [ ] 版本和 Snapshot 索引正确
- [ ] metadata.json 持久化和重新加载正确
- [ ] 基表层 INSERT ONLY 语义——重复 append 不覆盖

### 7.2 脚手架验收

- [ ] 每次 append/scan 产生 Trace span
- [ ] /metrics 暴露 storage_write_total / storage_read_total / storage_write_bytes
- [ ] 存储脚手架代码可作为模板引擎 v0.1 素材

### 7.3 错误测试验收

**Parquet 读写错误测试**：
- [ ] 不存在的文件返回 kReadFailed
- [ ] 损坏的 Parquet 文件（篡改 magic bytes）返回 kDataCorruption
- [ ] 空 Parquet 文件（0行）正确处理，返回空 ArrowBatch
- [ ] 超大文件（>可用内存）流式读取不崩溃
- [ ] 非法列名裁剪请求返回 kColumnNotFound
- [ ] 写入目录无权限返回 kWriteFailed
- [ ] 写入磁盘满返回 kStorageFull

**元数据错误测试**：
- [ ] 重复注册表返回 kTableAlreadyExists
- [ ] 查询不存在的表返回 kTableNotFound
- [ ] 元数据文件损坏后 reload 返回 kDataCorruption
- [ ] 元数据文件被删除后 reload 返回 kDataCorruption
- [ ] flush 时磁盘满返回 kStorageFull

**StorageBackend 错误测试**：
- [ ] 未注册表执行 append 返回 kTableNotFound
- [ ] 未注册表执行 scan 返回 kTableNotFound
- [ ] 不存在的 snapshot_id 返回 kSnapshotNotFound
- [ ] 空 batch append 正确（创建 0 行 part 文件）
- [ ] Schema 不匹配的 batch append 返回 kSchemaMismatch

### 7.4 边界条件测试

- [ ] 0 行 batch 写入 + 读回
- [ ] 1 行 batch 写入 + 读回
- [ ] 100000 行 batch 写入 + 流式读回
- [ ] 100 个 part 文件 append + scan 顺序正确
- [ ] 1000 列的 Parquet 文件读写
- [ ] 列名最大长度（256字符）
- [ ] 元数据 flush 1000 次后 reload 一致

### 7.5 测试验收

- [ ] 单元测试：接口 + Parquet 读写 + 元数据管理
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 每个 StorageErrorCode 至少 1 个测试用例触发
- [ ] 至少 25 个测试用例（18 错误 + 7 正向 + 边界）
- [ ] CI 自动运行
