SynthGen Core v3 脚手架设计规范
文档性质：脚手架级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v3 阶段设计规范 v1.0
估算：1 周

---

## v3 脚手架 5 项增强

| 脚手架 | v3 交付 | 验收标准 |
|--------|---------|---------|
| Explain 增强 | compaction 影响预估 | explain() 显示退化版本和偏差报告 |
| Trace 增强 | 持续对齐模型更新前后变化 | 模型更新 span 含 drift_score + compensation_status |
| 可观测性增强 | 版本链状态 + GC 历史 | /metrics 新增 model_versions_count + gc_compaction_history |
| 错误注入增强 | compaction 冲突场景 | 注入 compaction 冲突后系统正确处理 |
| 测试增强 | compaction 前后一致性 | compaction 后生成结果与直接生成结果一致 |
