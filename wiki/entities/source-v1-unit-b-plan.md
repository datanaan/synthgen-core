# v1 Unit B Plan — Storage Engine

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-b-plan.md
> 编译日期：2026-05-14

## 摘要

Unit B 是 v1 的存储地基，估算 1.5 周，无外部依赖（可与 Unit A 并行）。包含 6 个 Task、16 个步骤：公共类型定义（Result<T> + Error + 存储错误码）、Parquet 读写（ParquetReader/ParquetWriter，基于 Apache Arrow + Parquet）、元数据层 v1（TableMetadata + MetadataManager，内存管理 + JSON 持久化）、StorageBackend 抽象接口 + ObjectStoreBackend 默认实现、脚手架集成、标准测试数据集（sensor_1000.parquet）。

## 关键要点

- StorageBackend 是抽象接口，ObjectStoreBackend 是本地文件系统实现
- 基表层 INSERT ONLY：只允许 append，不支持修改/删除
- 元数据持久化使用原子写入（先写临时文件，再 rename）
- Parquet 读写支持列裁剪和流式读取
- 标准测试数据集 sensor_1000.parquet 包含 1000 行传感器数据

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `StorageErrorCode` | `src/storage/error.h` | 存储层错误码枚举（kTableAlreadyExists, kDataCorruption 等 8 种） |
| `ParquetReader` | `src/storage/parquet_reader.h/.cpp` | Parquet 文件读取（read_all, read_streaming, read_schema, read_columns） |
| `ParquetWriter` | `src/storage/parquet_writer.h/.cpp` | Parquet 文件写入（write, append） |
| `TableMetadata` / `MetadataManager` | `src/storage/metadata.h/.cpp` | 元数据管理，内存 + JSON 持久化 |
| `StorageBackend` | `src/storage/backend.h` | 抽象存储后端接口 |
| `ObjectStoreBackend` | `src/storage/object_store_backend.h/.cpp` | 本地文件系统实现 |

### 存储错误码

```cpp
enum class StorageErrorCode {
    kTableAlreadyExists, kTableNotFound, kDataCorruption,
    kStorageFull, kSchemaMismatch, kSnapshotNotFound,
    kWriteFailed, kReadFailed,
};
```

### 元数据持久化策略

- 内存中维护 TableMetadata 结构
- flush：序列化为 JSON，先写临时文件再 rename（POSIX 原子操作）
- reload：从 metadata.json 重新加载

### 测试策略

- Parquet 读写测试 8+ 用例
- 元数据测试 6+ 用例
- ObjectStoreBackend 集成测试 15+ 用例，错误测试占比 >= 30%
- 边界测试：0 行 batch、1 行 batch、1000 列 batch、100 个 part 文件

### 标准测试数据集

sensor_1000.parquet：1000 行传感器数据，包含 timestamp、temperature、pressure、vibration、status 五列，各列有特定分布参数。

## 提取的实体

- [[storage-engine]] — 存储引擎（已存在）
- [[result-pattern]] — Result<T> 错误处理模式（已创建）
- [[insert-only-base-table]] — 基表层只允许 append，不支持修改/删除（新实体）
- [[metadata-manager]] — 元数据管理器，内存 + JSON 持久化，原子写入（新实体）
