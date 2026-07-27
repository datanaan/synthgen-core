# Schema 兼容性校验器

> 类型：组件

## 定义

数据导入流程中的 Schema 比对组件。负责将 Parquet 文件的 Schema 与 SynthGen 定义的 Schema 进行兼容性检查，生成 CompatibilityReport。

## 核心功能

1. **列名映射**：Parquet 列名 -> SynthGen 列名（大小写敏感）
2. **类型映射**：Parquet 物理类型 -> SynthGen DataType
3. **兼容性检查**：
   - SynthGen Schema 的所有列在 Parquet 中是否存在
   - 类型是否兼容（如 FLOAT -> DOUBLE 兼容，INT32 -> FLOAT 不兼容）
   - 额外列（Parquet 有但 SynthGen 无）产生警告

## 类型兼容规则

| Parquet 类型 | SynthGen 类型 | 结果 |
|-------------|--------------|------|
| FLOAT | FLOAT | 兼容 |
| DOUBLE | FLOAT | 兼容 |
| INT32 | INT | 兼容 |
| INT32 | FLOAT | 不兼容 |
| BYTE_ARRAY | STRING | 兼容 |

## 文件位置

`src/storage/schema_compatibility.h`, `src/storage/schema_compatibility.cpp`

## 关联实体

- [[storage-engine]] — ParquetReader 提供 Schema 读取能力
- [[schema-registry]] — 提供 SynthGen Schema 定义
