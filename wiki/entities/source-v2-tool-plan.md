# v2 工具线 Plan — 开发辅助工具增强

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-tool-plan.md
> 编译日期：2026-05-14

## 摘要

v2 工具线实施计划分 4 个 Task，估算 1.5 周。Task 1 组件模板引擎 v0.2（#10-#17 新组件模板），Task 2 测试辅助库 v0.2（4 个新宏），Task 3 Schema 校验器 v1.0（接口注册机制 + Python 校验器脚本），Task 4 Trace 分析工具 v0.1（4 条规则引擎实现）。

## 关键要点

- Task 1：`tools/scaffold_templates/` 目录下新增 v2 组件模板
- Task 2：`src/scaffold/test_helpers.h` 扩展，4 个新宏实现
- Task 3：`tools/schema_checker/validate.py`，编译后自动运行，CI 集成
- Task 4：`tools/trace_analyzer/analyze.py`，4 条规则 + JSON 报告
- [COORDINATE] C7：Python 工具链，Schema 校验器可回退到 C++ 静态断言

## 提取的实体

- [[tool-line]] — 工具线实施
- [[schema-validator]] — Schema 校验器实现
- [[trace-analyzer]] — Trace 分析工具实现
