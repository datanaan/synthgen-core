SynthGen Core v4 阶段设计规范
文档性质：v4 阶段级约束——v4 全部 Unit 的共同基础
版本：v1.0
日期：2026-05-10
适用范围：SynthGen Core v4 高级分析
上游文档：整体设计规范 v1.0、路线图 v1.4、工程框架 v0.4
下游文档：Unit U-X 的各 spec 和 plan

---

## 一、v4 产品故事

行数窗口、分组聚合、会话切分——窗口类型全面扩展。约束完备度从布尔走向连续评分。

**v4 research 声明**：#29 反例搜索标为 research 里程碑。如预研未完成则标记 deferred，不影响其他 v4 组件交付。

---

## 二、v4 依赖图

```
Unit U: #25 ROWS + #26 PARTITION BY ← v2#11 聚合引擎
Unit V: #27 SESSION ← #25/#26
Unit W: #28 完备度评分 ← v2#13 执行路由器
Unit X: #29 反例搜索(research) + #30 EvidencePackage v3 ← #28 + v2#13 + 待测模型接入协议
Unit Y: 脚手架 v4
Unit Z: 工具线 v4
```

---

## 三、v4 组件接口定义

### 3.1 #25+#26 行数窗口+分组时间窗口

```cpp
enum class WindowTypeV2 {
    kInterval,      // v2 已有
    kRows,          // v4: OVER (ROWS 100)
    kPartitionBy,   // v4: OVER (PARTITION BY col, INTERVAL 1 HOUR)
};

struct RowsWindowDef {
    int64_t row_count;
    std::string column_name;
    AggregateFunction function;
    std::optional<double> min_val;
    std::optional<double> max_val;
};

struct PartitionWindowDef {
    std::string partition_column;
    std::string interval_spec;
    std::string aggregate_column;
    AggregateFunction function;
    std::optional<double> min_val;
    std::optional<double> max_val;
};
```

### 3.2 #27 会话窗口

```cpp
struct SessionWindowDef {
    std::string session_column;
    int64_t gap_ms;
    std::string aggregate_column;
    AggregateFunction function;
    std::optional<double> min_val;
    std::optional<double> max_val;
};

class SessionWindowEngine {
public:
    Result<std::vector<AggregationWindow>> compute_session_windows(
        const ArrowBatch& batch,
        const SessionWindowDef& def,
        const Schema& schema);
};
```

### 3.3 #28 完备度连续化评分

**评分算法**：

约束完备度评分基于"约束对数据空间的覆盖程度"：

```
completeness_score = Σ(dim_score_i) / N

其中 dim_score_i（第 i 维的评分）：
- 无约束：0.0
- 仅值域约束：0.6（矩形域覆盖有限）
- 值域+条件约束(DURING/WHEN)：0.8（非矩形域覆盖更好）
- 值域+行间+聚合全覆盖：1.0（完备约束）

特殊规则：
- ORDER 列不计入评分维度（排序不是约束）
- ENUM 类型列视为有值域约束（枚举值即值域）
- score == 1.0 当且仅当所有数据列都有约束覆盖
```

`should_allow_full_function(score, threshold)` 的语义：当评分低于阈值时，退化到后筛选路径。默认阈值 1.0 意味着只有完全完备才走全功能路径。

```cpp
struct CompletenessScore {
    double score;                  // 0.0-1.0
    std::vector<DimensionScore> dimension_scores;
    bool is_fully_constrained;    // score == 1.0 特例
};

struct DimensionScore {
    std::string dimension;
    double score;
    bool is_constrained;
};

class CompletenessScorer {
public:
    Result<CompletenessScore> score(
        const std::vector<ConstraintDef>& constraints);
    bool should_allow_full_function(double score, double threshold = 1.0) const;
};
```

### 3.4 #29 反例搜索(research)

```cpp
struct CounterExampleResult {
    bool available;
    std::string status;             // "available" / "deferred" / "research_failed"
    std::vector<ConstraintViolationRegion> violation_regions;
};

class CounterExampleSearcher {
public:
    // 注意：此接口签名可能因 C5(待测模型协议) 或 C6(理论基础) 的决策而调整
    Result<CounterExampleResult> search(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints,
        const TestModelProtocol& test_model);
    // [COORDINATE] 前置：待测模型接入协议(v3定义) + 理论基础
};
```

### 3.5 #30 EvidencePackage v3

```cpp
// EvidencePackage v3（组合而非继承，参见整体设计规范 §6.1a）
struct EvidencePackageV3 {
    std::string schema_version = "v3";
    // v1 完整字段副本
    ConstraintSummary constraint_summary;
    ConservativeTailReport conservative_tail_report;
    std::string audit_immutability;
    ProvenanceBase provenance_base;
    // v2 新增字段副本
    PostFilterInfo post_filter_info;
    std::string degradation_path;
    IdentityDeclaration identity;
    std::optional<DataEngineInfo> data_engine_info;
    // v3 新增字段
    ModelVersionProvenance model_provenance;
    CompletenessScore completeness_score;
    std::optional<CounterExampleResult> counter_example;
    std::optional<std::string> bias_report_ref;
};

struct ModelVersionProvenance {
    std::string model_name;
    std::string model_version_id;
    std::string training_data_range;
    double fidelity_score;
    bool was_compacted;
};
```

---

## 四、v4 诚实声明

| 声明 | 体现 |
|------|------|
| 完备度评分非布尔 | 0.0-1.0 连续评分 |
| 布尔判断是1.0特例 | is_fully_constrained = (score == 1.0) |
| 反例搜索研究性 | status: available/deferred/research_failed |

---

## 五、v4 错误测试验收标准

- [ ] ROWS 0/负数返回 kInvalidWindowSpec
- [ ] PARTITION BY 不存在列返回 kUndefinedColumn
- [ ] SESSION GAP 0 返回 kInvalidWindowSpec
- [ ] 空 Schema 评分 = 0.0
- [ ] 所有列有约束评分 = 1.0
- [ ] 待测模型接入协议未定义返回 kProtocolNotDefined
- [ ] 反例搜索不收敛返回 kSearchNotConverged

---

## 五a、v4 脚手架验收标准

| 脚手架 | v4 交付 | 验收标准 |
|--------|---------|---------|
| Explain 增强 | 完备度评分+维度分解+模型溯源+反例搜索状态 | Explain 输出含 completeness.score + dimensions + model_provenance + counter_example_search |
| Trace 增强 | 反例搜索轨迹+完备度评分变化 | Trace span 含 completeness_score_v4 + counter_example_search_v4 |
| 可观测性增强 | 窗口类型分布+完备度趋势 | /metrics 新增 window_type_distribution + completeness_score_trend |
| 测试增强 | 窗口回归测试 | ROWS/PARTITION BY/SESSION 三种窗口各至少 1 个回归测试 |

---

## 六、v4 Unit 分配总表

| Unit | 组件 | 估算 | 标注 |
|------|------|------|------|
| U | #25+#26 | 2w | [IMPLEMENT] |
| V | #27 | 1.5w | [IMPLEMENT] |
| W | #28 | 1w | [IMPLEMENT] |
| X | #29+#30 | 2w | [COORDINATE] C5,C6 |
| Y | 脚手架 | 0.5w | [IMPLEMENT] |
| Z | 工具线 | 0w | [IMPLEMENT] |

**v4 总估算**：4-5 周

---

## 七、[COORDINATE] 协调项

**已知差异说明**：工程框架 v0.4 §2.3 表中将 ROWS/PARTITION BY 标注为 v2，与路线图 v1.4 和本规范（v4）不一致。以路线图 v1.4 为准，ROWS/PARTITION BY 属于 v4 组件。

### C5: 待测模型接入协议
需 v3 交付。如果 v3 未定义，v4 反例搜索无法启动。

### C6: 反例搜索理论基础
理论框架 v1.3 未包含反例搜索独立章节。如预研结论为"不可行"，v4 将不含反例搜索。
