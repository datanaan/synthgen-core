SynthGen Core v3 工具线实施计划
文档性质：工具线级实施计划
版本：v1.0
日期：2026-05-10
估算：0.5 周
上游文档：v3 阶段设计规范 v1.0、整体设计规范 v1.0

---

## 概述

v3 工具线交付 4 项工具增强，在 v3 Wave 3 期间与功能组件并行开发。工具线产出必须可验证。

---

## Task 1：组件模板引擎 v0.3

**目标**：从 #18-#24 任一组件接口描述生成骨架代码

### Step 1.1：v3 组件模板

**做什么**：添加 v3 组件模板

**产出**：`tools/scaffold_templates/`（扩展）

**新增模板**：
- version_chain_template：ModelVersionChain + ModelVersion 骨架
- gc_compactor_template：GcCompactor + ProtectionCondition 骨架
- time_travel_template：TimeTravelEngine + TimeTravelResult 骨架
- alignment_template：ContinuousAlignmentEngine + DriftDetector 骨架
- model_storage_template：ModelStorageLayer 骨架
- bias_report_template：CompactionBiasReport 骨架

**关键逻辑**：
- 输入：组件名（如 "version_chain"）→ 输出：.h + .cpp 骨架 + _test.cpp 骨架
- 骨架包含：命名空间、类声明、Result<T> 接口、Explain/Trace 占位、测试框架
- 生成的骨架能通过编译和基础 CI

**验收**：
- [ ] 生成 #18-#24 任一组件的骨架
- [ ] 骨架代码能编译
- [ ] 骨架测试文件能运行（全部 SKIP）

---

## Task 2：测试辅助库 v0.3

**目标**：compaction 前后一致性测试辅助宏

### Step 2.1：compaction 一致性辅助宏

**做什么**：添加 compaction 前后一致性验证辅助工具

**产出**：`src/scaffold/test_helpers.h`（扩展）

**新增宏和函数**：
```cpp
// Compaction 前后统计一致性检查
ASSERT_CONSISTENT_AFTER_COMPACTION(generated_before, generated_after, tolerance);

// 版本链完整性检查
ASSERT_VERSION_CHAIN_INTEGRITY(version_chain, model_name);

// 漂移检测辅助：构造有/无漂移的数据
ArrowBatch make_drifted_data(const ArrowBatch& original, double shift_amount);
ArrowBatch make_stable_data(const ArrowBatch& original, uint64_t seed);
```

**验收**：
- [ ] 宏正确比较两个生成结果的统计特征
- [ ] tolerance 参数生效
- [ ] 版本链完整性检查覆盖深度和连续性

---

## Task 3：Schema 校验器 v1.1

**目标**：版本链字段校验规则

### Step 3.1：版本链 Schema 校验规则

**做什么**：为 v3 新增字段添加校验规则

**产出**：`tools/schema_checker/`（扩展）

**新增校验规则**：
- ModelVersion 必填字段：version_id, model_name, created_at, is_immutable
- CompactionConfig 必填字段：keep_recent_n (>=1), auto_compact
- CompactionBiasReport 必填字段：requested_version, returned_version, reason
- EvidencePackage v3 新增字段校验
- 适用性枚举校验：值必须在 {always, data_engaged, aggregation_present, drift_available, post_filter_engaged, not_applicable} 中

**验收**：
- [ ] 故意拼错版本链字段名能检出
- [ ] 适用性枚举非法值能检出
- [ ] v1/v2 字段校验不受影响

---

## Task 4：Trace 分析工具 v0.2

**目标**：compaction 冲突 span 检测

### Step 4.1：compaction 冲突检测规则

**做什么**：添加 compaction 相关的 Trace 分析规则

**产出**：`tools/trace_analyzer/`（扩展）

**新增规则**：
- 规则 5：compaction 期间出现版本创建 → 告警"concurrent version creation during compaction"
- 规则 6：atomic_write 缺少 data/meta/audit 三阶段 span → 告警"incomplete atomic_write"
- 规则 7：漂移检测 drift_score > 0.5 连续 3 次 → 标红"sustained high drift"
- 规则 8：时间旅行 was_degraded=true → 标黄"degraded time travel query"

**验收**：
- [ ] 构造 compaction 冲突 Trace 能标红
- [ ] 构造不完整 atomic_write Trace 能告警
- [ ] 高漂移 Trace 能标红

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1: 模板引擎 v0.3 | 0.125w | ⬜ |
| Task 2: 测试辅助库 v0.3 | 0.125w | ⬜ |
| Task 3: Schema 校验器 v1.1 | 0.125w | ⬜ |
| Task 4: Trace 分析 v0.2 | 0.125w | ⬜ |
| **合计** | **0.5w** | — |
