SynthGen Core Agent 辅助开发建议书

> **处理状态：已归档**
> 本建议书的核心建议已被路线图 v1.4 采纳（工具线），并做了三个修正：
> 1. 诚实命名——5个"Agent"中4个不需要AI，修正为模板引擎/规则引擎等确定性工具
> 2. 调整时机——v1前期不可能有这些工具（依赖v1正在建设的基础设施）
> 3. 务实验收——用工具标准而非AI Agent标准验收
> 归档日期：2026-05-10

文档性质：路线图补充——开发工具链中的AI编程辅助
版本：v1.0
提交对象：路线图团队 / 工程团队
提交日期：2026-05-10
背景：路线图v1.3已完成，脚手架工程已纳入。本建议书讨论如何在开发过程中利用Agent编程提升效率和质量。

一、定位：Agent是脚手架之上的工具
脚手架工程定义了六类设施（Explain、Trace、可观测性、测试框架、CI/CD、错误注入）——这些是产品的一部分。

Agent编程辅助是开发过程中的工具。它不产生最终交付物，但可以加速以下环节：

环节	Agent 的角色
测试用例生成	基于组件接口描述，自动生成边界条件测试和回归用例
文档同步检查	检查代码接口是否与EvidencePackage Schema、理论框架承诺一致
代码审查辅助	检查新PR是否违反设计约束（如不可变性、字段适用性）
错误诊断	基于Trace span链，辅助定位跨组件错误的根因
Scaffold代码生成	为每个新组件生成标准化的span输出、metrics暴露、Explain接口骨架
二、核心原则
2.1 Agent 不写产品代码
生成数据的约束验证逻辑、后筛选排除率计算、哈希链验证——这些核心逻辑必须由人编写。Agent 辅助的是周边工作：测试、文档、诊断、骨架代码、一致性检查。

2.2 Agent 的产出必须可验证
任何Agent生成的内容（测试用例、文档、诊断结论），必须有明确的验证步骤。测试用例必须能跑并通过。诊断结论必须能追溯到具体的span或metrics数据点。

2.3 Agent 的开发规范必须文档化
每个Agent的能力边界、输入格式、输出格式、已知盲区，必须有文档。新团队成员需要知道"这个Agent能做什么、不能做什么、什么时候应该怀疑它的输出"。

三、建议引入的Agent类型
3.1 Contract Validator
职责：验证代码实现是否与EvidencePackage Schema v1.2、理论框架v1.3的承诺一致。

输入：组件的接口定义（函数签名、返回值结构）+ EvidencePackage Schema
输出：不一致报告（如：字段缺失、枚举值不匹配、适用性标记缺失）

适用阶段：每个组件完成时、PR审查时

3.2 Test Case Generator
职责：基于组件接口描述和退化路径表，自动生成回归测试用例。

输入：组件接口 + 退化路径条件表 + 标准测试数据集
输出：测试用例代码（含给定输入→预期输出）

适用阶段：新组件接口定义完成时（测试先行开发）

3.3 Trace Diagnostician
职责：基于Trace span链，辅助诊断失败请求的根因。

输入：失败请求的EvidencePackage中的trace spans（JSON格式）
输出：异常span定位 + 可能原因列表 + 建议的排查方向

适用阶段：集成测试、端到端测试中请求失败时

3.4 Consistency Checker
职责：检查路线图、理论框架、工程框架、EvidencePackage Schema 四份文档之间的一致性。

输入：四份文档
输出：不一致报告（如：路线图承诺的字段在Schema中缺失，理论框架要求的偏差声明在路线图中未出现）

适用阶段：里程碑评审前、文档更新后

3.5 Scaffold Generator
职责：为新组件生成标准化的脚手架代码骨架。

输入：组件接口定义 + 脚手架规范（span格式、metrics命名、Explain结构）
输出：组件代码骨架（含span输出、metrics注册、Explain接口占位）

适用阶段：新组件开发启动时

四、与脚手架工程的对应关系
Agent	对应的脚手架设施	角色
Contract Validator	确定性测试框架	确保脚手架验证的基准本身正确
Test Case Generator	确定性测试框架 + CI/CD	加速测试用例扩充
Trace Diagnostician	Trace	增强Trace的可操作性
Consistency Checker	—（跨文档）	保证路线图、理论、工程、Schema四层一致
Scaffold Generator	Explain + Trace + 可观测性	降低脚手架实现的重复劳动
五、每个版本引入的Agent
版本	引入	理由
v1	Test Case Generator + Scaffold Generator	v1组件最多（9个功能+5个脚手架），生成骨架和测试用例的重复劳动最多
v2	Contract Validator + Trace Diagnostician	v2执行路由器重构后路径最复杂（5条退化路径），追踪诊断和Schema验证需求最高
v3	全部增强	持续对齐和compaction退化逻辑最复杂，诊断和一致性检查需求最高
v4	全部	反例搜索研究性工作，一致性检查防止承诺与实际不符
六、与其他工具的关系
Agent辅助不是推倒CI/CD重建一套东西。它在已有的开发流程（PR→CI→Review→Merge）中嵌入：

PR 提交 → CI 触发 → 跑全量测试 + Contract Validator 检查 Schema 一致性

测试失败 → Trace Diagnostician 辅助定位

新组件开发启动 → Scaffold Generator 生成骨架，开发者在骨架上填充逻辑

里程碑评审前 → Consistency Checker 检查四份文档一致性

Agent 是工具链的增强，不是工具链的替代。

七、注意事项
7.1 Agent 的边界声明

每个Agent在输出时必须附带一个边界声明。例如，Contract Validator 的输出应附带：

scope: validates against EvidencePackage Schema v1.2 and Theory Framework v1.3. does not check SynthLang grammar compliance. does not check performance characteristics.

7.2 不自动修复

Agent 只报告不一致，不自动修复代码。自动修复在核心逻辑上不可接受——它可能"修"出物理合法性逻辑的错误。

7.3 Agent 本身也是软件

Scaffold Generator 生成的骨架代码必须能通过CI。Test Case Generator 生成的测试用例必须能在标准测试数据集上通过。Agent 自身的质量由这些可验证的结果来保证。

八、对路线图的嵌入建议
Agent编程辅助不作为独立版本或独立里程碑。它是每个版本脚手架工程的增强工具。

可在路线图v1.3的脚手架工程章节后增加一小节：

Agent 辅助开发（可选增强）：列明每个版本建议引入的Agent类型及其角色。Agent 不写产品代码，只辅助测试、诊断、一致性检查和骨架生成。核心逻辑由人编写，Agent促进效率。

建议纳入路线图v1.4（如需要）。 如果路线图v1.3已经封闭，此建议书可作为《工程执行手册》的附录。