SynthGen Core 开发路线图 v1.4
文档版本：v1.4
上一版本：v1.3
修订性质：开发辅助工具线合并——将 Agent 辅助开发建议书经可行性审查后，以诚实命名和落地路径嵌入路线图
日期：2026-05-10

方法：能力里程碑——按"系统能做什么"划分版本，每个版本交付可演示的产品能力，内部含跨层技术拆解。
交付粒度：按功能里程碑，时间仅作估算。
与理论框架对齐：v1.3 约束分层（值域/行间/聚合）→ 路线图版本递进完全对齐。

文档结构：**明线**（功能组件）+ **暗线**（脚手架工程）+ **工具线**（开发辅助工具），三线并行，同版本交付。

---

## v1.3 → v1.4 修订记录

| # | 来源 | 修订内容 |
|---|------|---------|
| 1-9 | v1.2→v1.3 | 同 v1.3 修订记录（脚手架暗线嵌入、终审裁定处理等） |
| 10 | Agent建议书+可行性审查 | 新增"开发辅助工具线"——第三条线，与明线/暗线并行 |
| 11 | 可行性审查修正 | 诚实命名：Scaffold Agent→组件模板引擎，Test Agent→测试辅助库，Consistency Agent→Schema一致性校验器，Debug Agent→Trace分析工具 |
| 12 | 可行性审查修正 | 引入时机调整：组件模板引擎和测试辅助库不可能在v1前期就位（依赖v1正在建设的基础设施），修正为v1中后期引入 |
| 13 | 可行性审查修正 | 验收标准匹配实际能力：不使用AI Agent标准验收模板引擎和规则引擎 |
| 14 | 可行性审查修正 | 技术栈显式标注：模板引擎用inja(C++头文件库)或Jinja2(Python)，校验器用Python(CI脚本)，Trace分析用Python(独立工具) |
| 15 | 可行性审查修正 | 新增工具线风险：v1前期无模板素材、Python工具链与C++团队的认知负担、工具维护成本 |

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

## 脚手架工程原则

路线图包含三条线：**明线**（功能组件）、**暗线**（脚手架工程）和**工具线**（开发辅助工具）。

脚手架不是最终产品的一部分，它是建造产品所需的工具、设施和保障体系。暗线如果不在路线图中显式规划，会导致组件间协议接口错误、执行路径不可复现、约束过滤无法验证、EvidencePackage 来源不可追溯。

**核心原则**：

1. **与功能组件同版本交付**：Explain 不是 v3 才需要的高级功能——v1 就需要。没有脚手架，用户无法信任系统输出。
2. **脚手架代码也是生产代码**：Trace span 格式一旦定义就不能随意改。Explain 的输出结构一旦定义就成了隐式 API。脚手架不是临时工具，是产品的一部分。
3. **脚手架自身也需要可观测**：Explain 的准确性谁来验证？Trace 的完整性谁检查？脚手架设施自身也需要测试和监控。
4. **最小可行脚手架**：从最小开始。v1 脚手架只回答三个问题——系统会走什么路径？系统实际走了什么路径？系统现在运行状态如何？

**六类脚手架设施**：

| 脚手架 | 定义 | v1 | v2 | v3 | v4 |
|--------|------|----|----|----|-----|
| Explain | 执行计划可解释 | 最小：路由决策+约束分类+执行模式 | 增强：排除率预估+体积比+数据来源 | 增强：compaction影响预估 | 增强：完备度评分影响 |
| Trace | 执行过程可追踪 | 最小：span结构+trace_id | 增强：后筛选路径实时排除率 | 增强：持续对齐模型更新变化 | 增强：反例搜索探索轨迹 |
| 可观测性 | 系统内部状态暴露 | 最小：吞吐量+延迟+内存 | 增强：排除率趋势+退化路径命中率 | 增强：模型版本链状态+GC历史 | — |
| 确定性测试框架 | seed固定→输出一致 | 最小：seed固定+参考快照+EvidencePackage Schema验证 | 增强：5条退化路径回归测试 | 增强：compaction一致性测试 | — |
| CI/CD 回归测试 | 代码提交自动触发 | 建立：基础设施+标准数据集+参考快照 | — | — | — |
| 错误注入 | 人为制造故障验证退化 | — | v2引入：后筛选排除率+数据引擎故障 | 增强：compaction冲突场景 | — |

**暗线不增加版本数量，嵌入在每个版本的脚手架验收标准中。**

---

## 开发辅助工具线

### 定位：暗线之上的工具，不是产品的一部分

脚手架工程定义了六类设施——这些是产品的一部分，每个版本必须交付。

开发辅助工具是开发过程中的工具。它不产生最终交付物，但可以加速以下环节：

| 环节 | 工具的角色 |
|------|-----------|
| 组件开发启动 | 生成含脚手架功能框架的代码骨架 |
| 测试编写 | 提供参数化测试宏和断言，减少手工编写 |
| 接口一致性 | 比对代码接口与 Schema/理论框架是否一致 |
| 故障诊断 | 基于规则扫描 Trace span，定位异常 |

### 诚实命名

这四个工具中，三个不需要 AI。命名必须反映实际能力，否则会产生错误的实施方向和验收预期。

| 审核团队命名 | 本路线图命名 | 实际技术 | 需要AI吗 |
|-------------|------------|---------|---------|
| Scaffold Agent | **组件模板引擎** | 模板引擎（inja/Jinja2），模板展开 | 不需要 |
| Test Agent | **测试辅助库** | C++ 头文件库 + 测试宏 | 不需要 |
| Consistency Agent | **Schema 一致性校验器** | 结构化 diff + 字段比对 | 不需要 |
| Debug Agent | **Trace 分析工具** | 规则引擎为主，AI 可选增强 | 部分需要 |

命名不影响功能，但影响实施方向。叫"Agent"会让团队花精力在 prompt 设计和 LLM 集成上，而实际需要的是模板引擎和规则引擎。

### 工具线四条原则

**1. 先建后用**

工具依赖已有组件的惯例，不能先于惯例存在。组件模板引擎需要先有 2 个已完成组件的脚手架代码作为模板素材。测试辅助库需要先有测试框架和标准数据集。

**2. 不写产品代码**

生成数据的约束验证逻辑、后筛选排除率计算、哈希链验证——核心逻辑必须由人编写。工具只辅助周边工作：骨架代码、测试宏、一致性检查、Trace 分析。

**3. 产出必须可验证**

组件模板引擎生成的骨架必须能通过编译和基础 CI。测试辅助库的测试宏必须能在标准数据集上通过。Schema 校验器发现的不一致必须能定位到具体字段。Trace 分析工具的异常标注必须能追溯到具体 span。

**4. 工具也是软件，也需要维护**

模板会随组件接口变化而过时。测试宏会随测试框架升级而需要适配。Schema 校验规则会随 Schema 版本演进而需要更新。工具维护成本必须纳入估算。

### 四个工具的实施路径

#### 组件模板引擎

**做什么**：给定组件接口描述，自动生成含 span 创建/写入、metrics 注册/暴露、Explain 接口占位、错误处理框架的 .cpp 和 .h 骨架。骨架中核心逻辑处留 TODO 标记，由开发者填充。

**输入**：组件接口描述（JSON/YAML）
```json
{
  "name": "ValueRangeValidator",
  "namespace": "synthgen::constraint",
  "spans": ["validate_row", "check_constraint"],
  "metrics": ["rows_validated", "validation_errors"],
  "explain_fields": ["constraint_type", "execution_mode"]
}
```

**输出**：.h 和 .cpp 文件，含 span/metrics/explain 框架代码 + TODO 标记的核心逻辑占位。

**技术选型**：inja（C++ 头文件模板库）或 Jinja2（Python，如果团队接受 Python 工具链）。模板文件与代码库一起版本控制。

**关键依赖**：至少 2 个已完成组件的脚手架代码作为模板素材。v1 前期不可能有此工具。

**诚实边界**：
- ✅ 能做：生成符合脚手架规范的代码骨架，减少手动复制粘贴
- ❌ 做不了：生成核心逻辑代码（约束验证算法、采样策略等）

#### 测试辅助库

**做什么**：提供 C++ 测试宏和辅助函数，让开发者用一行宏定义即可生成边界/异常测试用例。

**核心宏**：
```cpp
// 值域边界测试：自动生成 min-ε, min, min+ε, max-ε, max, max+ε 测试
TEST_RANGE_VALIDATION(validator, "temperature", -50.0, 80.0);

// 约束合规测试：给定生成器和约束，验证通过率
TEST_CONSTRAINT_COMPLIANCE(generator, constraint, expected_pass_rate);

// 种子固定测试基类
class SeedFixedTest : public ::testing::Test {
protected:
    uint64_t test_seed_ = 42;
    // 自动加载参考快照比对
};
```

**技术选型**：C++ 头文件库（test_helpers.h），无外部依赖，与 Google Test 集成。

**诚实边界**：
- ✅ 能做：值域边界自动化测试、种子固定测试基类、参数化测试数据生成
- ❌ 做不了：约束语义的深层测试（如"行间约束跨 batch 状态传递"需要理解业务语义）

#### Schema 一致性校验器

**做什么**：编译后自动提取组件接口描述 → 比对 EvidencePackage Schema → 比对理论框架承诺清单 → 报告不一致。

**三方比对**：
1. 代码接口注册（编译期自动生成 .json）
2. EvidencePackage Schema v1.2（机器可读版，JSON Schema）
3. 理论框架承诺清单（手动维护的 YAML，列出每个承诺对应的 Schema 字段和代码接口）

**技术选型**：Python 脚本，作为 CI 步骤运行。输出不一致报告（字段缺失、枚举值不匹配、适用性标记缺失）。

**诚实边界**：
- ✅ 能做：字段名拼写检查、必选字段存在性检查、枚举值匹配
- ❌ 做不了：语义层面的一致性（如"排除率计算是否符合理论定义"需要人工审查）

#### Trace 分析工具

**做什么**：读取失败请求的 Trace spans（JSON），应用规则引擎扫描异常模式，输出高亮的时间线和异常列表。

**规则引擎**：
- `span.status == "error"` → 标红
- `span.duration > P99_threshold` → 标黄
- `span.exclusion_rate` 连续 3 个 span 上升 → 标红（排除率飙升）
- `span.path != expected_path` → 标黄（退化路径意外命中）

**技术选型**：Python 独立工具，输入为 EvidencePackage 中的 trace 字段（JSON），输出为终端高亮 + JSON 报告。

**诚实边界**：
- ✅ 能做：已知异常模式匹配、时间线可视化、排除率趋势检测
- ⚠️ 有限能做：根因推理（规则引擎覆盖已知模式，未知模式需要人工）
- ❌ 做不了：跨组件语义层面的自动诊断

**AI 增强路径（可选，v3+）**：在规则引擎之上，可接入 LLM 做自然语言根因分析。但这是锦上添花，规则引擎本身已覆盖大部分常见诊断场景。

### 工具线分版本交付计划

| 工具 | v1 | v2 | v3 | v4 |
|------|----|----|----|-----|
| 组件模板引擎 | v0.1：从#5/#6提炼模板，生成#8骨架 | v0.2：增加v2组件类型模板 | 增强：compaction组件模板 | — |
| 测试辅助库 | v0.1：参数化值域测试宏+种子固定基类 | v0.2：退化路径参数化测试 | 增强：compaction测试辅助 | — |
| Schema校验器 | — | v1.0：编译期校验+三方diff | 增强：版本链字段校验 | — |
| Trace分析工具 | — | v0.1：规则引擎+span异常检测 | v0.2：compaction规则 | 增强：反例搜索轨迹分析 |

**工具线工作量估算**：

| 版本 | 工具组件 | 额外估算 |
|------|---------|---------|
| v1 | 模板引擎v0.1 + 测试辅助库v0.1 | +0.5周（与脚手架开发并行，由同一人负责） |
| v2 | 模板引擎v0.2 + 测试辅助库v0.2 + Schema校验器v1.0 + Trace分析v0.1 | +0.5周 |
| v3 | 各工具增强 | +0.5周 |
| v4 | Trace分析增强 | 0周（含在功能开发中） |

**总计增加**：+1.5周，含工具线的总估算从 28-32周 调整为 **29-33周**。

---

## 总览

| 版本 | 产品能力 | 功能组件数 | 脚手架组件数 | 工具组件数 | 功能估算 | 含脚手架+工具估算 |
|------|---------|-----------|-------------|-----------|---------|-----------------|
| **v1** 最小可运行 | Schema→导入→纯物理采样(矩形域)→值域验证→证据包 | 9 | 5 | 2 | 7-8周 | 8.5-9.5周 |
| **v2** 约束完整 | 三类约束+数据引擎+后筛选+执行路由器+审计 | 9 | 4 | 4 | 10-11周 | 12-13周 |
| **v3** 时间智能 | 时间旅行+持续对齐+模型版本管理+tail_report增强 | 7 | 3 | 4 | 4-5周 | 6-7周 |
| **v4** 高级分析 | 多窗口+完备度+反例研究 | 6 | 2 | 1 | 4-5周 | 4-5周 |

**总计**：31个功能组件 + 14个脚手架组件 + 6个工具组件，29-33周

---

## v1: 最小可运行（8.5-9.5周）

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

### 功能组件清单（明线）

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

### 脚手架组件清单（暗线）

| 脚手架 | v1 交付内容 | 估算 |
|--------|-----------|------|
| Explain 最小版 | 约束分类结果 + 执行模式 + 路由决策（纯物理路径），插入在 #8 EvidencePackage 构建器中作为生成请求的可选预查 | 0.5周 |
| Trace 最小版 | 每个组件产生 span，写入 EvidencePackage provenance；trace_id = package_id | 0.5周 |
| 可观测性最小版 | /metrics 端点：生成吞吐量 + 请求完成延迟 + 内存占用（进程内 metrics） | 0.5周 |
| 确定性测试框架 | seed 固定 + 参考输出快照 + EvidencePackage Schema 自动验证 | 0.5周 |
| CI/CD 回归基础设施 | CI 环境就绪 + 标准测试数据集(1000行传感器Parquet) + 每次PR触发Parser单元测试+端到端生成测试 | 0.5周 |

**脚手架工作量**：约1.5人周，可与功能组件并行开发（由1人负责，可与SDK开发者复用）。

### 工具组件清单（工具线）

| 工具 | v1 交付内容 | 引入时机 | 估算 |
|------|-----------|---------|------|
| 组件模板引擎 v0.1 | 从 #5 物理引擎和 #6 值域验证器的脚手架代码中提炼模板；用模板生成 #8 EvidencePackage 构建器的骨架 | v1 第4-5周起（需先有#5/#6的脚手架代码作为素材） | 0.25周 |
| 测试辅助库 v0.1 | test_helpers.h：参数化值域测试宏(TEST_RANGE_VALIDATION)+种子固定测试基类(SeedFixedTest)+约束验证断言宏 | v1 第6-7周起（需先有测试框架和标准数据集） | 0.25周 |

**工具线工作量**：约0.5人周，与脚手架开发并行，由同一人负责。

**v1 工具线引入时间线**：

```
v1 第1-3周：工具线空白期
  - #5 物理引擎、#6 值域验证器正在开发
  - 这两个组件的脚手架代码 = 未来的模板素材
  - 此阶段不可能有组件模板引擎（没有素材）

v1 第4-5周：模板提炼期
  - #5/#6 脚手架代码完成
  - 从中提炼组件骨架模板（span格式、metrics注册、Explain占位）
  - 用模板生成 #8 EvidencePackage 构建器骨架

v1 第6-7周：测试辅助期
  - 测试框架和标准数据集就位
  - 构建测试辅助库 v0.1
  - 用于 #8 和 #9 的测试编写

v1 第8-9周：验证期
  - 用组件模板引擎生成的骨架通过编译和CI
  - 用测试辅助库的宏编写的测试在标准数据集上通过
```

### EvidencePackage v1 字段适用性

| 字段 | 适用性 | v1 状态 | 说明 |
|------|--------|---------|------|
| schema_hash | always | ✅ 填充 | — |
| constraint_summary | always | ✅ 填充 | 仅值域约束（矩形域） |
| exclusion_rate | always | ✅ 填充 | 纯物理路径下应为 0% |
| data_grade | always | ✅ 填充 | physics_guaranteed |
| row_count | always | ✅ 填充 | — |
| provenance | always | ✅ 填充 | 基础版：数据源+约束+生成参数+trace spans |
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

[暗线并行]
Explain最小 ←── #8 EvidencePackage
Trace最小 ←── 各组件span写入
可观测性最小 ←── 独立（进程内metrics端点）
测试框架 ←── #5 物理引擎（seed固定）+ #8 EvidencePackage（Schema验证）
CI/CD ←── 独立基础设施

[工具线并行，第4周起]
组件模板引擎v0.1 ←── #5/#6 脚手架代码（素材源）→ 生成 #8 骨架
测试辅助库v0.1 ←── 测试框架 + 标准数据集（第6周起）
```

### 可并行开发

- #1 Parser + #4 存储：无依赖，可同时开工
- #8 EvidencePackage：接口先定义，与 #5/#6 并行开发
- #5 物理引擎：独立模块，可与 Parser/存储并行
- 脚手架五项：与功能组件并行，1人负责
- 工具两项：第4周起与功能组件并行，同1人负责

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

# Explain：预览执行计划
plan = client.explain("sensor_log", constraints=["safe_range"])
# plan.execution_mode: row_by_row
# plan.path: physics_sampling
# plan.constraint_classification: {value_range: 2, inter_row: 0, aggregate: 0}

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

**输出**：1000行合成数据 + EvidencePackage（含 tail_report、偏差声明、字段适用性标注、audit_immutability: not_applicable、trace spans）

### v1 功能验收标准

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

### v1 脚手架验收标准

- [ ] Explain 可显示约束分类结果 + 执行模式 + 路由决策（纯物理路径）
- [ ] 每个生成请求产生唯一 trace_id，span 记录到 EvidencePackage provenance
- [ ] /metrics 端点可查询生成吞吐量、请求延迟、内存占用
- [ ] 固定 seed 产生的输出可与参考快照逐行比对
- [ ] CI 环境就绪，每次 PR 触发 Parser 单元测试 + 端到端生成测试

### v1 工具验收标准

- [ ] 组件模板引擎从 #5/#6 提炼的模板，生成的 #8 骨架代码能通过编译和基础 CI
- [ ] 测试辅助库的 TEST_RANGE_VALIDATION 宏能自动检测越界值（min-ε 和 max+ε 测试失败，min 和 max 测试通过）

---

## v2: 约束完整（12-13周）

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

**⚠️ 工程执行注记**：v2 的早期迭代应先测 3 条不依赖数据引擎的退化路径（纯物理/统计生成/KDE扰动占位），数据引擎就位后再补全 5 条路径的集成测试。这不需要改路线图，是工程执行层面的注意事项。

### 功能组件清单（明线）

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

### 脚手架组件清单（暗线）

| 脚手架 | v2 交付内容 | 估算 |
|--------|-----------|------|
| Explain 增强 | 排除率预估 + 体积比计算细节 + 数据来源 | 0.5周 |
| Trace 增强 | 后筛选路径实时排除率变化记录 | 0.5周 |
| 可观测性增强 | 排除率实时趋势 + 退化路径命中率 + 审计验证状态 | 0.5周 |
| 错误注入 v2 | 后筛选排除率爆炸模拟 + 数据引擎故障注入 | 0.5周 |
| 确定性测试增强 | 5条退化路径各一个回归测试用例 | 0.5周 |

**脚手架工作量**：约1人周，与功能组件并行。

### 工具组件清单（工具线）

| 工具 | v2 交付内容 | 引入时机 | 估算 |
|------|-----------|---------|------|
| 组件模板引擎 v0.2 | 增加v2新组件类型模板（行间/聚合/路由器/数据引擎/审计） | v2 第1-2周 | 0.25周 |
| 测试辅助库 v0.2 | 退化路径参数化测试宏(TEST_DEGRADATION_PATH)+后筛选排除率断言 | v2 第5-6周 | 0.25周 |
| Schema一致性校验器 v1.0 | 编译期Schema校验+接口注册机制+三方diff(代码↔Schema↔理论框架) | v2 第3-4周 | 0.5周 |
| Trace分析工具 v0.1 | 规则引擎(4条规则)+span异常检测+终端高亮+JSON报告 | v2 第8-9周 | 0.5周 |

**工具线工作量**：约1人周，与功能组件并行。

**Schema 一致性校验器的实施路径**：

```
v2 第1-2周：定义接口注册机制
  - 每个组件编译时自动生成接口描述 .json（编译后步骤）
  - 定义 EvidencePackage Schema 的 JSON Schema 版本
  - 定义理论框架承诺清单 YAML（手动维护）

v2 第3-4周：实现校验器脚本
  - 输入：接口注册 .json + Schema .json + 承诺清单 .yaml
  - 输出：不一致报告（字段缺失/枚举不匹配/适用性标记缺失）
  - 集成：CI 步骤，编译后自动运行

v2 第5周起：日常使用
  - 每次 PR 提交自动触发校验
  - 发现不一致时 CI 红灯 + 报告
```

**Trace 分析工具的实施路径**：

```
v2 第6-7周：积累 Trace 数据
  - v2 早期迭代中，后筛选路径和退化路径产生 Trace spans
  - 收集已知异常模式（人工诊断的案例）

v2 第8-9周：实现规则引擎
  - 规则1：span.status == "error" → 标红
  - 规则2：span.duration > P99_threshold → 标黄
  - 规则3：exclusion_rate 连续3个span上升 → 标红
  - 规则4：span.path != expected_path → 标黄
  - 输出：终端高亮 + JSON 报告

v2 第10-11周：验证和积累
  - 用已知故障案例验证规则引擎
  - 持续积累新规则
```

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

[暗线并行]
Explain增强 ←── #13 路由器 + #15b 数据引擎
Trace增强 ←── #14 后筛选
可观测性增强 ←── #13 路由器 + #15 审计
错误注入 ←── #14 后筛选 + #15b 数据引擎
测试增强 ←── #13 路由器（5条退化路径）

[工具线并行]
组件模板引擎v0.2 ←── v2新组件接口定义
测试辅助库v0.2 ←── 测试增强（退化路径）
Schema校验器v1.0 ←── #17 EvidencePackage v2 Schema + 接口注册机制
Trace分析v0.1 ←── Trace增强（排除率spans）
```

### 可并行开发

- #15 审计日志：与引擎开发并行（仅依赖基础存储）
- #15b 数据引擎(KDE)：与引擎开发并行（独立模块）
- #10 行间 + #11 聚合：行间先行，聚合依赖行间
- #13 路由器：依赖所有引擎+数据引擎，在后期开始
- #12 分类器：可与引擎并行
- 脚手架五项：与功能组件并行
- 工具四项：与功能组件并行，Schema校验器第3-4周，Trace分析第8-9周

### 演示场景

```python
# Python SDK（v2 用户接口）
constraint = client.define_constraint("wind_safety", "sensor_log", [
    RangeCheck("wind_speed", min=0, max=25, during=("status", "normal")),
    InterRowCheck("vibration", delta_max=5.0),  # 行间约束
    AggregateCheck("temperature", func="AVG", window="INTERVAL 1 HOUR", max_val=40.0)  # 聚合约束
])

# Explain：v2 增强版
plan = client.explain("sensor_log", constraints=["wind_safety"])
# plan.path: constrained_fusion
# plan.exclusion_rate_estimate: 0.45
# plan.data_source: {model_version: "kde_v1", training_rows: 10000}

result = client.generate("sensor_log", constraints=["wind_safety"], limit=1000, include_tail_report=True)
# EvidencePackage 中：
#   constraint_type_breakdown: {range: 2, inter_row: 1, aggregate: 1}
#   generator_identity: "constraint_driven_synthetic"
#   statistical_fidelity: {available: true, ...}
#   audit_immutability: verified
#   trace: {trace_id: "evp_...", spans: [...]}
```

**输出**：
- 三类约束过滤后的数据
- EvidencePackage v2（含统计签名、约束分类、身份声明、审计可验证、trace spans）
- tail_report（含误差界）
- 审计日志（哈希链可验证）

### v2 功能验收标准

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

### v2 脚手架验收标准

- [ ] Explain 可显示排除率预估和体积比计算细节
- [ ] Trace 可记录后筛选路径的实时排除率变化
- [ ] 可观测性增加排除率实时趋势和退化路径命中率
- [ ] 错误注入可模拟后筛选排除率爆炸，验证系统回退
- [ ] 5条退化路径各有一个回归测试用例

### v2 工具验收标准

- [ ] 组件模板引擎 v0.2 可生成 #10-#17 任一新组件的骨架，骨架能通过编译
- [ ] 测试辅助库的 TEST_DEGRADATION_PATH 宏能自动测试5条退化路径的路由正确性
- [ ] Schema 校验器的字段 diff 能发现人为引入的接口注册与 Schema 定义的不一致（验收测试：故意在接口注册中拼错一个字段名，校验器能检出）
- [ ] Trace 分析工具的规则引擎能识别 exclusion_rate 超阈值的 span（验收测试：构造一个排除率飙升的 Trace，工具能标红对应 span）

---

## v3: 时间智能（6-7周）

**产品故事**：数据在演化，模型在进化。时间旅行回到任意版本，持续对齐保持数据与时偕行。

**用户接口**：Python SDK + REST API。SynthLang 为内部编译目标。

### 功能组件清单（明线）

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 18 | 模型版本链 | 版本创建/引用/列表+不可变写入+版本元数据(训练数据范围/fidelity_score) | v1 #4 存储+元数据层 | 1周 |
| 19 | 模型版本GC | 3保护条件(快照引用/anchored/N版本内)+自动compaction+合并元数据保留 | #18 版本链 | 1周 |
| 20 | 时间旅行(AS OF) | 按版本读取快照+compaction退化行为(返回最近版本+偏差报告) | #18 版本链 + #19 GC | 0.5周 |
| 21 | 持续对齐(UPDATE MODEL) | 新数据纳入+漂移检测(auto)+SAVE AS新版本+身份延续+代偿收敛时限 | #18 版本链 + v2 #13 执行路由器 + **v2 #15b 数据引擎v1** | 1.5周 |
| 22 | tail_report 增强版 | **将v2已有的误差界数据增强呈现到tail_report中**：排除率与data_grade联动+fidelity_mismatch标记+代偿模型状态。**注意：误差界数据在v2的statistical_fidelity中已有，此处是呈现增强，非新增计算** | v2 #14 后筛选完整版 + #21 持续对齐 | 1周 |
| 23 | 存储模型层 | 检查点存储+流式加载+版本索引+atomic_write事务(两阶段提交:先写数据→写元数据→提交审计) | v1 #4 元数据层 | 1周 |
| 24 | 偏差报告 | compaction偏差(requested/returned/reason/merged_from/training_data_range/fidelity_score_range/version_mismatch) | #19 GC + #18 版本链 | 0.5周 |

### 脚手架组件清单（暗线）

| 脚手架 | v3 交付内容 | 估算 |
|--------|-----------|------|
| Explain 增强 | compaction 影响预估（请求的版本已被 compaction 时，explain 显示将退化的版本和偏差报告） | 0.5周 |
| Trace 增强 | 持续对齐的模型更新前后分布变化记录 | 0.5周 |
| 可观测性增强 | 模型版本链状态 + GC compaction 历史 | 0.5周 |
| 错误注入增强 | compaction 冲突场景注入 | 0.5周 |
| 确定性测试增强 | compaction 前后生成结果一致性验证 | 0.5周 |

**脚手架工作量**：约1人周，与功能组件并行。

### 工具组件清单（工具线）

| 工具 | v3 交付内容 | 估算 |
|------|-----------|------|
| 组件模板引擎 v0.3 | 增加 compaction/版本链组件模板 | 0.25周 |
| 测试辅助库 v0.3 | compaction 前后一致性测试辅助宏 | 0.25周 |
| Schema校验器 v1.1 | 增加版本链字段校验规则 | 0.25周 |
| Trace分析工具 v0.2 | 增加 compaction 冲突规则（版本退化 span 检测） | 0.25周 |

**工具线工作量**：约0.5人周，与功能组件并行。

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

[暗线并行]
Explain增强 ←── #19 GC + #20 时间旅行
Trace增强 ←── #21 持续对齐
可观测性增强 ←── #18 版本链 + #19 GC
错误注入增强 ←── #19 GC
测试增强 ←── #19 GC

[工具线并行]
组件模板引擎v0.3 ←── #18-#24 新组件接口
测试辅助库v0.3 ←── 测试增强（compaction）
Schema校验器v1.1 ←── #24 偏差报告字段 + 版本链 Schema
Trace分析v0.2 ←── Trace增强（compaction spans）
```

### 可并行开发

- #22 tail_report增强版 + #23 存储模型层：与版本链开发并行
- #18/#19/#20 串行，但 #21 和 #24 可并行
- 脚手架五项：与功能组件并行
- 工具四项：与功能组件并行

### 演示场景

```python
# 持续对齐：新数据到来，模型进化
model_v2 = client.update_model("gen_model_v1",
    incorporate_from="sensor_log",
    where="timestamp > '2026-05-01'",
    drift_check="auto",
    save_as="gen_model_v2"
)

# Explain：compaction 影响
plan = client.explain("sensor_log", model_version="gen_model_v1")
# 如果 gen_model_v1 已被 compaction:
#   plan.compaction_warning: {requested: "v1", returned: "v2", reason: "compacted", bias_report_ref: "..."}

# 时间旅行：回到旧版本
result = client.query_as_of("gen_samples", model_version="gen_model_v1")
# 如果 gen_model_v1 已被 compaction 合并，返回偏差报告
```

**输出**：
- 新版本模型（gen_model_v2）
- 时间旅行数据（含偏差报告）
- EvidencePackage（含模型版本链provenance、drift_detection）
- tail_report增强版（将v2已有的误差界数据与data_grade联动呈现，含fidelity_mismatch、代偿模型状态）

### v3 功能验收标准

- [ ] 模型版本链创建、引用、列表正确
- [ ] GC compaction 3保护条件全部生效
- [ ] AS OF 读取正确版本，compaction退化返回偏差报告
- [ ] UPDATE MODEL 新数据纳入，漂移检测工作，代偿收敛时限生效
- [ ] 持续对齐依赖数据引擎v1，模型训练/增量更新正确
- [ ] atomic_write 事务：中断恢复以元数据层状态为准
- [ ] tail_report增强版将v2误差界数据与data_grade联动，含代偿模型状态
- [ ] 偏差报告字段完整，证明链可重建
- [ ] EvidencePackage 中 drift_detection 从 not_applicable 变为填充
- [ ] **定义待测模型接入协议**（v4 反例搜索预研前置条件）

### v3 脚手架验收标准

- [ ] Explain 可显示 compaction 影响预估
- [ ] Trace 可记录持续对齐的模型更新前后变化
- [ ] 可观测性增加模型版本链状态和 GC 历史
- [ ] 错误注入可模拟 compaction 冲突
- [ ] compaction 前后生成结果一致性验证通过

### v3 工具验收标准

- [ ] 组件模板引擎 v0.3 可生成 #18-#24 任一组件的骨架
- [ ] 测试辅助库可辅助编写 compaction 前后一致性测试
- [ ] Schema 校验器能校验版本链相关字段的完整性
- [ ] Trace 分析工具能识别 compaction 冲突导致的版本退化 span（验收测试：构造一次 compaction 退化的 Trace，工具能标红退化 span 并输出退化原因）

---

## v4: 高级分析（4-5周）

**产品故事**：行数窗口、分组聚合、会话切分——窗口类型全面扩展。约束完备度从布尔走向连续评分。

**用户接口**：Python SDK + REST API。SynthLang 为内部编译目标。

### 功能组件清单（明线）

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 25 | 行数窗口(ROWS) | OVER (ROWS 100) 语法+执行+行数滑动窗口聚合 | v2 #11 聚合引擎 | 1周 |
| 26 | 分组时间窗口(PARTITION BY) | OVER (PARTITION BY col, INTERVAL 1 HOUR) 语法+执行+分组聚合 | v2 #11 聚合引擎 | 1周 |
| 27 | 会话窗口(SESSION) | OVER (SESSION BY col, GAP 5 MINUTES) 语法+执行+会话切分 | #25/#26 窗口基础 | 1.5周 |
| 28 | 约束完备度连续化评分 | 0.0-1.0评分+基于已覆盖约束维度加权+布尔判断作为阈值1.0特例+执行路由器联动(评分<1.0时退化路径选择) | v2 #13 执行路由器 | 1周 |
| 29 | **反例搜索(research)** | 研究性里程碑：探索约束域边界+不满足约束的区域可视化。**理论基础待补充**。交付取决于：(1) v2/v3阶段的理论和工程预研 (2) **待测模型接入协议（v3阶段定义）** | v2 #13 路由器 + #14 后筛选 + **待测模型接入协议(v3定义)** | 1.5周 |
| 30 | EvidencePackage v3 | 新增模型版本链provenance+完备度评分+反例区域(如预研成功)+偏差报告引用 | v3 EvidencePackage + #28 | 0.5周 |

### 脚手架组件清单（暗线）

| 脚手架 | v4 交付内容 | 估算 |
|--------|-----------|------|
| Explain 增强 | 完备度评分对各退化路径的影响 | 0.5周 |
| Trace 增强 | 反例搜索的探索轨迹记录 | 0.5周 |

**脚手架工作量**：约0.5人周，与功能组件并行。

### 工具组件清单（工具线）

| 工具 | v4 交付内容 | 估算 |
|------|-----------|------|
| Trace分析工具 v0.3 | 增加反例搜索轨迹分析规则（如预研成功） | 0周（含在功能开发中） |

**工具线工作量**：0（含在功能开发中）。

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

[暗线并行]
Explain增强 ←── #28 完备度评分
Trace增强 ←── #29 反例搜索(如预研成功)

[工具线]
Trace分析v0.3 ←── #29 反例搜索轨迹(如预研成功)
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

# Explain：完备度影响
plan = client.explain("sensor_log", constraints=["wind_safety"])
# plan.completeness_score: 0.75
# plan.path: constrained_fusion (score >= threshold)
# plan.degradation_risk: "if_score_below_0.5→KDE_perturbation"
```

**输出**：
- 多窗口类型约束执行结果
- 约束完备度评分（0.0-1.0）
- EvidencePackage v3（含完备度+版本链provenance）
- 反例区域报告（如预研成功）

### v4 功能验收标准

- [ ] ROWS/PARTITION BY/SESSION 三种窗口语法解析正确
- [ ] 窗口聚合计算正确（边界、分组、会话切分）
- [ ] 完备度评分 0.0-1.0，布尔判断作为1.0特例
- [ ] 评分<1.0时路由器选择正确退化路径
- [ ] EvidencePackage v3 包含完备度评分字段
- [ ] 反例搜索：如预研成功，返回不满足约束的区域；如预研未完成，明确标记为 deferred

### v4 脚手架验收标准

- [ ] Explain 可显示完备度评分对各路径的影响
- [ ] Trace 可记录反例搜索的探索轨迹（如预研成功）

### v4 工具验收标准

- [ ] 全套辅助工具在里程碑评审前可自动扫描文档与代码一致性（Schema 校验器 + 手动触发文档一致性检查）
- [ ] Trace 分析工具能分析反例搜索的探索轨迹（如预研成功）

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
│  ─ ─ [暗线] Explain最小 / Trace最小 / 可观测性最小 / 测试框架 / CI/CD
│  ─ ─ [工具线] 组件模板引擎v0.1(第4周起) / 测试辅助库v0.1(第6周起)
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
│    ─ ─ [暗线] Explain增强 / Trace增强 / 可观测性增强 / 错误注入 / 测试增强
│    ─ ─ [工具线] 模板引擎v0.2 / 测试辅助v0.2 / Schema校验器v1.0 / Trace分析v0.1
│
├──→ v3 时间智能
│    #18 模型版本链 ←── v1#4
│    #19 GC compaction ←── #18
│    #20 时间旅行 ←── #18 + #19
│    #21 持续对齐 ←── #18 + v2#13 + v2#15b数据引擎
│    #22 tail_report增强版 ←── v2#14 + #21
│    #23 存储模型层 ←── v1#4
│    #24 偏差报告 ←── #19 + #18
│    ─ ─ [暗线] Explain增强 / Trace增强 / 可观测性增强 / 错误注入增强 / 测试增强
│    ─ ─ [工具线] 模板引擎v0.3 / 测试辅助v0.3 / Schema校验器v1.1 / Trace分析v0.2
│
└──→ v4 高级分析
     #25 行数窗口 ←── v2#11
     #26 分组时间窗口 ←── v2#11
     #27 会话窗口 ←── #25/#26
     #28 完备度评分 ←── v2#13
     #29 反例搜索(research) ←── v2#13 + v2#14 + 待测模型接入协议(v3定义)
     #30 EvidencePackage v3 ←── #28 + #29
     ─ ─ [暗线] Explain增强 / Trace增强
     ─ ─ [工具线] Trace分析v0.3
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
- v1.3：脚手架暗线保证可追溯性从 v1 起有 Trace span 落地
- v1.4：工具线保证脚手架惯例从 v1 中后期起有模板引擎落地

---

## 三线分版本交付总表

| 版本 | 功能组件(明线) | 脚手架组件(暗线) | 工具组件(工具线) | 功能估算 | 含全部估算 |
|------|--------------|----------------|----------------|---------|-----------|
| v1 | 9 | 5 | 2 | 7-8周 | 8.5-9.5周 |
| v2 | 9 | 5 | 4 | 10-11周 | 12-13周 |
| v3 | 7 | 5 | 4 | 4-5周 | 6-7周 |
| v4 | 6 | 2 | 1 | 4-5周 | 4-5周 |

**总计**：31个功能组件 + 14个脚手架组件 + 6个工具组件（去重后），29-33周

---

## 人员假设

基于 aboutme.md：专业数据库团队，C/C++/Rust 技术栈。

- **Parser + 类型系统**：1人（编译原理背景）
- **存储引擎**：1-2人（存储/文件系统背景）
- **约束引擎 + 路由器**：1-2人（数据库执行引擎背景）
- **数据引擎(KDE)**：1人（统计/ML背景，熟悉核密度估计）
- **SDK + API**：1人（应用层）
- **脚手架 & 基础设施 & 工具线**：1人（可与SDK开发者复用，Python/C++双栈）
- **总团队**：4-6人

v1 最小可运行阶段，2-3人即可并行（Parser+存储→物理引擎+SDK+脚手架+工具）。
v2 需要数据引擎专人，建议 4-5人。

**工具线技术栈说明**：

| 工具 | 实现语言 | 理由 |
|------|---------|------|
| 组件模板引擎 | inja(C++头文件库) 或 Jinja2(Python) | 模板展开，无AI调用。如果团队接受Python工具链则用Jinja2（生态更成熟），否则用inja（零依赖） |
| 测试辅助库 | C++ | 头文件库，与Google Test集成，无额外依赖 |
| Schema一致性校验器 | Python | CI脚本，比对JSON/YAML，Python生态最成熟 |
| Trace分析工具 | Python | 独立工具，解析JSON，规则引擎，Python最合适 |

Python 工具（校验器、Trace分析）需要团队接受 Python 工具链。C++ 团队维护 Python 脚本的认知负担需纳入风险。

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
| v2 数据引擎延迟导致5路径无法全测 | v2 集成测试延迟 | **工程执行注记**：v2 早期迭代先测3条不依赖数据引擎的退化路径（纯物理/统计生成/KDE扰动占位），数据引擎就位后补全5条路径集成测试 |
| **v1 前期无模板素材** | **工具线延迟** | **v1 前3周工具线空白是正常的。第4周起从已完成组件提炼模板。不强制提前引入** |
| **Python工具链与C++团队的认知负担** | **维护成本** | **Python工具限于CI脚本和独立工具（校验器、Trace分析），不涉及产品代码。模板引擎优先用inja(C++)** |
| **工具随接口变化而过时** | **维护成本** | **工具模板与代码库一起版本控制。Schema变更时校验器规则同步更新。每个版本的脚手架验收标准隐含工具可用性验证** |

---

文档结束
