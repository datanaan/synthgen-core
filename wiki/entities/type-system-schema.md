# Type System / Schema DDL

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义
SynthGen Core 的类型系统和 Schema 定义语言。支持 FLOAT/INT/DATETIME/STRING/ENUM 五种数据类型，配合值域范围声明和 Schema 校验规则，为生成引擎提供列定义和约束边界。

## 详情
**支持的数据类型**：
| 类型 | 内部表示 | 值域支持 |
|------|---------|---------|
| FLOAT | double | [min, max] |
| INT | int64_t | [min, max] |
| DATETIME | int64_t (epoch us) | -- |
| STRING | std::string | -- |
| ENUM | uint8_t + 值表 | -- |

**Schema 对象**（`synthgen::schema` 命名空间）：
- `ColumnDef`：列定义，含 name、type、not_null、is_order、range_min/max、enum_values
- `Schema`：类型定义，含 type_name 和 columns 向量，提供 order_columns()、find_column()、column_index()、validate() 等查询方法
- `SchemaRegistry`：Schema 注册中心，提供 register_schema()、get_schema()、has_schema()

**校验规则**：列名唯一、range_min < range_max、ENUM 值非空、ORDER 列存在、type_name 唯一、约束引用列存在且类型匹配。

**ORDER 预留**：v1 中 ORDER 声明被解析和存储（ColumnDef.is_order = true），但不影响执行行为。v2 行间约束引擎将使用 ORDER 列作为默认排序列。

## v1 范围
- 五种数据类型全部支持
- 值域范围 [min, max] 声明
- ORDER 关键字解析和存储但不使用
- 不支持复杂类型（数组、嵌套结构）

## 关联实体
- [[synthlang-parser]] — Parser 解析 DEFINE TYPE 语句生成 Schema 对象
- [[schema-registry]] — Schema 对象的注册和查询服务
- [[physics-engine]] — 物理引擎使用 Schema 的列定义和值域范围进行采样

## 来源
- [[source-v1-unit-a-spec]] — §三 Type System + Schema DDL 完整设计
