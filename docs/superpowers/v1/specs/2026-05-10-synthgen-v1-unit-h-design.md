SynthGen Core v1 Unit H 设计规范：Scaffold v1
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit H 实施计划
组件：Explain 最小版 / Trace 最小版 / 可观测性最小版 / 确定性测试框架 / CI/CD
估算：1.5 周
依赖：与 Unit C-G 并行

---

## 一、本 Unit 交付什么

Unit H 实现 v1 的五项脚手架设施。脚手架与功能组件享有同等地位——脚手架不过 = 版本不交付。

交付物：
1. **Explain 最小版**：约束分类 + 执行模式 + 路由决策
2. **Trace 最小版**：span 结构 + trace_id
3. **可观测性最小版**：/metrics 端点（吞吐量 + 延迟 + 内存）
4. **确定性测试框架**：seed 固定 + 参考快照 + Schema 验证
5. **CI/CD 基础设施**：每次 PR 触发测试

---

## 二、Explain 最小版

### 2.1 功能

**回答的问题**：系统会走什么路径？

```cpp
struct ExplainInfo {
    ExecutionMode execution_mode;  // row_by_row | stateful_batch | two_phase
    std::string path;              // physics_sampling | constrained_fusion | ...
    ConstraintClassification constraint_classification;
    std::string distribution;      // uniform | gaussian
    double estimated_exclusion_rate;
};

// v1 输出示例：
// {
//   "execution_mode": "row_by_row",
//   "path": "physics_sampling",
//   "constraint_classification": {"value_range": 2, "inter_row": 0, "aggregate": 0},
//   "distribution": "uniform",
//   "estimated_exclusion_rate": 0.0
// }
```

### 2.2 实现方式

- 每个组件提供 `explain() const` 方法
- 生成请求前可调用 `explain()` 预览
- Explain 输出结构一旦定义，即成隐式 API，不可随意改

### 2.3 验收标准

- [ ] `client.explain()` 返回 execution_mode + path + constraint_classification
- [ ] 纯物理路径 → path = "physics_sampling"
- [ ] 含值域约束 → constraint_classification.value_range > 0
- [ ] 不含行间约束 → constraint_classification.inter_row = 0
- [ ] 不含聚合约束 → constraint_classification.aggregate = 0
- [ ] estimated_exclusion_rate = 0.0（v1 纯物理）

---

## 三、Trace 最小版

### 3.1 Span 结构

```cpp
struct TraceSpan {
    std::string trace_id;       // = package_id（全局唯一）
    std::string span_id;        // 组件内唯一
    std::string parent_span_id; // 上游组件的 span_id
    std::string component;      // "parser" | "physics_engine" | "validator" | ...
    std::string operation;      // "parse" | "generate_batch" | "validate_row" | ...
    Timestamp start_time;
    Timestamp end_time;
    std::string status;         // "ok" | "error" | "timeout"
    std::map<std::string, std::string> attributes;
};
```

### 3.2 实现方式

- RAII SpanGuard：构造时创建 span，析构时写入
- 不侵入业务逻辑
- 每个 span 写入 EvidencePackage provenance

```cpp
Result<ParseResult> Parser::parse(const std::string& source) const {
    SpanGuard span("parser", "parse", trace_id_);
    // ... 解析逻辑 ...
    span.set_attribute("statement_count", std::to_string(statements.size()));
    return result;
}
```

### 3.3 v1 必须产生的 Span

| 组件 | 操作 | 属性 |
|------|------|------|
| parser | parse | statement_count |
| physics_engine | generate | limit, distribution, batch_count |
| physics_engine | generate_batch | batch_index, batch_rows |
| validator | validate_batch | rows_checked, rows_passed |
| tail_report | build | — |
| evidence | build | — |
| import | import | table_id, rows_imported, rows_skipped |

### 3.4 验收标准

- [ ] 每个生成请求产生唯一 trace_id
- [ ] EvidencePackage.provenance.trace_spans 非空
- [ ] 每个组件产生至少 1 个 span
- [ ] 错误时 span.status = "error"
- [ ] span 包含 start_time + end_time + duration

---

## 四、可观测性最小版

### 4.1 Metrics

```cpp
struct Metrics {
    Counter generation_total;       // 生成调用次数
    Histogram generation_duration_ms; // 生成耗时
    Counter generation_rows;        // 生成总行数
    Counter validation_total;       // 验证调用次数
    Counter validation_passed;      // 验证通过次数
    Counter validation_failed;      // 验证失败次数
    Counter import_total;           // 导入调用次数
    Counter import_errors;          // 导入错误次数
    Gauge memory_usage_bytes;       // 内存占用
};
```

### 4.2 /metrics 端点

```
GET /v1/metrics

# 返回 Prometheus 格式：
generation_total 100
generation_duration_ms_bucket{le="10"} 50
generation_duration_ms_bucket{le="100"} 95
generation_duration_ms_bucket{le="1000"} 100
generation_rows 100000
validation_total 100
validation_passed 100
validation_failed 0
import_total 50
import_errors 2
memory_usage_bytes 104857600
```

### 4.3 验收标准

- [ ] /metrics 返回 Prometheus 格式
- [ ] generation_total 正确计数
- [ ] generation_duration_ms 为直方图
- [ ] memory_usage_bytes 为 Gauge
- [ ] 错误时 metrics 不中断

---

## 五、确定性测试框架

### 5.1 种子固定

- 固定 seed → 固定输出（逐行可比对）
- 参考快照存入 `tests/snapshots/`
- CI 自动比对

### 5.2 参考快照

```
tests/snapshots/
├── physics_seed42_1000rows_uniform.parquet
├── physics_seed42_1000rows_gaussian.parquet
├── evidence_package_v1_seed42.json
└── README.md（快照生成说明）
```

### 5.3 快照比对

```cpp
TEST(DeterminismTest, Seed42Uniform) {
    auto result = sampler.generate(request_with_seed_42);
    auto snapshot = load_snapshot("physics_seed42_1000rows_uniform.parquet");
    EXPECT_BATCH_EQ(result.data, snapshot);
}
```

### 5.4 EvidencePackage Schema 验证

- 每次生成后自动验证 EvidencePackage 结构
- 使用 JSON Schema 验证
- 字段缺失/类型错误 → 测试失败

### 5.5 验收标准

- [ ] seed=42 两次生成逐行一致
- [ ] 参考快照与生成结果一致
- [ ] EvidencePackage Schema 验证通过
- [ ] 快照不一致 = 测试失败
- [ ] 快照生成脚本可用

---

## 六、CI/CD 基础设施

### 6.1 CI 配置

```yaml
# .github/workflows/ci.yml
name: CI
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: cmake -B build && cmake --build build
      - name: Unit Tests
        run: ctest --test-dir build --output-on-failure
      - name: Integration Tests
        run: ./build/tests/integration_tests
      - name: E2E Tests
        run: ./build/tests/e2e_tests
      - name: Snapshot Check
        run: ./build/tests/snapshot_tests
      - name: Schema Validation
        run: ./build/tests/schema_validation_tests
```

### 6.2 标准测试数据集

- **sensor_1000.parquet**：1000 行传感器数据
- 生成脚本：`scripts/generate_test_data.py`
- 版本控制：数据集与代码一起版本控制

### 6.3 验收标准

- [ ] PR 提交触发 CI
- [ ] CI 运行单元测试 + 集成测试 + E2E 测试
- [ ] CI 运行快照比对
- [ ] CI 运行 Schema 验证
- [ ] CI 失败 = 合并阻塞
- [ ] 标准测试数据集在 CI 中可用

---

## 七、Unit H 验收标准

### 7.1 Explain 验收

- [ ] explain() 返回正确信息
- [ ] 纯物理路径 → path = "physics_sampling"
- [ ] 含值域约束 → value_range > 0

### 7.2 Trace 验收

- [ ] 每个组件产生 span
- [ ] trace_id 全局唯一
- [ ] 错误时 status = "error"
- [ ] spans 写入 EvidencePackage

### 7.3 可观测性验收

- [ ] /metrics 返回 Prometheus 格式
- [ ] 所有指标正确
- [ ] 错误时 metrics 不中断

### 7.4 确定性测试验收

- [ ] seed 固定 → 输出一致
- [ ] 参考快照可比对
- [ ] EvidencePackage Schema 验证通过
- [ ] 快照不一致 = 测试失败

### 7.5 CI/CD 验收

- [ ] PR 触发 CI
- [ ] 单元 + 集成 + E2E 测试
- [ ] 快照比对
- [ ] Schema 验证
- [ ] CI 失败阻塞合并

### 7.6 测试验收

- [ ] 脚手架测试用例 ≥ 20
- [ ] CI 配置正确
- [ ] 标准数据集可用
