SynthGen Core v3 Unit T 实施计划：tail_report增强 + 存储模型层 + 偏差报告
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit T 设计规范 v1.0
估算：2.5 周
依赖：v2#14 后筛选 + #21 持续对齐 + #19 GC + #18 版本链

---

## 概述

Unit T 交付三个 v3 增强组件：tail_report 增强版（排除率联动+fidelity_mismatch+代偿状态）、存储模型层（检查点存储+流式加载+atomic_write 事务）、偏差报告（compaction 偏差字段完整）。Unit T 跨 Wave 1-2，其中 #23 存储模型层在 Wave 1 与 Unit Q 并行。

---

## Task 1：tail_report 增强版（#22）

**目标**：扩展 tail_report 支持 v3 新增字段

### Step 1.1：TailReportV3 结构体扩展

**做什么**：在 TailReportV1 基础上扩展 v3 字段

**产出**：`src/engine/evidence/tail_report.h`（扩展）

**关键逻辑**：
- TailReportV3 继承 TailReportV1，新增字段：
  - rate_band：ExclusionRateBand（low/medium/high/critical）
  - data_grade：联动排除率的字符串
  - fidelity_mismatch：bool，compaction 退化时为 true
  - mismatch_reason：如 "compaction_degraded"
  - compensation_status：converging/converged/diverging/timeout_degraded
  - compensation_deadline：代偿收敛时限
- 填充逻辑：
  - rate_band 和 data_grade 从 PostFilter 结果联动
  - fidelity_mismatch 从 TimeTravelEngine 的 was_degraded 联动
  - compensation_status 从 ContinuousAlignmentEngine 联动

**验收**：
- [ ] v3 新增字段定义正确
- [ ] v1 字段不受影响（向后兼容）
- [ ] 序列化正确

### Step 1.2：联动逻辑实现

**做什么**：实现排除率与 data_grade 联动、fidelity_mismatch 联动、代偿状态联动

**产出**：`src/engine/evidence/tail_report.cpp`（扩展）

**关键逻辑**：
- 排除率联动：复用 v2 PostFilter 的 ExclusionRateBand 映射
- fidelity_mismatch：当 TimeTravelResult.was_degraded=true 时设置
- compensation_status：从 AlignmentResult.compensation_status 传入
- TailReportV3 构建：在 EvidencePackage 构建器中调用

**验收**：
- [ ] 排除率 0-30% → rate_band=low, data_grade=statistics_guaranteed
- [ ] 排除率 30-70% → rate_band=medium, data_grade=limited_fidelity
- [ ] fidelity_mismatch 在 compaction 退化时标记
- [ ] compensation_status 正确传递

### Step 1.3：tail_report 增强版测试

**做什么**：编写 TailReportV3 单元测试

**产出**：`tests/unit/tail_report_v3_test.cpp`

**测试用例**（至少 8 个）：
- v3 字段填充正确
- 排除率与 data_grade 联动正确（4 个 band）
- fidelity_mismatch 标记正确
- mismatch_reason 字符串正确
- compensation_status 传递正确
- compensation_deadline 设置正确
- v1 字段不受影响
- 序列化/反序列化一致

**验收**：8+ 测试通过

---

## Task 2：存储模型层（#23）

**目标**：实现检查点存储、流式加载和 atomic_write 事务

### Step 2.1：检查点存储

**做什么**：实现 save_checkpoint 接口

**产出**：`src/storage/model/model_storage_layer.h`, `src/storage/model/model_storage_layer.cpp`

**关键逻辑**：
- save_checkpoint(model_name, version_id, data_engine)：
  1. 序列化 DataEngineV1 状态（KDE 参数、训练统计、带宽矩阵）
  2. 写入 Parquet 文件：`<storage_root>/models/<model_name>/<version_id>.parquet`
  3. 更新版本索引文件
- 检查点包含：KDE 参数 + 训练数据统计 + Schema 信息
- 检查点大小预估：20 维 KDE 约 10MB

**验收**：
- [ ] 检查点文件正确写入
- [ ] 版本索引更新
- [ ] 同一版本重复保存覆盖（幂等）

### Step 2.2：流式加载

**做什么**：实现 load_model 接口

**关键逻辑**：
- load_model(model_name, version_id)：
  1. 从版本索引查找文件路径
  2. 读取 Parquet 文件
  3. 反序列化为 DataEngineV1
  4. 验证版本一致性（fingerprint 校验）
- 大模型优化：分批读取 KDE 训练统计（避免一次性加载 >1GB）
- 缓存策略：最近加载的模型保留在内存中（LRU，默认缓存 5 个）

**验收**：
- [ ] 加载后 DataEngineV1 状态与保存时一致
- [ ] 大模型（>100MB）流式加载不 OOM
- [ ] 版本不存在返回 kVersionNotFound
- [ ] 文件损坏返回 kDataCorruption

### Step 2.3：atomic_write 事务

**做什么**：实现两阶段提交的原子写入

**关键逻辑**：
- atomic_write(model_name, data_engine, version)：
  1. **阶段一：写数据** → 临时文件 `<version_id>.tmp`
  2. **阶段二：写元数据** → 版本链注册（调用 ModelVersionChain::create_version）
  3. **阶段三：提交审计** → 审计日志记录 atomic_write 事件
  4. 重命名临时文件为正式文件
- 中断恢复策略：
  - 阶段一中断：临时文件残留，下次启动清理
  - 阶段二中断：数据文件存在但无版本注册，清理
  - 阶段三中断：数据和元数据都存在，补写审计日志
- 判定标准：以元数据层（版本链注册）为权威源

**验收**：
- [ ] 正常流程三阶段全部完成
- [ ] 阶段一中断后恢复正确
- [ ] 阶段二中断后恢复正确
- [ ] 阶段三中断后恢复正确
- [ ] 并发写入同一模型不冲突

### Step 2.4：版本索引

**做什么**：实现 list_model_versions 接口

**关键逻辑**：
- 维护版本索引：model_name → [version_id, ...]
- 索引存储：`<storage_root>/models/<model_name>/index.json`
- 每次创建版本时更新索引

**验收**：
- [ ] 列出所有版本
- [ ] 新版本创建后索引更新
- [ ] 索引不存在时返回空列表

### Step 2.5：存储模型层测试

**做什么**：编写存储模型层单元测试

**产出**：`tests/unit/model_storage_test.cpp`

**测试用例**（至少 10 个）：
- save_checkpoint 正确
- load_model 正确
- save + load 一致性
- atomic_write 三阶段完整
- 阶段一中断恢复
- 阶段二中断恢复
- 阶段三中断恢复
- 版本索引正确
- 大模型流式加载
- 文件损坏处理

**验收**：10+ 测试通过

---

## Task 3：偏差报告（#24）

**目标**：实现 CompactionBiasReport 完整字段

### Step 3.1：CompactionBiasReport 实现

**做什么**：实现偏差报告结构体和填充逻辑

**产出**：`src/storage/gc/compaction_bias_report.h`, `src/storage/gc/compaction_bias_report.cpp`

**关键逻辑**：
- 字段：
  - requested_version：用户请求的版本
  - returned_version：实际返回的版本
  - reason：compacted / anchored / snapshot_referenced
  - merged_from：被合并的版本列表
  - training_data_range：训练数据范围
  - fidelity_score_range_min/max：保真度评分范围
  - version_mismatch：版本不匹配标记
- 填充时机：TimeTravelEngine 检测到退化时生成
- 证明链可重建：merged_from → 请求版本 → 返回版本的完整路径

**验收**：
- [ ] 偏差报告字段完整
- [ ] 证明链可重建（从 merged_from 可以追溯）
- [ ] version_mismatch 在请求≠返回时标记

### Step 3.2：偏差报告测试

**测试用例**（至少 5 个）：
- 偏差报告字段完整
- 证明链可重建
- version_mismatch 标记正确
- fidelity_score_range 正确（min/max）
- 无退化时偏差报告为空

**验收**：5+ 测试通过

---

## Task 4：脚手架集成

**目标**：为存储模型层和偏差报告添加 Trace/Explain/Metrics

### Step 4.1：Trace span 集成

```cpp
// atomic_write 三阶段 span
SpanGuard span_data("model_storage", "atomic_write_data", trace_id_);
SpanGuard span_meta("model_storage", "atomic_write_meta", trace_id_);
SpanGuard span_audit("model_storage", "atomic_write_audit", trace_id_);
```

**验收**：atomic_write 产生 3 个子 span

### Step 4.2：Metrics 注册

```
model_storage_checkpoint_size_bytes  — 检查点文件大小
model_storage_load_latency_ms        — 加载延迟
model_storage_atomic_write_ms        — 原子写入延迟
model_cache_hit_rate                 — 模型缓存命中率
```

**验收**：metrics 端点暴露上述指标

---

## Task 5：集成测试

**目标**：验证三个组件的端到端集成

### Step 5.1：集成测试

**产出**：`tests/integration/v3_enhancement_integration_test.cpp`

**测试用例**（至少 8 个）：
- 完整流程：创建版本 → compact → 时间旅行 → tail_report 增强版
- atomic_write + 版本链联动
- 检查点存储 + 流式加载一致性
- 偏差报告 + 时间旅行退化联动
- tail_report 的 fidelity_mismatch 联动
- 审计日志记录 atomic_write 事件
- Trace span 完整性
- 大模型端到端性能（>50MB 检查点）

**验收**：8+ 集成测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: tail_report增强 | 3 | 0.5w | ⬜ |
| Task 2: 存储模型层 | 5 | 1w | ⬜ |
| Task 3: 偏差报告 | 2 | 0.25w | ⬜ |
| Task 4: 脚手架 | 2 | 0.25w | ⬜ |
| Task 5: 集成测试 | 1 | 0.5w | ⬜ |
| **合计** | **13** | **2.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| atomic_write 阶段二中断导致孤立数据文件 | 启动时扫描临时文件，无版本注册的文件自动清理 |
| 大模型检查点 >1GB 时加载缓慢 | 流式加载 + LRU 缓存（默认 5 个模型） |
| tail_report v3 字段与 v2 不兼容 | v3 字段使用独立的 TailReportV3 结构体，v2 保持不变 |
| 偏差报告的证明链在多层 compaction 后过长 | 证明链只保留最近一层，历史层通过 merged_from 间接追溯 |
