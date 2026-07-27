SynthGen Core v3 Unit S 设计规范：时间旅行 + 持续对齐
文档性质：Unit 级设计规范 [COORDINATE]
版本：v1.0
日期：2026-05-10
上游文档：v3 阶段设计规范 v1.0
下游文档：Unit S 实施计划
组件：#20 时间旅行(AS OF) + #21 持续对齐(UPDATE MODEL)
估算：2 周
依赖：#18 版本链 + #19 GC + v2#13 执行路由器 + v2#15b 数据引擎
协调项：C4（持续对齐与数据引擎接口协议）、C5（待测模型接入协议）

---

## 一、本 Unit 交付什么

1. **#20 时间旅行(AS OF)**（0.5w）：按版本读取快照 + compaction 退化行为
2. **#21 持续对齐(UPDATE MODEL)**（1.5w）：新数据纳入 + 漂移检测 + 代偿收敛时限

---

## 二、#20 时间旅行(AS OF)

### 2.1 核心语义

时间旅行允许用户查询任意模型版本的数据。如果请求的版本已被 compaction，返回最近可用版本 + 偏差报告。

### 2.2 接口定义

（定义见 v3 阶段设计规范 3.3 节）

### 2.3 compaction 退化行为

请求版本 V1 → V1 存在 → 返回 V1 数据
请求版本 V1 → V1 已被 compact → 返回最近版本 V2 + 偏差报告（was_degraded=true）

### 2.4 错误处理

```cpp
enum class TimeTravelErrorCode {
    kVersionNotFound,
    kVersionCompacted,
    kNoAvailableVersion,
    kSnapshotLoadFailed,
};
```

---

## 三、#21 持续对齐(UPDATE MODEL)

### 3.1 核心语义

持续对齐保持模型与最新数据同步：新数据到来 → 检测漂移 → 启动代偿 → 收敛/降级

### 3.2 [COORDINATE] 与数据引擎接口协议

**待决策**：持续对齐与数据引擎的接口

| 问题 | 选项 | 推荐 |
|------|------|------|
| 增量更新 vs 重新 fit | 增量更新（更高效）或重新 fit（更简单） | 增量更新 |
| 待测模型接入协议 | v3 定义标准协议供 v4 使用 | 必须定义 |

**占位**：支持增量更新 `fit_incremental()` + 定义 `TestModelProtocol`。

### 3.3 接口定义

（定义见 v3 阶段设计规范 3.4 节）

### 3.4 错误处理

```cpp
enum class AlignmentErrorCode {
    kDataEngineUnavailable,
    kEmptyTrainingData,
    kDriftDetectionFailed,
    kCompensationTimeout,
    kCompensationDiverging,
    kVersionCreationFailed,
};
```

---

## 四、Unit S 验收标准

### 4.1 功能验收

- [ ] AS OF 读取正确版本
- [ ] compaction 退化返回偏差报告
- [ ] 新数据纳入正确
- [ ] 漂移检测工作
- [ ] 代偿收敛时限生效

### 4.2 错误测试验收

- [ ] kVersionNotFound / kEmptyTrainingData / kDriftDetectionFailed / kCompensationTimeout / kDataEngineUnavailable

### 4.3 测试验收

- [ ] 至少 25 个测试用例，错误测试占比 ≥ 30%

---

## 五、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `TimeTravelEngine::query_as_of()` | SDK/API | 时间旅行 |
| `ContinuousAlignmentEngine::update_model()` | SDK/API | 持续对齐 |
| `AlignmentResult` | Unit T | 代偿状态 |
| `CompactionBiasReport` | Unit T | compaction 偏差 |
| `TestModelProtocol` | v4 | 待测模型接入 |
