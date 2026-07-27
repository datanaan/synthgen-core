SynthGen Core v2 脚手架设计规范
文档性质：脚手架级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：v2 脚手架实施计划
组件：v2 脚手架 5 项增强
估算：1 周
依赖：v2 功能组件

---

## 一、本 Unit 交付什么

v2 脚手架在 v1 最小版基础上增加 5 项增强，与 v2 功能组件同版本交付。

| 脚手架 | v2 交付内容 | 估算 |
|--------|-----------|------|
| Explain 增强 | 排除率预估 + 体积比 + 数据来源 + 退化路径选择 | 0.5w |
| Trace 增强 | 后筛选路径实时排除率变化记录 | 0.5w |
| 可观测性增强 | 排除率趋势 + 退化路径命中率 + 审计验证状态 | 0.5w |
| 错误注入 v2 | 后筛选排除率爆炸 + 数据引擎故障 | 0.5w |
| 测试增强 | 5 条退化路径各一个回归测试 | 0.5w |

---

## 二、Explain 增强

### 2.1 v2 Explain 新增字段

```cpp
struct ExplainInfoV2 : public ExplainInfoV1 {
    // v2 新增
    double exclusion_rate_estimate;          // 排除率预估
    double volume_ratio;                     // 体积比
    std::string data_source;                 // 数据来源（模型版本）
    DegradationPath selected_path;           // 选中的退化路径
    std::vector<DegradationPath> available_paths;  // 可用路径列表
    std::string selection_reason;             // 选择理由
    bool data_engine_available;               // 数据引擎可用性
};
```

### 2.2 验收标准

- [ ] `client.explain()` 返回 exclusion_rate_estimate + volume_ratio + data_source + degradation_path
- [ ] 各组件 explain() 方法输出 v2 字段

---

## 三、Trace 增强

### 3.1 v2 Trace 新增 span 属性

```cpp
// 后筛选 span 新增属性
span.set_attribute("pre_filter_rows", pre_filter_rows);
span.set_attribute("post_filter_rows", post_filter_rows);
span.set_attribute("exclusion_rate", exclusion_rate);
span.set_attribute("exclusion_rate_band", band_name);
span.set_attribute("was_timeout_truncated", was_timeout);
// 实时排除率子 span
for (const auto& rate : realtime_rates) {
    SpanGuard sub_span("post_filter", "exclusion_rate_check", trace_id);
    sub_span.set_attribute("checked_rows", i);
    sub_span.set_attribute("current_rate", rate);
}
```

### 3.2 验收标准

- [ ] EvidencePackage.provenance.trace_spans 含后筛选排除率变化 span
- [ ] 行间引擎 span 含 batch 间状态传递信息
- [ ] 聚合引擎 span 含两阶段分别的 span

---

## 四、可观测性增强

### 4.1 v2 新增 Metrics

```
# 排除率趋势
post_filter_exclusion_rate         — 后筛选排除率
post_filter_exclusion_rate_band   — 排除率分级

# 退化路径命中率
router_path_selected{path="full_function"}   — 全功能路径命中
router_path_selected{path="post_filter"}      — 后筛选路径命中
router_path_selected{path="pure_physics"}     — 纯物理路径命中
router_path_selected{path="statistical"}      — 统计生成路径命中
router_path_selected{path="kde_perturbation"} — KDE 扰动路径命中

# 审计验证状态
audit_chain_verification_status   — 哈希链验证状态
audit_records_total               — 审计记录总数
audit_forks_detected              — 分叉检测数
```

### 4.2 验收标准

- [ ] `/metrics` 新增排除率趋势和退化路径命中率指标
- [ ] `/metrics` 新增审计验证状态指标

---

## 五、错误注入 v2

### 5.1 新增注入场景

| 场景 | 注入方式 | 预期行为 |
|------|---------|---------|
| 后筛选排除率爆炸 | 注入极高排除率约束 | 排除率 >90% 时拒绝后筛选 |
| 数据引擎不可用 | mock data_engine = nullptr | 路由器退化到纯物理路径 |
| 数据引擎 fit 失败 | mock fit() 返回错误 | 路由器退化到纯物理路径 |
| 审计日志写入失败 | mock storage 写入失败 | 审计记录丢失但不阻塞生成 |
| 哈希链断裂 | 手动修改一条审计记录 | daily_verification 返回 false |

### 5.2 验收标准

- [ ] 注入排除率 >90% 时系统正确拒绝后筛选
- [ ] 注入数据引擎不可用时路由器正确退化
- [ ] 注入审计写入失败时系统不阻塞

---

## 六、测试增强

### 6.1 退化路径回归测试

5 条退化路径各至少 1 个端到端回归测试用例：

| 路径 | 测试场景 | 验收 |
|------|---------|------|
| kFullFunction | 约束完备 + 数据引擎可用 | 输出满足所有约束 + 身份正确 |
| kPostFilter | 排除率 50% | 后筛选正确 + 排除率合理 |
| kPurePhysics | 仅值域约束 | 输出与 v1 等价 |
| kStatisticalGeneration | 约束不完备 + 数据引擎 | 输出分布接近训练数据 |
| kKDEPerturbation | 约束极度不完备 + 数据引擎 | 输出保持基础分布形态 |

### 6.2 验收标准

- [ ] 5 条退化路径各至少 1 个回归测试用例
- [ ] 每个回归测试包含 EvidencePackage 验证
