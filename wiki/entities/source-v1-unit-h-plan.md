# v1 Unit H Plan — Scaffold v1

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-h-plan.md
> 编译日期：2026-05-14

## 摘要

Unit H 实现 v1 的五项脚手架设施，估算 1.5 周，与 Unit C-G 并行开发。包含 5 个 Task、15 个步骤：Trace 基础设施（TraceSpan + SpanGuard RAII + TraceCollector）、Explain 基础设施（ExplainInfo 结构 + 各组件 explain() 方法）、Metrics 基础设施（MetricsRegistry + Prometheus 格式暴露）、确定性测试框架（SnapshotManager + 参考快照生成 + 确定性测试）、CI/CD 基础设施（GitHub Actions + CMake 测试配置 + 标准数据集）。由 1 人负责，与功能组件并行。

## 关键要点

- TraceSpan 使用 RAII SpanGuard 自动管理生命周期，防止内存泄漏
- ExplainInfo 统一结构：execution_mode + path + constraint_classification + distribution + estimated_exclusion_rate
- MetricsRegistry 支持 Counter、Histogram、Gauge 三种指标类型，暴露 Prometheus 格式
- 参考快照保证确定性：seed=42 uniform/gaussian + evidence_package_v1
- CI 工作流包含：编译、单元测试、集成测试、E2E 测试、快照比对、Schema 验证

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `TraceSpan` / `SpanGuard` | `src/scaffold/trace.h/.cpp` | Trace span 定义 + RAII 守卫 |
| `TraceCollector` | `src/scaffold/trace_collector.h/.cpp` | Span 收集、按 trace_id 分组、JSON 序列化 |
| `ExplainInfo` | `src/scaffold/explain.h` | Explain 信息统一结构 |
| `MetricsRegistry` | `src/scaffold/metrics.h/.cpp` | 指标注册和暴露（Counter/Histogram/Gauge） |
| `SnapshotManager` | `src/scaffold/snapshot.h/.cpp` | 快照保存、加载、比对 |

### SpanGuard RAII 模式

```cpp
Result<ParseResult> Parser::parse(const std::string& source) const {
    SpanGuard span("parser", "parse", trace_id_);
    // ... 解析逻辑 ...
    span.set_attribute("statement_count", statements.size());
    return result;
}  // span 自动销毁并记录
```

### MetricsRegistry 接口

```cpp
class MetricsRegistry {
public:
    void register_counter(const std::string& name);
    void register_histogram(const std::string& name, const std::vector<double>& buckets);
    void register_gauge(const std::string& name);
    void increment(const std::string& name, double value = 1.0);
    void observe(const std::string& name, double value);
    void set(const std::string& name, double value);
    std::string to_prometheus() const;
};
```

### 参考快照

| 快照文件 | 内容 |
|---------|------|
| `physics_seed42_1000rows_uniform.parquet` | 均匀分布 1000 行 |
| `physics_seed42_1000rows_gaussian.parquet` | 高斯分布 1000 行 |
| `evidence_package_v1_seed42.json` | EvidencePackage v1 |

### 测试策略

- Trace 测试 8+ 用例
- Explain 测试 6+ 用例
- Metrics 测试 8+ 用例
- 确定性测试 6+ 用例
- CI 配置验证：所有步骤可触发、失败时阻塞合并

## 提取的实体

- [[scaffolding]] — 脚手架工程（已存在）
- [[span-guard]] — RAII SpanGuard，Trace span 的生命周期管理（新实体）
- [[metrics-registry]] — 指标注册表，Counter/Histogram/Gauge + Prometheus 格式（新实体）
- [[snapshot-manager]] — 快照管理器，确定性测试的参考快照比对（新实体）
