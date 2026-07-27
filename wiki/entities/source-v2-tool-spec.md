# v2 工具线 Spec — 开发辅助工具增强

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-tool-design.md
> 编译日期：2026-05-14

## 摘要

v2 工具线在 v1 基础上新增/增强 4 项工具。组件模板引擎 v0.2：增加 v2 新组件（行间/聚合/分类器/路由器/数据引擎/审计日志）模板。测试辅助库 v0.2：新增退化路径参数化测试宏（TEST_DEGRADATION_PATH 等）。Schema 校验器 v1.0：编译期三方 diff（代码接口注册 vs EvidencePackage Schema vs 理论框架承诺清单），Python 实现。Trace 分析工具 v0.1：4 条规则引擎（error 标红、duration 超阈值标黄、排除率飙升标红、退化路径意外标黄）。含 [COORDINATE] Python 工具链团队接受度。

## 关键要点

- 组件模板引擎 v0.2：#10-#17 新组件模板，inja(C++) 或 Jinja2(Python)
- 测试辅助库 v0.2：TEST_DEGRADATION_PATH、ASSERT_EXCLUSION_RATE_BAND、ASSERT_AUDIT_CHAIN_VALID、ASSERT_KDE_DISTRIBUTION_CLOSE 四个新宏
- Schema 校验器 v1.0：三方 diff（接口注册.json vs Schema 定义 vs 理论承诺），4 条校验规则
- Trace 分析工具 v0.1：4 条规则引擎（R1 error/R2 duration/R3 排除率飙升/R4 路径意外），终端高亮+JSON 报告
- [COORDINATE] C7：Python 工具链接受度，Schema 校验器可回退到 C++ 编译期静态断言

## 提取的实体

- [[tool-line]] — 工具线 4 项工具
- [[schema-validator]] — Schema 校验器 v1.0
- [[trace-analyzer]] — Trace 分析工具 v0.1
- [[degradation-path]] — 测试辅助库退化路径宏
