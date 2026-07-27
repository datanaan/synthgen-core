SynthGen Core v4 一致性检查
文档性质：版本级一致性验证
版本：v1.0
日期：2026-05-10
检查范围：v4 全部 Unit（U-Z）与上游文档的一致性

---

## 一、接口一致性

### 1.1 Unit U (#25+#26) 与 v4 设计规范

| 检查项 | 设计规范定义 | Unit U spec | 一致 |
|--------|------------|------------|------|
| WindowTypeV2 枚举 | kInterval/kRows/kPartitionBy | ✅ 一致 | ✅ |
| RowsWindowDef 字段 | row_count, column_name, function, min_val, max_val | ✅ 一致 | ✅ |
| PartitionWindowDef 字段 | partition_column, interval_spec, aggregate_column, function, min_val, max_val | ✅ 一致 | ✅ |

### 1.2 Unit V (#27) 与 v4 设计规范

| 检查项 | 设计规范定义 | Unit V spec | 一致 |
|--------|------------|------------|------|
| SessionWindowDef 字段 | session_column, gap_ms, aggregate_column, function, min_val, max_val | ✅ 一致 | ✅ |
| SessionWindowEngine::compute_session_windows | ArrowBatch, SessionWindowDef, Schema | ✅ 一致 | ✅ |

### 1.3 Unit W (#28) 与 v4 设计规范

| 检查项 | 设计规范定义 | Unit W spec | 一致 |
|--------|------------|------------|------|
| CompletenessScore 字段 | score, dimension_scores, is_fully_constrained | ✅ 一致 | ✅ |
| CompletenessScorer::score() | constraints → CompletenessScore | ✅ 一致 | ✅ |
| should_allow_full_function | score, threshold → bool | ✅ 一致 | ✅ |

### 1.4 Unit X (#29+#30) 与 v4 设计规范

| 检查项 | 设计规范定义 | Unit X spec | 一致 |
|--------|------------|------------|------|
| CounterExampleResult 字段 | available, status, violation_regions | ✅ 一致 | ✅ |
| EvidencePackageV3 字段 | schema_version, model_provenance, completeness_score, counter_example, bias_report_ref | ✅ 一致 | ✅ |
| ModelVersionProvenance 字段 | model_name, model_version_id, training_data_range, fidelity_score, was_compacted | ✅ 一致 | ✅ |

---

## 二、依赖一致性

| 依赖 | 设计规范 | Unit spec | 一致 |
|------|---------|----------|------|
| Unit U ← v2#11 | ✅ | ✅ | ✅ |
| Unit V ← Unit U | ✅ | ✅ | ✅ |
| Unit W ← v2#13 | ✅ | ✅ | ✅ |
| Unit X ← Unit W | ✅ | ✅ | ✅ |
| Unit X ← v2#13 | ✅ | ✅ | ✅ |

---

## 三、诚实声明一致性

| 声明 | 设计规范 | Unit spec | 一致 |
|------|---------|----------|------|
| 完备度非布尔 | 0.0-1.0 | ✅ | ✅ |
| 布尔是 1.0 特例 | is_fully_constrained | ✅ | ✅ |
| 反例搜索 research | status: available/deferred/failed | ✅ | ✅ |

---

## 四、[COORDINATE] 一致性

| 协调项 | 设计规范 | Unit spec | 一致 |
|--------|---------|----------|------|
| C5 待测模型接入协议 | 标注 | Unit X 标注 | ✅ |
| C6 反例搜索理论基础 | 标注 | Unit X 标注 | ✅ |

---

## 五、检查结论

**v4 一致性检查结果：✅ 全部通过**

所有 Unit 的接口定义、依赖关系、诚实声明和协调项标注与 v4 阶段设计规范完全一致。
