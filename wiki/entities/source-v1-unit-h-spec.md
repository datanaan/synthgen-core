# v1 Unit H Spec — Scaffold v1

> 来源：raw/specs/v1-unit-h-design.md
> 编译日期：2026-05-14

## 摘要

Unit H 实现 v1 五项脚手架设施：Explain 最小版、Trace 最小版、可观测性最小版、确定性测试框架、CI/CD 基础设施。脚手架与功能组件享有同等地位——脚手架不过 = 版本不交付。估算 1.5 周，与 Unit C-G 并行。

## 关键要点

- Explain 最小版：返回 execution_mode + path + constraint_classification，每个组件提供 explain() const 方法
- Trace 最小版：span 结构 + trace_id，SpanGuard RAII 自动创建/写入
- 可观测性最小版：/metrics 端点（吞吐量 + 延迟 + 内存），MetricsRegistry 计数器/直方图
- 确定性测试框架：seed 固定 + 参考快照 + Schema 验证
- CI/CD：每次 PR 触发测试，含内存检查（ASan/UBSan）
- Explain 输出结构一旦定义即成隐式 API，不可随意改

## 提取的实体

- [[scaffolding]] — 六类脚手架设施
- [[span-guard]] — Trace span RAII 守卫
- [[metrics-registry]] — 可观测性指标注册器
- [[exclusion-rate]] — 排除率（Explain 返回 estimated_exclusion_rate）
