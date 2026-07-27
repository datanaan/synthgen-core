# 开发路线图 v1.4

> 来源：docs/core/SynthGen Core 开发路线图 v1.4.md
> 编译日期：2026-05-11

## 摘要

定义 v1-v4 四个版本的 30 个功能组件、14 个脚手架组件、6 个工具组件，采用能力里程碑方法论（自行车→摩托车→轿车→跑车），三线并行（明线功能/暗线脚手架/工具线开发辅助）。总计 29-33 周，4-6 人团队。

## 关键要点

- **v1 最小可运行**（8.5-9.5周）：Schema→导入→纯物理矩形域采样→值域验证→EvidencePackage→SDK/REST
- **v2 约束完整**（12-13周）：三类约束+数据引擎KDE+执行路由器重构(5退化路径)+后筛选+审计+DURING/WHEN
- **v3 时间智能**（6-7周）：模型版本链+GC compaction+时间旅行AS OF+持续对齐UPDATE MODEL+tail_report增强
- **v4 高级分析**（4-5周）：行数/分组/会话窗口+完备度评分0.0-1.0+反例搜索(research)
- **诚实声明贯穿**：v1 起即声明物理优先偏差，不适用的 EvidencePackage 字段标记 not_applicable
- **脚手架与功能同等地位**：脚手架不过=版本不交付

## 提取的实体

- [[synthgen-core]] — 项目整体
- [[scaffolding]] — 六类脚手架设施
- [[tool-line]] — 四个开发辅助工具
- [[evidence-package]] — EvidencePackage v1→v2→v3 演进
- [[physics-engine]] — v1 矩形域采样
- [[data-engine]] — v2 KDE 数据引擎
- [[model-version-chain]] — v3 版本链+GC
