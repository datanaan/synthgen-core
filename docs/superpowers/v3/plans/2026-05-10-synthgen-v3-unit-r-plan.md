SynthGen Core v3 Unit R 实施计划：GC compaction
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit R 设计规范 v1.0
估算：1 周
依赖：#18 模型版本链

---

## 概述

Unit R 交付 GC compaction——3 保护条件（快照引用/锚定/N版本内）+ 自动合并 + 元数据保留。Compaction 将旧版本合并为新版本以节省存储空间，同时保持版本链的完整性。此 Unit 是 Unit S(时间旅行退化) 和 Unit T(偏差报告) 的前置。

---

## Task 1：保护条件实现

**目标**：实现 3 种保护条件判定逻辑

### Step 1.1：ProtectionCondition 枚举和检查框架

**做什么**：定义保护条件框架和判定接口

**产出**：`src/storage/gc/protection.h`, `src/storage/gc/protection.cpp`

**关键逻辑**：
- ProtectionCondition 枚举：kSnapshotReferenced, kAnchored, kWithinNVersions
- `is_protected(version)` 遍历所有条件，任一满足返回 true
- 每个条件有独立的检查方法

**验收**：
- [ ] 枚举值与阶段设计规范一致
- [ ] is_protected 正确组合多个条件

### Step 1.2：快照引用保护

**做什么**：检测版本是否被活跃快照引用

**关键逻辑**：
- 查询存储层：该版本是否有未释放的快照引用
- 快照引用 = 有生成请求正在使用此版本
- 实现方式：维护 active_snapshots 集合（set<version_id>）

**验收**：
- [ ] 有快照引用的版本返回 protected
- [ ] 无快照引用的版本不返回此保护条件

### Step 1.3：锚定保护

**做什么**：检测用户是否显式锚定版本

**关键逻辑**：
- 用户可通过 API 锚定/取消锚定版本
- 锚定信息持久化到存储层
- 实现：维护 anchored_versions 集合

**验收**：
- [ ] 锚定的版本返回 protected
- [ ] 未锚定的版本不返回此保护条件
- [ ] 锚定/取消锚定操作正确

### Step 1.4：N 版本内保护

**做什么**：最近 N 个版本不 compact

**关键逻辑**：
- N 值来自 CompactionConfig::keep_recent_n（默认 10）
- 按时间排序取最近 N 个版本
- 新创建的版本自动受此保护

**验收**：
- [ ] 最近 N 个版本返回 protected
- [ ] N 之外的旧版本不返回此保护条件
- [ ] N=0 时所有版本可 compact（边界情况）

---

## Task 2：GcCompactor 核心实现

**目标**：实现 compaction 合并和自动 compaction

### Step 2.1：compact 合并实现

**做什么**：实现版本合并逻辑

**产出**：`src/storage/gc/gc_compactor.h`, `src/storage/gc/gc_compactor.cpp`

**关键逻辑**：
- 输入：model_name
- 流程：列出所有版本 → 排除受保护版本 → 选择相邻可 compact 版本 → 合并
- 合并策略：
  - 创建新版本，parent_version_id = 被合并版本中最早的父版本
  - training_data_range = 被合并版本的并集
  - fidelity_score = min(被合并版本的 fidelity_score)（保守估计）
  - training_rows = sum(被合并版本的 training_rows)
  - 标记被合并版本为 compacted（逻辑删除）
- 合并后更新版本链索引

**验收**：
- [ ] 合并后新版本元数据正确
- [ ] 被合并版本标记为 compacted
- [ ] 受保护版本不被合并
- [ ] 合并后版本链不断裂

### Step 2.2：元数据合并保留

**做什么**：确保合并过程中关键元数据不丢失

**关键逻辑**：
- custom_metadata：merge 所有被合并版本的 key-value（后者覆盖前者）
- created_by = "auto_compact"
- 保留被合并版本的版本 ID 列表（merged_from 字段）
- 审计日志记录 compaction 操作

**验收**：
- [ ] custom_metadata 合并不丢失 key
- [ ] merged_from 列表正确
- [ ] 审计日志记录 compaction 事件

### Step 2.3：自动 compaction

**做什么**：实现定时自动 compaction 检查

**关键逻辑**：
- CompactionConfig::auto_compact = true 时启用
- compact_interval_ms 定时触发（默认 1 小时）
- auto_compact_check()：遍历所有模型 → 检查是否有可 compact 版本 → 执行
- compaction 进行中时跳过（防并发）

**验收**：
- [ ] 定时触发正确
- [ ] auto_compact=false 时不触发
- [ ] 并发 compaction 正确处理
- [ ] compact 完成后版本链状态正确

---

## Task 3：脚手架集成

**目标**：为 GcCompactor 添加 Trace/Explain/Metrics

### Step 3.1：Trace span 集成

**做什么**：为 compact/auto_compact_check 添加 span

```cpp
Result<CompactionResult> GcCompactor::compact(...) {
    SpanGuard span("gc_compactor", "compact", trace_id_);
    span.set_attribute("model_name", model_name);
    // ...
    span.set_attribute("compacted_count", result.compacted_versions.size());
    return result;
}
```

**验收**：每次 compaction 产生 span，含 compacted 版本数

### Step 3.2：Explain 接口

**做什么**：为 GcCompactor 添加 explain() 方法

```cpp
struct GcExplainInfo {
    int total_versions;
    int protected_versions;
    int compactable_versions;
    CompactionConfig config;
};
```

**验收**：explain() 返回可 compact 版本数和配置

### Step 3.3：Metrics 注册

```
gc_compaction_total          — compaction 执行次数
gc_versions_compacted        — 被合并的版本总数
gc_compaction_duration_ms    — compaction 耗时
gc_protected_versions        — 当前受保护版本数
```

**验收**：metrics 端点暴露上述指标

---

## Task 4：错误处理和测试

**目标**：完善错误路径和测试覆盖

### Step 4.1：错误路径实现

**ErrorCode 覆盖**：
- kCompactionInProgress：正在 compaction 时再次调用
- kProtectedVersion：尝试 compact 受保护版本（内部逻辑不应触发）
- kCompactionFailed：合并存储失败
- kMetadataMergeConflict：custom_metadata key 冲突（后者覆盖，不报错但记录）
- kAutoCompactDisabled：auto_compact=false 时调用 auto_compact_check

**验收**：每个 ErrorCode 至少 1 个触发测试

### Step 4.2：单元测试

**做什么**：编写 GC compaction 单元测试

**产出**：`tests/unit/gc_compactor_test.cpp`

**测试用例**（至少 15 个）：

功能测试（10+）：
- 3 保护条件各自生效
- 3 保护条件组合生效
- compaction 合并 2 个版本
- compaction 合并 5+ 个版本
- 合并后元数据保留
- 合并后版本链可遍历
- 自动 compaction 定时触发
- auto_compact=false 不触发
- anchored 集合持久化
- keep_recent_n=10 保护最近 10 个版本

错误测试（5+）：
- 并发 compaction → kCompactionInProgress
- 无可 compact 版本 → 返回空结果
- 所有版本受保护 → 返回空结果
- compaction 中断后恢复
- metadata 合并冲突处理

**验收**：15+ 测试通过，错误测试占比 ≥ 33%

### Step 4.3：边界条件测试

**测试用例**：
- 0 个可 compact 版本
- 所有版本受保护
- compaction 中断后恢复（模拟写数据后崩溃）
- 单个版本无法 compact（至少需要 2 个相邻版本）
- keep_recent_n = 总版本数（全部保护）

**验收**：5+ 边界条件测试通过

---

## Task 5：集成测试

**目标**：验证 GC compaction 与版本链、存储层的集成

### Step 5.1：集成测试

**产出**：`tests/integration/gc_integration_test.cpp`

**测试用例**（至少 6 个）：
- 完整流程：创建版本链 → 触发 compaction → 验证合并
- compaction 后时间旅行退化正确（需 Unit S，此处 mock）
- 快照引用阻止 compaction
- 锚定版本阻止 compaction
- Trace span 完整性
- Explain 输出正确性

**验收**：6+ 集成测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 保护条件 | 4 | 0.25w | ⬜ |
| Task 2: 核心实现 | 3 | 0.375w | ⬜ |
| Task 3: 脚手架 | 3 | 0.125w | ⬜ |
| Task 4: 错误和测试 | 3 | 0.125w | ⬜ |
| Task 5: 集成测试 | 1 | 0.125w | ⬜ |
| **合计** | **14** | **1w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| compaction 中断导致版本链不一致 | 采用"先创建新版本→标记旧版本→提交"三步策略，中断后以新版本是否创建为准 |
| 快照引用集合维护成本 | 活跃快照通常 <100，使用 set<version_id> 足够 |
| 元数据合并策略争议 | 保守策略：fidelity_score 取 min，training_rows 取 sum，custom_metadata 后者覆盖 |
| keep_recent_n 调整后已 compact 版本不可恢复 | 不可逆操作，explain() 在执行前展示影响预估 |
