# SynthGen Core 工程审查 — 守门员知识储备

**日期**：2026-05-09
**用途**：为工程设计文档的对抗性审查提供技术弹药

---

## 一、数据库内核架构（核心参考）

### 1.1 经典三组件架构
所有现代数据库内核都遵循：**优化器 → 运行时 → 存储引擎** 三层。

- **PostgreSQL**：Parser → Analyzer → Rewriter → Planner → Executor → Storage（堆表+索引+MVCC）
- **DuckDB**：Parser → Logical Planner → Physical Planner → Executor → Storage（列式向量化）
- **DB3 (2026)**：同样的三组件，但做了 ground-up 新实现，强调 kernel 组合性

**对SynthGen Core的审视点**：
- 工程文档的"接口层→计算引擎→存储引擎"映射是否精确？
- 计算引擎是否遗漏了优化器阶段？（当前只有"查询解析&计划编译"和"引擎能力矩阵查询"，没有代价模型和执行路径选择）
- 三大引擎Operator的接口是tuple流还是batch流？这直接影响向量化执行的可能

### 1.2 BOSS — 数据库内核组合架构 (VLDB 2024)
**核心洞察**：数据库内核可以通过可组合的计算kernel构建，而非单一整体架构。

关键设计：
- **模块化kernel**：将操作符拆解为细粒度计算kernel
- **管道式组合**：kernel间通过数据管道串联
- **嵌套组合**：支持kernel的层次化嵌套
- **性能问题**：组合式架构相对手写专用代码的性能差距（虚函数调用、数据移动、中间物化）

**对SynthGen Core的审视点**：
- 三大引擎的Operator组合方式是什么？BOSS证明了组合式有性能开销
- 如果物理引擎输出batch → 约束引擎逐行检查，这里有巨大的性能陷阱
- 后筛选模式的"全量生成→逐行过滤"在batch size大时的内存压力

### 1.3 MVCC与版本链
四种版本存储方式 (VLDB 2017 综述)：

| 方式 | 写入性能 | 读取性能 | 存储 | 代表 |
|------|---------|---------|------|------|
| Append-Only | 差（旧版本不删除） | 中（需遍历链） | 高 | PostgreSQL |
| Time-Travel | 好（主表不存旧版本） | 好（旧版本在独立空间） | 高 | |
| Delta Storage | 最好（仅存变化字段） | 差（需重构） | 低 | |
| In-Place + Undo Log | 最好 | 最好（总是最新） | 低 | MySQL |

**对SynthGen Core的审视点**：
- 存储分层的"版本时间线"声称用MVCC，但没有声明用哪种版本存储
- 快照层用Parquet列存，那模型层呢？参数存什么格式？
- 时间旅行的读性能取决于版本链设计——如果每次对齐产生新版本，链会很长

---

## 二、类SQL接口的工程现实

### 2.1 PostgreSQL Custom Scan Provider
PostgreSQL的扩展机制允许注册Custom Scan来替换默认执行路径。

**实际限制**（文档明确标注为"实验性功能"）：
- 必须在planning阶段生成access path
- 必须在execution阶段提供CustomScanState
- **无法添加新的SQL语法**——只能替换现有语法的执行路径
- 如果要加GENERATE关键字，需要修改Parser（patch PostgreSQL源码），不是extension能做的

**对SynthGen Core的审视点**：
- **"扩展语法SQL接口"在PostgreSQL上需要fork源码**，不是插件
- 在DuckDB上同样需要修改parser
- 如果不想fork，替代方案：用UDF（如 `SELECT synthgen_generate('sensor_data', 1000, ...)`）——但UDF无法参与优化器
- 这是工程文档最大的架构风险：声称复用数据库生态，但核心语法需要修改数据库本身

### 2.2 CHECK约束的性能现实
数据库CHECK约束在逐行插入时触发。**批量插入时的性能损失可达30-50%** (MSSQL Tips, 2023)。

**后筛选模式的致命问题**：
- 工程文档的"后筛选"= 用WHERE子句过滤 = 数据库全表扫描
- 如果生成1M行，90%被过滤（排除率阈值），实际产出100K行
- 但数据库已经为1M行分配了内存/存储
- **这不是"后筛选"，是"拒绝采样"，效率极低**

Oracle/IBM的解决方案：批量操作时临时禁用约束，事后批量验证。
→ SynthGen Core的后筛选是否需要类似的"批量约束检查"模式？

### 2.3 Generated Column（生成列）
PostgreSQL 12+ / MySQL 5.7+ 支持生成列：
- `STORED`：写入时计算并存储（占空间，读取快）
- `VIRTUAL`：查询时计算（不占空间，读取慢，PG 18新增）

**对SynthGen Core的审视点**：
- EvidencePackage中的metadata（statistical_fidelity, tail_report等）本质上是生成列
- 如果每次查询都重新计算tail_report → VIRTUAL模式 → 慢
- 如果生成时计算并存储 → STORED模式 → 存储膨胀
- 没有讨论这个trade-off

---

## 三、SDV — 合成数据生成的工业参考

### 3.1 SDV元数据系统
三层类层次：`SingleTableMetadata → MultiTableMetadata → Metadata`

关键设计：
- **SDType语义类型**：5大类（基础/ID/PII/地理/未知），每类独立验证器
- **列检测管线**：原始数据 → 列级推断 → SDType检测 → 键检测 → 验证确认
- **约束与Schema分离**：元数据负责Schema级约束，CAG（Constraint-Augmented Generation）负责业务级约束
- **双层验证**：单表验证（类型/键/关系）+ 多表验证（跨表引用完整性）

**对SynthGen Core的审视点**：
- SynthGen Core的"约束卡片"是单一概念，SDV已经分离为Schema约束和业务约束
- 物理约束（风速∈[0,25]）是业务约束，格式约束（温度是float）是Schema约束
- 把两者混在CHECK约束里，会导致约束卡片膨胀且难以维护
- **建议约束卡片分层**：格式约束（DDL自动生成）+ 物理约束（领域专家编写）

### 3.2 SDV约束增强生成（CAG）
管线本质：元数据提供Schema上下文 → 约束系统叠加业务规则 → 合成器同时满足两者

SDV支持的约束类型：
- 固定组合（FixedCombinations）
- 不等式（Inequality）
- 范围（Range）
- 唯一性（Unique）
- 自定义Python函数

**对SynthGen Core的审视点**：
- 工程文档的CHECK约束只能表达简单范围和不等式
- 复杂约束（如"当温度>30时湿度必须<80%"）需要条件约束，SQL CHECK能写但很丑
- 跨行约束（如"时间序列的单调性"）SQL CHECK完全无法表达
- 约束卡片的表达力上限决定了物理驱动的上限

---

## 四、存储引擎的关键设计决策

### 4.1 不可变存储 vs Lakehouse时间旅行
Delta Lake / Apache Iceberg 的时间旅行实现：
- **Delta Lake**：`_delta_log` 事务日志（JSON+Parquet），每次操作追加一条commit
- **Iceberg**：Manifest文件树 + Snapshot层，每次写入产生新snapshot
- 共同点：**逻辑不可变**（旧版本标记为不可见但物理文件保留），定期compaction合并

**对SynthGen Core的审视点**：
- 工程文档声称"物理删除不存在"，但没讨论compaction/GC
- 如果每次对齐产生新模型版本，旧版本永远保留 → 存储无限增长
- 需要定义"版本保留策略"——与理论框架第八节的记忆策略联动
- Lakehouse的snapshot隔离（MVCC）是否适用于模型版本？模型不是数据行

### 4.2 WAL与审计
WAL的核心保证：**数据页的修改必须在日志写入后才写入磁盘**（先写日志原则）

**对SynthGen Core的审视点**：
- 审计日志层声称"WAL/不可变追加"，但WAL不是审计日志
- WAL是崩溃恢复机制，不保证不可篡改（数据库启动后可以修改WAL）
- 真正的不可变审计需要：append-only文件 + WORM存储 + 数字签名
- 工程文档混淆了"崩溃恢复的WAL"和"审计不可变的日志"

---

## 五、流式与代偿机制

### 5.1 Exactly-Once语义
流处理的三种语义：At-Most-Once / At-Least-Once / Exactly-Once

**对SynthGen Core的审视点**：
- 持续对齐（5.1节）本质上是一个流处理问题：新数据持续输入 → 增量更新模型
- 如果更新过程中崩溃，重试可能导致同一批数据被训练两次 → 模型偏差
- 需要：幂等性保证（每批数据有唯一ID，重复训练检测）或事务性保证
- 工程文档没有讨论这个

### 5.2 背压（Backpressure）
流处理中，当消费速度<生产速度时，需要背压机制。

**对SynthGen Core的审视点**：
- 如果新数据到达速度 > 模型对齐速度 → 系统应该怎么做？
- 理论框架说"降频为批量更新"，但没有定义触发条件和降频策略
- 缺少队列/缓冲区设计

---

## 六、模型服务与版本管理

### 6.1 Triton推理服务器架构
- 模型仓库（文件系统目录结构）
- 多版本共存：版本号子目录
- 动态批处理：自动合并请求
- Ensemble模型：多模型流水线编排

**对SynthGen Core的审视点**：
- 模型层的"版本链"在工程上如何实现？
- 代偿机制的双通道生成 = 同时服务两个模型版本 = 类似A/B部署
- 代偿模型的训练和核心模型的生成可能争抢GPU → 资源隔离问题
- 工程文档没有讨论模型服务的资源管理

---

## 七、守门员审查清单（待用）

基于以上知识，对工程文档v0.2的重点攻击方向：

### 🔴 致命级
1. **GENERATE语法需要修改数据库源码**：声称复用生态但核心功能需要fork，这是自相矛盾
2. **后筛选=拒绝采样**：效率灾难，排除率>90%时系统退化为随机噪声筛选器（理论v1.2已识别但工程没解决）
3. **WAL≠审计日志**：混淆了崩溃恢复和审计不可变

### 🟠 高优先级
4. **缺少优化器阶段**：只有"编译"没有"代价估计"和"路径选择"
5. **约束卡片表达力上限**：SQL CHECK无法表达跨行约束和条件约束
6. **模型版本的存储增长**：没有compaction/GC策略
7. **持续对齐的Exactly-Once问题**：崩溃恢复时的幂等性
8. **约束检查的批量模式**：逐行CHECK的性能陷阱

### 🟡 中优先级
9. **生成列的VIRTUAL vs STORED**：metadata计算时机未定
10. **背压与缓冲区**：新数据到达速度vs对齐速度
11. **模型服务资源隔离**：代偿模型和核心模型争抢资源
12. **版本保留策略**：与理论框架记忆策略的联动
