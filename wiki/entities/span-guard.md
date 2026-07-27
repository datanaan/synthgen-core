# SpanGuard (RAII Trace 守卫)

> 类型：组件

## 定义

SynthGen Core Trace 系统的 RAII 守卫类。通过构造函数创建 TraceSpan，析构函数自动完成 span 并提交到 TraceCollector。确保 span 生命周期与作用域一致，防止忘记关闭 span 或内存泄漏。

## 核心接口

```cpp
// src/scaffold/trace.h
class SpanGuard {
public:
    SpanGuard(const std::string& component,
              const std::string& operation,
              const std::string& trace_id);
    ~SpanGuard();  // 自动完成 span 并提交
    void set_attribute(const std::string& key, const std::string& value);
    void set_status(const std::string& status);
private:
    TraceSpan span_;
};
```

## TraceSpan 结构

```cpp
struct TraceSpan {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string component;
    std::string operation;
    Timestamp start_time;
    Timestamp end_time;
    std::string status;
    std::map<std::string, std::string> attributes;
};
```

## 使用模式

```cpp
Result<ParseResult> Parser::parse(const std::string& source) const {
    SpanGuard span("parser", "parse", trace_id_);
    // ... 解析逻辑 ...
    span.set_attribute("statement_count", statements.size());
    return result;
}  // ~SpanGuard() 自动完成 span
```

## 设计原则

- **RAII**：资源获取即初始化，作用域结束自动释放
- **零成本**：正常路径无额外开销
- **不可拷贝**：span 所有权唯一

## 关联实体

- [[scaffolding]] — SpanGuard 是脚手架 Trace 系统的核心
- [[result-pattern]] — 失败时 span.status = "error"
