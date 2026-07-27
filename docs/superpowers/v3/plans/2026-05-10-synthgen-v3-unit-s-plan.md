SynthGen Core v3 Unit S 实施计划：时间旅行 + 持续对齐
文档性质：Unit 级实施计划 [COORDINATE]
版本：v1.0
日期：2026-05-10
上游文档：Unit S 设计规范 v1.0
估算：2 周
依赖：#18 版本链 + #19 GC + v2#13 执行路由器 + v2#15b 数据引擎
协调项：C4(增量更新接口), C5(待测模型协议)

---

## 概述

Unit S 交付时间旅行(AS OF)和持续对齐(UPDATE MODEL)——v3 的核心时间智能能力。时间旅行允许用户查询任意模型版本数据；持续对齐保持模型与最新数据同步。此 Unit 是 Unit T(tail_report增强+偏差报告) 的前置。

---

## Task 1：时间旅行(AS OF)实现

**目标**：实现按版本读取快照 + compaction 退化行为

### Step 1.1：TimeTravelEngine 核心实现

**做什么**：实现 query_as_of 接口

**产出**：`src/storage/timetravel/time_travel_engine.h`, `src/storage/timetravel/time_travel_engine.cpp`

**关键逻辑**：
- 输入：model_name + version_id + schema
- 流程：
  1. 从 ModelVersionChain 查找 version_id
  2. 版本存在 → 加载快照数据 → 返回 TimeTravelResult(was_degraded=false)
  3. 版本已被 compact → 查找最近可用版本 → 加载 → 返回 TimeTravelResult(was_degraded=true + bias_report)
  4. 版本不存在 → 返回 kVersionNotFound
- 快照数据加载：调用 ModelStorageLayer::load_model

**验收**：
- [ ] 存在的版本返回正确数据
- [ ] 已 compact 的版本返回最近可用版本 + 偏差报告
- [ ] 不存在的版本返回 kVersionNotFound
- [ ] was_degraded 标记正确

### Step 1.2：compaction 退化行为

**做什么**：实现 compaction 退化时的偏差报告生成

**关键逻辑**：
- 退化查找：从被请求版本向上遍历版本链，找到最近的未 compact 版本
- CompactionBiasReport 填充：
  - requested_version = 用户请求的版本
  - returned_version = 实际返回的版本
  - reason = "compacted"
  - merged_from = 被合并的版本列表
  - fidelity_score_range_min/max = 被合并版本的保真度范围
  - version_mismatch = true

**验收**：
- [ ] 偏差报告字段完整
- [ ] 退化到最近版本（不是随机版本）
- [ ] 多层 compact 时找到最终可用的版本

### Step 1.3：时间旅行测试

**做什么**：编写时间旅行单元测试

**产出**：`tests/unit/time_travel_test.cpp`

**测试用例**（至少 8 个）：
- 查询存在版本返回正确数据
- 查询已 compact 版本返回退化数据 + 偏差报告
- 查询不存在版本返回 kVersionNotFound
- 快照数据与写入时一致
- 多层 compact 退化正确
- 空模型名称返回 kModelNotFound
- Explain 输出含退化信息
- Trace span 正确记录

**验收**：8+ 测试通过

---

## Task 2：漂移检测实现

**目标**：实现 KS 检验漂移检测（C10 决策）

### Step 2.1：DriftDetector 实现

**做什么**：实现基于 KS 检验的漂移检测

**产出**：`src/engine/alignment/drift_detector.h`, `src/engine/alignment/drift_detector.cpp`

**关键逻辑**：
- 支持 3 种模式：ks（默认）、kl（KL 散度）、none（不检测）
- KS 检验流程：
  1. 对每个维度独立计算经验 CDF
  2. 计算 KS 统计量：max|CDF_current - CDF_new|
  3. 与临界值比较（显著性水平 α=0.05）
  4. 任一维度显著 → drift_detected = true
- 多维处理：逐维 KS + Bonferroni 校正（α/维度数）
- drift_score = max(KS 统计量)（归一化到 0-1）

**验收**：
- [ ] KS 检验在数据漂移时返回 drift_detected=true
- [ ] KS 检验在数据未漂移时返回 drift_detected=false
- [ ] drift_score 在 [0, 1] 范围内
- [ ] mode=none 时跳过检测，返回 drift_detected=false

### Step 2.2：漂移检测测试

**测试用例**（至少 6 个）：
- 均值漂移检测
- 方差漂移检测
- 无漂移时不误报
- 多维联合漂移
- KL 散度模式（如果实现）
- 极端情况：完全不同的分布

**验收**：6+ 测试通过

---

## Task 3：持续对齐引擎实现

**目标**：实现 ContinuousAlignmentEngine 核心逻辑

### Step 3.1：update_model 核心流程

**做什么**：实现持续对齐的主流程

**产出**：`src/engine/alignment/continuous_alignment_engine.h`, `src/engine/alignment/continuous_alignment_engine.cpp`

**关键逻辑**：
- 输入：AlignmentRequest（model_name, current_version_id, incorporate_from, where_condition, drift_check, save_as）
- 流程：
  1. 加载当前版本的数据引擎
  2. 读取新数据（incorporate_from + where_condition 筛选）
  3. 漂移检测：比较当前分布与新数据分布
  4. 如果漂移检测失败 → 保守估计（drift_detected=true, drift_score=1.0）
  5. 增量更新数据引擎（fit_incremental）
  6. 创建新版本
  7. 返回 AlignmentResult（new_version, drift_detected, drift_score, compensation_status, compensation_deadline）
- 依赖：ModelVersionChain, DataEngineV1, ExecutionRouter

**验收**：
- [ ] 新数据正确纳入模型
- [ ] 漂移检测结果正确传递
- [ ] 新版本创建正确
- [ ] 代偿状态正确设置

### Step 3.2：代偿收敛机制

**做什么**：实现代偿收敛时限和状态管理

**关键逻辑**：
- 代偿状态：converging（收敛中）→ converged（已收敛）→ diverging（发散）
- 收敛判定：连续 N 次（默认 3）drift_score < 阈值（默认 0.1）
- 发散判定：连续 M 次（默认 5）drift_score > 高阈值（默认 0.5）
- compensation_deadline：用户设置或默认 24 小时
- 超时降级：超过 deadline 仍 diverging → 标记 compensation_status="timeout_degraded"

**验收**：
- [ ] 收敛判定正确
- [ ] 发散判定正确
- [ ] 超时降级正确
- [ ] deadline 设置生效

### Step 3.3：[COORDINATE→DECIDED] 增量更新接口

**做什么**：为 DataEngineV1 添加 fit_incremental() 方法

**产出**：`src/engine/data/kde.h`（扩展）, `src/engine/data/kde.cpp`（扩展）

**关键逻辑**：
- fit_incremental(new_data, schema)：将新数据与现有 KDE 模型合并
- 实现策略：维护训练数据的分箱统计（histogram bins），增量更新 bins
- 如果增量数据导致维度变化 → 返回 kDimensionMismatch
- 序列化检查点供 ModelStorageLayer 存储

**验收**：
- [ ] 增量更新后模型正确
- [ ] 增量更新 vs 全量 fit 结果近似
- [ ] 维度不匹配时返回错误

### Step 3.4：[COORDINATE] 待测模型接入协议定义

**做什么**：定义 TestModelProtocol 供 v4 使用

**产出**：`src/engine/alignment/test_model_protocol.h`

```cpp
// 待测模型接入协议（v3 定义，v4 使用）
struct TestModelProtocol {
    std::string model_id;
    std::string model_type;           // "kde" / "gmm" / "custom"
    std::vector<std::string> supported_queries;  // 支持的查询类型
    // 查询密度：给定数据点的概率密度
    Result<double> query_density(const std::vector<double>& point);
    // 查询边界：给定约束的可行域边界
    Result<std::vector<double>> query_boundary(const std::string& constraint);
};
```

**验收**：
- [ ] 协议定义完整
- [ ] KDE 实现 TestModelProtocol 接口
- [ ] 接口文档注释完整

### Step 3.5：持续对齐测试

**做什么**：编写持续对齐引擎单元测试

**产出**：`tests/unit/continuous_alignment_test.cpp`

**测试用例**（至少 17 个）：

功能测试（12+）：
- 新数据纳入正确
- 漂移检测联动正确
- 代偿收敛判定
- 代偿发散判定
- 超时降级
- deadline 设置生效
- 增量更新接口
- 代偿状态传递到 AlignmentResult
- 新版本创建和元数据
- 审计日志记录对齐操作
- 保守估计（漂移检测失败时）
- where_condition 筛选正确

错误测试（5+）：
- 新数据为空 → kEmptyTrainingData
- 漂移检测失败 → 保守估计
- 数据引擎不可用 → kDataEngineUnavailable
- 版本创建失败 → kVersionCreationFailed
- 维度不匹配 → kDimensionMismatch

**验收**：17+ 测试通过，错误测试占比 ≥ 29%

---

## Task 4：脚手架集成

**目标**：为时间旅行和持续对齐添加 Trace/Explain/Metrics

### Step 4.1：Trace span 集成

**做什么**：为 query_as_of 和 update_model 添加 span

```cpp
// 时间旅行 span
SpanGuard span("time_travel", "query_as_of", trace_id_);
span.set_attribute("version_id", version_id);
span.set_attribute("was_degraded", result.was_degraded);

// 持续对齐 span
SpanGuard span("alignment", "update_model", trace_id_);
span.set_attribute("drift_detected", result.drift_detected);
span.set_attribute("drift_score", result.drift_score);
span.set_attribute("compensation_status", result.compensation_status);
```

**验收**：span 含漂移检测和对齐状态信息

### Step 4.2：Explain 接口

**做什么**：添加 explain() 方法

```cpp
struct AlignmentExplainInfo {
    std::string drift_check_mode;       // ks/kl/none
    double drift_threshold;
    int convergence_window;             // N 次收敛判定窗口
    std::string compensation_deadline;  // 默认 24h
};
```

**验收**：explain() 返回对齐配置信息

### Step 4.3：Metrics 注册

```
alignment_drift_detected_total     — 漂移检测触发次数
alignment_drift_score              — 最近一次漂移分数
alignment_update_model_latency_ms  — 模型更新延迟
alignment_compensation_status      — 当前代偿状态 (0=converging, 1=converged, 2=diverging)
```

**验收**：metrics 端点暴露上述指标

---

## Task 5：集成测试

**目标**：验证时间旅行+持续对齐与版本链/GC/数据引擎的集成

### Step 5.1：集成测试

**产出**：`tests/integration/alignment_integration_test.cpp`

**测试用例**（至少 8 个）：
- 完整时间旅行流程：创建版本 → compact → 时间旅行 → 退化报告
- 完整对齐流程：加载数据 → 漂移检测 → 增量更新 → 新版本
- 时间旅行 + compaction 退化联动
- 持续对齐 + 版本链联动
- Trace span 完整性（含漂移检测子 span）
- Explain 输出正确性
- 与 v2 执行路由器协同（路由器使用新版本）
- 代偿收敛时限到期后的降级行为

**验收**：8+ 集成测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 时间旅行 | 3 | 0.5w | ⬜ |
| Task 2: 漂移检测 | 2 | 0.25w | ⬜ |
| Task 3: 持续对齐 | 5 | 0.75w | ⬜ |
| Task 4: 脚手架 | 3 | 0.25w | ⬜ |
| Task 5: 集成测试 | 1 | 0.25w | ⬜ |
| **合计** | **14** | **2w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| KS 检验在高维数据中检验力不足 | v3 已声明"高维数据引擎优化→后续版本"，KS + Bonferroni 校正是合理的 v3 方案 |
| 增量更新 KDE 的数值稳定性 | fit_incremental 内部验证：增量 vs 全量 fit 的 KL 散度 < 阈值，否则 fallback 到全量 fit |
| 漂移检测假阳性导致不必要的模型更新 | 代偿收敛机制：单次漂移不立即触发，需连续收敛判定 |
| 待测模型协议 v4 使用时接口不匹配 | v3 定义最小可用协议，v4 使用前做兼容性检查 |
