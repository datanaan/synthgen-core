SynthGen Core v1 Unit B 实施计划：Storage Engine
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit B 设计规范 v1.0
估算：1.5 周
依赖：无（与 Unit A 并行）

---

## 概述

Unit B 是 v1 的存储地基。交付 StorageBackend 抽象接口、ObjectStoreBackend 默认实现、Parquet 读写和元数据层 v1。

---

## Task 1：公共类型定义

**目标**：定义存储层公共类型

### Step 1.1：Result<T> + Error 类型

**做什么**：实现通用 Result<T> 和 Error 类型（如果 Unit A 尚未实现，则在此实现；如果 Unit A 已实现，则复用）

**产出**：`src/common/result.h`, `src/common/error.h`

**验收**：Result<T> 可用，Error 类型覆盖存储错误码

### Step 1.2：存储错误码定义

**做什么**：定义存储层错误码枚举

**产出**：`src/storage/error.h`

```cpp
enum class StorageErrorCode {
    kTableAlreadyExists,
    kTableNotFound,
    kDataCorruption,
    kStorageFull,
    kSchemaMismatch,
    kSnapshotNotFound,
    kWriteFailed,
    kReadFailed,
};
```

**验收**：错误码覆盖 v1 存储场景

---

## Task 2：Parquet 读写

**目标**：基于 Apache Arrow + Parquet 实现列式读写

### Step 2.1：Arrow 集成

**做什么**：CMake 配置 Arrow/Parquet 依赖，基础编译验证

**产出**：CMakeLists.txt 更新

**验收**：Arrow/Parquet 可编译链接

### Step 2.2：ParquetReader 实现

**做什么**：实现 ParquetReader（read_all, read_streaming, read_schema, read_columns）

**产出**：`src/storage/parquet_reader.h`, `src/storage/parquet_reader.cpp`

**关键逻辑**：
- 使用 Arrow IPC 或 Parquet C++ API
- 支持列裁剪（只读指定列）
- 流式读取支持大文件
- Schema 读取：Parquet schema → SynthGen Schema 转换

**验收**：
- [ ] 能读取标准 Parquet 文件
- [ ] 列裁剪正确
- [ ] 流式读取工作

### Step 2.3：ParquetWriter 实现

**做什么**：实现 ParquetWriter（write, append）

**产出**：`src/storage/parquet_writer.h`, `src/storage/parquet_writer.cpp`

**验收**：
- [ ] 能写入 ArrowBatch 到 Parquet
- [ ] append 创建新 part 文件
- [ ] 写入的文件可被 ParquetReader 读回

### Step 2.4：Parquet 读写测试

**做什么**：编写 Parquet 读写单元测试

**产出**：`tests/unit/parquet_io_test.cpp`

**测试用例**（至少 8 个）：
- 写入+读回全列
- 写入+读回指定列
- 写入+读回 Schema
- 追加写入多个 part
- 空文件处理
- 大文件流式读取
- 不存在文件错误
- 损坏文件错误

**验收**：8+ 测试用例全通过

---

## Task 3：元数据层 v1

**目标**：实现元数据管理（版本索引 + Snapshot 索引 + 表注册）

### Step 3.1：TableMetadata 结构

**做什么**：定义 TableMetadata 和 MetadataManager

**产出**：`src/storage/metadata.h`

（接口见 Unit B 设计规范 3.2 节）

**验收**：结构定义正确

### Step 3.2：MetadataManager 实现

**做什么**：实现 MetadataManager（内存管理 + JSON 持久化）

**产出**：`src/storage/metadata.cpp`

**关键逻辑**：
- 表注册 → 内存中维护 TableMetadata
- 版本/Snapshot 添加 → 更新内存 + flush 到 metadata.json
- reload → 从 metadata.json 重新加载
- flush → 序列化为 JSON 并原子写入（先写临时文件，再 rename）

**验收**：
- [ ] register_table + get_table 正确
- [ ] add_version + add_snapshot 正确
- [ ] flush + reload 正确
- [ ] 重复注册返回 kTableAlreadyExists
- [ ] 查询不存在的表返回 kTableNotFound

### Step 3.3：元数据测试

**做什么**：编写 MetadataManager 单元测试

**产出**：`tests/unit/metadata_test.cpp`

**测试用例**（至少 6 个）：
- 注册+查询
- 重复注册
- 版本添加
- Snapshot 添加
- flush+reload 一致性
- 不存在的表查询

**验收**：6+ 测试用例全通过

---

## Task 4：StorageBackend 接口 + ObjectStoreBackend

**目标**：实现完整的存储后端

### Step 4.1：StorageBackend 接口定义

**做什么**：定义 StorageBackend 抽象接口

**产出**：`src/storage/backend.h`

（接口见 Unit B 设计规范 第二节）

**验收**：接口可编译

### Step 4.2：ObjectStoreBackend 实现

**做什么**：实现 ObjectStoreBackend（本地文件系统后端）

**产出**：`src/storage/object_store_backend.h`, `src/storage/object_store_backend.cpp`

**关键逻辑**：
- register_table → 创建目录结构 + 写入 schema.json + MetadataManager.register_table
- append → ParquetWriter.append + MetadataManager.add_version
- scan → 按 version 查找 part 文件 + ParquetReader 顺序读取
- list_versions / get_snapshot → MetadataManager 查询
- 存储布局：见 Unit B 设计规范 3.1 节

**验收**：
- [ ] register_table → has_table = true
- [ ] append → scan 读回全部数据
- [ ] 列裁剪 scan 正确
- [ ] 版本和 Snapshot 索引正确

### Step 4.3：基表层 INSERT ONLY 验证

**做什么**：验证基表层只允许 append，不支持修改

**验收**：
- [ ] 多次 append 产生多个 part 文件
- [ ] scan 读回全部 append 的数据
- [ ] 数据顺序正确（按 append 顺序）

### Step 4.4：ObjectStoreBackend 集成测试

**做什么**：编写端到端存储测试

**产出**：`tests/integration/storage_integration_test.cpp`

**测试用例**（至少 15 个）：
- 完整流程：register → append → scan → list_versions
- 多次 append（100 个 part 文件）
- 列裁剪 scan
- 版本查询
- 不存在的表操作返回 kTableNotFound
- 重复注册返回 kTableAlreadyExists
- 大数据量 append（10000 行）
- 重启后数据持久化（模拟：创建新实例指向同目录）
- **错误测试**：未注册表 append 返回 kTableNotFound
- **错误测试**：未注册表 scan 返回 kTableNotFound
- **错误测试**：不存在的 snapshot_id 返回 kSnapshotNotFound
- **错误测试**：Schema 不匹配的 batch append 返回 kSchemaMismatch
- **边界测试**：0 行 batch append + scan
- **边界测试**：1 行 batch append + scan
- **边界测试**：1000 列 batch append + scan

**验收**：15+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 5：脚手架集成

**目标**：为存储层添加 Trace/Metrics

### Step 5.1：Trace span

**做什么**：为 append/scan 添加 span

- append → span(component="storage", operation="append", attributes={table_id, row_count})
- scan → span(component="storage", operation="scan", attributes={table_id, snapshot_id})

**验收**：每次 append/scan 产生 span

### Step 5.2：Metrics 注册

**做什么**：注册存储相关 metrics

```
storage_append_total       — append 调用次数
storage_scan_total         — scan 调用次数
storage_append_bytes       — append 写入字节数
storage_scan_bytes         — scan 读取字节数
storage_append_duration_ms — append 耗时
storage_scan_duration_ms   — scan 耗时
```

**验收**：metrics 端点暴露上述指标

---

## Task 6：标准测试数据集

**目标**：创建 v1 标准测试数据集

### Step 6.1：sensor_1000.parquet

**做什么**：生成 1000 行传感器数据，包含：
- timestamp: DATETIME (2026-01-01 到 2026-01-02，每 86.4 秒一条)
- temperature: FLOAT [-50.0, 80.0]（正态分布，均值 20，标准差 15）
- pressure: FLOAT [900.0, 1100.0]（正态分布，均值 1013，标准差 30）
- vibration: FLOAT [0.0, 10.0]（正态分布，均值 2，标准差 1.5）
- status: ENUM('normal', 'warning', 'fault')（normal 85%，warning 12%，fault 3%）

**产出**：`tests/fixtures/sensor_1000.parquet` + 生成脚本

**验收**：Parquet 文件可被 ParquetReader 正确读取

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 公共类型 | 2 | 0.1w | ⬜ |
| Task 2: Parquet 读写 | 4 | 0.4w | ⬜ |
| Task 3: 元数据层 | 3 | 0.3w | ⬜ |
| Task 4: StorageBackend | 4 | 0.5w | ⬜ |
| Task 5: 脚手架 | 2 | 0.1w | ⬜ |
| Task 6: 测试数据集 | 1 | 0.1w | ⬜ |
| **合计** | **16** | **1.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| Arrow/Parquet C++ 依赖复杂 | 先用 conan/vcpkg 管理依赖；备选：轻量 Parquet 库 |
| 元数据持久化原子性 | 先写临时文件，再 rename（POSIX 原子操作） |
| 大文件性能 | 流式读取 + 列裁剪，避免全量加载 |
