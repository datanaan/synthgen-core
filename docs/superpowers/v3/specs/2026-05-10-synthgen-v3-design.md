SynthGen Core v3 阶段设计规范
文档性质：v3 阶段级约束——v3 全部 Unit 的共同基础
版本：v1.0
日期：2026-05-10
适用范围：SynthGen Core v3 时间智能
上游文档：整体设计规范 v1.0、路线图 v1.4、工程框架 v0.4、工程执行守则 v1.0
下游文档：Unit Q-T 的各 spec 和 plan

---

## 一、v3 产品故事

数据在演化，模型在进化。时间旅行回到任意版本，持续对齐保持数据与时偕行。

**v3 相对 v2 的核心变化**：

v2 建立了完整的三类约束体系和执行路由器，但所有生成都使用同一个模型。v3 引入模型版本管理，每次数据更新产生新模型版本，用户可以时间旅行回到任意版本，持续对齐保持模型与最新数据同步。

**v3 能做的**：
- 模型版本链：创建/引用/列表 + 不可变写入
- GC compaction：3 保护条件 + 自动合并
- 时间旅行(AS OF)：按版本读取快照 + compaction 退化行为
- 持续对齐(UPDATE MODEL)：新数据纳入 + 漂移检测 + 代偿收敛时限
- tail_report 增强版：排除率与 data_grade 联动 + fidelity_mismatch 标记
- 存储模型层：检查点存储 + 流式加载 + atomic_write 事务
- 偏差报告：compaction 偏差字段完整

**v3 做不了的（诚实声明）**：
- ❌ 行数窗口/分组时间窗口/会话窗口 → v4
- ❌ 完备度连续化评分 → v4
- ❌ 反例搜索 → v4 (research)
- ❌ 高维数据引擎优化 → 后续版本

---

## 二、v3 依赖图

```
Unit Q: 模型版本链 ──────────┐
    │ #18 版本链 (1w)         │
    │ ← v1#4 存储+元数据层     │
    └──────┬──────────────────┤
           │                  │
Unit R: GC compaction        │
    │ #19 GC (1w)             │
    │ ← #18 版本链             │
    └──────┬──────────────────┤
           │                  │
Unit S: 时间旅行+持续对齐 ←──┘
    │ #20 时间旅行 (0.5w)      │
    │ #21 持续对齐 (1.5w)      │
    │ ← #18 + #19 + v2#13 + v2#15b
    └──────┬──────────────────┤
           │                  │
Unit T: 增强组件              │
    │ #22 tail_report增强 (1w)  │
    │ #23 存储模型层 (1w)       │
    │ #24 偏差报告 (0.5w)       │
    │ ← v2#14 + #21 + #19 + #18│
    └──────┘

Unit U: 脚手架 v3 ──── 与 Q-T 并行
Unit V: 工具线 v3 ──── 与 Q-T 并行
```

### 2.1 开发波次

| 波次 | 时间 | 活跃 Unit | 里程碑 |
|------|------|----------|---------|
| Wave 1 | 第1-2周 | Q, T(#23存储模型层) | 版本链创建/引用/列表；存储模型层 atomic_write 就位 |
| Wave 2 | 第3-4周 | R, T(#22,#24) | GC compaction 3 保护条件生效；tail_report 增强版呈现；偏差报告字段完整 |
| Wave 3 | 第4-6周 | S, U, V | 时间旅行正确版本；持续对齐漂移检测；脚手架/工具增强 |

---

## 三、v3 组件接口定义

### 3.1 #18 模型版本链

```cpp
namespace synthgen::storage::version {

struct ModelVersion {
    std::string version_id;            // 版本 ID
    std::string model_name;            // 模型名称
    std::string parent_version_id;     // 父版本 ID
    Timestamp created_at;
    std::string created_by;            // 创建者（user/system/auto_compact）
    bool is_immutable = true;         // 不可变写入

    // 模型元数据
    std::string training_data_range;   // 训练数据时间范围
    double fidelity_score;            // 保真度评分
    int64_t training_rows;            // 训练行数
    std::map<std::string, std::string> custom_metadata;
};

class ModelVersionChain {
public:
    explicit ModelVersionChain(StorageBackend& storage);

    // 创建新版本
    Result<ModelVersion> create_version(
        const std::string& model_name,
        const std::string& parent_version_id,
        const ModelVersion& metadata);

    // 引用版本
    Result<const ModelVersion*> get_version(const std::string& version_id) const;

    // 列出版本
    Result<std::vector<ModelVersion>> list_versions(
        const std::string& model_name,
        int limit = 100) const;

    // 不可变保证：已写入版本不可修改
    // 此方法签名故意不接受有效载荷，仅用于返回 kImmutableViolation
    Result<void> modify_version(const std::string& version_id);  // → 永远返回 kImmutableViolation
};

}  // namespace synthgen::storage::version
```

### 3.2 #19 GC compaction

```cpp
namespace synthgen::storage::gc {

// 3 保护条件
enum class ProtectionCondition {
    kSnapshotReferenced,     // 快照引用（有生成请求引用此版本）
    kAnchored,               // 用户锚定（用户显式保留）
    kWithinNVersions,        // N 版本内（最近 N 个版本不 compact）
};

struct CompactionConfig {
    int keep_recent_n = 10;           // 保留最近 N 个版本（与路线图 v1.4 对齐）
    bool auto_compact = true;          // 自动 compaction
    int64_t compact_interval_ms = 3600000;  // 1 小时检查一次
};

struct CompactionResult {
    std::vector<std::string> compacted_versions;  // 被 compact 的版本
    std::string merged_version_id;                // 合并后的版本
    CompactionBias bias;                            // 偏差信息
};

class GcCompactor {
public:
    explicit GcCompactor(
        ModelVersionChain& version_chain,
        StorageBackend& storage,
        const CompactionConfig& config);

    // 检查保护条件
    bool is_protected(const ModelVersion& version) const;

    // 执行 compaction
    Result<CompactionResult> compact(const std::string& model_name);

    // 自动 compaction（定时调用）
    Result<void> auto_compact_check();
};

}  // namespace synthgen::storage::gc
```

### 3.3 #20 时间旅行(AS OF)

```cpp
namespace synthgen::storage::timetravel {

struct TimeTravelResult {
    ArrowBatch data;
    ModelVersion version;
    std::optional<CompactionBiasReport> bias_report;  // compaction 退化时的偏差报告
    bool was_degraded;                                  // 是否退化到最近版本
};

class TimeTravelEngine {
public:
    Result<TimeTravelResult> query_as_of(
        const std::string& model_name,
        const std::string& version_id,
        const Schema& schema);

    // compaction 退化行为：
    // 如果请求的版本已被 compact：
    // 1. 返回最近可用版本
    // 2. 填充 bias_report（requested vs returned vs reason）
    // 3. was_degraded = true
};

}  // namespace synthgen::storage::timetravel
```

### 3.4 #21 持续对齐(UPDATE MODEL)

```cpp
namespace synthgen::engine::alignment {

struct AlignmentRequest {
    std::string model_name;
    std::string current_version_id;
    std::string incorporate_from;       // 新数据来源
    std::string where_condition;        // 新数据筛选条件
    std::string drift_check = "auto";   // 漂移检测模式："auto"=KS检验(默认), "kl"=KL散度, "none"=不检测
    std::string save_as;                 // 新版本名称
};

struct AlignmentResult {
    ModelVersion new_version;
    bool drift_detected;
    double drift_score;
    std::string compensation_status;    // 代偿模型状态
    Timestamp compensation_deadline;     // 代偿收敛时限
};

class ContinuousAlignmentEngine {
public:
    explicit ContinuousAlignmentEngine(
        ModelVersionChain& version_chain,
        DataEngineV1& data_engine,
        ExecutionRouter& router);

    Result<AlignmentResult> update_model(const AlignmentRequest& request);

    // 漂移检测
    Result<bool> detect_drift(
        const ModelVersion& current,
        const ArrowBatch& new_data);

    // 代偿收敛时限
    Result<void> set_compensation_deadline(
        const std::string& model_name,
        Timestamp deadline);
};

}  // namespace synthgen::engine::alignment
```

### 3.5 #22 tail_report 增强版

```cpp
namespace synthgen::engine::evidence {

struct TailReportV3 : public TailReportV1 {
    // v3 新增

    // 排除率与 data_grade 联动
    ExclusionRateBand rate_band;
    std::string data_grade;

    // fidelity_mismatch 标记
    bool fidelity_mismatch = false;
    std::string mismatch_reason;         // 如 "compaction_degraded"

    // 代偿模型状态
    std::string compensation_status;     // "converging" / "converged" / "diverging"
    Timestamp compensation_deadline;
};

}  // namespace synthgen::engine::evidence
```

### 3.6 #23 存储模型层

```cpp
namespace synthgen::storage::model {

class ModelStorageLayer {
public:
    explicit ModelStorageLayer(StorageBackend& storage);

    // 检查点存储
    Result<void> save_checkpoint(
        const std::string& model_name,
        const std::string& version_id,
        const DataEngineV1& engine);

    // 流式加载
    Result<DataEngineV1> load_model(
        const std::string& model_name,
        const std::string& version_id);

    // 版本索引
    Result<std::vector<std::string>> list_model_versions(
        const std::string& model_name);

    // atomic_write 事务
    // 两阶段提交：先写数据 → 写元数据 → 提交审计
    Result<void> atomic_write(
        const std::string& model_name,
        const DataEngineV1& engine,
        const ModelVersion& version);
};

}  // namespace synthgen::storage::model
```

### 3.7 #24 偏差报告

```cpp
namespace synthgen::storage::gc {

struct CompactionBiasReport {
    std::string requested_version;       // 请求的版本
    std::string returned_version;        // 实际返回的版本
    std::string reason;                  // compacted / anchored / snapshot_referenced
    std::vector<std::string> merged_from; // 合并了哪些版本
    std::string training_data_range;     // 训练数据范围
    double fidelity_score_range_min;     // 保真度评分范围
    double fidelity_score_range_max;
    bool version_mismatch;               // 版本不匹配标记
};

}  // namespace synthgen::storage::gc
```

---

## 四、v3 诚实声明

| 声明 | EvidencePackage 体现 | 代码实现位置 |
|------|---------------------|------------|
| 模型版本可追溯 | provenance.model_version_chain | #18 版本链 |
| compaction 退化诚实 | bias_report + was_degraded | #20 时间旅行 |
| 漂移检测 | drift_detection: {available: true, ...} | #21 持续对齐 |
| 代偿收敛时限 | tail_report.compensation_deadline | #21 持续对齐 |
| 存储事务原子性 | provenance.trace_spans 含 atomic_write span（begin_data + begin_meta + commit 三阶段） | #23 存储模型层 |
| fidelity_mismatch 标记 | tail_report.fidelity_mismatch | #22 tail_report |

---

## 五、v3 错误测试验收标准

**版本链错误测试**：
- [ ] 修改已写入版本返回 kImmutableViolation
- [ ] 引用不存在版本返回 kVersionNotFound
- [ ] 创建版本时父版本不存在返回 kParentNotFound
- [ ] 版本链循环检测

**GC 错误测试**：
- [ ] 保护条件全部生效（快照引用/锚定/N版本内）
- [ ] compaction 中断后恢复
- [ ] 合并元数据保留

**时间旅行错误测试**：
- [ ] 请求已被 compact 的版本返回偏差报告
- [ ] 请求不存在版本返回 kVersionNotFound
- [ ] compaction 退化行为正确

**持续对齐错误测试**：
- [ ] 新数据为空时返回 kEmptyTrainingData
- [ ] 漂移检测失败时保守估计
- [ ] 代偿收敛超时时降级
- [ ] 数据引擎不可用时返回 kDataEngineUnavailable

**tail_report 增强错误测试**：
- [ ] fidelity_mismatch 在 compaction 退化时标记
- [ ] 排除率与 data_grade 联动正确

---

## 六、v3 脚手架验收标准

| 脚手架 | v3 交付 | 验收标准 |
|--------|---------|---------|
| Explain 增强 | compaction 影响预估 | explain() 显示退化版本和偏差报告 |
| Trace 增强 | 持续对齐模型更新前后变化 | 模型更新 span 含 drift_score + compensation_status |
| 可观测性增强 | 版本链状态 + GC 历史 | /metrics 新增 model_versions_count + gc_compaction_history |
| 错误注入增强 | compaction 冲突场景 | 注入 compaction 冲突后系统正确处理 |
| 测试增强 | compaction 前后一致性 | compaction 后生成结果与直接生成结果一致 |

---

## 七、v3 工具验收标准

| 工具 | 验收标准 |
|------|---------|
| 组件模板引擎 v0.3 | 生成 #18-#24 任一组件骨架 |
| 测试辅助库 v0.3 | compaction 前后一致性测试辅助宏 |
| Schema 校验器 v1.1 | 版本链字段校验规则 |
| Trace 分析工具 v0.2 | compaction 冲突 span 检测 |

---

## 八、v3 Unit 分配总表

| Unit | 名称 | 组件 | 估算 | 依赖 | 波次 |
|------|------|------|------|------|------|
| Q | 模型版本链 | #18 | 1w | v1#4 | W1 |
| R | GC compaction | #19 | 1w | #18 | W2 |
| S | 时间旅行+持续对齐 | #20+#21 | 2w | #18+#19+v2#13+v2#15b | W3 |
| T | tail_report+存储模型+偏差 | #22+#23+#24 | 2.5w | v2#14+#21+#19+#18 | W1-2 |
| U | 脚手架 v3 | 5项增强 | 1w | 与Q-T并行 | W3 |
| V | 工具线 v3 | 4项增强 | 0.5w | W3+ | W3 |

**v3 总估算**：6-7 周

---

## 九、[COORDINATE] 协调项占位

### C4: 持续对齐与数据引擎接口协议 [COORDINATE]

**待决策**：#21 持续对齐与 v2#15b 数据引擎的接口协议

| 问题 | 选项 | 推荐 |
|------|------|------|
| 模型训练接口 | fit() 支持增量更新？或重新 fit？ | 增量更新（更高效） |
| 待测模型接入协议 | 定义标准协议供 v4 反例搜索使用 | 必须在 v3 定义 |

**占位推荐**：fit() 支持增量更新 + v3 验收标准增加"定义待测模型接入协议"。

### C5: 待测模型接入协议 [COORDINATE]

**待决策**：v4 反例搜索前置条件——待测模型接入协议

此协议需在 v3 阶段定义，否则 v4 反例搜索无法启动。

**占位内容**：
```cpp
// 待测模型接入协议（v3 定义，v4 使用）
struct TestModelProtocol {
    std::string model_id;
    std::string model_type;           // "kde" / "gmm" / "custom"
    std::vector<std::string> supported_queries;  // 支持的查询类型
    Result<double> query_density(const std::vector<double>& point);
    Result<std::vector<double>> query_boundary(const std::string& constraint);
};
```

### C10: 漂移检测算法选型 [COORDINATE→DECIDED]

**决策**：v3 `drift_check = "auto"` 使用 **KS 检验（Kolmogorov-Smirnov test）** 作为默认漂移检测算法。

| 选项 | 精度 | 性能 | 多维支持 | 选择 |
|------|------|------|---------|------|
| KS 检验 | 适中（1D 最优） | O(n log n) | 每维独立检验 | ✅ 默认 |
| KL 散度 | 高（分布差异敏感） | O(n) 估计 | 原生多维 | 可选 |
| Wasserstein 距离 | 高（几何距离） | O(n log n) | 原生多维 | 备选 |

**理由**：KS 检验实现简单、统计理论成熟、1D 场景最优。v3 中低维 KDE 为主的场景足够。多维漂移通过逐维 KS + 多重检验校正（Bonferroni）处理。

**扩展点**：`drift_check` 参数支持 `"auto"`/`"ks"`/`"kl"`/`"none"`，后续版本可增加更多算法。
