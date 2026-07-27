SynthGen Core v2 Unit M 设计规范：执行路由器重构
文档性质：Unit 级设计规范 [COORDINATE]
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit M 实施计划
组件：#13 执行路由器重构
估算：2 周
依赖：#10 行间引擎 + #11 聚合引擎 + #12 分类器 + v1#5/#6
协调项：C2（执行路由器与 v1 接口兼容策略）

---

## 一、本 Unit 交付什么

**Unit M 是 v2 的核心重构**——将 v1 的硬编码调度逻辑重构为多路径路由器驱动。

交付物：
1. **ExecutionRouter**：5 条退化路径 + 身份切换 + 体积比预估
2. **RoutingDecision**：路由决策结果 + 约束完备性检查
3. **DegradationPath**：5 条退化路径枚举和实现
4. **IdentityDeclaration**：生成器身份声明
5. **v1 接口适配**：v1 硬编码路径 → 路由器驱动

---

## 二、#13 执行路由器重构

### 2.1 核心语义

v1 的生成流程是硬编码的：

```
v1: 物理引擎矩形域采样 → 值域验证 → EvidencePackage
```

v2 的生成流程是路由器驱动的：

```
v2: 约束分类 → 路由决策 → 选择路径执行 → EvidencePackage
```

**5 条退化路径**（按约束完备性从高到低）：

| 路径 | 身份 | 条件 | 说明 |
|------|------|------|------|
| kFullFunction | constraint_driven_synthetic | 约束完备 + 数据引擎可用 | 全功能约束驱动 |
| kPostFilter | post_filter_synthetic | 排除率 < 90% | 物理采样 + 后筛选 |
| kPurePhysics | physics_sampler | 仅值域约束 / 无数据引擎 | v1 等价路径 |
| kStatisticalGeneration | statistical_generator | 约束不完备 + 数据引擎可用 | 统计生成 |
| kKDEPerturbation | kde_perturbation_generator | 约束极度不完备 + 数据引擎可用 | 格式化扰动 |

### 2.2 [COORDINATE] v1 接口兼容策略

**待决策**：v1 硬编码调度 → v2 路由器驱动的迁移边界

**选项 A：完全重构**
- v1 生成入口直接委托给路由器
- v1 的 `RectangularSampler::generate()` 被路由器内部调用
- 架构干净，但改动量大

**选项 B：适配器模式**
- v1 入口不变，内部注入路由器
- 新增 `GenerationService` 作为统一入口
- v1 入口委托给 GenerationService
- v1 接口兼容，但适配器层增加复杂度

**占位推荐**：选项 A（完全重构）。理由：路线图已明确"摩托车是新的整车"。

> ⚠️ 本 spec 按选项 A 编写。如果团队决策为选项 B，需调整 Step 3.1 的适配层设计。

### 2.3 接口定义

```cpp
namespace synthgen::engine::router {

// 退化路径
enum class DegradationPath {
    kFullFunction,
    kPostFilter,
    kPurePhysics,
    kStatisticalGeneration,
    kKDEPerturbation,
};

// 身份声明
struct IdentityDeclaration {
    std::string identity;          // 生成器身份名称
    std::string justification;     // 选择理由
    DegradationPath path;          // 对应路径

    // 序列化为字符串（写入 EvidencePackage）
    std::string to_string() const;
};

// 体积比信息
struct VolumeRatioInfo {
    double constraint_volume;      // 约束空间体积
    double data_distribution_volume; // 数据分布体积
    double ratio;                  // 约束空间/数据分布
    bool estimated;               // 是否为预估值
};

// 路由决策
struct RoutingDecision {
    DegradationPath selected_path;
    IdentityDeclaration identity;
    ClassificationResult classification;
    VolumeRatioInfo volume_ratio;
    double estimated_exclusion_rate;
    bool data_engine_available;
    std::string decision_reason;   // 人可读的选择理由
};

// 执行路由器
class ExecutionRouter {
public:
    explicit ExecutionRouter(
        const DataEngineV1* data_engine = nullptr);

    // 路由决策
    Result<RoutingDecision> route(
        const ClassificationResult& classification,
        const Schema& schema,
        const GenerationRequest& request);

    // 执行生成（根据路由决策选择路径）
    Result<GenerationResult> execute(
        const RoutingDecision& decision,
        const Schema& schema,
        const GenerationRequest& request);

    // 身份映射
    static const char* identity_for_path(DegradationPath path);

    // 数据引擎可用性
    bool is_data_engine_available() const;

    // Explain
    ExplainInfo explain(const ClassificationResult& classification) const;

private:
    const DataEngineV1* data_engine_;  // 可选，nullptr 时不可用

    // 路由规则实现
    Result<RoutingDecision> route_full_function(
        const ClassificationResult& classification,
        const Schema& schema);
    Result<RoutingDecision> route_post_filter(
        const ClassificationResult& classification,
        const Schema& schema);
    Result<RoutingDecision> route_pure_physics(
        const ClassificationResult& classification);
    Result<RoutingDecision> route_statistical_generation(
        const Schema& schema);
    Result<RoutingDecision> route_kde_perturbation(
        const Schema& schema);

    // 体积比计算
    Result<double> compute_volume_ratio(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints);

    // 排除率预估
    Result<double> estimate_exclusion_rate(
        const ClassificationResult& classification,
        double volume_ratio);
};

}  // namespace synthgen::engine::router
```

### 2.4 路由决策算法

```
输入：ClassificationResult, Schema, GenerationRequest

1. 检查数据引擎可用性
   if data_engine == nullptr || !data_engine->is_fitted():
       data_engine_available = false

2. 计算体积比（如果数据引擎可用）
   if data_engine_available:
       volume_ratio = data_engine->volume_ratio(schema, constraints)
   else:
       volume_ratio = 1.0  // 保守估计

3. 预估排除率
   estimated_exclusion_rate = estimate(volume_ratio, classification)

4. 路由决策（从高到低优先级）
   if classification 完备 && data_engine_available:
       return kFullFunction
   elif estimated_exclusion_rate < 0.90:
       return kPostFilter
   elif 仅值域约束 || !data_engine_available:
       return kPurePhysics
   elif data_engine_available && !classification 完备:
       return kStatisticalGeneration
   else:
       return kKDEPerturbation

5. 构建身份声明
   identity = identity_for_path(selected_path)
   justification = decision_reason
```

### 2.5 错误处理

```cpp
enum class RouterErrorCode {
    kNoAvailablePath,              // 无可用生成路径
    kDataEngineUnavailable,         // 数据引擎不可用（信息，非错误）
    kVolumeRatioComputationFailed,  // 体积比计算失败
    kExclusionRateEstimationFailed, // 排除率预估失败
    kRoutingDecisionConflict,       // 路由决策与实际执行不一致
    kIdentityDeclarationMissing,    // 身份声明缺失
    kConstraintIncompleteness,     // 约束不完备（信息，非错误）
    kV1InterfaceMigrationError,    // v1 接口迁移错误
};
```

---

## 三、Unit M 验收标准

### 3.1 功能验收

- [ ] 5 条退化路径全部可达
- [ ] 路由决策算法正确
- [ ] 身份声明每条路径正确
- [ ] 体积比计算正确（数据引擎可用时）
- [ ] 排除率预估合理（与实际偏差 <20%）
- [ ] 数据引擎不可用时退化到纯物理路径
- [ ] v1 接口（RectangularSampler）通过路由器可用

### 3.2 脚手架验收

- [ ] ExecutionRouter 提供 explain() 方法（返回所有路径 + 选择理由）
- [ ] 路由决策写入 Trace span（含 selected_path + decision_reason）
- [ ] 退化路径命中率写入 Metrics

### 3.3 错误测试验收

- [ ] 无可用路径返回 kNoAvailablePath
- [ ] 数据引擎不可用时退化到纯物理
- [ ] 体积比计算失败时保守估计
- [ ] 并发路由决策竞态条件处理
- [ ] 路由决策与实际执行不一致时回退
- [ ] 身份声明缺失时返回 kIdentityDeclarationMissing
- [ ] v1 接口迁移后功能不变

### 3.4 边界条件测试

- [ ] 纯值域约束 → kPurePhysics
- [ ] 含行间约束 → kPostFilter 或 kFullFunction
- [ ] 含聚合约束 → kPostFilter 或 kFullFunction
- [ ] 排除率恰好 90% → kPostFilter 还是 kPurePhysics？
- [ ] 数据引擎 fit 但维度 >20 → 警告 + 退化
- [ ] 空 Schema → 错误
- [ ] 约束列表为空 → kPurePhysics

### 3.5 测试验收

- [ ] 单元测试覆盖：5 条路径、路由决策、身份映射、体积比
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 至少 30 个测试用例
- [ ] CI 自动运行

---

## 四、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `ExecutionRouter::route()` | Unit N (后筛选), Unit P (EvidencePackage) | 路由决策 |
| `RoutingDecision` | Unit N, Unit P | 生成路径 + 身份 |
| `DegradationPath` | Unit Q (脚手架) | 退化路径测试 |
| `IdentityDeclaration` | Unit P (EvidencePackage) | 身份声明写入证据包 |
