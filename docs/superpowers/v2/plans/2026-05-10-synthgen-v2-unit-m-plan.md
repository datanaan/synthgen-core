SynthGen Core v2 Unit M 实施计划：执行路由器重构
文档性质：Unit 级实施计划 [COORDINATE]
版本：v1.0
日期：2026-05-10
上游文档：Unit M 设计规范 v1.0
估算：2 周
依赖：#10 行间引擎 + #11 聚合引擎 + #12 分类器 + v1#5/#6
协调项：C2

---

## 概述

Unit M 交付执行路由器重构——v2 的核心架构变化。将 v1 硬编码调度重构为多路径路由器驱动。

> ⚠️ 本计划按"选项 A：完全重构"编写。如团队选择"选项 B：适配器模式"，需调整 Task 3。

---

## Task 1：退化路径和身份声明

**目标**：定义退化路径枚举和身份声明

### Step 1.1：DegradationPath 和 IdentityDeclaration

**做什么**：定义退化路径枚举和身份声明结构

**产出**：`src/engine/router/degradation.h`, `src/engine/router/degradation.cpp`

```cpp
const char* identity_for_path(DegradationPath path) {
    switch (path) {
        case DegradationPath::kFullFunction:           return "constraint_driven_synthetic";
        case DegradationPath::kPostFilter:              return "post_filter_synthetic";
        case DegradationPath::kPurePhysics:             return "physics_sampler";
        case DegradationPath::kStatisticalGeneration:   return "statistical_generator";
        case DegradationPath::kKDEPerturbation:         return "kde_perturbation_generator";
    }
}
```

**验收**：5 条路径身份映射正确

### Step 1.2：退化路径测试

**做什么**：编写退化路径基础测试

**产出**：`tests/unit/degradation_test.cpp`

**验收**：5 个路径 + 5 个身份映射测试通过

---

## Task 2：路由决策算法

**目标**：实现路由决策核心逻辑

### Step 2.1：体积比计算

**做什么**：实现体积比计算逻辑

**产出**：`src/engine/router/volume_ratio.h`, `src/engine/router/volume_ratio.cpp`

**关键逻辑**：
- 数据引擎可用时：调用 data_engine->volume_ratio()
- 数据引擎不可用时：保守估计 = 1.0
- 计算失败时：保守估计 = 1.0 + 警告日志

**验收**：
- [ ] 数据引擎可用时正确调用
- [ ] 不可用时保守估计
- [ ] 失败时保守估计 + 警告

### Step 2.2：排除率预估

**做什么**：实现排除率预估

**产出**：`src/engine/router/exclusion_estimator.h`, `src/engine/router/exclusion_estimator.cpp`

**关键逻辑**：
- 基于体积比的排除率预估
- 体积比 → 排除率映射（简化版）：
  - ratio < 0.3 → 排除率 < 30%（kLow）
  - 0.3 ≤ ratio < 0.7 → 排除率 30-70%（kMedium）
  - 0.7 ≤ ratio < 0.9 → 排除率 70-90%（kHigh）
  - ratio ≥ 0.9 → 排除率 >90%（kCritical）

**验收**：
- [ ] 排除率预估在合理范围内
- [ ] 边界值处理正确

### Step 2.3：路由决策实现

**做什么**：实现 ExecutionRouter::route()

**产出**：`src/engine/router/execution_router.h`, `src/engine/router/execution_router.cpp`

**关键逻辑**（见设计规范 2.4 节算法）

**验收**：
- [ ] 5 条路径可达
- [ ] 决策理由清晰
- [ ] 身份声明正确

### Step 2.4：路由决策测试

**做什么**：编写路由决策核心测试

**产出**：`tests/unit/routing_decision_test.cpp`

**测试用例**（至少 15 个）：
- 纯值域 → kPurePhysics
- 值域+行间+数据引擎 → kFullFunction 或 kPostFilter
- 聚合+数据引擎 → kFullFunction 或 kPostFilter
- 数据引擎不可用 → kPurePhysics
- 体积比高 → kPostFilter
- 排除率 >90% → 不走后筛选
- 身份映射正确
- 决策理由非空
- 体积比保守估计
- 排除率预估边界
- 空 Schema 错误
- 空约束 → kPurePhysics
- 并发路由决策
- 体积比计算失败退化
- 排除率预估失败退化

**验收**：15+ 路由决策测试通过

---

## Task 3：v1 接口迁移

**目标**：将 v1 硬编码调度迁移到路由器

### Step 3.1：[COORDINATE] v1 入口重构

**做什么**：将 v1 的生成入口重构为委托路由器

**选项 A（本计划默认）**：
```cpp
// v1 入口变为委托路由器
Result<GenerationResult> generate(const GenerationRequest& request) {
    auto classification = classifier_.classify(request.constraints, request.schema);
    auto decision = router_.route(classification, request.schema, request);
    return router_.execute(decision, request.schema, request);
}
```

**选项 B（如团队选择）**：
```cpp
// 新增 GenerationService，v1 入口委托
class GenerationService {
    Result<GenerationResult> generate(const GenerationRequest& request);
};
// v1 入口委托给 GenerationService
```

**验收**：
- [ ] v1 原有功能（纯物理路径）通过路由器可达
- [ ] v1 测试不因重构而失败
- [ ] v1 EvidencePackage 字段保持兼容

### Step 3.2：迁移回归测试

**做什么**：确保 v1 功能在路由器下正常工作

**产出**：`tests/regression/v1_regression_test.cpp`

**验收**：
- [ ] v1 全部端到端测试在新路由器下通过
- [ ] v1 EvidencePackage 格式兼容

---

## Task 4：各路径执行实现

**目标**：实现 5 条退化路径的执行逻辑

### Step 4.1：纯物理路径执行

**做什么**：实现 kPurePhysics 路径执行

**关键逻辑**：调用 v1 RectangularSampler + ValueRangeValidator

**验收**：纯物理路径输出与 v1 等价

### Step 4.2：后筛选路径执行

**做什么**：实现 kPostFilter 路径执行

**关键逻辑**：物理采样 + PostFilter 后筛选过滤

**验收**：后筛选路径排除率在合理范围

### Step 4.3：全功能路径执行

**做什么**：实现 kFullFunction 路径执行

**关键逻辑**：约束驱动 + 数据引擎采样 + 两阶段执行

**验收**：全功能路径正确调用所有引擎

### Step 4.4：统计生成路径执行

**做什么**：实现 kStatisticalGeneration 路径执行

**关键逻辑**：数据引擎直接采样（无约束过滤）

**验收**：统计生成路径输出分布与训练数据分布接近

### Step 4.5：KDE 扰动路径执行

**做什么**：实现 kKDEPerturbation 路径执行

**关键逻辑**：格式化扰动（在基础数据上小幅随机扰动）

**验收**：KDE 扰动输出保持基础分布形态

### Step 4.6：路径执行测试

**做什么**：编写 5 条路径的执行测试

**产出**：`tests/unit/path_execution_test.cpp`

**验收**：5 条路径各至少 3 个测试用例

---

## Task 5：脚手架集成

**目标**：为 ExecutionRouter 添加 Trace/Explain/Metrics

### Step 5.1：Explain 增强

**做什么**：实现路由器 Explain

```cpp
struct RouterExplainInfo {
    std::vector<DegradationPath> available_paths;
    DegradationPath selected_path;
    std::string selection_reason;
    double volume_ratio;
    double estimated_exclusion_rate;
    bool data_engine_available;
};
```

**验收**：explain() 返回所有可用路径 + 选择理由

### Step 5.2：Trace span

**做什么**：路由决策和执行写入 Trace

**验收**：route() + execute() 分别产生 span

### Step 5.3：Metrics 注册

```
router_path_selected          — 路径选择计数（按路径标签）
router_volume_ratio           — 体积比分布
router_estimated_exclusion_rate — 排除率预估
router_data_engine_available  — 数据引擎可用性
```

**验收**：metrics 端点暴露路由器指标

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 退化路径 | 2 | 0.25w | ⬜ |
| Task 2: 路由决策 | 4 | 0.5w | ⬜ |
| Task 3: v1 迁移 | 2 | 0.5w | ⬜ |
| Task 4: 路径执行 | 6 | 0.5w | ⬜ |
| Task 5: 脚手架 | 3 | 0.25w | ⬜ |
| **合计** | **17** | **2w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| v1 迁移改动量大，影响现有功能 | Task 3 专门确保 v1 回归测试通过 |
| [COORDINATE] 接口兼容策略未定 | 本计划按选项 A 编写，待 C2 决策后调整 |
| 数据引擎延迟导致全功能/统计/KDE 扰动路径无法测试 | 先用 mock 数据引擎测试路由逻辑 |
| 并发路由决策竞态 | 路由决策无状态，每次独立计算 |
| 排除率预估与实际偏差过大 | 记录偏差，v2.1 可校准预估模型 |
