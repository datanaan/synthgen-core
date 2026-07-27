SynthGen Core v3 工具线设计规范
文档性质：工具线级设计规范
版本：v1.0
日期：2026-05-10
估算：0.5 周

---

## v3 工具线 4 项增强

| 工具 | v3 交付 | 验收标准 |
|------|---------|---------|
| 组件模板引擎 v0.3 | 增加 compaction/版本链组件模板 | 生成 #18-#24 骨架 |
| 测试辅助库 v0.3 | compaction 前后一致性测试宏 | ASSERT_COMPACTION_CONSISTENCY 宏 |
| Schema 校验器 v1.1 | 版本链字段校验规则 | 校验版本链相关字段完整性 |
| Trace 分析工具 v0.2 | compaction 冲突规则 | 构造 compaction 退化 Trace 能标红 |
