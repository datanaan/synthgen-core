SynthGen Core v1 Unit A 设计规范：Parser + Type System
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit A 实施计划
组件：#1 SynthLang Parser + #2 Type System / Schema DDL
估算：2.5 周
依赖：无（Wave 1 起步组件）

---

## 一、本 Unit 交付什么

**Unit A 是 v1 的地基**——所有后续 Unit 都依赖它。

交付物：
1. **SynthLang Parser**：能解析 v1 语法子集，输出 AST
2. **Type System + Schema DDL**：能定义数据类型、值域范围、ENUM、ORDER 预留
3. **Schema Registry**：能注册和查询 Schema
4. **v1 限制检查**：DURING/WHEN/行间/聚合语法返回 unsupported_in_v1

---

## 二、#1 SynthLang Parser

### 2.1 v1 支持的语法

```
program := statement*

statement := define_type | load_data | define_constraint | generate_table

define_type := "DEFINE" "TYPE" IDENT "{" column_def ("," column_def)* "}"
column_def := IDENT ":" type_spec (NOT_NULL)? (ORDER)?
type_spec := "FLOAT" ("[" FLOAT_LITERAL "," FLOAT_LITERAL "]")?
           | "INT"
           | "DATETIME"
           | "STRING"
           | "ENUM" "(" STRING_LITERAL ("," STRING_LITERAL)* ")"

load_data := "LOAD" "DATA" "INTO" IDENT "FROM" STRING_LITERAL

define_constraint := "DEFINE" "CONSTRAINT" IDENT "ON" IDENT "{" constraint_body "}"
constraint_body := constraint_item ("," constraint_item)*
constraint_item := column_ref "BETWEEN" FLOAT_LITERAL "AND" FLOAT_LITERAL
                 | column_ref ">" FLOAT_LITERAL
                 | column_ref "<" FLOAT_LITERAL
                 | column_ref ">=" FLOAT_LITERAL
                 | column_ref "<=" FLOAT_LITERAL

generate_table := "GENERATE" "TABLE" IDENT "FROM" IDENT "WITH" "CONSTRAINTS" IDENT "LIMIT" INTEGER

// 注释
comment := "--" [^\n]*

// v1 不支持但必须识别的语法（返回 unsupported_in_v1 错误）
// DURING column = value → 识别关键字但返回错误
// WHEN condition THEN constraint → 识别关键字但返回错误
// 行间约束 (vibration[t] - vibration[t-1] < 5.0) → 识别 [t] 语法但返回错误
// 聚合约束 (AVG(...) OVER ...) → 识别关键字但返回错误
```

### 2.2 AST 定义

```cpp
namespace synthgen::parser::ast {

struct ColumnDef {
    std::string name;
    DataType type;
    bool not_null = false;
    bool is_order = false;
    std::optional<double> range_min;
    std::optional<double> range_max;
    std::vector<std::string> enum_values;
};

struct DefineTypeStmt {
    std::string type_name;
    std::vector<ColumnDef> columns;
};

struct LoadDataStmt {
    std::string type_name;
    std::string file_path;
};

enum class ConstraintOperator {
    kBetween, kGreaterThan, kLessThan, kGreaterEqual, kLessEqual
};

struct ConstraintItem {
    std::string column_name;
    ConstraintOperator op;
    double value_min;  // BETWEEN 用两个值，其他用一个
    double value_max;
};

struct DefineConstraintStmt {
    std::string constraint_name;
    std::string type_name;
    std::vector<ConstraintItem> items;
};

struct GenerateTableStmt {
    std::string table_name;
    std::string type_name;
    std::string constraint_name;
    int64_t limit;
};

using Statement = std::variant<DefineTypeStmt, LoadDataStmt,
                                DefineConstraintStmt, GenerateTableStmt>;

struct Program {
    std::vector<Statement> statements;
};

}  // namespace synthgen::parser::ast
```

### 2.3 错误处理

```cpp
enum class ParseErrorCode {
    kSyntaxError,
    kUndefinedType,          // DEFINE CONSTRAINT 引用了不存在的 type
    kDuplicateColumnName,   // 同一 type 内列名重复
    kInvalidRange,           // min > max
    kUnsupportedInV1,        // DURING/WHEN/行间/聚合
    kTypeMismatch,           // 约束引用的列类型不匹配
};

struct ParseError {
    ParseErrorCode code;
    std::string message;
    int line;
    int column;
};
```

### 2.4 v1 限制检查

| 语法 | Parser 行为 | 错误码 |
|------|-----------|--------|
| `DURING column = value` | 识别 DURING 关键字 | kUnsupportedInV1 |
| `WHEN condition THEN constraint` | 识别 WHEN 关键字 | kUnsupportedInV1 |
| `column[t] - column[t-1]` | 识别 `[t]` 语法 | kUnsupportedInV1 |
| `AVG(...) OVER (...)` | 识别 AVG/OVER 关键字 | kUnsupportedInV1 |
| `ROWS` / `PARTITION BY` / `SESSION` | 识别关键字 | kUnsupportedInV1 |

**实现方式**：
- Lexer 阶段识别所有关键字（包括 v2+ 的）
- Parser 阶段对 v1 不支持的语法返回 kUnsupportedInV1 错误
- 错误消息包含版本提示：`"DURING constraints are supported from v2. Use value range constraints in v1."`

### 2.5 组件模板引擎 v0.1 的素材准备

Parser 完成后，其脚手架代码（span 创建、metrics 注册、Explain 接口）将作为模板引擎 v0.1 的素材。因此：

- **Parser 必须包含完整的脚手架代码**——span 创建、metrics 注册、Explain 占位
- 代码结构应足够规范，能作为其他组件的参考模板

---

## 三、#2 Type System + Schema DDL

### 3.1 数据类型

| 类型 | 内部表示 | 值域支持 | v1 状态 |
|------|---------|---------|--------|
| FLOAT | double | [min, max] | ✅ |
| INT | int64_t | [min, max] | ✅ |
| DATETIME | int64_t (epoch us) | — | ✅ |
| STRING | std::string | — | ✅ |
| ENUM | uint8_t + 值表 | — | ✅ |

### 3.2 Schema 对象

```cpp
namespace synthgen::schema {

struct ColumnDef {
    std::string name;
    DataType type;
    bool not_null = false;
    bool is_order = false;          // v1 预留
    std::optional<double> range_min;
    std::optional<double> range_max;
    std::vector<std::string> enum_values;
};

struct Schema {
    std::string type_name;
    std::vector<ColumnDef> columns;

    // 派生查询
    std::vector<std::string> order_columns() const;  // ORDER 声明的列
    std::optional<ColumnDef> find_column(const std::string& name) const;
    int column_index(const std::string& name) const;  // 列序号

    // 校验
    Result<void> validate() const;
    // 检查：列名唯一、range_min < range_max、enum_values 非空、order 列存在
};

class SchemaRegistry {
public:
    Result<void> register_schema(Schema schema);
    Result<const Schema*> get_schema(const std::string& type_name) const;
    bool has_schema(const std::string& type_name) const;

private:
    std::unordered_map<std::string, Schema> schemas_;
};

}  // namespace synthgen::schema
```

### 3.3 Schema 校验规则

| 规则 | 检查时机 | 错误码 |
|------|---------|--------|
| 列名唯一 | register_schema | kDuplicateColumnName |
| range_min < range_max | register_schema | kInvalidRange |
| ENUM 值非空 | register_schema | kInvalidEnum |
| ORDER 列存在 | register_schema | kUndefinedColumn |
| type_name 唯一 | register_schema | kDuplicateTypeName |
| 约束引用的列存在 | define_constraint | kUndefinedColumn |
| 约束引用的列类型匹配 | define_constraint | kTypeMismatch |

### 3.4 ORDER 预留

v1 中 ORDER 声明被解析和存储，但不影响执行行为。v2 行间约束引擎将使用 ORDER 列作为默认排序列。

- Parser 解析 `ORDER` 关键字 → ColumnDef.is_order = true
- Schema.order_columns() 返回 ORDER 列
- 物理引擎 v1 忽略 ORDER 声明
- v2 行间引擎使用 ORDER 列

---

## 四、Unit A 验收标准

### 4.1 功能验收

- [ ] Parser 能解析 v1 语法子集，输出正确 AST
- [ ] DURING/WHEN/行间/聚合语法返回 unsupported_in_v1，错误消息含版本提示
- [ ] Schema 可定义 FLOAT/INT/DATETIME/STRING/ENUM 类型
- [ ] Schema 值域范围 [min, max] 可声明
- [ ] ORDER 声明被解析和存储（v1 不使用）
- [ ] Schema Registry 可注册和查询
- [ ] 所有校验规则生效（列名唯一、范围合法等）

### 4.2 脚手架验收

- [ ] Parser 每次解析产生 Trace span（component="parser", operation="parse"）
- [ ] Parser 提供 explain() 方法（返回 v1 支持的语法子集信息）
- [ ] Parser 脚手架代码可作为模板引擎 v0.1 的素材

### 4.3 错误测试验收

**Lexer 错误测试**：
- [ ] 空输入返回 T_EOF
- [ ] 非法字符（如 `@#$`）返回 T_ERROR，行号/列号正确
- [ ] 未闭合的字符串字面量返回 T_ERROR
- [ ] 超长标识符（>1024字符）返回 T_ERROR
- [ ] 浮点数格式错误（如 `1.2.3`）返回 T_ERROR
- [ ] 注释未闭合（文件以 `--` 结尾）正确处理

**Parser 错误测试**：
- [ ] 空输入返回 ParseResult.errors 非空
- [ ] 缺少关键字（如 `DEFINE TYPE {` 缺少名称）返回 kSyntaxError
- [ ] 重复列名返回 kDuplicateColumnName，指出重复列名
- [ ] range_min > range_max 返回 kInvalidRange
- [ ] ENUM 空值列表返回 kInvalidEnum
- [ ] 引用不存在的 type 返回 kUndefinedType
- [ ] 引用不存在的 column 返回 kUndefinedColumn
- [ ] 对 STRING 列施加 BETWEEN 返回 kTypeMismatch
- [ ] DURING 关键字返回 kUnsupportedInV1 + `"supported from v2"`
- [ ] WHEN 关键字返回 kUnsupportedInV1 + `"supported from v2"`
- [ ] AVG/OVER 关键字返回 kUnsupportedInV1 + `"supported from v2"`
- [ ] `[t]` 语法返回 kUnsupportedInV1 + `"supported from v2"`
- [ ] LIMIT = 0 允许（返回空结果）
- [ ] LIMIT < 0 返回 kInvalidArgument
- [ ] 多个语法错误时，Parser 报告第一个错误并停止

**Schema 错误测试**：
- [ ] 空列列表返回 kInvalidSchema
- [ ] 列名包含非法字符返回 kInvalidColumnName
- [ ] 重复注册返回 kDuplicateTypeName
- [ ] 查询不存在的 type 返回 kTypeNotFound

### 4.4 边界条件测试

- [ ] 最大标识符长度（1024字符）刚好通过，1025字符失败
- [ ] 最小 FLOAT 值（-DBL_MAX）和最大 FLOAT 值（DBL_MAX）正确解析
- [ ] 0 行 Schema（空表定义）行为确定
- [ ] 1000 列的 Schema 解析性能可接受（<100ms）
- [ ] 100 个约束项的 DEFINE CONSTRAINT 解析性能可接受（<50ms）

### 4.5 测试验收

- [ ] 单元测试覆盖：合法语法、非法语法、v1 不支持语法
- [ ] 错误测试用例占比 ≥ 30%（至少 15 个错误测试）
- [ ] 每个 ErrorCode 至少 1 个测试用例触发
- [ ] 至少 25 个测试用例（15 错误 + 10 正向）
- [ ] CI 自动运行

---

## 五、与后续 Unit 的接口

Unit A 交付后，以下接口供后续 Unit 使用：

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `Parser::parse()` | Unit D (Physics), Unit E (Validation) | 获取 AST |
| `Schema` 对象 | Unit C (Import), Unit D (Physics) | 列定义和值域范围 |
| `SchemaRegistry` | 全部 | Schema 查询 |
| `ast::DefineConstraintStmt` | Unit D, Unit E | 约束定义 |
