# v3 工具线 Spec — 4 项增强

> 来源：docs/superpowers/v3/specs/2026-05-10-synthgen-v3-tool-design.md
> 编译日期：2026-05-14

## 摘要

v3 工具线交付 4 项增强，估算 0.5 周。组件模板引擎从 v0.2 升级到 v0.3，增加 compaction/版本链组件模板，能生成 #18-#24 任一组件骨架。测试辅助库 v0.3 新增 ASSERT_COMPACTION_CONSISTENCY 宏，用于 compaction 前后一致性测试。Schema 校验器 v1.1 新增版本链字段校验规则。Trace 分析工具 v0.2 新增 compaction 冲突规则，能标红 compaction 退化 Trace。

## 关键要点

- 组件模板引擎 v0.3：增加 compaction/版本链模板，生成 #18-#24 骨架
- 测试辅助库 v0.3：ASSERT_COMPACTION_CONSISTENCY 宏
- Schema 校验器 v1.1：版本链字段校验规则
- Trace 分析工具 v0.2：compaction 冲突 span 检测规则

## 提取的实体

- [[tool-line]] — 已有实体，v3 新增 4 项工具增强
