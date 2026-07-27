# Trace 分析工具 (Trace Analyzer)

> 类型：工具
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

Trace 分析工具是 v2 工具线的开发辅助工具，通过规则引擎对 EvidencePackage 中的 Trace span 进行异常检测和质量分析。

## 详情

Trace 分析工具基于 4 条核心规则检测 Trace 中的异常：

| 规则 | 条件 | 标记 |
|------|------|------|
| R1 | span.status == "error" | 标红（执行错误） |
| R2 | span.duration > P99_threshold | 标黄（性能异常） |
| R3 | exclusion_rate 连续 3 个 span 上升 | 标红（排除率飙升） |
| R4 | span.path != expected_path | 标黄（退化路径意外命中） |

输出格式：
- 终端高亮显示（红色/黄色标记异常 span）
- JSON 结构化报告（机器可读）

输入：EvidencePackage 中的 trace 字段（JSON 格式）

实现语言：Python（独立工具，非产品代码）

## v2 范围

v2 工具线 Task 4 实现 Trace 分析工具 v0.1：
- `tools/trace_analyzer/analyze.py`
- 4 条规则引擎实现
- 终端高亮 + JSON 报告输出
- 至少 4 个分析工具测试

## 关联实体

- [[tool-line]] — 工具线组件之一
- [[scaffolding]] — Trace 是脚手架的核心能力
- [[exclusion-rate]] — R3 规则检测排除率飙升
- [[degradation-path]] — R4 规则检测退化路径意外命中
- [[evidence-package]] — Trace 数据来源

## 来源

- [[source-v2-tool-spec]] — 五、Trace 分析工具 v0.1
- [[source-v2-tool-plan]] — Task 4：Trace 分析工具 v0.1
