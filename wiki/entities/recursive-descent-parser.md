# 递归下降 Parser

> 类型：设计模式

## 定义

SynthLang 语法分析器采用递归下降（Recursive Descent）设计模式。每个语法产生式对应一个解析方法，从上到下递归调用。

## 为什么选择递归下降

- **可扩展性**：v2/v3/v4 需要新增语法时，只需添加新的解析方法，不影响已有逻辑
- **可读性**：代码结构与语法规则一一对应，便于对照语法规范审查
- **错误报告精准**：可以精确定位到行号/列号，给出有意义的错误消息
- **无外部依赖**：不需要 Parser 生成器（如 ANTLR/Bison）

## v1 支持的语句

| 方法 | 语句 | v1 状态 |
|------|------|---------|
| `parse_define_type()` | DEFINE TYPE | 支持 |
| `parse_load_data()` | LOAD DATA | 支持 |
| `parse_define_constraint()` | DEFINE CONSTRAINT | 仅值域约束 |
| `parse_generate_table()` | GENERATE TABLE | 支持 |

## v1 版本门控

对 v1 不支持的语法，Parser 识别关键字后返回 `kUnsupportedInV1`：
- DURING / WHEN / 行间语法 [t] / 聚合语法 AVG/OVER

## 关键类

```cpp
// src/parser/parser.h
class Parser {
public:
    Result<ParseResult> parse(const std::string& source) const;
private:
    Result<Statement> parse_statement();
    Result<DefineTypeStmt> parse_define_type();
    Result<LoadDataStmt> parse_load_data();
    Result<DefineConstraintStmt> parse_define_constraint();
    Result<GenerateTableStmt> parse_generate_table();
    bool check(TokenType type) const;
    Token advance();
    Token expect(TokenType type);
};
```

## 关联实体

- [[synthlang]] — 被解析的 DSL 语言
- [[v1-version-gate]] — 版本门控机制
- [[result-pattern]] — 返回 Result<ParseResult>
