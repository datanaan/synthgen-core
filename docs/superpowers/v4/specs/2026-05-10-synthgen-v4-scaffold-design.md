SynthGen Core v4 脚手架设计规范：Explain 增强 + Trace 增强
文档性质：脚手架级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v4 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：v4 脚手架实施计划
组件：v4 脚手架工程（暗线）
估算：0.5 周

---

## 一、本 Unit 交付什么

v4 脚手架交付两项增强：Explain 增强（完备度评分影响展示）和 Trace 增强（反例搜索轨迹记录）。

交付物：
1. **Explain v4 增强**：完备度评分 + 维度分解 + 模型溯源 + 反例搜索状态
2. **Trace v4 增强**：反例搜索探索轨迹（如执行）+ 完备度评分变化记录

---

## 二、Explain 增强

### 2.1 完备度评分展示

Explain 输出新增字段：

```json
{
  "completeness": {
    "score": 0.75,
    "dimensions": [
      {"name": "value_range", "score": 1.0, "weight": 0.3},
      {"name": "inter_row", "score": 0.5, "weight": 0.2},
      {"name": "aggregate", "score": 0.8, "weight": 0.2},
      {"name": "statistical_signature", "score": 0.0, "weight": 0.2},
      {"name": "physical_legality", "score": 1.0, "weight": 0.1}
    ],
    "is_fully_constrained": false,
    "path_decision": "post_filter"
  }
}
```

### 2.2 模型溯源展示

```json
{
  "model_provenance": {
    "model_name": "production_v3",
    "model_version_id": "v3.2.1",
    "fidelity_score": 0.92,
    "was_compacted": true
  }
}
```

### 2.3 反例搜索状态展示

```json
{
  "counter_example_search": {
    "status": "available",
    "violation_regions_count": 3
  }
}
```

---

## 三、Trace 增强

### 3.1 完备度评分变化 Trace

```json
{
  "span_id": "completeness_score_v4",
  "attributes": {
    "completeness.score": 0.75,
    "completeness.is_fully_constrained": false,
    "completeness.dimensions_count": 5
  }
}
```

### 3.2 反例搜索轨迹 Trace（如执行）

```json
{
  "span_id": "counter_example_search_v4",
  "attributes": {
    "search.status": "available",
    "search.iterations": 47,
    "search.duration_ms": 3200,
    "violation_regions_count": 3
  }
}
```

---

## 四、验收标准

### 4.1 Explain 验收

- [ ] 完备度评分和维度分解正确展示
- [ ] 模型溯源信息正确展示
- [ ] 反例搜索状态正确展示
- [ ] deferred 状态下 Explain 不报错

### 4.2 Trace 验收

- [ ] 完备度评分 Trace span 正确记录
- [ ] 反例搜索 Trace span 正确记录（如执行）
- [ ] deferred 状态下 Trace 不记录反例搜索 span

### 4.3 可观测性验收

- [ ] completeness_score 作为 Prometheus gauge 暴露
- [ ] counter_example_status 作为 Prometheus gauge 暴露（0=deferred, 1=available, 2=failed）

### 4.4 测试验收

- [ ] 至少 8 个测试用例
