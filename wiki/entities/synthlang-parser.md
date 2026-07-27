# SynthLang Parser

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义
SynthGen Core 自定义 DSL（SynthLang）的解析器，负责将文本输入转为 AST。v1 支持四类语句（DEFINE TYPE、LOAD DATA、DEFINE CONSTRAINT、GENERATE TABLE），同时识别 v2+ 语法关键字并返回版本限制错误。

## 详情
**两阶段架构**：
- **Lexer 阶段**：词法分析，识别所有关键字（含 v2+ 的 DURING/WHEN/AVG/OVER/ROWS 等），输出 token 流
- **Parser 阶段**：语法分析，v1 支持的语法生成 AST 节点，v1 不支持的语法返回 kUnsupportedInV1 错误

**v1 支持的语句**：
- `DEFINE TYPE`：定义数据类型 Schema，包含列定义、值域范围、ENUM、ORDER 预留
- `LOAD DATA INTO ... FROM ...`：数据导入语句
- `DEFINE CONSTRAINT ... ON ...`：约束定义，v1 仅支持值域约束（BETWEEN/>/</>=/<=）
- `GENERATE TABLE ... FROM ... WITH CONSTRAINTS ... LIMIT ...`：生成语句

**AST 结构**（`synthgen::parser::ast` 命名空间）：
- `DefineTypeStmt`：类型定义，含 type_name 和 columns 向量
- `LoadDataStmt`：数据导入，含 type_name 和 file_path
- `DefineConstraintStmt`：约束定义，含约束名、类型名、约束项列表
- `GenerateTableStmt`：生成请求，含表名、类型名、约束名、limit
- `Program`：顶层节点，包含 Statement 向量

**v1 限制检查**：DURING/WHEN/行间（[t]语法）/聚合（AVG/OVER）均在 Parser 阶段返回 kUnsupportedInV1，错误消息包含版本提示（如 "supported from v2"）。

**错误码**：kSyntaxError、kUndefinedType、kDuplicateColumnName、kInvalidRange、kUnsupportedInV1、kTypeMismatch

**脚手架要求**：每次解析产生 Trace span（component="parser", operation="parse"）；提供 explain() 方法；脚手架代码作为模板引擎 v0.1 素材。

## v1 范围
- 仅解析 v1 语法子集
- v2+ 语法识别但不执行，返回明确的版本限制错误
- 不支持嵌套约束、复杂表达式
- ORDER 声明解析并存储但不影响执行

## 关联实体
- [[type-system-schema]] — Parser 输出的 DEFINE TYPE 语句用于构建 Schema 对象
- [[schema-registry]] — Parser 产生的 Schema 通过 Registry 注册
- [[synthlang]] — SynthLang DSL 语法定义

## 来源
- [[source-v1-unit-a-spec]] — §二 SynthLang Parser 完整设计
