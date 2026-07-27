SynthGen Core v4 工具线设计规范：Trace 分析 v0.3
文档性质：工具线级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v4 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：v4 工具线实施计划
组件：v4 开发辅助工具线
估算：0 周（工具维护，非新增工具）
标注：[IMPLEMENT]

---

## 一、本 Unit 交付什么

v4 工具线无新增工具。Trace 分析工具从 v0.2 升级到 v0.3，新增反例搜索轨迹分析规则。

交付物：
1. **Trace 分析 v0.3**：新增反例搜索 span 的分析规则

---

## 二、Trace 分析 v0.3 增量

### 2.1 新增分析规则

| 规则 | 触发条件 | 输出 |
|------|---------|------|
| counter_example_status_check | search.status != "available" | 标注反例搜索未成功 |
| completeness_score_drop | score 从 >0.8 降到 <0.5 | 标注完备度显著下降 |
| search_iteration_exceeded | iterations > 500 | 标注搜索迭代过多 |
| model_provenance_missing | model_provenance 字段缺失 | 标注模型溯源缺失 |

### 2.2 诚实边界

- ✅ 能做：基于规则扫描 Trace span，标注异常
- ❌ 做不了：理解约束语义，判断反例搜索是否合理

---

## 三、验收标准

- [ ] 4 条新增规则正确触发
- [ ] 不误报（正常 span 不触发异常标注）
- [ ] 与 v0.2 已有规则兼容

### 测试验收

- [ ] 至少 4 个测试用例（每规则一个）
