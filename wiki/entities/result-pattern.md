# Result<T> 错误处理模式

> 类型：设计模式

## 定义

SynthGen Core 的统一错误处理模式。所有公开方法返回 `Result<T>` 而非抛出异常。`Result<T>` 要么包含成功值（T），要么包含错误信息（Error）。

## 设计原则

- **不使用异常**：C++ 异常在数据库内核场景中性能不可控
- **强制错误检查**：调用方必须显式处理 Result，避免忽略错误
- **Error 携带上下文**：错误码 + 错误消息 + 行号/列号（Parser 场景）

## 关键类型

```cpp
// src/common/result.h
template<typename T>
class Result {
    // 要么 value_，要么 error_
};

// src/common/error.h
class Error {
    ErrorCode code;
    std::string message;
    int line;
    int column;
};
```

## 使用示例

```cpp
Result<ParseResult> Parser::parse(const std::string& source) const;
Result<Schema> SchemaBuilder::build(const ast::DefineTypeStmt& stmt);
Result<void> Validator::validate_batch(const ArrowBatch& batch);
```

## 错误码分类

各模块定义自己的错误码枚举（如 `ParseErrorCode`、`StorageErrorCode`），所有错误码使用 `kPascalCase` 命名。

## 关联实体

- [[scaffolding]] — Trace span 与 Result<T> 配合，失败时 span.status = "error"
- [[synthgen-core]] — 项目级设计约束：不使用异常
