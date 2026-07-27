SynthGen Core v4 Unit X 设计规范：反例搜索(research) + EvidencePackage v3
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v4 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit X 实施计划
组件：#29 反例搜索(research) + #30 EvidencePackage v3
估算：2 周
依赖：Unit W (#28)、v2 #13 执行路由器、待测模型接入协议
标注：[COORDINATE] C5, C6

---

## 一、本 Unit 交付什么

**Unit X 是 v4 的研究性扩展**——反例搜索探索约束系统的"反向验证"能力，EvidencePackage v3 集成所有 v4 元数据。

交付物：
1. **CounterExampleSearcher**：约束违反区域搜索（research 里程碑）
2. **EvidencePackageV3**：集成模型溯源 + 完备度评分 + 反例结果
3. **ModelVersionProvenance**：模型训练溯源信息
4. **反例搜索状态管理**：available / deferred / research_failed 三态

**[COORDINATE] 协调项**：

- **C5**：待测模型接入协议——v3 需定义 TestModelProtocol，否则反例搜索无法启动。如 v3 未交付，本 Unit 的 #29 部分标记 deferred。
- **C6**：反例搜索理论基础——理论框架 v1.3 未包含反例搜索独立章节。如预研结论为"不可行"，#29 整体标记 deferred。

---

## 二、#29 反例搜索(research)

### 2.1 核心语义

反例搜索探索约束系统的"反向验证"——寻找约束未覆盖的区域，或约束本身存在矛盾的实例：

- **目的**：验证约束系统的完备性，而非生成违反约束的数据
- **方法**：基于待测模型，搜索约束边界附近的违反区域
- **状态**：research 里程碑，可能不收敛或不可行

**research 声明**：

反例搜索是研究性功能。它可能：
1. 成功找到约束违反区域（status: available）
2. 因理论不成熟而推迟（status: deferred）
3. 因算法不收敛而失败（status: research_failed）

**三种结果的应对**：

| 状态 | 含义 | 影响 |
|------|------|------|
| available | 找到违反区域 | EvidencePackage 包含反例信息 |
| deferred | 前置条件未就绪 | 不影响其他 v4 功能 |
| research_failed | 搜索不收敛 | 记录失败原因，不影响其他功能 |

### 2.2 接口定义

（定义见 v4 阶段设计规范 3.4 节）

### 2.3 错误处理

```cpp
enum class CounterExampleErrorCode {
    kProtocolNotDefined,        // [COORDINATE] 待测模型接入协议未定义
    kSearchNotConverged,        // 搜索不收敛
    kSearchTimeout,             // 搜索超时
    kTheoryNotReady,            // [COORDINATE] 理论基础未就绪
    kInvalidConstraintSet,      // 约束集无效
    kModelUnavailable,          // 待测模型不可用
};
```

---

## 三、#30 EvidencePackage v3

### 3.1 核心语义

EvidencePackage v3 是 v4 的元数据容器——集成模型溯源、完备度评分和可选的反例信息：

- 继承 v2 的所有字段
- 新增 ModelVersionProvenance：模型训练溯源
- 新增 CompletenessScore：完备度评分
- 新增可选 CounterExampleResult：反例搜索结果
- 新增可选 bias_report_ref：偏差报告引用

### 3.2 接口定义

（定义见 v4 阶段设计规范 3.5 节）

### 3.3 错误处理

```cpp
enum class EvidencePackageV3ErrorCode {
    kMissingModelProvenance,    // 缺少模型溯源信息
    kMissingCompletenessScore,  // 缺少完备度评分
    kSchemaVersionMismatch,     // Schema 版本不匹配
    kInvalidModelVersionId,     // 无效的模型版本 ID
    kFidelityScoreOutOfRange,   // fidelity_score 超出 [0.0, 1.0]
};
```

---

## 四、Unit X 验收标准

### 4.1 功能验收

**#29 反例搜索**：
- [ ] available 状态：找到约束违反区域，返回 violation_regions
- [ ] deferred 状态：前置条件未就绪，返回 status="deferred"
- [ ] research_failed 状态：搜索不收敛，返回 status="research_failed"
- [ ] 搜索超时正确处理

**#30 EvidencePackage v3**：
- [ ] ModelVersionProvenance 正确填充
- [ ] CompletenessScore 正确嵌入
- [ ] counter_example 为 optional，deferred 时不包含
- [ ] schema_version = "v3"
- [ ] 与 v2 EvidencePackage 向后兼容（v2 字段仍可读取）

### 4.2 错误测试验收

- [ ] kProtocolNotDefined
- [ ] kSearchNotConverged
- [ ] kSearchTimeout
- [ ] kTheoryNotReady
- [ ] kMissingModelProvenance
- [ ] kMissingCompletenessScore
- [ ] kSchemaVersionMismatch
- [ ] kInvalidModelVersionId
- [ ] kFidelityScoreOutOfRange

### 4.3 边界条件测试

- [ ] 反例搜索返回空 violation_regions
- [ ] ModelVersionProvenance 中 was_compacted=true
- [ ] fidelity_score = 0.0 / 1.0 边界
- [ ] bias_report_ref 为空
- [ ] v2→v3 升级兼容性
- [ ] 反例搜索 1000 次迭代的性能

### 4.4 测试验收

- [ ] 至少 15 个测试用例
- [ ] 错误测试占比 ≥ 30%
- [ ] 反例搜索 deferred 场景有测试

### 4.5 脚手架验收

- [ ] Explain 输出包含完备度评分和模型溯源
- [ ] Trace span 记录反例搜索过程（如执行）
- [ ] 反例搜索状态（available/deferred/failed）作为可观测性指标

### 4.6 诚实声明验收

- [ ] 反例搜索是 research，不是生产功能
- [ ] deferred 和 research_failed 是合法状态，不是错误
- [ ] EvidencePackage v3 包含完备度评分——高评分 ≠ 高质量
- [ ] ModelVersionProvenance 中的 fidelity_score 是参考值，不是保证

---

## 五、[COORDINATE] 协调项占位

### C5: 待测模型接入协议

**当前状态**：v3 计划中未明确定义 TestModelProtocol

**选项**：
1. v3 交付时定义 TestModelProtocol → #29 可按计划执行
2. v3 未定义 → #29 标记 deferred，仅交付 #30 EvidencePackage v3
3. 在 v4 中定义最小协议（仅含模型加载/推理接口）

**推荐**：选项 1，在 v3 Unit S 或 Unit T 中定义

### C6: 反例搜索理论基础

**当前状态**：理论框架 v1.3 未包含反例搜索独立章节

**选项**：
1. 理论框架 v1.4 新增反例搜索章节 → #29 有理论支撑
2. v4 预研阶段产出理论文档 → 根据预研结果决定
3. 取消反例搜索 → #29 从路线图中移除

**推荐**：选项 2，v4 启动前进行 1-2 周预研

---

## 六、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `CounterExampleSearcher::search()` | EvidencePackage v3 | 约束违反区域 |
| `EvidencePackageV3` | 用户端, 审计 | v4 元数据容器 |
| `ModelVersionProvenance` | Explain, 审计 | 模型溯源 |
| `CounterExampleResult` | Explain | 反例信息展示 |
