SynthGen Core v2 Unit N 设计规范：后筛选完整版
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit N 实施计划
组件：#14 后筛选完整版
估算：1 周
依赖：#13 执行路由器 + #15b 数据引擎 v1

---

## 一、本 Unit 交付什么

**Unit N 是 v2 后筛选路径的核心实现**——在物理采样后通过约束过滤，实现排除率预估、超时截断、误差界联动。

交付物：
1. **PostFilter**：后筛选执行引擎
2. **ExclusionRateBand**：排除率分级（0-30%/30-70%/70-90%/>90%）
3. **超时截断**：后筛选超时保护
4. **误差界联动表**：排除率与 data_grade 联动
5. **实时排除率监控**：后筛选过程中排除率变化记录

---

## 二、#14 后筛选完整版

### 2.1 核心语义

后筛选是执行路由器退化路径之一。当约束不完备（不能通过物理采样直接满足）时，系统选择后筛选路径：

1. 物理引擎在值域范围内大量采样
2. 逐行检查所有约束（值域 + 行间 + 聚合）
3. 过滤掉不满足约束的行
4. 返回满足约束的行

**关键参数**：
- 排除率 = 被过滤的行数 / 采样总行数
- 超时截断：后筛选超过 30 秒则截断
- 排除率 >90% 时拒绝后筛选（效率过低）

### 2.2 误差界联动表

| 排除率范围 | 分级 | data_grade | 行为 |
|-----------|------|-----------|------|
| 0-30% | kLow | statistics_guaranteed | 正常后筛选 |
| 30-70% | kMedium | limited_fidelity | 需要关注，保守偏向 |
| 70-90% | kHigh | limited_fidelity(保守) | 保守偏向，大量排除 |
| >90% | kCritical | 拒绝 | 不执行后筛选，返回错误 |

### 2.3 接口定义

```cpp
namespace synthgen::engine::postfilter {

// 排除率分级
enum class ExclusionRateBand {
    kLow,       // 0-30%
    kMedium,    // 30-70%
    kHigh,      // 70-90%
    kCritical,  // >90%
};

// 排除率与 data_grade 联动映射
struct ExclusionGradeMapping {
    ExclusionRateBand band;
    double rate_min;
    double rate_max;
    std::string data_grade;
    std::string behavior;
    bool allow_post_filter;  // 是否允许继续后筛选
};

// 后筛选配置
struct PostFilterConfig {
    double timeout_ms = 30000;                  // 超时（30秒）
    double high_exclusion_threshold = 0.80;     // 保守偏向阈值
    double critical_exclusion_threshold = 0.90; // 拒绝后筛选阈值
    bool enable_realtime_monitoring = true;      // 实时监控
    double oversampling_ratio = 3.0;            // 过采样比（采样数 = target * ratio）
};

// 后筛选结果
struct PostFilterResult {
    ArrowBatch filtered_data;
    int64_t pre_filter_rows;
    int64_t post_filter_rows;
    double actual_exclusion_rate;
    ExclusionRateBand rate_band;
    std::string data_grade;
    bool was_timeout_truncated;
    std::vector<double> realtime_exclusion_rate_series;  // 实时排除率变化
    int64_t processing_time_ms;
};

// 后筛选引擎
class PostFilter {
public:
    explicit PostFilter(const PostFilterConfig& config);

    // 执行后筛选
    Result<PostFilterResult> execute(
        const ArrowBatch& sampled_data,
        const std::vector<ConstraintDef>& constraints,
        const Schema& schema);

    // 排除率预估（依赖数据引擎体积比）
    Result<double> estimate_exclusion_rate(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints,
        const DataEngineV1& data_engine);

    // 分级判定
    ExclusionRateBand classify_rate(double exclusion_rate) const;

    // data_grade 映射
    std::string data_grade_for_band(ExclusionRateBand band) const;

    // Explain
    ExplainInfo explain() const;

private:
    PostFilterConfig config_;

    // 实时排除率监控
    struct RealtimeMonitor {
        std::vector<double> rate_series;
        void record(int64_t checked, int64_t passed);
        double current_rate() const;
    };

    // 超时检查
    bool is_timeout(std::chrono::steady_clock::time_point start) const;
};

}  // namespace synthgen::engine::postfilter
```

### 2.4 错误处理

```cpp
enum class PostFilterErrorCode {
    kExclusionRateTooHigh,         // 排除率 >90%，拒绝后筛选
    kTimeoutTruncated,             // 超时截断
    kEmptyInput,                   // 输入数据为空
    kNoConstraints,               // 无约束
    kEstimationFailed,             // 排除率预估失败
    kOversamplingFailed,           // 过采样失败
    kRealtimeMonitorOverflow,      // 实时监控数据溢出
};
```

---

## 三、Unit N 验收标准

### 3.1 功能验收

- [ ] 后筛选正确过滤不满足约束的行
- [ ] 排除率预估与实际偏差 <20%
- [ ] 排除率 >80% 走保守偏向
- [ ] 排除率 >90% 拒绝后筛选
- [ ] 超时截断正确工作
- [ ] 误差界联动表正确映射
- [ ] 实时排除率监控记录正确
- [ ] data_grade 与排除率联动

### 3.2 脚手架验收

- [ ] PostFilter 提供 explain() 方法
- [ ] 后筛选排除率变化写入 Trace span
- [ ] 实时排除率趋势写入 Metrics

### 3.3 错误测试验收

- [ ] 排除率 >90% 返回 kExclusionRateTooHigh
- [ ] 超时截断返回 kTimeoutTruncated + 部分数据
- [ ] 空输入返回 kEmptyInput
- [ ] 无约束返回 kNoConstraints
- [ ] 排除率预估失败保守估计
- [ ] 实时监控大量数据不溢出

### 3.4 边界条件测试

- [ ] 排除率 = 0%（所有行通过）
- [ ] 排除率 = 100%（所有行被过滤）
- [ ] 排除率恰好 80%（保守偏向边界）
- [ ] 排除率恰好 90%（拒绝边界）
- [ ] 超大输入（1M行）
- [ ] 超小输入（1行）
- [ ] 过采样比 = 1.0（不过采样）
- [ ] 过采样比 = 10.0

### 3.5 测试验收

- [ ] 至少 20 个测试用例
- [ ] 错误测试占比 ≥ 30%
- [ ] CI 自动运行

---

## 四、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `PostFilter::execute()` | Unit M (路由器) | 后筛选路径执行 |
| `PostFilter::estimate_exclusion_rate()` | Unit M (路由器) | 排除率预估 |
| `PostFilterResult` | Unit P (EvidencePackage) | 后筛选信息 |
| `ExclusionRateBand` | Unit P (EvidencePackage) | 排除率分级 |
