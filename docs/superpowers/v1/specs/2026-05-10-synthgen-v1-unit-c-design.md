SynthGen Core v1 Unit C 设计规范：Data Import
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0、Unit A 设计规范、Unit B 设计规范
下游文档：Unit C 实施计划
组件：#3 LOAD DATA
估算：0.5 周
依赖：Unit A (Parser + Type System) + Unit B (Storage Engine)

---

## 一、本 Unit 交付什么

Unit C 实现数据导入功能——将外部 Parquet 文件的数据加载到 SynthGen 的基表层。

交付物：
1. **DataImporter**：读取 Parquet 文件，校验 Schema 兼容性，写入基表层
2. **Schema 兼容性校验器**：比对 Parquet schema 与 SynthGen Schema 的列名、类型、值域范围
3. **LOAD DATA 执行器**：将 Parser 产生的 AST 转为实际导入操作
4. **导入结果报告**：行数、校验结果、错误摘要

---

## 二、DataImporter 接口

```cpp
namespace synthgen::storage {

struct ImportResult {
    int64_t rows_imported;
    int64_t rows_skipped;      // Schema 不匹配的行
    std::string table_id;
    std::string snapshot_id;
    std::vector<ImportError> errors;  // 最多记录前 100 个错误
};

struct ImportError {
    int64_t row_index;
    std::string column;
    std::string reason;
};

enum class ImportMode {
    kStrict,    // 任何不匹配 → 整体失败
    kLenient,   // 不匹配行跳过，继续导入
};

class DataImporter {
public:
    explicit DataImporter(StorageBackend& storage);

    Result<ImportResult> import(const Schema& schema,
                                 const std::string& parquet_path,
                                 ImportMode mode = ImportMode::kStrict);

    // 预检查：不实际导入，只检查兼容性
    Result<CompatibilityReport> check_compatibility(
        const Schema& schema,
        const std::string& parquet_path);

private:
    // Schema 兼容性检查
    Result<void> validate_schema_compatibility(
        const Schema& synthgen_schema,
        const Schema& parquet_schema);

    // 值域范围校验（数值列是否在 Schema 声明的 [min, max] 内）
    Result<void> validate_value_ranges(
        const ArrowBatch& batch,
        const Schema& schema);
};

struct CompatibilityReport {
    bool compatible;
    std::vector<std::string> missing_columns;      // Parquet 缺少的列
    std::vector<std::string> extra_columns;        // Parquet 多出的列
    std::vector<std::string> type_mismatches;      // 类型不匹配的列
    std::vector<std::string> range_violations;     // 值域超出的列
    int64_t parquet_row_count;
    int64_t parquet_column_count;
};

}  // namespace synthgen::storage
```

---

## 三、Schema 兼容性校验规则

### 3.1 列级校验

| 校验项 | 规则 | 严格模式 | 宽松模式 |
|--------|------|---------|---------|
| 列名存在 | Parquet 必须包含 SynthGen Schema 的所有列 | ❌ 失败 | ❌ 失败 |
| 列类型匹配 | Parquet 类型必须与 SynthGen 类型兼容 | ❌ 失败 | 跳过该行 |
| 值域范围 | 数值列必须在 Schema 声明的 [min, max] 内 | ❌ 失败 | 跳过该行 |
| NOT NULL | 标记 NOT NULL 的列不能有空值 | ❌ 失败 | 跳过该行 |
| 额外列 | Parquet 可包含 SynthGen Schema 没有的列 | ⚠️ 警告 | ⚠️ 警告 |

### 3.2 类型兼容映射

| SynthGen 类型 | 兼容的 Parquet 类型 | 不兼容的 Parquet 类型 |
|--------------|-------------------|---------------------|
| FLOAT | FLOAT, DOUBLE | INT, STRING, BOOLEAN |
| INT | INT32, INT64 | FLOAT, STRING |
| DATETIME | INT64 (timestamp), STRING (ISO format) | FLOAT, BOOLEAN |
| STRING | BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY | INT, FLOAT |
| ENUM | BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY | INT, FLOAT |

### 3.3 值域范围校验

```cpp
// 对 FLOAT/INT 列，检查所有值是否在 Schema 声明的 [min, max] 内
for each row in batch:
    for each column with range_min/range_max:
        if value < range_min || value > range_max:
            if mode == kStrict: return kRangeViolation
            if mode == kLenient: mark row as skipped
```

**注意**：值域范围校验在 v1 是可选的（由用户决定是否启用）。v2 后筛选路径需要此数据来估计排除率。

---

## 四、LOAD DATA 执行流程

```
Parser 产生 LoadDataStmt AST
        ↓
LOAD DATA 执行器
        ↓
1. 检查 type_name 是否已注册（SchemaRegistry）
   - 不存在 → kUndefinedType
        ↓
2. 检查 Parquet 文件是否存在且可读
   - 不存在 → kFileNotFound
   - 不可读 → kPermissionDenied
        ↓
3. 读取 Parquet Schema（ParquetReader::read_schema）
        ↓
4. Schema 兼容性校验
   - 不兼容 → 返回 CompatibilityReport
        ↓
5. 读取 Parquet 数据（ParquetReader::read_all 或 read_streaming）
        ↓
6. 值域范围校验（可选）
        ↓
7. 写入基表层（StorageBackend::append）
        ↓
8. 返回 ImportResult
```

---

## 五、错误处理

| 错误场景 | 错误码 | 行为 | 审计日志 |
|---------|--------|------|---------|
| type_name 未注册 | kUndefinedType | 失败 | 记录 |
| 文件不存在 | kFileNotFound | 失败 | 记录 |
| 文件不可读 | kPermissionDenied | 失败 | 记录 |
| Parquet 文件损坏 | kDataCorruption | 失败 | 记录 |
| 列名缺失 | kSchemaMismatch | 失败（严格）/跳过（宽松） | 记录 |
| 类型不匹配 | kTypeMismatch | 失败（严格）/跳过（宽松） | 记录 |
| 值域超出 | kRangeViolation | 失败（严格）/跳过（宽松） | 记录 |
| NOT NULL 列含空值 | kNullViolation | 失败（严格）/跳过（宽松） | 记录 |
| 磁盘空间不足 | kStorageFull | 失败 | 记录 |
| 空文件（0行） | kEmptyFile | 警告，允许 | 记录 |

---

## 六、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `DataImporter::import()` | Unit D (Physics) | 导入训练数据 |
| `DataImporter::check_compatibility()` | Unit G (SDK) | 用户预检查 |
| `ImportResult` | Unit F (Evidence) | provenance 中记录导入结果 |
| `CompatibilityReport` | Unit G (SDK) | 用户查看兼容性报告 |

---

## 七、Unit C 验收标准

### 7.1 功能验收

- [ ] DataImporter 能读取标准 Parquet 文件并写入基表层
- [ ] Schema 兼容性校验正确识别列名缺失、类型不匹配
- [ ] 值域范围校验正确识别超出范围的值
- [ ] NOT NULL 列含空值正确检测
- [ ] 严格模式下任何不匹配 → 整体失败
- [ ] 宽松模式下不匹配行被跳过，其余行正常导入
- [ ] 额外列产生警告但不失败
- [ ] 空文件（0行）产生警告但允许
- [ ] 导入结果报告包含行数、跳过数、错误摘要

### 7.2 错误测试验收

- [ ] type_name 未注册返回 kUndefinedType
- [ ] 文件不存在返回 kFileNotFound
- [ ] 文件不可读返回 kPermissionDenied
- [ ] 损坏的 Parquet 文件返回 kDataCorruption
- [ ] 列名缺失返回 kSchemaMismatch
- [ ] 类型不匹配返回 kTypeMismatch
- [ ] 值域超出返回 kRangeViolation
- [ ] NOT NULL 列含空值返回 kNullViolation
- [ ] 磁盘空间不足返回 kStorageFull
- [ ] 空文件返回 kEmptyFile（警告，不失败）
- [ ] 严格模式下 1 行不匹配 → 整体失败，0 行导入
- [ ] 宽松模式下 1 行不匹配 → 跳过该行，其余导入
- [ ] 错误摘要最多记录 100 个错误
- [ ] 超过 100 个错误时，返回 kTooManyErrors + 前 100 个

### 7.3 边界条件测试

- [ ] 0 行 Parquet 文件导入
- [ ] 1 行 Parquet 文件导入
- [ ] 1000000 行 Parquet 文件流式导入
- [ ] 1000 列 Parquet 文件导入
- [ ] 列名最大长度（256字符）
- [ ] 值域边界值刚好在范围内（min 和 max）
- [ ] 值域边界值刚好超出范围（min-ε 和 max+ε）
- [ ] 所有行都不匹配（严格模式 → 0 行导入）
- [ ] 所有行都不匹配（宽松模式 → 0 行导入，全部跳过）

### 7.4 脚手架验收

- [ ] import 操作产生 Trace span（component="import", operation="import"）
- [ ] /metrics 暴露 import_total / import_errors / import_duration_ms
- [ ] 导入失败时 EvidencePackage 标记 failed: true

### 7.5 测试验收

- [ ] 单元测试：兼容性校验 + 值域校验 + 导入流程
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 每个 ImportErrorCode 至少 1 个测试用例触发
- [ ] 至少 20 个测试用例
- [ ] CI 自动运行
