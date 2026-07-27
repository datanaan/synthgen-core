SynthGen Core v4 Unit X 实施计划：反例搜索(research) + EvidencePackage v3
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit X 设计规范 v1.0
估算：2 周
依赖：Unit W (#28)、v2 #13 执行路由器、待测模型接入协议
标注：[COORDINATE] C5, C6

---

## 概述

Unit X 交付反例搜索（research 里程碑）和 EvidencePackage v3。如协调项未解决，#29 部分标记 deferred，仅交付 #30。

---

## Task 1：ModelVersionProvenance 实现

### Step 1.1：数据结构

**产出**：`src/evidence/model_version_provenance.h`, `src/evidence/model_version_provenance.cpp`

**验收**：ModelVersionProvenance 构造和序列化正确，fidelity_score ∈ [0.0, 1.0]

### Step 1.2：与版本链集成

**验收**：从 ModelVersionChain 填充 ModelVersionProvenance，was_compacted 正确反映 GC 状态

---

## Task 2：CounterExampleSearcher 实现（条件执行）

**[COORDINATE] 前置条件**：C5（待测模型接入协议）和 C6（理论基础）须至少一项就绪。

### Step 2.1：CounterExampleResult 数据结构

**产出**：`src/search/counter_example_result.h`, `src/search/counter_example_result.cpp`

**验收**：三态（available/deferred/research_failed）正确表示

### Step 2.2：搜索核心（如前置条件就绪）

**产出**：`src/search/counter_example_searcher.h`, `src/search/counter_example_searcher.cpp`

**验收**：
- available：找到约束违反区域
- 搜索不收敛返回 kSearchNotConverged
- 搜索超时返回 kSearchTimeout

### Step 2.3：deferred 路径（如前置条件未就绪）

**产出**：最小实现，search() 直接返回 CounterExampleResult{status="deferred"}

**验收**：deferred 状态不影响 EvidencePackage v3 其他字段

---

## Task 3：EvidencePackage v3 实现

### Step 3.1：EvidencePackageV3 数据结构

**产出**：`src/evidence/evidence_package_v3.h`, `src/evidence/evidence_package_v3.cpp`

**验收**：继承 v2 字段，新增 model_provenance / completeness_score / counter_example / bias_report_ref

### Step 3.2：向后兼容

**验收**：v2 字段仍可读取，schema_version = "v3"

### Step 3.3：完整性校验

**验收**：kMissingModelProvenance / kMissingCompletenessScore 正确触发

---

## Task 4：脚手架集成

### Step 4.1：Explain 增强

**产出**：修改 `src/explain/explain_engine.h/cpp`

**验收**：Explain 输出包含完备度评分、模型溯源、反例搜索状态

### Step 4.2：Trace 集成

**验收**：反例搜索过程记录到 Trace span（如执行）

### Step 4.3：可观测性

**验收**：counter_example_status 作为 Prometheus 指标暴露

---

## Task 5：测试

### Step 5.1：错误路径测试清单

**验收**：以下 9 个 ErrorCode 全部有对应测试用例：

| ErrorCode | 测试场景 | 用例数 |
|-----------|---------|--------|
| kSearchNotConverged | 搜索不收敛 | 1 |
| kSearchTimeout | 搜索超时 | 1 |
| kMissingModelProvenance | EvidencePackage v3 缺少 model_provenance | 1 |
| kMissingCompletenessScore | EvidencePackage v3 缺少 completeness_score | 1 |
| kInvalidFidelityScore | fidelity_score ∉ [0.0, 1.0] | 1 |
| kCounterExampleUnavailable | 前置条件未就绪时请求反例搜索 | 1 |
| kIncompatibleEvidenceVersion | v1 EvidencePackage 传入 v3 路径 | 1 |
| kDeferredFeature | #29 标记 deferred 时的接口行为 | 1 |
| kSerializationError | 序列化/反序列化失败 | 1 |

### Step 5.2：功能测试

**产出**：`tests/unit/counter_example_test.cpp`, `tests/unit/evidence_package_v3_test.cpp`

**验收**：22+ 测试通过，错误测试占比 ≥ 30%

**测试分类**：
- EvidencePackage v3 测试：12+（构造 3，字段填充 4，向后兼容 3，完整性校验 2）
- 反例搜索测试：10+（available 3，deferred 3，research_failed 2，超时/不收敛 2）
- 错误测试：9+（对应上述 ErrorCode）

### Step 5.3：deferred 场景测试

**验收**：前置条件未就绪时，#29 返回 deferred，#30 仍正常

**deferred 测试场景**：
- C5 未解决：search() 返回 CounterExampleResult{status="deferred"}
- C6 未解决：同上
- 两者均未解决：同上
- deferred 状态下 EvidencePackage v3 其他字段正常填充

### Step 5.4：兼容性测试

**产出**：`tests/integration/v4_evidence_v2_v3_compat_test.cpp`

**验收**：v2→v3 升级无数据丢失

**兼容性场景**：
- v2 字段在 v3 中可完整读取
- schema_version 正确区分 v2/v3
- 路由器根据 schema_version 选择正确的处理路径

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1 | 0.25w | ⬜ |
| Task 2 | 0.5w (条件) | ⬜ |
| Task 3 | 0.5w | ⬜ |
| Task 4 | 0.25w | ⬜ |
| Task 5 | 0.5w | ⬜ |
| **合计** | **2w** | — |

---

## 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| [COORDINATE] C5 未解决 | #29 无法实现 | #29 标记 deferred，仅交付 #30 |
| [COORDINATE] C6 未解决 | #29 理论基础缺失 | 预研后再决策 |
| 反例搜索不收敛 | research_failed | 记录失败原因，不影响其他功能 |
| EvidencePackage v2→v3 不兼容 | 升级问题 | 向后兼容测试覆盖 |
