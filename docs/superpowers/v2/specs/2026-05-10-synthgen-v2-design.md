SynthGen Core v2 阶段设计规范
文档性质：v2 阶段级约束——v2 全部 Unit 的共同基础
版本：v1.0
日期：2026-05-10
适用范围：SynthGen Core v2 约束完整
上游文档：整体设计规范 v1.0、路线图 v1.4、工程框架 v0.4、工程执行守则 v1.0
下游文档：Unit J-P 的各 spec 和 plan

---

## 一、v2 产品故事

行间依赖、窗口聚合、退化路径——完整的三类约束体系。数据引擎 v1(KDE) 就位，后筛选路径接入，执行路由器支持5条退化路径，每条路径有身份声明和审计记录。

**v2 相对 v1 的核心变化**：

v1 的生成流程是"物理引擎矩形域采样→值域验证"，是单一固定路径。v2 引入执行路由器后，生成流程变为"约束分类→路由决策→多路径执行"。这不是在 v1 旁边"加一个引擎"，而是**重构执行调度逻辑**。

v1 的值域约束**验证逻辑**不变（逐行检查 BETWEEN/MIN/MAX），但**调度方式**从硬编码变为路由器驱动。

**v2 能做的**：
- 三类约束完整支持：值域（v1 扩展调度）、行间（batch 有状态）、聚合（两阶段执行）
- 约束分类器：编译时识别三类约束 → 标记 PHASE_ONE/PHASE_TWO
- 执行路由器：5 条退化路径（全功能/后筛选/纯物理/统计生成/KDE 扰动）+ 身份切换
- 数据引擎 v1(KDE)：核密度估计学习训练数据分布 + 体积比计算 + 后筛选排除率预估
- 后筛选完整版：排除率预估 + 超时截断 + 误差界联动表
- 哈希链审计日志：创世记录 + 写入验证 + 分叉检测 + 每日全链校验
- DURING/WHEN 条件约束：非矩形约束域支持（拒绝采样/MCMC）
- EvidencePackage v2：statistical_fidelity + constraint_type_breakdown + 身份声明 + audit_immutability: verified

**v2 做不了的（诚实声明）**：
- ❌ 模型版本管理 → v3
- ❌ 时间旅行 → v3
- ❌ 持续对齐 → v3
- ❌ 高维数据引擎（>20维 KDE 精度不足）→ 明确声明中低维限制
- ❌ 反例搜索 → v4 (research)
- ❌ 行数窗口/分组时间窗口/会话窗口 → v4

---

## 二、v2 依赖图

```
Unit J: 行间约束引擎 ────────────┐
    │ #10 行间引擎 (1.5w)        │
    │ ← v1#5 物理引擎 + v1#6    │
    └──────┬─────────────────────┤
           │                    │
Unit K: 聚合约束引擎           │
    │ #11 聚合引擎 (1.5w)       │
    │ ← v1#6 验证器 + #10      │
    └──────┬─────────────────────┤
           │                    │
Unit L: 约束分类器 ──────┐     │
    │ #12 分类器 (1w)     │     │
    │ ← Parser扩展        │     │
    └──────┬──────────────┤     │
           │              │     │
Unit M: 执行路由器重构 ←─┘     │
    │ #13 路由器重构 (2w)       │
    │ ← #10/#11/#12 + v1#5/#6  │
    └──────┬─────────────────────┘
           │
Unit N: 后筛选完整版
    │ #14 后筛选 (1w)
    │ ← #13 + #15b 数据引擎
    └──────┬─────────────────────┘
           │
Unit O: 审计 + 数据引擎 ──┐
    │ #15 哈希链审计 (1w)  │
    │ #15b 数据引擎KDE (3w)│
    │ ← v1#4 存储           │
    └──────┬────────────────┤
           │                │
Unit P: DURING/WHEN + EvidencePackage v2
    │ #16 DURING/WHEN (1w)    │
    │ #17 EvidencePackage v2 (0.5w)
    │ ← #13 + #14 + #15      │
    └──────┘

Unit Q: 脚手架 v2 ────── 与 J-P 并行
    │ Explain/Trace/可观测/错误注入/测试增强 (1w)

Unit R: 工具线 v2 ────── v2 第3周起
    │ 模板v0.2/测试v0.2/Schema校验器v1.0/Trace分析v0.1 (1w)
```

### 2.1 开发波次

| 波次 | 时间 | 活跃 Unit | 里程碑 |
|------|------|----------|---------|
| Wave 1 | 第1-4周 | J, K, L, O(#15审计部分) | 行间引擎跨 batch 状态传递正确；聚合引擎两阶段执行正确；分类器标记 PHASE_ONE/TWO；审计日志哈希链可验证 |
| Wave 2 | 第4-7周 | M, O(#15b KDE部分) | 执行路由器5条退化路径可达；数据引擎 KDE 可学习训练数据分布 |
| Wave 3 | 第7-9周 | N, P, Q | 后筛选排除率预估工作；DURING/WHEN 条件约束正确；EvidencePackage v2 字段完整；脚手架增强就位 |
| Wave 4 | 第9-12周 | R, 集成测试 | Schema 校验器三方 diff 工作；Trace 分析工具规则引擎工作；端到端5路径集成测试通过 |

---

## 三、v2 组件接口定义

### 3.1 #10 行间约束引擎

**输入**：行间约束定义 + 排序列 + 当前 batch 数据 + 上一 batch 状态
**输出**：过滤后的 batch + 更新后的状态

```cpp
namespace synthgen::engine::constraint {

// 行间约束定义（来自 Parser v2 扩展）
struct InterRowConstraintDef {
    std::string column_name;        // 约束列
    std::string order_column;       // 排序列（来自 Schema ORDER 声明）
    double delta_max;               // |x[t] - x[t-1]| < delta_max
    std::optional<double> delta_min; // 可选：变化率下限
};

// batch 间传递的状态
struct InterRowState {
    std::optional<double> last_value;  // 上一 batch 最后一个有效值
    bool initialized = false;          // 是否有上一 batch 的状态
};

// 行间约束执行结果
struct InterRowResult {
    ArrowBatch filtered_batch;     // 过滤后的数据
    InterRowState outgoing_state;  // 传递给下一 batch 的状态
    int64_t rows_filtered;         // 被过滤的行数
    double filter_rate;            // 过滤率
};

class InterRowEngine {
public:
    explicit InterRowEngine(const Schema& schema,
                             const std::vector<InterRowConstraintDef>& constraints);

    Result<InterRowResult> execute_batch(
        const ArrowBatch& batch,
        const InterRowState& incoming_state);

    // 获取 ORDER 列（来自 Schema 声明）
    const std::string& order_column() const;

    // Explain
    ExplainInfo explain() const;

private:
    // frame buffer：存储上一 batch 最后 N 行，用于跨 batch 约束检查
    static constexpr int kFrameBufferSize = 2;
    // 按 ORDER 列排序后逐行检查行间约束
    // 约束检查：|row[i].col - row[i-1].col| < delta_max
};

}  // namespace synthgen::engine::constraint
```

### 3.2 #11 聚合约束引擎

**输入**：聚合约束定义 + 值域/行间过滤后的数据
**输出**：两阶段过滤后的数据 + 窗口排除率

```cpp
namespace synthgen::engine::constraint {

// 聚合约束定义
struct AggregateConstraintDef {
    std::string column_name;       // 聚合列
    std::string function;          // AVG / SUM / MIN / MAX / COUNT
    std::string window_type;       // INTERVAL / ROWS (v4)
    std::string window_spec;       // "1 HOUR" / "100" 等
    std::optional<double> min_val;  // 聚合结果下限
    std::optional<double> max_val;  // 聚合结果上限
};

// 聚合窗口
struct AggregationWindow {
    int64_t start_row;
    int64_t end_row;
    std::vector<int64_t> included_rows;    // 窗口内包含的行索引
    std::vector<int64_t> excluded_rows;    // 被排除的行索引（partial_window）
};

// 两阶段执行结果
struct TwoPhaseResult {
    // 阶段一：值域 + 行间逐行过滤结果
    PhaseOneResult phase_one;

    // 阶段二：聚合窗口验证结果
    PhaseTwoResult phase_two;

    // 整体排除率
    double total_exclusion_rate;

    // 窗口排除率明细
    std::vector<WindowExclusionRate> window_exclusion_rates;
};

// 阶段一结果：逐行过滤
struct PhaseOneResult {
    ArrowBatch filtered_batch;          // 值域+行间过滤后的数据
    int64_t rows_input;                 // 输入行数
    int64_t rows_passed;                // 通过行数
    double phase_one_exclusion_rate;    // 阶段一排除率
};

// 阶段二结果：聚合窗口验证
struct PhaseTwoResult {
    int64_t windows_checked;            // 检查的窗口数
    int64_t windows_passed;             // 通过的窗口数
    int64_t windows_excluded;           // 排除的窗口数
    double phase_two_exclusion_rate;    // 阶段二排除率
};

struct WindowExclusionRate {
    std::string constraint_name;
    std::string window_spec;
    double exclusion_rate;
    bool is_partial;  // partial_window_excluded 标记
};

class AggregateEngine {
public:
    explicit AggregateEngine(const Schema& schema,
                              const std::vector<AggregateConstraintDef>& constraints);

    // 两阶段执行
    // 阶段一：调用值域验证器 + 行间引擎逐行过滤
    // 阶段二：对阶段一输出执行时间窗口聚合验证
    Result<TwoPhaseResult> execute(
        const ArrowBatch& batch,
        const ValueRangeValidator& range_validator,
        const InterRowEngine& inter_row_engine,
        const InterRowState& inter_row_state);

    // Explain
    ExplainInfo explain() const;
};

}  // namespace synthgen::engine::constraint
```

### 3.3 #12 约束分类器（编译时）

**输入**：约束 AST
**输出**：约束分类结果（值域/行间/聚合 + PHASE 标记 + 执行模式）

```cpp
namespace synthgen::engine::router {

// 约束类型
enum class ConstraintType {
    kValueRange,      // 值域约束
    kInterRow,        // 行间约束
    kAggregate,       // 聚合约束
};

// 执行阶段标记
enum class ExecutionPhase {
    kPhaseOne,        // 阶段一：逐行过滤（值域 + 行间）
    kPhaseTwo,        // 阶段二：聚合验证（窗口聚合）
};

// 执行模式
enum class ExecutionMode {
    kRowByRow,        // 逐行（仅值域约束）
    kStatefulBatch,   // batch 有状态（含行间约束）
    kTwoPhase,        // 两阶段（含聚合约束）
};

// 单个约束的分类结果
struct ConstraintClassification {
    std::string constraint_name;
    ConstraintType type;
    ExecutionPhase phase;
};

// 整体分类结果
struct ClassificationResult {
    std::vector<ConstraintClassification> classifications;
    ExecutionMode execution_mode;      // 整体执行模式
    int value_range_count;             // 值域约束数量
    int inter_row_count;               // 行间约束数量
    int aggregate_count;               // 聚合约束数量
};

class ConstraintClassifier {
public:
    // 编译时分类：从约束 AST 确定类型和阶段
    Result<ClassificationResult> classify(
        const std::vector<ConstraintDef>& constraints,
        const Schema& schema);

    // 分类规则：
    // - 有聚合约束 → execution_mode = kTwoPhase
    // - 有行间约束但无聚合 → execution_mode = kStatefulBatch
    // - 仅值域约束 → execution_mode = kRowByRow
    // - 聚合约束标记 PHASE_TWO，其余标记 PHASE_ONE
};

}  // namespace synthgen::engine::router
```

### 3.4 #13 执行路由器重构

**输入**：约束分类结果 + 数据引擎状态
**输出**：路由决策 + 生成路径选择 + 身份声明

```cpp
namespace synthgen::engine::router {

// 退化路径
enum class DegradationPath {
    kFullFunction,          // 全功能：约束驱动 + 数据引擎
    kPostFilter,            // 后筛选：物理采样 + 后筛选过滤
    kPurePhysics,           // 纯物理：v1 路径（矩形域采样）
    kStatisticalGeneration, // 统计生成：数据引擎直接采样
    kKDEPerturbation,      // KDE 扰动：格式化扰动
};

// 身份声明
struct IdentityDeclaration {
    std::string identity;       // 生成器身份
    std::string justification;  // 选择理由
    DegradationPath path;
};

// 路由决策
struct RoutingDecision {
    DegradationPath selected_path;
    IdentityDeclaration identity;
    ConstraintClassification classification;
    double volume_ratio;                // 体积比（约束空间/数据分布）
    double estimated_exclusion_rate;    // 预估排除率
    bool data_engine_available;         // 数据引擎是否可用
};

// 执行路由器
class ExecutionRouter {
public:
    explicit ExecutionRouter(const DataEngineV1& data_engine);

    // 路由决策
    Result<RoutingDecision> route(
        const ClassificationResult& classification,
        const Schema& schema,
        const GenerationRequest& request);

    // 路由规则（从高到低优先级）：
    // 1. 全功能路径：约束完备 + 数据引擎可用
    // 2. 后筛选路径：排除率 < 90%
    // 3. 纯物理路径：仅值域约束 或 无数据引擎
    // 4. 统计生成路径：约束不完备 + 数据引擎可用
    // 5. KDE 扰动路径：约束极度不完备 + 数据引擎可用

    // 身份映射
    static const char* identity_for_path(DegradationPath path);
    // kFullFunction → "constraint_driven_synthetic"
    // kPostFilter → "post_filter_synthetic"
    // kPurePhysics → "physics_sampler"
    // kStatisticalGeneration → "statistical_generator"
    // kKDEPerturbation → "kde_perturbation_generator"

    // Explain
    ExplainInfo explain(const ClassificationResult& classification) const;
};

}  // namespace synthgen::engine::router
```

### 3.5 #14 后筛选完整版

```cpp
namespace synthgen::engine::postfilter {

// 误差界联动表
enum class ExclusionRateBand {
    kLow,       // 0-30%：正常范围
    kMedium,    // 30-70%：需要关注
    kHigh,      // 70-90%：保守偏向
    kCritical,  // >90%：拒绝后筛选
};

// 排除率与 data_grade 联动
struct ExclusionGradeMapping {
    ExclusionRateBand band;
    std::string data_grade;
    std::string behavior;
};

// 后筛选配置
struct PostFilterConfig {
    double timeout_ms = 30000;       // 超时截断（30秒）
    double high_exclusion_threshold = 0.80;   // 保守偏向阈值
    double critical_exclusion_threshold = 0.90;  // 拒绝后筛选阈值
    bool enable_realtime_monitoring = true;
};

// 后筛选结果
struct PostFilterResult {
    ArrowBatch filtered_data;
    double actual_exclusion_rate;
    ExclusionRateBand rate_band;
    std::string data_grade;
    bool was_timeout_truncated;
    std::vector<double> realtime_exclusion_rate_series;  // 实时排除率变化
};

class PostFilter {
public:
    explicit PostFilter(const PostFilterConfig& config);

    Result<PostFilterResult> execute(
        const ArrowBatch& sampled_data,
        const std::vector<ConstraintDef>& constraints,
        const Schema& schema);

    // 排除率预估（依赖数据引擎体积比）
    // 跨命名空间依赖：DataEngineV1 位于 synthgen::engine::data，
    // 通过前向声明引用，编译单元需 #include "data_engine.h"
    Result<double> estimate_exclusion_rate(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints,
        const DataEngineV1& data_engine);

    // Explain
    ExplainInfo explain() const;
};

}  // namespace synthgen::engine::postfilter
```

### 3.6 #15 哈希链审计日志

```cpp
namespace synthgen::storage::audit {

// 审计记录
struct AuditRecord {
    std::string record_id;         // 唯一 ID
    std::string operation;         // generate / update_model / compact / ...
    std::string actor_identity;    // 执行者身份
    Timestamp timestamp;
    std::string prev_hash;         // 前一条记录的哈希
    std::string content_hash;      // 本记录内容的哈希
    std::string chain_hash;        // hash(prev_hash + content_hash)
    std::map<std::string, std::string> metadata;
};

// 审计日志
class AuditLog {
public:
    explicit AuditLog(StorageBackend& storage);

    // 创世记录
    Result<void> create_genesis();

    // 追加记录
    Result<AuditRecord> append(const std::string& operation,
                                const std::string& actor_identity,
                                const std::map<std::string, std::string>& metadata);

    // 验证哈希链完整性
    Result<bool> verify_chain();

    // 每日全链校验
    Result<ChainVerificationReport> daily_verification();

    // 分叉检测
    Result<std::vector<ForkDetection>> detect_forks();
};

// WORM（Write Once Read Many）存储
class WORMStorage {
public:
    Result<void> write(const AuditRecord& record);  // 写入后不可修改
    Result<AuditRecord> read(const std::string& record_id);
    Result<std::vector<AuditRecord>> scan(const std::optional<Timestamp>& from,
                                           const std::optional<Timestamp>& to);

    // WORM 保证：显式删除修改/删除操作
    Result<void> modify(const AuditRecord& record) = delete;
    Result<void> delete_record(const std::string& record_id) = delete;

    // WORM 保证机制：
    // - 通过 = delete 在编译期禁止 modify/delete
    // - 底层使用带哈希校验的 Parquet，每次审计追加新文件
    // - 违反 WORM 语义的操作（如直接修改底层文件）由哈希链校验 detect
};

}  // namespace synthgen::storage::audit
```

### 3.7 #15b 数据引擎 v1(KDE)

```cpp
namespace synthgen::engine::data {

// KDE 配置
struct KDEConfig {
    std::string kernel = "gaussian";    // 核函数类型
    double bandwidth = 0.0;            // 带宽（0 = 自动选择）
    int max_dimensions = 20;            // 最大支持维度
    int max_training_rows = 1000000;    // 最大训练行数
};

// 数据引擎 v1
class DataEngineV1 {
public:
    explicit DataEngineV1(const KDEConfig& config);

    // 从训练数据学习 KDE
    Result<void> fit(const ArrowBatch& training_data, const Schema& schema);

    // 密度采样：从学习到的分布中采样
    Result<ArrowBatch> sample(int64_t count, uint64_t seed);

    // 体积比计算：约束空间体积 / 数据分布体积
    Result<double> volume_ratio(const Schema& schema,
                                 const std::vector<ConstraintDef>& constraints);

    // 密度估计：在给定点估计概率密度
    Result<double> estimate_density(const std::vector<double>& point);

    // Explain
    ExplainInfo explain() const;

    // 状态查询
    bool is_fitted() const;
    int dimensions() const;
    const KDEConfig& config() const;

private:
    // 核密度估计实现
    // 带宽选择：Silverman 规则（默认）或用户指定
    // 维度限制：>20 维返回 kDimensionTooHigh 警告
    // 采样：从 KDE 分布中采样的拒绝采样法
};

}  // namespace synthgen::engine::data
```

### 3.8 #16 DURING/WHEN 语义

```cpp
namespace synthgen::engine::constraint {

// 条件约束定义
struct ConditionalConstraintDef {
    // DURING 语义：当 column = value 时，约束生效
    std::string during_column;         // DURING 列名
    std::string during_value;          // DURING 值

    // WHEN 语义：当 condition 为真时，约束生效
    std::string when_condition;        // WHEN 条件表达式

    // 条件生效时的约束
    std::vector<ConstraintItem> conditional_constraints;

    // 约束类型
    enum class ConditionType { kDuring, kWhen } condition_type;
};

// 条件约束引擎
class ConditionalConstraintEngine {
public:
    Result<ArrowBatch> apply(
        const ArrowBatch& batch,
        const std::vector<ConditionalConstraintDef>& constraints,
        const Schema& schema);

    // 非矩形约束域处理
    // DURING/WHEN 产生的约束域可能不是简单的矩形空间
    // 物理引擎 v2 需要支持拒绝采样/MCMC 处理非矩形域
    Result<GenerationResult> generate_in_conditional_domain(
        const Schema& schema,
        const std::vector<ConditionalConstraintDef>& constraints,
        int64_t limit,
        uint64_t seed);

    // Explain
    ExplainInfo explain() const;
};

}  // namespace synthgen::engine::constraint
```

### 3.9 #17 EvidencePackage v2

```cpp
namespace synthgen::engine::evidence {

// EvidencePackage v2 字段
struct EvidencePackageV2 {
    // === 继承 v1 的 always 字段 ===
    std::string schema_version = "v2";
    std::string schema_hash;
    ConstraintSummary constraint_summary;
    double exclusion_rate;
    std::string data_grade;
    int64_t row_count;
    ProvenanceV2 provenance;
    ConservativeTailReport conservative_tail_report;

    // === v2 新增字段 ===

    // statistical_fidelity（数据驱动时填充）
    StatisticalFidelity statistical_fidelity;   // 适用性：data_engaged

    // constraint_type_breakdown（三类约束分类）
    ConstraintTypeBreakdown constraint_type_breakdown;  // 适用性：aggregation_present

    // 身份声明
    IdentityDeclaration generator_identity;     // 适用性：always

    // 审计不可变保证
    std::string audit_immutability = "verified"; // 适用性：always（v2 起生效）

    // 后筛选信息
    PostFilterInfo post_filter_info;            // 适用性：post_filter_engaged
};

// v2 新增结构体
struct StatisticalFidelity {
    bool available = false;
    std::string model_version;           // KDE 模型版本
    double fidelity_score;                // 保真度评分
    std::string training_data_range;      // 训练数据范围
    int64_t training_rows;               // 训练行数
};

struct ConstraintTypeBreakdown {
    int value_range_count = 0;
    int inter_row_count = 0;
    int aggregate_count = 0;
};

struct PostFilterInfo {
    bool was_post_filtered = false;
    double pre_filter_rows;
    double post_filter_rows;
    double actual_exclusion_rate;
    std::string exclusion_rate_band;       // low/medium/high/critical
    bool was_timeout_truncated = false;
    std::vector<double> realtime_exclusion_rate_series;
};

// v2 provenance 扩展（组合而非继承，参见整体设计规范 §6.1a）
struct ProvenanceV2 {
    // v1 字段完整副本（组合）
    std::string generator_id;                // v1: 生成器标识
    std::string engine_version;              // v1: 引擎版本
    Timestamp generation_timestamp;          // v1: 生成时间
    std::string seed_lineage;                // v1: 种子血缘
    // v2 新增字段
    std::string degradation_path;           // 走的退化路径
    IdentityDeclaration identity;            // 身份声明
    std::optional<DataEngineInfo> data_engine_info;  // 数据引擎信息（如使用）
};

struct DataEngineInfo {
    std::string model_version;
    int dimensions;
    double bandwidth;
    double volume_ratio;
};

}  // namespace synthgen::engine::evidence
```

---

## 四、v2 诚实声明（必须传递）

以下声明在每个 v2 的 EvidencePackage 中必须体现：

| 声明 | EvidencePackage 体现 | 代码实现位置 |
|------|---------------------|------------|
| 物理优先认识论偏差 | conservative_tail_report.epistemological_bias = "physical_first" | 后筛选 |
| 尾部事件系统性排除 | conservative_tail_report.tail_exclusion_statement | 后筛选 |
| 条件保证 | data_grade 按排除率联动表填充 | 后筛选 + EvidencePackage |
| 身份声明 | generator_identity.identity + path | 执行路由器 |
| 审计不可变保证 | audit_immutability = "verified" | 审计日志 |
| 统计签名有条件 | statistical_fidelity.available = true（仅数据引擎参与时） | 数据引擎 |
| 约束类型分类 | constraint_type_breakdown 三类计数 | 约束分类器 |
| 数据引擎维度限制 | 数据引擎 >20 维返回 kDimensionTooHigh 警告 | 数据引擎 |
| 排除率与 data_grade 联动 | 0-30%→statistics_guaranteed, 30-70%→limited_fidelity, 70-90%→limited_fidelity(保守), >90%→拒绝 | 后筛选 |
| DURING/WHEN 非矩形约束域 | 物理引擎 v2 支持拒绝采样/MCMC | 条件约束引擎 |

---

## 五、v2 错误测试验收标准

**行间引擎错误测试**：
- [ ] 空 batch 输入返回空结果 + 空状态
- [ ] ORDER 列不存在返回 kUndefinedColumn
- [ ] 无 ORDER 列但有行间约束返回 kOrderColumnRequired
- [ ] delta_max ≤ 0 返回 kInvalidConstraint
- [ ] 约束列不存在于 Schema 返回 kUndefinedColumn
- [ ] 约束列类型非数值返回 kTypeMismatch
- [ ] batch 间状态传递在空状态时正确初始化
- [ ] 排序列有 NULL 值时行为确定（跳过或报错，取决于 Schema NOT NULL）

**聚合引擎错误测试**：
- [ ] 空窗口（0行数据）的聚合结果行为确定
- [ ] 聚合函数不支持返回 kUnsupportedAggregateFunction
- [ ] 窗口语法错误返回 kInvalidWindowSpec
- [ ] 阶段一过滤后 0 行数据进入阶段二时正确处理
- [ ] partial_window 标记在窗口不完整时正确设置
- [ ] 聚合结果溢出（SUM 超过 INT64_MAX）返回 kOverflow

**约束分类器错误测试**：
- [ ] 空约束列表返回 kEmptyConstraints
- [ ] 约束列名重复返回 kDuplicateConstraint
- [ ] 不支持的约束类型返回 kUnsupportedConstraintType
- [ ] 分类结果与实际约束不匹配时返回 kClassificationError

**执行路由器错误测试**：
- [ ] 无可用生成路径时返回 kNoAvailablePath
- [ ] 数据引擎不可用时退化到纯物理路径
- [ ] 体积比计算失败时保守估计（排除率预估 = 1.0）
- [ ] 并发路由决策竞态条件处理
- [ ] 路由决策与实际执行不一致时回退

**后筛选错误测试**：
- [ ] 超时截断后返回已过滤的部分数据 + was_timeout_truncated = true
- [ ] 排除率 >90% 时拒绝后筛选，返回 kExclusionRateTooHigh
- [ ] 实时排除率监控线程安全
- [ ] 排除率预估与实际排除率偏差 >20% 时记录警告

**审计日志错误测试**：
- [ ] 修改已写入的审计记录返回 kWriteOnceViolation
- [ ] 哈希链断裂检测：手动修改一条记录后 daily_verification 返回 false
- [ ] 创世记录重复写入返回 kGenesisAlreadyExists
- [ ] 分叉检测：两条记录 prev_hash 相同但 chain_hash 不同

**数据引擎错误测试**：
- [ ] 未 fit 调用 sample 返回 kNotFitted
- [ ] 维度 >20 返回 kDimensionTooHigh 警告（不拒绝，但警告）
- [ ] 训练数据为空返回 kEmptyTrainingData
- [ ] 训练数据行数 > max_training_rows 返回 kTrainingDataTooLarge
- [ ] 带宽为负数返回 kInvalidBandwidth
- [ ] 采样 count ≤ 0 返回 kInvalidArgument
- [ ] 体积比计算中约束空间为空返回 kEmptyConstraintSpace

**DURING/WHEN 错误测试**：
- [ ] DURING 列不存在返回 kUndefinedColumn
- [ ] DURING 值与列类型不匹配返回 kTypeMismatch
- [ ] WHEN 条件语法错误返回 kInvalidCondition
- [ ] 非矩形约束域 + 拒绝采样不收敛返回 kRejectionSamplingFailed

**EvidencePackage v2 错误测试**：
- [ ] statistical_fidelity 在无数据引擎时 available = false
- [ ] constraint_type_breakdown 与实际约束分类不一致时返回 kConsistencyError
- [ ] audit_immutability 在无审计日志时仍为 "verified"（v2 保证审计就位）
- [ ] Schema 验证：缺失 v2 新增必填字段返回 kSchemaViolation

---

## 六、v2 脚手架验收标准（与功能验收同等地位）

| 脚手架 | v2 交付 | 验收标准 |
|--------|---------|---------|
| Explain 增强 | 排除率预估 + 体积比 + 数据来源 + 退化路径选择 | `client.explain()` 返回 exclusion_rate_estimate + volume_ratio + data_source + degradation_path |
| Trace 增强 | 后筛选路径实时排除率变化 | EvidencePackage.provenance.trace_spans 含后筛选排除率变化 span |
| 可观测性增强 | 排除率趋势 + 退化路径命中率 + 审计验证状态 | `/metrics` 新增 exclusion_rate_trend + degradation_path_hit_rate + audit_verification_status |
| 错误注入 v2 | 后筛选排除率爆炸 + 数据引擎故障 | 注入排除率 >90% 时系统正确拒绝；注入数据引擎不可用时路由器正确退化 |
| 测试增强 | 5 条退化路径回归测试 | 每条退化路径至少 1 个端到端回归测试用例 |

---

## 七、v2 工具验收标准

| 工具 | 验收标准 |
|------|---------|
| 组件模板引擎 v0.2 | 从 #10-#17 任一新组件接口描述生成骨架代码，骨架能通过编译和基础 CI |
| 测试辅助库 v0.2 | TEST_DEGRADATION_PATH 宏：5 条退化路径自动测试路由正确性 |
| Schema 校验器 v1.0 | 编译期接口注册 ↔ Schema ↔ 理论框架三方 diff；故意拼错字段名能检出 |
| Trace 分析工具 v0.1 | 4 条规则引擎；构造排除率飙升 Trace 能标红对应 span |

---

## 八、v2 盲区 deadline（来自工程执行守则）

| 盲区 | deadline | 不做的后果 |
|------|---------|----------|
| 执行路由器与 v1 接口兼容策略 | v2 Wave 1 前 | 重构范围不确定，2 周估算失效 |
| 数据引擎 KDE 技术选型 | v2 Wave 1 前 | #15b 3 周估算失效，后筛选排除率预估无计算基础 |
| v2 早期迭代先测 3 条不依赖数据引擎的退化路径 | v2 Wave 2 | 数据引擎延迟导致 5 路径全卡 |
| 哈希链审计 WORM 存储实现选型 | v2 Wave 1 前 | 审计不可变保证无法生效 |
| 真实数据冷启动 | v2 开发启动前 | 后筛选排除率预估纸上谈兵 |

---

## 九、v2 Unit 分配总表

| Unit | 名称 | 组件 | 估算 | 依赖 | 波次 | 标注 |
|------|------|------|------|------|------|------|
| J | 行间约束引擎 | #10 | 1.5w | v1#5+#6 | W1 | [IMPLEMENT] |
| K | 聚合约束引擎 | #11 | 1.5w | v1#6+#10 | W1 | [IMPLEMENT] |
| L | 约束分类器 | #12 | 1w | Parser扩展 | W1 | [IMPLEMENT] |
| M | 执行路由器重构 | #13 | 2w | #10/#11/#12+v1#5/#6 | W2 | [COORDINATE] C2 |
| N | 后筛选完整版 | #14 | 1w | #13+#15b | W3 | [IMPLEMENT] |
| O | 审计+数据引擎 | #15+#15b | 4w | v1#4 | W1-2 | [COORDINATE] C1,C8 |
| P | DURING/WHEN+EvidencePackage | #16+#17 | 1.5w | #13+#14+#15 | W3 | [IMPLEMENT] |
| Q | 脚手架 v2 | 5项增强 | 1w | 与J-P并行 | W3 | [IMPLEMENT] |
| R | 工具线 v2 | 4项工具 | 1w | W3+ | W4 | [COORDINATE] C7 |

**v2 总估算**：12-13 周（含脚手架 1w + 工具 1w）

---

## 十、v2 协调项占位

### C1: KDE 技术选型 [RESOLVED]

**决策**：自研 C++ KDE。理由：团队 C++ 为主，自研可控制维度限制和带宽选择逻辑。

**实际实现**：v2 已使用自研 C++ KDE 实现，通过 Silverman 规则带宽选择 + 拒绝采样，已集成并通过 653 个测试。此协调项闭环。

### C2: 执行路由器与 v1 接口兼容策略 [COORDINATE]

**待决策**：v1 硬编码调度 → v2 路由器驱动的迁移边界

| 选项 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| 完全重构 | v1 生成入口直接委托给路由器 | 架构干净 | 改动大，2w 估算可能不够 |
| 适配器模式 | v1 入口不变，内部注入路由器 | v1 接口兼容 | 适配器层增加复杂度 |

**推荐**：完全重构。理由：路线图已明确"摩托车是新的整车"，适配器模式会留下技术债。

### C8: 哈希链审计 WORM 存储选型 [RESOLVED]

**决策**：带哈希校验的 Parquet。理由：WORM 保证在应用层实现，底层存储用 Parquet 生态成熟。

**Append 策略**：每次审计追加新文件（不合并），确保 append-only 语义。哈希链校验在应用层通过 `prev_hash` 字段实现，修改检测由 `verify_chain()` 和 `daily_verification()` 自动执行。
