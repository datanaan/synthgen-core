SynthGen Core v3 脚手架实施计划
文档性质：脚手架级实施计划
版本：v1.0
日期：2026-05-10
估算：1 周
上游文档：v3 阶段设计规范 v1.0、整体设计规范 v1.0

---

## 概述

v3 脚手架交付 5 项增强，与 Unit Q-T 并行开发。脚手架与功能组件享有同等地位，脚手架不过=v3 不交付。

---

## Task 1：Explain 增强——compaction 影响预估

**目标**：explain() 显示退化版本和偏差报告

### Step 1.1：Explain 扩展字段定义

**做什么**：为 v3 组件添加 Explain 信息

**产出**：`src/scaffold/explain.h`（扩展）

**关键逻辑**：
- ModelVersionChain Explain：模型名 + 版本总数 + 最大链深度 + 最新版本
- GcCompactor Explain：总版本数 + 受保护数 + 可 compact 数 + 配置
- TimeTravelEngine Explain：查询版本 + 是否退化 + 退化原因
- ContinuousAlignmentEngine Explain：漂移检测模式 + 阈值 + 收敛窗口 + 代偿期限

**验收**：
- [ ] 每个组件 explain() 返回 v3 新增字段
- [ ] compaction 前调用 explain() 可预览影响（哪些版本将被 compact）

### Step 1.2：Explain 测试

**测试用例**（至少 4 个）：
- 版本链 explain 正确
- GC explain 展示可 compact 版本数
- 时间旅行 explain 展示退化信息
- 持续对齐 explain 展示漂移配置

**验收**：4+ 测试通过

---

## Task 2：Trace 增强——持续对齐模型更新前后变化

**目标**：模型更新 span 含 drift_score + compensation_status

### Step 2.1：Trace span 扩展

**做什么**：为 v3 组件添加 span

**关键逻辑**：
- version_chain span：operation(create/get/list) + model_name + version_id
- gc_compactor span：operation(compact) + compacted_count + duration_ms
- time_travel span：operation(query_as_of) + version_id + was_degraded + bias_report
- alignment span：operation(update_model) + drift_detected + drift_score + compensation_status

**验收**：
- [ ] 每个操作产生 span
- [ ] 漂移检测结果在 span 中可查
- [ ] atomic_write 产生 3 个子 span（data/meta/audit）

### Step 2.2：Trace 测试

**测试用例**（至少 4 个）：
- 版本链操作 Trace 完整
- GC compaction Trace 含合并信息
- 时间旅行 Trace 含退化信息
- 持续对齐 Trace 含漂移和代偿信息

**验收**：4+ 测试通过

---

## Task 3：可观测性增强——版本链状态 + GC 历史

**目标**：/metrics 新增 v3 指标

### Step 3.1：Metrics 注册

**做什么**：注册 v3 组件 metrics

**新增指标**：
```
# 版本链
version_chain_total_versions        — 版本总数（按模型分标签）
version_chain_create_latency_ms     — 创建延迟
version_chain_depth                 — 最大链深度

# GC
gc_compaction_total                 — compaction 执行次数
gc_versions_compacted               — 被合并版本总数
gc_compaction_duration_ms           — compaction 耗时
gc_protected_versions               — 受保护版本数

# 存储模型层
model_storage_checkpoint_size_bytes — 检查点大小
model_storage_load_latency_ms       — 加载延迟
model_cache_hit_rate                — 缓存命中率

# 持续对齐
alignment_drift_detected_total      — 漂移触发次数
alignment_drift_score               — 最近漂移分数
alignment_compensation_status       — 代偿状态
```

**验收**：/metrics 端点暴露上述指标

### Step 3.2：Metrics 测试

**测试用例**（至少 3 个）：
- 版本创建后 version_chain_total_versions 递增
- compaction 后 gc_versions_compacted 正确
- 漂移检测后 alignment_drift_score 更新

**验收**：3+ 测试通过

---

## Task 4：错误注入增强——compaction 冲突场景

**目标**：注入 compaction 冲突后系统正确处理

### Step 4.1：错误注入框架扩展

**做什么**：为 v3 场景添加错误注入点

**注入场景**：
- compaction 写入中途失败（模拟存储层错误）
- 快照引用在 compaction 过程中新增（并发冲突）
- atomic_write 阶段二中断（模拟进程崩溃）
- 漂移检测超时（模拟数据引擎响应慢）

**验收**：
- [ ] 注入 compaction 中断后系统恢复正确
- [ ] 注入并发快照引用后 compaction 跳过受保护版本
- [ ] 注入 atomic_write 中断后重启恢复正确

### Step 4.2：错误注入测试

**测试用例**（至少 4 个）：
- compaction 中断恢复
- 并发快照引用冲突
- atomic_write 中断恢复
- 漂移检测超时降级

**验收**：4+ 测试通过

---

## Task 5：compaction 前后一致性测试

**目标**：compaction 后生成结果与直接生成结果一致

### Step 5.1：一致性测试框架

**做什么**：编写 compaction 前后一致性验证测试

**测试方法**：
1. 使用固定 seed 生成数据（生成结果 A）
2. 创建版本 → compact → 时间旅行加载（生成结果 B）
3. 比较 A 和 B 的统计特征（均值/方差/分位数）
4. 允许误差：均值差异 <1%，方差差异 <5%

**产出**：`tests/integration/compaction_consistency_test.cpp`

### Step 5.2：一致性测试

**测试用例**（至少 4 个）：
- 值域约束 compaction 前后一致
- 行间约束 compaction 前后一致
- 聚合约束 compaction 前后一致
- KDE 采样 compaction 前后统计一致

**验收**：4+ 一致性测试通过

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1: Explain 增强 | 0.2w | ⬜ |
| Task 2: Trace 增强 | 0.2w | ⬜ |
| Task 3: 可观测性 | 0.2w | ⬜ |
| Task 4: 错误注入 | 0.2w | ⬜ |
| Task 5: 一致性测试 | 0.2w | ⬜ |
| **合计** | **1w** | — |
