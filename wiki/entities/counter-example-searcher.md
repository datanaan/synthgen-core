# counter-example-searcher

CounterExampleSearcher 是 v4 Unit X 交付的反例搜索引擎（research 里程碑），探索约束系统的"反向验证"能力——寻找约束未覆盖的区域或约束本身存在矛盾的实例。三种状态：available（找到违反区域）、deferred（前置条件未就绪，受 [COORDINATE] C5/C6 制约）、research_failed（算法不收敛或不可行）。标注为研究性功能，可能因待测模型接入协议（C5）或理论基础（C6）未解决而标记 deferred。搜索超时返回 kSearchTimeout，不收敛返回 kSearchNotConverged。

## 相关文档

- [[source-v4-unit-x-spec]] — Unit X 设计规范
- [[source-v4-unit-x-plan]] — Unit X 实施计划
