SynthGen Core v1 Unit H 实施计划：Scaffold v1
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit H 设计规范 v1.0
估算：1.5 周
依赖：与 Unit C-G 并行

---

## 概述

Unit H 实现 v1 的五项脚手架设施。与功能组件并行开发，由 1 人负责。

---

## Task 1：Trace 基础设施

**目标**：实现 Trace span 系统

### Step 1.1：Span 结构定义

**做什么**：定义 TraceSpan 和 SpanGuard

**产出**：`src/scaffold/trace.h`, `src/scaffold/trace.cpp`

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

class SpanGuard {
public:
    SpanGuard(const std::string& component,
              const std::string& operation,
              const std::string& trace_id);
    ~SpanGuard();
    void set_attribute(const std::string& key, const std::string& value);
    void set_status(const std::string& status);
private:
    TraceSpan span_;
};
```

**验收**：
- [ ] 结构定义完整
- [ ] SpanGuard RAII 正确
- [ ] 可编译

### Step 1.2：Trace 收集器

**做什么**：实现 span 收集和存储

**产出**：`src/scaffold/trace_collector.h`, `src/scaffold/trace_collector.cpp`

**关键逻辑**：
- 收集所有 span
- 按 trace_id 分组
- 序列化为 JSON

**验收**：
- [ ] 收集正确
- [ ] 分组正确
- [ ] 序列化正确

### Step 1.3：Trace 测试

**做什么**：编写 Trace 单元测试

**产出**：`tests/unit/trace_test.cpp`

**测试用例**（至少 8 个）：
- SpanGuard 创建/销毁
- 属性设置
- 状态设置
- 多个 span 收集
- **错误测试**：空 component
- **错误测试**：空 operation
- **边界测试**：1000 个 span 收集
- **边界测试**：超长属性值（1KB）

**验收**：8+ 测试用例全通过

---

## Task 2：Explain 基础设施

**目标**：实现 Explain 接口

### Step 2.1：Explain 结构定义

**做什么**：定义 ExplainInfo

**产出**：`src/scaffold/explain.h`

```cpp
struct ExplainInfo {
    ExecutionMode execution_mode;
    std::string path;
    ConstraintClassification constraint_classification;
    std::string distribution;
    double estimated_exclusion_rate;
};
```

**验收**：
- [ ] 结构定义完整
- [ ] 可编译

### Step 2.2：Explain 实现

**做什么**：为每个组件实现 explain()

**产出**：各组件的 explain() 方法

**验收**：
- [ ] Parser::explain() 正确
- [ ] Sampler::explain() 正确
- [ ] Validator::explain() 正确

### Step 2.3：Explain 测试

**做什么**：编写 Explain 单元测试

**产出**：`tests/unit/explain_test.cpp`

**测试用例**（至少 6 个）：
- Parser explain
- Sampler explain（uniform）
- Sampler explain（gaussian）
- Validator explain
- **错误测试**：空约束 explain
- **边界测试**：100 个约束 explain

**验收**：6+ 测试用例全通过

---

## Task 3：Metrics 基础设施

**目标**：实现 /metrics 端点

### Step 3.1：Metrics 注册

**做什么**：实现 metrics 注册和暴露

**产出**：`src/scaffold/metrics.h`, `src/scaffold/metrics.cpp`

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

**验收**：
- [ ] Counter 正确
- [ ] Histogram 正确
- [ ] Gauge 正确
- [ ] Prometheus 格式正确

### Step 3.2：/metrics 端点

**做什么**：实现 HTTP /metrics 端点

**产出**：`src/api/metrics_handler.cpp`

**验收**：
- [ ] 返回 Prometheus 格式
- [ ] 所有指标正确
- [ ] 错误时不崩溃

### Step 3.3：Metrics 测试

**做什么**：编写 metrics 单元测试

**产出**：`tests/unit/metrics_test.cpp`

**测试用例**（至少 8 个）：
- Counter 递增
- Histogram 观察
- Gauge 设置
- Prometheus 格式
- **错误测试**：未注册的指标
- **错误测试**：负值 Counter
- **边界测试**：极大值
- **边界测试**：极小值

**验收**：8+ 测试用例全通过

---

## Task 4：确定性测试框架

**目标**：实现 seed 固定 + 参考快照

### Step 4.1：快照管理

**做什么**：实现快照保存和比对

**产出**：`src/scaffold/snapshot.h`, `src/scaffold/snapshot.cpp`

```cpp
class SnapshotManager {
public:
    Result<void> save(const ArrowBatch& batch, const std::string& name);
    Result<ArrowBatch> load(const std::string& name);
    Result<bool> compare(const ArrowBatch& batch, const std::string& name);
};
```

**验收**：
- [ ] 保存正确
- [ ] 加载正确
- [ ] 比对正确

### Step 4.2：参考快照生成

**做什么**：生成 v1 参考快照

**产出**：`tests/snapshots/` 目录

**快照**：
- physics_seed42_1000rows_uniform.parquet
- physics_seed42_1000rows_gaussian.parquet
- evidence_package_v1_seed42.json

**验收**：
- [ ] 快照文件存在
- [ ] 快照可读取
- [ ] 快照与生成结果一致

### Step 4.3：确定性测试

**做什么**：编写确定性测试

**产出**：`tests/integration/determinism_test.cpp`

**测试用例**（至少 6 个）：
- seed=42 uniform 两次一致
- seed=42 gaussian 两次一致
- seed=43 与 seed=42 不同
- **错误测试**：快照损坏 → 测试失败
- **边界测试**：100 次重复生成一致
- **边界测试**：不同 batch_size 相同输出

**验收**：6+ 测试用例全通过

---

## Task 5：CI/CD 基础设施

**目标**：实现 CI 配置

### Step 5.1：GitHub Actions 配置

**做什么**：编写 CI 工作流

**产出**：`.github/workflows/ci.yml`

**关键逻辑**：
- 编译
- 单元测试
- 集成测试
- E2E 测试
- 快照比对
- Schema 验证

**验收**：
- [ ] CI 可触发
- [ ] 所有步骤运行
- [ ] 失败时阻塞合并

### Step 5.2：CMake 测试配置

**做什么**：配置 CMake 测试目标

**产出**：`CMakeLists.txt` 更新

**验收**：
- [ ] `cmake --build . --target test` 可运行
- [ ] 测试分类（unit/integration/e2e）

### Step 5.3：标准数据集

**做什么**：创建标准测试数据集

**产出**：`tests/fixtures/sensor_1000.parquet`

**验收**：
- [ ] 数据集存在
- [ ] 数据集可读取
- [ ] 数据集在 CI 中可用

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: Trace | 3 | 0.3w | ⬜ |
| Task 2: Explain | 3 | 0.2w | ⬜ |
| Task 3: Metrics | 3 | 0.3w | ⬜ |
| Task 4: 确定性测试 | 3 | 0.3w | ⬜ |
| Task 5: CI/CD | 3 | 0.4w | ⬜ |
| **合计** | **15** | **1.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| CI 环境依赖复杂 | 使用 Docker 容器 |
| 快照跨平台不一致 | 使用 Arrow 标准格式 |
| Metrics 性能影响 | 采样率控制 |
| Trace 内存泄漏 | RAII 自动释放 |
