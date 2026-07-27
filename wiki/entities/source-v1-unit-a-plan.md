# v1 Unit A Plan — Parser + Type System

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-a-plan.md
> 编译日期：2026-05-14

## 摘要

Unit A 是 v1 的地基，交付 SynthLang Parser（v1 语法子集）和 Type System/Schema DDL。估算 2.5 周，无外部依赖。包含 6 个 Task、21 个步骤：Lexer（Token 定义 + 词法分析）、AST 节点定义、递归下降 Parser（DEFINE TYPE / LOAD DATA / DEFINE CONSTRAINT / GENERATE TABLE）、Schema + SchemaRegistry + SchemaBuilder、脚手架集成（Trace/Explain/Metrics）、CI 集成。所有后续 Unit 依赖本 Unit 的输出。

## 关键要点

- Lexer 识别所有 v1-v4 关键字，确保 v2+ 不需要大规模重构
- Parser 使用递归下降，对 v1 不支持的语法（DURING/WHEN/聚合/行间）返回 `kUnsupportedInV1` + 版本提示
- Schema 校验规则：列名唯一、range_min < range_max、ENUM 值非空、ORDER 列存在
- 脚手架（Trace/Explain/Metrics）作为一等公民，与功能代码同步交付
- 测试覆盖率要求：错误测试占比 >= 30%

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `Token` / `TokenType` | `src/parser/token.h` | 词法单元定义，覆盖 v1-v4 全部关键字、字面量、符号 |
| `Lexer` | `src/parser/lexer.h/.cpp` | 词法分析，源文本 -> Token 流；错误恢复：非法字符 -> T_ERROR |
| `ast::DefineTypeStmt` 等 | `src/parser/ast.h` | AST 节点，含 `find_column()`、`get_column_constraints()` 辅助方法 |
| `Parser` | `src/parser/parser.h/.cpp` | 递归下降语法分析，返回 `Result<ParseResult>` |
| `Schema` | `src/schema/schema.h/.cpp` | Schema 对象及校验逻辑 |
| `SchemaRegistry` | `src/schema/schema_registry.h/.cpp` | 类型注册表，管理 Schema 实例 |
| `SchemaBuilder` | `src/schema/schema_builder.h/.cpp` | AST -> Schema 转换器 |

### 设计模式

- **递归下降**：Parser 使用经典递归下降模式，易扩展（v2+ 新增语法只需添加方法）
- **RAII SpanGuard**：Trace span 通过 RAII 守卫自动管理生命周期
- **Result<T>**：所有公开方法返回 Result<T>，不使用异常

### 测试策略

- Lexer 单元测试 15+ 用例
- Parser 集成测试 10+ 用例（完整语句解析流程）
- Schema 单元测试 15+ 用例（含错误测试 >= 30%）
- 边界测试覆盖：标识符最大长度、FLOAT 极值、空输入、0 行 Schema
- 性能测试：1000 列 Schema 解析 < 100ms

### v1 限制检查

DURING/WHEN/行间语法/聚合语法 -> `kUnsupportedInV1`，错误消息格式：
- `"DURING constraints are not supported in v1. Supported from v2."`
- `"Inter-row constraints are not supported in v1. Supported from v2."`
- `"Aggregate constraints are not supported in v1. Supported from v2."`

## 提取的实体

- [[synthlang]] — SynthLang 自定义 DSL 语言（已存在）
- [[scaffolding]] — 脚手架工程：Trace/Explain/Metrics 设施（已存在）
- [[result-pattern]] — Result<T> 错误处理模式，所有公开方法返回 Result<T>，不使用异常（新实体）
- [[recursive-descent-parser]] — 递归下降 Parser 设计模式，SynthLang 语法分析的核心实现策略（新实体）
- [[schema-registry]] — Schema 注册表，管理 DEFINE TYPE 产生的 Schema 实例（新实体）
- [[v1-version-gate]] — v1 版本门控机制，对 v1 不支持的语法返回 kUnsupportedInV1 + 版本提示（新实体）
