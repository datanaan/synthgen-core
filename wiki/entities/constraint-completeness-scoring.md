# constraint-completeness-scoring

完备度连续化评分是 v4 的核心诚实性机制，将约束覆盖程度从布尔判断转化为 0.0-1.0 的连续评分。5 个维度（值域约束 0.3、行间约束 0.2、聚合约束 0.2、统计签名 0.2、物理合法性 0.1）加权汇总，反映约束系统对数据空间的真实覆盖能力。score == 1.0 是特例而非默认。完备度评分影响执行路由器的路径选择和身份声明。

## 相关文档

- [[source-v4-unit-w-spec]] — Unit W 设计规范
- [[source-v4-unit-w-plan]] — Unit W 实施计划
- [[source-v4-scaffold-spec]] — 脚手架 Explain/Trace 增强
