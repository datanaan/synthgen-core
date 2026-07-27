SynthGen Core v1 Unit A 实施计划：Parser + Type System
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit A 设计规范 v1.0
估算：2.5 周
依赖：无

---

## 概述

Unit A 是 v1 的地基。交付 SynthLang Parser（v1 语法子集）和 Type System/Schema DDL。所有后续 Unit 都依赖本 Unit 的输出。

---

## Task 1：Lexer 实现

**目标**：SynthLang 词法分析器，识别所有 v1-v4 关键字

### Step 1.1：Token 定义

**做什么**：定义 Token 类型枚举和 Token 结构体

**产出**：`src/parser/token.h`

```cpp
enum class TokenType {
    // 关键字（v1）
    K_DEFINE, K_TYPE, K_LOAD, K_DATA, K_INTO, K_FROM,
    K_CONSTRAINT, K_ON, K_GENERATE, K_TABLE, K_WITH, K_LIMIT,
    K_BETWEEN, K_AND, K_NOT, K_NULL, K_ORDER,
    K_FLOAT, K_INT, K_DATETIME, K_STRING, K_ENUM,

    // 关键字（v2+ 预留，v1 不支持但需识别）
    K_DURING, K_WHEN, K_THEN,
    K_AVG, K_OVER, K_INTERVAL,
    K_ROWS, K_PARTITION, K_BY, K_SESSION, K_GAP,
    K_UPDATE, K_MODEL, K_INCORPORATE, K_WHERE,
    K_AS, K_OF, K_VERSION,
    K_INCLUDE, K_MODE, K_FALLBACK, K_SAVE,

    // 字面量
    L_FLOAT, L_INT, L_STRING, L_IDENT,

    // 符号
    S_LBRACE, S_RBRACE, S_LPAREN, S_RPAREN,
    S_LBRACKET, S_RBRACKET,
    S_COMMA, S_COLON, S_SEMICOLON,
    S_DOT, S_EQ, S_GT, S_LT, S_GE, S_LE,

    // 特殊
    T_EOF, T_ERROR
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
};
```

**验收**：Token 枚举覆盖所有 v1-v4 关键字

### Step 1.2：Lexer 实现

**做什么**：实现 Lexer 类，将源文本转为 Token 流

**产出**：`src/parser/lexer.h`, `src/parser/lexer.cpp`

**关键逻辑**：
- 跳过空白和注释（`--` 到行尾）
- 识别关键字 vs 标识符（关键字优先）
- 浮点数字面量（支持负数）
- 字符串字面量（单引号）
- 错误恢复：非法字符 → T_ERROR token，继续词法分析

**验收**：
- [ ] 合法输入正确分词
- [ ] 注释被跳过
- [ ] 非法字符产生 T_ERROR
- [ ] 行号/列号正确

### Step 1.3：Lexer 单元测试

**做什么**：编写 Lexer 单元测试

**产出**：`tests/unit/lexer_test.cpp`

**测试用例**（至少 15 个）：
- 基础关键字识别（DEFINE, TYPE, FLOAT, BETWEEN 等）
- 标识符识别
- 浮点数/整数/字符串字面量
- 运算符识别
- 注释跳过
- 行号/列号追踪
- 错误恢复
- v2+ 关键字识别（DURING, WHEN, AVG 等）

**验收**：15+ 测试用例全通过

---

## Task 2：AST 定义

**目标**：定义 SynthLang v1 的 AST 节点类型

### Step 2.1：AST 节点定义

**做什么**：定义 AST 节点数据结构

**产出**：`src/parser/ast.h`

（定义见 Unit A 设计规范 2.2 节）

**验收**：AST 节点覆盖所有 v1 语法

### Step 2.2：AST 辅助方法

**做什么**：为 AST 节点添加查询辅助方法

**产出**：`src/parser/ast.h`（扩展）

- `DefineTypeStmt::find_column(name)` → 查找列定义
- `DefineConstraintStmt::get_column_constraints(column_name)` → 按列筛选约束

**验收**：辅助方法工作正确

---

## Task 3：Parser 实现

**目标**：SynthLang v1 语法分析器

### Step 3.1：递归下降 Parser 框架

**做什么**：实现 Parser 类的基本框架（递归下降）

**产出**：`src/parser/parser.h`, `src/parser/parser.cpp`

```cpp
class Parser {
public:
    Result<ParseResult> parse(const std::string& source) const;

private:
    // 语句解析
    Result<Statement> parse_statement();
    Result<DefineTypeStmt> parse_define_type();
    Result<LoadDataStmt> parse_load_data();
    Result<DefineConstraintStmt> parse_define_constraint();
    Result<GenerateTableStmt> parse_generate_table();

    // 辅助
    Result<ColumnDef> parse_column_def();
    Result<ConstraintItem> parse_constraint_item();
    bool check(TokenType type) const;
    Token advance();
    Token expect(TokenType type);
    ParseError error(ParseErrorCode code, const std::string& msg);
};
```

**验收**：Parser 框架可编译

### Step 3.2：DEFINE TYPE 解析

**做什么**：实现 DEFINE TYPE 语句解析

**关键逻辑**：
- 解析列定义（类型 + NOT NULL + ORDER + 值域范围）
- 校验列名唯一
- 校验值域范围合法（min < max）

**验收**：
- [ ] 合法 DEFINE TYPE 正确解析
- [ ] 重复列名返回 kDuplicateColumnName
- [ ] 无效范围返回 kInvalidRange

### Step 3.3：LOAD DATA 解析

**做什么**：实现 LOAD DATA 语句解析

**验收**：
- [ ] 合法 LOAD DATA 正确解析
- [ ] 路径为空返回语法错误

### Step 3.4：DEFINE CONSTRAINT 解析

**做什么**：实现 DEFINE CONSTRAINT 语句解析（v1 仅值域约束）

**关键逻辑**：
- 解析 BETWEEN / > / < / >= / <= 约束
- 校验引用的 type_name 已定义
- 校验引用的 column_name 存在
- 校验列类型匹配（约束只能用于数值列）

**验收**：
- [ ] 值域约束正确解析
- [ ] 引用不存在的 type 返回 kUndefinedType
- [ ] 引用不存在的 column 返回 kUndefinedColumn
- [ ] 对非数值列施加 BETWEEN 返回 kTypeMismatch

### Step 3.5：GENERATE TABLE 解析

**做什么**：实现 GENERATE TABLE 语句解析

**验收**：
- [ ] 合法 GENERATE TABLE 正确解析
- [ ] LIMIT 为 0 或负数返回错误

### Step 3.6：v1 限制检查

**做什么**：对 v1 不支持的语法返回 kUnsupportedInV1

**关键逻辑**：
- 识别 DURING 关键字 → kUnsupportedInV1 + 版本提示
- 识别 WHEN 关键字 → kUnsupportedInV1 + 版本提示
- 识别 [t] 语法 → kUnsupportedInV1 + 版本提示
- 识别 AVG/OVER 等聚合关键字 → kUnsupportedInV1 + 版本提示

**错误消息格式**：
```
"ERROR: DURING constraints are not supported in v1. Supported from v2."
"ERROR: Inter-row constraints are not supported in v1. Supported from v2."
"ERROR: Aggregate constraints are not supported in v1. Supported from v2."
```

**验收**：
- [ ] DURING → kUnsupportedInV1 + 正确消息
- [ ] WHEN → kUnsupportedInV1 + 正确消息
- [ ] 行间语法 → kUnsupportedInV1 + 正确消息
- [ ] 聚合语法 → kUnsupportedInV1 + 正确消息

### Step 3.7：Parser 集成测试

**做什么**：编写 Parser 集成测试（完整语句解析）

**产出**：`tests/integration/parser_integration_test.cpp`

**测试用例**（至少 10 个）：
- 完整的 DEFINE TYPE + LOAD DATA + DEFINE CONSTRAINT + GENERATE TABLE 流程
- 多个 DEFINE TYPE
- 嵌套约束
- v1 不支持语法
- 空输入
- 语法错误恢复

**验收**：10+ 测试用例全通过

---

## Task 4：Type System + Schema DDL

**目标**：实现 Schema 对象和 SchemaRegistry

### Step 4.1：Schema 实现

**做什么**：实现 Schema 类及其校验逻辑

**产出**：`src/schema/schema.h`, `src/schema/schema.cpp`

（接口见 Unit A 设计规范 3.2 节）

**校验规则**：
- 列名唯一
- range_min < range_max
- ENUM 值非空
- ORDER 列存在

**验收**：
- [ ] Schema 构造正确
- [ ] 校验规则全部生效
- [ ] find_column / order_columns / column_index 工作正确

### Step 4.2：SchemaRegistry 实现

**做什么**：实现 SchemaRegistry 类

**产出**：`src/schema/schema_registry.h`, `src/schema/schema_registry.cpp`

**验收**：
- [ ] 注册和查询正确
- [ ] 重复注册返回错误
- [ ] 查询不存在的 type 返回错误

### Step 4.3：AST → Schema 转换

**做什么**：实现从 DEFINE TYPE AST 到 Schema 对象的转换

**产出**：`src/schema/schema_builder.h`, `src/schema/schema_builder.cpp`

```cpp
class SchemaBuilder {
public:
    Result<Schema> build(const ast::DefineTypeStmt& stmt);
};
```

**验收**：
- [ ] AST 正确转为 Schema
- [ ] 转换过程中校验 Schema 有效性

### Step 4.4：Schema 单元测试

**做什么**：编写 Schema 和 SchemaRegistry 单元测试

**产出**：`tests/unit/schema_test.cpp`

**测试用例**（至少 15 个）：
- 正常 Schema 构建
- 列名重复
- 范围不合法（min > max）
- ENUM 空值
- ORDER 列不存在
- 注册/查询
- 重复注册
- **错误测试**：空列列表
- **错误测试**：列名含非法字符
- **错误测试**：查询不存在的 type
- **边界测试**：最大标识符长度（1024 vs 1025）
- **边界测试**：最小 FLOAT 值（-DBL_MAX）
- **边界测试**：最大 FLOAT 值（DBL_MAX）
- **边界测试**：0 行 Schema
- **性能测试**：1000 列 Schema 解析（<100ms）

**验收**：15+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 5：脚手架集成

**目标**：为 Parser 和 Schema 添加 Trace/Explain/Metrics

### Step 5.1：Trace span 集成

**做什么**：为 Parser::parse() 添加 span 创建

**实现方式**：RAII SpanGuard

```cpp
Result<ParseResult> Parser::parse(const std::string& source) const {
    SpanGuard span("parser", "parse", trace_id_);
    // ... 解析逻辑 ...
    span.set_attribute("statement_count", statements.size());
    return result;
}
```

**验收**：每次 parse 产生 span

### Step 5.2：Explain 接口

**做什么**：为 Parser 添加 explain() 方法

```cpp
struct ParserExplainInfo {
    std::vector<std::string> supported_statements;
    std::vector<std::string> unsupported_in_v1;
    std::string version = "v1";
};

ExplainInfo Parser::explain() const;
```

**验收**：explain() 返回 v1 支持和不支持的语法列表

### Step 5.3：Metrics 注册

**做什么**：注册 Parser 相关 metrics

```
parser_parse_total      — 解析调用次数
parser_parse_errors     — 解析错误次数
parser_parse_duration_ms — 解析耗时
```

**验收**：metrics 端点暴露上述指标

---

## Task 6：CI 集成

**目标**：Unit A 的测试在 CI 中自动运行

### Step 6.1：CMake 配置

**做什么**：配置 CMakeLists.txt，添加 test 目标

**验收**：`cmake --build . --target test` 可运行全部测试

### Step 6.2：CI 脚本

**做什么**：添加 GitHub Actions / GitLab CI 配置

**验收**：PR 提交触发 Parser 单元测试 + Schema 单元测试

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: Lexer | 3 | 0.5w | ⬜ |
| Task 2: AST | 2 | 0.25w | ⬜ |
| Task 3: Parser | 7 | 1w | ⬜ |
| Task 4: Type System | 4 | 0.5w | ⬜ |
| Task 5: Scaffold | 3 | 0.25w | ⬜ |
| Task 6: CI | 2 | 0w | ⬜ |
| **合计** | **21** | **2.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| v2+ 语法预留不足导致 v2 Parser 大重构 | Lexer 识别所有 v1-v4 关键字；AST 预留扩展点；Parser 用递归下降易扩展 |
| Schema 校验规则遗漏 | 参考 PostgreSQL DDL engine 的校验逻辑 |
| 脚手架代码不够规范，无法作为模板素材 | Task 5 专门确保脚手架代码质量 |
