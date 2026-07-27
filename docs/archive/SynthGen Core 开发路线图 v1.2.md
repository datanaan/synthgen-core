SynthGen Core 开发路线图 v1.2
文档版本：v1.2
上一版本：v1.1
修订性质：回应第二轮对抗性审查（6项攻击，全部接受）
日期：2026-05-10

方法：能力里程碑——按"系统能做什么"划分版本，每个版本交付可演示的产品能力，内部含跨层技术拆解。
交付粒度：按功能里程碑，时间仅作估算。
与理论框架对齐：v1.3 约束分层（值域/行间/聚合）→ 路线图版本递进完全对齐。

---

## v1.1 → v1.2 修订记录

| # | 审查攻击 | 修订内容 |
|---|---------|---------|
| 1 | 物理引擎v1工作量被低估 | 物理引擎v1限定为仅矩形约束域（BETWEEN），复合约束域+拒绝采样/MCMC留v2；估算从1周调整为1.5周 |
| 2 | 审计不可变延迟到v2 | v1声明无审计不可变保证；EvidencePackage v1新增audit_immutability: not_applicable；v1诚实声明中新增此限制 |
| 3 | v3 tail_report v2与v2误差界重叠 | v3 #22修正述为"tail_report增强版"——将v2已有的误差界数据增强呈现到tail_report中，而非新增计算 |
| 4 | v2后筛选缺数据引擎 | v2新增组件#15b"数据引擎v1(KDE简化版)"：核密度估计学习训练数据分布，支持后筛选排除率预估；估算3周 |
| 5 | 反例搜索依赖不完整 | v4 #29增加依赖"待测模型接入协议（需在v3阶段定义）" |
| 6 | 持续对齐缺数据引擎依赖 | v3 #21依赖图补充v2数据引擎依赖 |

---

## 诚实声明

本路线图基于理论框架 v1.3 和工程框架 v0.4。以下理论承诺在路线图的每个版本中都必须传递，不可因"简化"而省略：

**1. 认识论偏差声明**

物理优先策略导致生成数据世界的风险谱比真实物理世界更窄。极端工况和尾部事件被系统性排除。这是理论选择的结果，不是功能缺陷。从 v1 的第一天起，每次生成都必须声明此偏差。用户将在 EvidencePackage 的 conservative_tail_report 中看到完整的偏差报告。

**2. 条件保证**

data_grade 不是"好/坏"的标签，而是有条件的保证声明：
- physics_guaranteed：物理合法性无条件保证
- statistics_guaranteed：统计签名保证有条件（依赖数据驱动参与度）
- limited_fidelity：条件降级，保证范围收窄

v1 仅提供 physics_guaranteed。统计签名的条件保证从 v2 起随数据引擎参与而生效。

**3. 身份切换**

执行路由器根据约束完备性选择生成路径，每条路径的身份不同：
- 全功能/后筛选：约束驱动的合成生成器
- 纯物理：物理引擎采样器
- 统计生成：统计相似生成器
- KDE 扰动：格式化扰动生成器

身份不是"降级"，是不同认识论承诺的声明。v1 仅走纯物理路径，身份为"物理引擎采样器"。

**4. 审计不可变保证的阶段性声明**

- v1：审计日志无篡改检测。审计不可变保证从 v2 起生效。
- v2：哈希链审计日志就位，审计不可变保证生效。

v1 的 EvidencePackage 中 audit_immutability 字段标记为 not_applicable。

---

## 总览

| 版本 | 产品能力 | 核心新组件数 | 估算 |
|------|---------|------------|------|
| **v1** 最小可运行 | Schema定义→数据导入→纯物理采样(矩形约束域)→值域约束验证→证据包输出 | 9 | 7-8周 |
| **v2** 约束完整 | 三类约束全做+数据引擎v1(KDE)+执行路由器(5路径)+后筛选+审计日志 | 9 | 10-11周 |
| **v3** 时间智能 | 时间旅行+持续对齐+模型版本管理+tail_report增强 | 7 | 4-5周 |
| **v4** 高级分析 | 多窗口+完备度评分+反例研究 | 6 | 4-5周 |

**总计**：31个组件，25-29周

---

## v1: 最小可运行（7-8周）

**产品故事**：定义数据域，导入真实数据，在矩形约束域内物理采样，获得物理合法的合成数据和诚实的证据包。

**用户接口**：Python SDK + REST API。SynthLang 为内部编译目标，v1 不直接对用户暴露。

**生成路径**：纯物理采样（仅矩形约束域）。值域约束直接作为采样边界，物理引擎在矩形域内按基础分布（均匀/高斯）采样。**v1 不处理复合约束域**（如 DURING/WHEN 产生的非矩形约束空间），复合约束域在 v2 由物理引擎 v2 + 拒绝采样/MCMC 处理。

**v1 的已知限制**：
- v1 不具备数据驱动能力，EvidencePackage 中 statistical_fidelity 标记为 not_applicable
- v1 不具备漂移检测，drift_detection 标记为 not_applicable
- v1 不具备审计不可变保证，audit_immutability 标记为 not_applicable
- v1 的数据世界风险谱比真实物理世界更窄，尾部事件被值域约束系统性排除
- v1 的 tail_report 仅含值域约束的排除率和偏差声明
- v1 的物理引擎仅支持矩形约束域（BETWEEN/MIN/MAX），不支持 DURING/WHEN 产生的非矩形约束空间

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 1 | SynthLang Parser 核心语法 | DEFINE TYPE / LOAD DATA / DEFINE CONSTRAINT(仅值域BETWEEN) / GENERATE TABLE | 无 | 1.5周 |
| 2 | 类型系统 + Schema DDL | 数据类型、**ORDER声明(v1预留，v2使用)**、值域范围声明([min,max])、ENUM | #1 Parser | 1周 |
| 3 | 数据导入(LOAD DATA) | 读取Parquet，写入基表层，Schema校验 | #2 类型系统 + #4 存储 | 0.5周 |
| 4 | 基础存储引擎 | 对象存储+Parquet读写+自研元数据层v1(版本/Snapshot索引) | 无 | 1.5周 |
| 5 | 物理引擎 v1(矩形域采样) | **仅矩形约束域**：在BETWEEN/MIN/MAX定义的矩形空间内采样（均匀/高斯）+种子控制+批量生成。**不支持非矩形约束域** | #2 Schema | 1.5周 |
| 6 | 值域约束验证器 | 逐行验证采样结果是否在矩形值域内（纯物理路径下应100%通过，验证器作为安全网） | #5 物理引擎 | 0.5周 |
| 7 | tail_report v1 | 值域约束排除率+偏差声明（物理优先认识论偏差）+data_grade | #6 验证器 | 0.5周 |
| 8 | EvidencePackage 构建器 v1 | schema_hash/constraint_summary/exclusion_rate/data_grade/row_count/provenance基础/conservative_tail_report/audit_immutability(not_applicable)。**字段适用性标注** | #7 tail_report + #6 验证器 | 1周 |
| 9 | Python SDK + REST API | 客户端封装，SynthLang作为内部编译目标，用户用SDK调用 | 全部 | 1周 |

### EvidencePackage v1 字段适用性

| 字段 | 适用性 | v1 状态 | 说明 |
|------|--------|---------|------|
| schema_hash | always | ✅ 填充 | — |
| constraint_summary | always | ✅ 填充 | 仅值域约束（矩形域） |
| exclusion_rate | always | ✅ 填充 | 纯物理路径下应为 0% |
| data_grade | always | ✅ 填充 | physics_guaranteed |
| row_count | always | ✅ 填充 | — |
| provenance | always | ✅ 填充 | 基础版：数据源+约束+生成参数 |
| conservative_tail_report | always | ✅ 填充 | 值域偏差声明 |
| audit_immutability | always | ⬜ not_applicable | v1 无哈希链审计，审计不可变保证从 v2 起生效 |
| statistical_fidelity | data_engaged | ⬜ not_applicable | v1 无数据驱动 |
| drift_detection | drift_available | ⬜ not_applicable | v1 无持续对齐 |
| constraint_type_breakdown | aggregation_present | ⬜ not_applicable | v1 仅有值域 |

### 依赖链

```
#1 Parser → #2 类型系统(含ORDER预留) → Schema DDL
                    ↓
#4 基础存储 ← #3 数据导入 ←────────────┘
      ↓
#5 物理引擎v1(仅矩形域) → #6 值域验证器 → #7 tail_report v1
                                                   ↓
                                       #8 EvidencePackage v1
                                                   ↓
                                       #9 Python SDK + REST API
```

### 可并行开发

- #1 Parser + #4 存储：无依赖，可同时开工
- #8 EvidencePackage：接口先定义，与 #5/#6 并行开发
- #5 物理引擎：独立模块，可与 Parser/存储并行

### 演示场景

```python
# Python SDK（v1 用户接口）
from synthgen import SynthGenClient

client = SynthGenClient()

# 定义 Schema
schema = client.define_type("sensor_log", columns={
    "timestamp": Column(DATETIME, order=True),
    "temperature": Column(FLOAT, range=[-50.0, 80.0]),
    "pressure": Column(FLOAT, range=[900.0, 1100.0])
})

# 导入数据
client.load_data("sensor_log", "/data/sensors.parquet")

# 定义约束（v1 仅支持矩形约束域）
constraint = client.define_constraint("safe_range", "sensor_log", [
    RangeCheck("temperature", min=-10, max=45),
    RangeCheck("pressure", min=980, max=1040)
])

# 生成
result = client.generate("sensor_log", constraints=["safe_range"], limit=1000)

# 内部编译目标（SynthLang，用户不直接写）
# DEFINE TYPE sensor_log {
#     timestamp: DATETIME NOT NULL ORDER,
#     temperature: FLOAT [-50.0, 80.0],
#     pressure: FLOAT [900.0, 1100.0]
# };
# DEFINE CONSTRAINT safe_range ON sensor_log {
#     temperature BETWEEN -10 AND 45,
#     pressure BETWEEN 980 AND 1040
# };
# GENERATE TABLE gen_samples FROM sensor_log
# WITH CONSTRAINTS safe_range LIMIT 1000;
```

**输出**：1000行合成数据 + EvidencePackage（含 tail_report、偏差声明、字段适用性标注、audit_immutability: not_applicable）

### v1 验收标准

- [ ] SynthLang 核心语法解析无错误
- [ ] Parquet 文件导入成功，Schema 校验通过
- [ ] ORDER 声明在 Schema 中可定义（v1 不使用但预留）
- [ ] 物理引擎在**矩形**约束域内采样，值域验证器100%通过
- [ ] 物理引擎不支持非矩形约束域，传入DURING/WHEN约束时返回 unsupported_in_v1 错误
- [ ] EvidencePackage 含适用性标注，不适用的字段标记为 not_applicable
- [ ] audit_immutability 字段标记为 not_applicable
- [ ] tail_report 包含物理优先偏差声明
- [ ] Python SDK 可端到端调用
- [ ] 用户不直接接触 SynthLang，通过 SDK/REST API 交互

---

## v2: 约束完整（10-11周）

**产品故事**：行间依赖、窗口聚合、退化路径——完整的三类约束体系。数据引擎 v1(KDE) 就位，后筛选路径接入，执行路由器支持5条退化路径，每条路径有身份声明和审计记录。

**用户接口**：Python SDK + REST API。SynthLang 为内部编译目标。

**v2 相对 v1 的重构**：

v1 的生成流程是"物理引擎矩形域采样→值域验证"，是单一固定路径。v2 引入执行路由器后，生成流程变为"约束分类→路由决策→多路径执行"。这不是在 v1 旁边"加一个引擎"，而是**重构执行调度逻辑**。类比：自行车加引擎不是后座绑发动机，而是改车架、传动、制动——摩托车是新的整车，不是自行车+引擎。

v1 的值域约束**验证逻辑**不变（逐行检查 BETWEEN/MIN/MAX），但**调度方式**从硬编码变为路由器驱动。重构量估算：2周。

**v2 的数据引擎**：

v2 引入数据引擎 v1，基于核密度估计(KDE)学习训练数据分布。KDE 选择理由：
- 无需完整模型训练周期（对比 GAN/VAE）
- 可直接估计任意形状的概率密度
- 支持后筛选的排除率预估（体积比计算需要数据分布体积）
- v3 持续对齐可在此基础上升级为完整数据引擎

**限制**：KDE 在高维空间有维度灾难问题。v2 的数据引擎 v1 适用于中低维数据（<20维），高维场景在后续版本优化。

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 10 | 行间约束引擎 | batch有状态执行+frame buffer+跨batch状态传递+排序列绑定(来自Schema ORDER) | v1 #5 物理引擎 + #6 验证器 | 1.5周 |
| 11 | 聚合约束引擎 | 两阶段执行（阶段一：值域+行间逐行过滤；阶段二：时间窗口聚合验证）+排除率窗口语义+partial_window_excluded标记 | v1 #6 验证器 + #10 行间 | 1.5周 |
| 12 | 约束分类器(编译时) | 识别值域/行间/聚合→标记PHASE_ONE/PHASE_TWO+执行模式标记(row_by_row/stateful_batch/two_phase) | Parser扩展 | 1周 |
| 13 | **执行路由器重构** | **重构v1的硬编码调度为多路径路由**+5条退化路径（全功能/后筛选/纯物理/统计生成/KDE扰动）+约束完备性检查+身份切换+体积比预估 | #12 分类器 + #10/#11 三种引擎 + v1 #5/#6 | 2周 |
| 14 | 后筛选完整版 | 排除率预估(依赖#15b数据引擎的体积比计算)+超时截断+排除率实时监控+误差界联动表(0-30%/30-70%/70-90%) | #13 执行路由器 + #15b 数据引擎v1 | 1周 |
| 15 | 哈希链审计日志 | 创世记录+写入验证(prev_hash一致性)+分叉检测+每日全链校验+WORM存储 | v1 #4 存储 | 1周 |
| 15b | **数据引擎 v1(KDE简化版)** | 核密度估计学习训练数据分布+密度采样+体积比计算(约束空间体积/数据分布体积)+后筛选排除率预估支持。**限制：中低维(<20维)有效，高维需后续优化** | v1 #4 存储 + #2 Schema | 3周 |
| 16 | DURING/WHEN语义 | 条件约束：DURING column=value + WHEN condition THEN constraint。**v1物理引擎不支持的非矩形约束域在此引入**——物理引擎v2需支持拒绝采样/MCMC | #12 分类器 + 引擎 + #15b 数据引擎 | 1周 |
| 17 | EvidencePackage v2 | 新增statistical_fidelity(数据驱动时填充)+constraint_type_breakdown(三类)+身份声明(生成器身份)+audit_immutability: verified | #13 路由器 + #14 后筛选 + #15 审计 | 0.5周 |

### 数据引擎 v1 在 v2 的角色

数据引擎 v1(KDE) 在 v2 中承担三个角色：

1. **后筛选排除率预估**：#14 后筛选完整版需要体积比（约束空间体积/数据分布体积），KDE 提供数据分布体积估计
2. **退化路径中的生成器**：执行路由器 5 条退化路径中，"统计生成"和"KDE扰动"路径依赖数据引擎
3. **v3 持续对齐的前置**：v3 的持续对齐(UPDATE MODEL)需要数据引擎支持模型训练/增量更新

### 依赖链

```
#12 约束分类器 ← Parser扩展(v2语法)
      ↓
#10 行间引擎 ←── v1值域验证器
      ↓                       ↓
#11 聚合引擎(两阶段) ──→ #13 执行路由器重构 ←── v1物理引擎+验证器
                              ↓
#15b 数据引擎v1(KDE) ──→ #14 后筛选完整版
      ↓                           ↓
#16 DURING/WHEN ←─────── #13 + #15b
                              ↓
                         #17 EvidencePackage v2

#15 哈希链审计日志（独立，可与引擎并行）
```

### 可并行开发

- #15 审计日志：与引擎开发并行（仅依赖基础存储）
- #15b 数据引擎(KDE)：与引擎开发并行（独立模块）
- #10 行间 + #11 聚合：行间先行，聚合依赖行间
- #13 路由器：依赖所有引擎+数据引擎，在后期开始
- #12 分类器：可与引擎并行

### 演示场景

```python
# Python SDK（v2 用户接口）
constraint = client.define_constraint("wind_safety", "sensor_log", [
    RangeCheck("wind_speed", min=0, max=25, during=("status", "normal")),
    InterRowCheck("vibration", delta_max=5.0),  # 行间约束
    AggregateCheck("temperature", func="AVG", window="INTERVAL 1 HOUR", max_val=40.0)  # 聚合约束
])

result = client.generate("sensor_log", constraints=["wind_safety"], limit=1000, include_tail_report=True)
# EvidencePackage 中：
#   constraint_type_breakdown: {range: 2, inter_row: 1, aggregate: 1}
#   generator_identity: "constraint_driven_synthetic"
#   statistical_fidelity: {available: true, ...}
#   audit_immutability: verified
```

**输出**：
- 三类约束过滤后的数据
- EvidencePackage v2（含统计签名、约束分类、身份声明、审计可验证）
- tail_report（含误差界）
- 审计日志（哈希链可验证）

### v2 验收标准

- [ ] 行间约束跨batch状态传递正确，ORDER列来自Schema声明
- [ ] 聚合约束两阶段执行，阶段一包含值域+行间
- [ ] 约束分类器正确标记PHASE_ONE/PHASE_TWO
- [ ] 执行路由器5条退化路径全部可达，身份声明正确
- [ ] 数据引擎v1(KDE)可学习训练数据分布，支持体积比计算
- [ ] 排除率>80%时走保守偏向，>90%时拒绝后筛选
- [ ] 审计日志哈希链完整，可验证无篡改
- [ ] DURING/WHEN条件约束正确生效（含非矩形约束域的拒绝采样/MCMC）
- [ ] EvidencePackage v2 填充 statistical_fidelity、constraint_type_breakdown、audit_immutability: verified
- [ ] 数据引擎v1在中低维数据(<20维)上KDE估计有效

---

## v3: 时间智能（4-5周）

**产品故事**：数据在演化，模型在进化。时间旅行回到任意版本，持续对齐保持数据与时偕行。

**用户接口**：Python SDK + REST API。SynthLang 为内部编译目标。

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 18 | 模型版本链 | 版本创建/引用/列表+不可变写入+版本元数据(训练数据范围/fidelity_score) | v1 #4 存储+元数据层 | 1周 |
| 19 | 模型版本GC | 3保护条件(快照引用/anchored/N版本内)+自动compaction+合并元数据保留 | #18 版本链 | 1周 |
| 20 | 时间旅行(AS OF) | 按版本读取快照+compaction退化行为(返回最近版本+偏差报告) | #18 版本链 + #19 GC | 0.5周 |
| 21 | 持续对齐(UPDATE MODEL) | 新数据纳入+漂移检测(auto)+SAVE AS新版本+身份延续+代偿收敛时限 | #18 版本链 + v2 #13 执行路由器 + **v2 #15b 数据引擎v1** | 1.5周 |
| 22 | tail_report 增强版 | **将v2已有的误差界数据增强呈现到tail_report中**：排除率与data_grade联动+fidelity_mismatch标记+代偿模型状态。**注意：误差界数据在v2的statistical_fidelity中已有，此处是呈现增强，非新增计算** | v2 #14 后筛选完整版 + #21 持续对齐 | 1周 |
| 23 | 存储模型层 | 检查点存储+流式加载+版本索引+atomic_write事务(两阶段提交:先写数据→写元数据→提交审计) | v1 #4 元数据层 | 1周 |
| 24 | 偏差报告 | compaction偏差(requested/returned/reason/merged_from/training_data_range/fidelity_score_range/version_mismatch) | #19 GC + #18 版本链 | 0.5周 |

### 依赖链

```
#23 存储模型层(检查点+事务)
      ↓
#18 模型版本链 → #19 GC compaction → #20 时间旅行(AS OF)
      ↓                              ↓
#21 持续对齐(UPDATE MODEL) ←─────────┘
      ↑
      └──── 依赖 v2#15b 数据引擎v1（模型训练/增量更新）
      ↓
#22 tail_report增强版 ← v2后筛选 + #21 持续对齐
#24 偏差报告 ← #19 GC + #18 版本链
```

### 可并行开发

- #22 tail_report增强版 + #23 存储模型层：与版本链开发并行
- #18/#19/#20 串行，但 #21 和 #24 可并行

### 演示场景

```python
# 持续对齐：新数据到来，模型进化
model_v2 = client.update_model("gen_model_v1",
    incorporate_from="sensor_log",
    where="timestamp > '2026-05-01'",
    drift_check="auto",
    save_as="gen_model_v2"
)

# 时间旅行：回到旧版本
result = client.query_as_of("gen_samples", model_version="gen_model_v1")
# 如果 gen_model_v1 已被 compaction 合并，返回偏差报告
```

**输出**：
- 新版本模型（gen_model_v2）
- 时间旅行数据（含偏差报告）
- EvidencePackage（含模型版本链provenance、drift_detection）
- tail_report增强版（将v2已有的误差界数据与data_grade联动呈现，含fidelity_mismatch、代偿模型状态）

### v3 验收标准

- [ ] 模型版本链创建、引用、列表正确
- [ ] GC compaction 3保护条件全部生效
- [ ] AS OF 读取正确版本，compaction退化返回偏差报告
- [ ] UPDATE MODEL 新数据纳入，漂移检测工作，代偿收敛时限生效
- [ ] 持续对齐依赖数据引擎v1，模型训练/增量更新正确
- [ ] atomic_write 事务：中断恢复以元数据层状态为准
- [ ] tail_report增强版将v2误差界数据与data_grade联动，含代偿模型状态
- [ ] 偏差报告字段完整，证明链可重建
- [ ] EvidencePackage 中 drift_detection 从 not_applicable 变为填充

---

## v4: 高级分析（4-5周）

**产品故事**：行数窗口、分组聚合、会话切分——窗口类型全面扩展。约束完备度从布尔走向连续评分。

**用户接口**：Python SDK + REST API。SynthLang 为内部编译目标。

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 25 | 行数窗口(ROWS) | OVER (ROWS 100) 语法+执行+行数滑动窗口聚合 | v2 #11 聚合引擎 | 1周 |
| 26 | 分组时间窗口(PARTITION BY) | OVER (PARTITION BY col, INTERVAL 1 HOUR) 语法+执行+分组聚合 | v2 #11 聚合引擎 | 1周 |
| 27 | 会话窗口(SESSION) | OVER (SESSION BY col, GAP 5 MINUTES) 语法+执行+会话切分 | #25/#26 窗口基础 | 1.5周 |
| 28 | 约束完备度连续化评分 | 0.0-1.0评分+基于已覆盖约束维度加权+布尔判断作为阈值1.0特例+执行路由器联动(评分<1.0时退化路径选择) | v2 #13 执行路由器 | 1周 |
| 29 | **反例搜索(research)** | 研究性里程碑：探索约束域边界+不满足约束的区域可视化。**理论基础待补充**。交付取决于：(1) v2/v3阶段的理论和工程预研 (2) **待测模型接入协议（需在v3阶段定义）** | v2 #13 路由器 + #14 后筛选 + **待测模型接入协议(v3定义)** | 1.5周 |
| 30 | EvidencePackage v3 | 新增模型版本链provenance+完备度评分+反例区域(如预研成功)+偏差报告引用 | v3 EvidencePackage + #28 | 0.5周 |

### 反例搜索的状态声明

反例搜索在理论框架 v1.3 中没有独立章节，产品定位 v2.0 将其列为"暂不包含的能力"。v4 将其标为 **research** 里程碑：
- 如果 v2/v3 阶段完成了理论和工程预研，v4 可交付
- 如果预研结论为"不可行"或"需要更多理论工作"，v4 将不含反例搜索，路线图需更新
- 反例搜索不影响其他 v4 组件的交付
- **前置依赖**：待测模型接入协议需在 v3 阶段定义，否则 v4 反例搜索预研无法开始

### 依赖链

```
#25 行数窗口 ←── v2聚合引擎
#26 分组时间窗口 ←── v2聚合引擎
#27 会话窗口 ←── #25/#26

#28 完备度评分 ←── v2执行路由器
#29 反例搜索(research) ←── v2路由器 + 后筛选 + 待测模型接入协议(v3定义)

#30 EvidencePackage v3 ←── #28 + #29
```

### 可并行开发

- #25/#26 可并行（同层级，不同窗口类型）
- #25/#26 与 #28 可并行
- #29 独立研究，不阻塞其他组件
- #30 在 #28/#29 完成后

### 演示场景

```python
# 行数窗口
constraint = client.define_constraint("vibration_check", "sensor_log", [
    AggregateCheck("vibration", func="AVG", window="ROWS 100", max_val=2.0)
])

# 约束完备度
completeness = client.evaluate_completeness("sensor_log", constraints=["wind_safety"])
# completeness.score = 0.75  # 布尔判断是 score=1.0 的特例
```

**输出**：
- 多窗口类型约束执行结果
- 约束完备度评分（0.0-1.0）
- EvidencePackage v3（含完备度+版本链provenance）
- 反例区域报告（如预研成功）

### v4 验收标准

- [ ] ROWS/PARTITION BY/SESSION 三种窗口语法解析正确
- [ ] 窗口聚合计算正确（边界、分组、会话切分）
- [ ] 完备度评分 0.0-1.0，布尔判断作为1.0特例
- [ ] 评分<1.0时路由器选择正确退化路径
- [ ] EvidencePackage v3 包含完备度评分字段
- [ ] 反例搜索：如预研成功，返回不满足约束的区域；如预研未完成，明确标记为 deferred

---

## 版本间依赖总图

```
v1 最小可运行
│  #1 Parser
│  #2 类型系统(含ORDER预留)
│  #3 数据导入
│  #4 基础存储
│  #5 物理引擎v1(仅矩形域采样)
│  #6 值域约束验证器
│  #7 tail_report v1(偏差声明)
│  #8 EvidencePackage v1(含适用性标注+audit_immutability:not_applicable)
│  #9 SDK+REST
│
├──→ v2 约束完整
│    #10 行间引擎 ←── v1#5+#6
│    #11 聚合引擎 ←── v1#6 + #10
│    #12 约束分类器 ←── Parser扩展
│    #13 执行路由器重构 ←── #10/#11/#12 + v1#5/#6 【重构2周】
│    #14 后筛选完整版 ←── #13 + #15b
│    #15 哈希链审计 ←── v1#4
│    #15b 数据引擎v1(KDE) ←── v1#4 + #2 【3周】
│    #16 DURING/WHEN ←── #12 + #15b
│    #17 EvidencePackage v2 ←── #13 + #14 + #15
│
├──→ v3 时间智能
│    #18 模型版本链 ←── v1#4
│    #19 GC compaction ←── #18
│    #20 时间旅行 ←── #18 + #19
│    #21 持续对齐 ←── #18 + v2#13 + v2#15b数据引擎
│    #22 tail_report增强版 ←── v2#14 + #21
│    #23 存储模型层 ←── v1#4
│    #24 偏差报告 ←── #19 + #18
│
└──→ v4 高级分析
     #25 行数窗口 ←── v2#11
     #26 分组时间窗口 ←── v2#11
     #27 会话窗口 ←── #25/#26
     #28 完备度评分 ←── v2#13
     #29 反例搜索(research) ←── v2#13 + v2#14 + 待测模型接入协议(v3定义)
     #30 EvidencePackage v3 ←── #28 + #29
```

---

## 与工程框架 v0.4 的对齐验证

| 工程框架 v0.4 组件 | 路线图覆盖 |
|-------------------|-----------|
| SynthLang 解析器 | v1 #1 + v2 #12(Parser扩展) |
| 类型/Schema 系统 | v1 #2(含ORDER预留) |
| 约束分类器 | v2 #12 |
| 值域约束 → 逐行路径 | v1 #6(验证器) + v1 #5(物理采样) |
| 行间约束 → batch有状态路径 | v2 #10 |
| 聚合约束 → 两阶段路径 | v2 #11 |
| 执行路由器 + 退化路径 | v2 #13(重构) |
| 后筛选保障 | v2 #14(完整版) |
| 数据引擎 | v2 #15b(KDE简化版) |
| 引擎适配层 | v2 #13(路由器内含) |
| EvidencePackage 构建器 | v1 #8 + v2 #17 + v4 #30 |
| 存储抽象层(StorageBackend) | v1 #4 + v3 #23 |
| 基表层(INSERT ONLY) | v1 #3 |
| 快照层(不可变) | v1 #4 |
| 模型层(版本链+GC) | v3 #18 + #19 |
| 审计日志(哈希链+WORM) | v2 #15 |
| 模型版本GC | v3 #19 |
| 时间旅行(AS OF) | v3 #20 |
| 持续对齐(UPDATE MODEL) | v3 #21 |
| tail_report | v1 #7(v1基础版) + v3 #22(增强呈现版) |
| 排除率与data_grade联动 | v2 #14 + v3 #22 |
| DURING/WHEN语义 | v2 #16 |
| 窗口类型扩展(ROWS/PARTITION/SESSION) | v4 #25/#26/#27 |
| 约束完备度连续化评分 | v4 #28 |
| 反例搜索 | v4 #29(research) |

**覆盖率**：25/25 = 100%（反例搜索标记为 research，数据引擎从无编号变为 #15b）

---

## 与理论框架 v1.3 的对齐验证

| 理论框架 v1.3 要求 | 路线图覆盖 | 传递方式 |
|-------------------|-----------|---------|
| 值域约束逐行过滤 | v1 #6 验证器 | 直接实现 |
| 行间约束有状态过滤 | v2 #10 | 直接实现 |
| 聚合约束两阶段执行 | v2 #11 | 直接实现 |
| 后筛选是最终防线 | v2 #14 | v1 走纯物理不需后筛选；v2 后筛选路径作为退化路径之一 |
| 物理优先认识论偏差 | v1 #7 tail_report | **从 v1 起即声明**，每份 EvidencePackage 必含 |
| 排除率窗口语义 | v2 #11 | 聚合约束阶段二的窗口排除率 |
| 代偿收敛时限 | v3 #21 | 持续对齐中的代偿模型管理 |
| 记忆策略退化分析 | v2 #13 执行路由器 | 身份切换：5种生成器身份 |
| 审计不可变 | v2 #15 | 哈希链+WORM；**v1 声明无此保证** |
| 可追溯性 | v1 #8 + v3 #18 | provenance基础(v1) + 模型版本链(v3) |

**覆盖率**：10/10 = 100%

**关键修正**：
- v1.0：物理优先偏差仅在 v3 tail_report
- v1.1：修正为从 v1 起声明
- v1.2：进一步明确 v1 审计不可变为 not_applicable，v2 起生效

---

## 人员假设

基于 aboutme.md：专业数据库团队，C/C++/Rust 技术栈。

- **Parser + 类型系统**：1人（编译原理背景）
- **存储引擎**：1-2人（存储/文件系统背景）
- **约束引擎 + 路由器**：1-2人（数据库执行引擎背景）
- **数据引擎(KDE)**：1人（统计/ML背景，熟悉核密度估计）
- **SDK + API**：1人（应用层）
- **总团队**：4-6人

v1 最小可运行阶段，2-3人即可并行（Parser+存储→物理引擎+SDK）。
v2 需要数据引擎专人，建议 4-5人。

---

## 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| v1→v2 执行路由器重构 | v2 延迟2周 | v1 设计时即预留路由器接口（即使硬编码），降低重构成本 |
| v1 Parser 子集在 v2 扩展时重构 | 延迟1-2周 | 工程框架已设计预留（unsupported_in_v1标记），Parser 架构从一开始就支持扩展 |
| 聚合约束两阶段语义理解偏差 | v2 延迟 | v1 阶段就完成两阶段语义的设计文档审查 |
| 元数据层事务协调复杂度 | v3 延迟 | v1 用简化版（无事务），v3 引入两阶段提交 |
| ONNX Runtime/TensorRT 集成 | 全局 | 物理引擎作为独立模块，可先 mock 再集成 |
| 反例搜索理论基础缺失 | v4 交付不确定性 | 标记为 research，不阻塞其他 v4 组件 |
| v1 EvidencePackage 不适用字段引起用户困惑 | 信任 | 字段适用性标注 + 用户文档明确声明 |
| 数据引擎v1(KDE)高维维度灾难 | v2 后筛选精度 | v2 明确声明中低维有效；高维场景用简化分布替代（如高斯混合） |
| v1 无审计不可变保证 | 信任 | v1 诚实声明 + EvidencePackage audit_immutability: not_applicable |
| v3 持续对齐依赖数据引擎 | v3 延迟 | v2 必须交付数据引擎v1，v3 依赖明确 |
| 待测模型接入协议未在v3定义 | v4 反例搜索无法启动 | v3 验收标准增加"定义待测模型接入协议" |

---

文档结束
