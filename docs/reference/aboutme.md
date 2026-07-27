“模拟一切”（SynthGen Core）：是一个独立的数据生成产品/功能。它有自己完整的内部架构（TaskReceiver、Generator Engine、Packager），有能力自主进化成一个强大的合成数据引擎。
• Polymorphic-Twin：是一个数字孪生可信治理基础设施。它有自己独立的使命——回答“什么可信、什么不可信”，执行约束治理、资格审判、安全回落。
两者关系是：独立产品 + 标准协议的配合关系，而不是一个吞噬另一个的包含关系。
以下我们基于这个清晰定位，重新梳理两者之间的边界、协议和协同。
￼
一、各自独立的核心定位
￼
￼
维度
SynthGen Core
Polymorphic-Twin
核心使命
生成高质量、多样、可追溯的模拟数据
数字孪生的可信治理
回答的问题
"我能制造什么样的数据？"
"现在什么可信、什么不可信？"
运行哲学
最大化数据的覆盖、多样、真实感
最小化不可信输出的风险
独立性
可以独立部署、独立销售、独立服务客户
可以独立部署，对接任意数据源，不依赖特定生成器
￼
二、协作接口：标准协议，而非侵入式集成
SynthGen Core与Polymorphic-Twin通过标准化的EvidencePackage协议配合。这意味着：
SynthGen Core的配合承诺：
• 它的Packager模块，输出格式符合Polymorphic-Twin定义的EvidencePackage Schema（manifest + dataset + summary + provenance）
• 它为每个数据包提供完整的生成元数据（生成器版本、参数快照、随机种子、约束自检报告）
• 它不假设Core会如何判决它的产出——它只是交付证据
Polymorphic-Twin的配合承诺（对SynthGen Core而言）：
• Core的检疫端点（/api/v1/core/quarantine/*）是标准入口，不关心EvidencePackage来自哪个生成器，只要格式合规
• Core会对入检数据进行硬门槛检验（格式完整性、基本物理一致性）
• Core会为通过检疫的数据分配链路等级（sandbox/shadow/diagnostic/production）
这种设计的好处：
• SynthGen Core可以独立迭代自己的LLM编译器、PINN生成器、反例搜索策略，不需要Polymorphic-Twin审批
• Polymorphic-Twin可以对接多个生成器（SynthGen Core是一个，未来可能有第三方合成数据引擎），不需要为每个生成器定制逻辑
• 两者可以独立上市、独立定价、独立服务不同客户群
￼
三、服务协同场景
场景A：两者配套部署（完整方案）
客户同时部署Polymorphic-Twin和SynthGen Core。工作流是：
1. 领域专家通过DomainPack定义场景约束
2. 专家向SynthGen Core下达生成指令（自然语言或结构化spec）
3. SynthGen Core在隔离环境中生成EvidencePackage
4. EvidencePackage自动提交至Polymorphic-Twin的Core进行检疫
5. 通过检疫的数据进入TwinObjectModel，供Bridge在生成行动空间时参考
这是最完整的价值链：从数据生成到数据信任，形成闭环。
场景B：SynthGen Core独立部署
客户只需要一个智能合成数据工具，没有Polymorphic-Twin。工作流是：
1. 用户定义场景参数（手动，或通过LLM编译器）
2. SynthGen Core生成数据
3. 数据直接输出为标准化文件（EvidencePackage格式，但无人检疫）
4. 用户自行使用这些数据训练模型、测试系统
此时的价值：用户获得了一个带物理约束、能自我检查的智能数据生成器。即使没有Core来审判，Packager内置的物理自检仍能过滤掉明显违反物理定律的样本。
场景C：Polymorphic-Twin对接其他生成器
客户已有其他合成数据来源（如从DataMesh、NVIDIA Omniverse自建管道）。Polymorphic-Twin不强制绑定SynthGen Core。
1. 客户的生成器产出数据
2. 客户自行开发一个轻量适配器，将数据包装为EvidencePackage格式
3. 提交至Core的检疫端点
4. Polymorphic-Twin的治理体系照常运作
此时的价值：Polymorphic-Twin成为“合成数据的中立仲裁者”，不绑定特定生成器生态。
￼
四、需要共同遵守的协议清单
为了实现这种松耦合配合，两者必须对以下协议达成一致：
￼
￼
协议层
内容
责任方
EvidencePackage Schema
数据包的结构定义（字段、类型、必选项）
Polymorphic-Twin定义，SynthGen Core遵守
CallerIdentity格式
调用者身份令牌的格式和权限等级
Polymorphic-Twin定义
约束卡片格式
DomainPack中物理约束的声明格式
Polymorphic-Twin定义，SynthGen Core可在生成阶段引用（做自检）
生成元数据标准
每个数据包的provenance字段应包含什么
建议由SynthGen Core提出草案，Polymorphic-Twin确认
数据版本溯源码
数据与生成器版本、DomainPack版本之间的关联标记
双方协商
这些协议是两者之间唯一的正式耦合点。除此之外，它们各自独立演进。
￼
五、演进独立性保障
为了确保SynthGen Core能独立作为产品销售，同时Polymorphic-Twin不受其绑架，建议：
SynthGen Core侧：
• 提供两种输出模式：① 标准EvidencePackage（供Polymorphic-Twin消费）② 通用开放格式（HDF5、Parquet、ROS bag等，供其他系统消费）
• 不硬编码任何Polymorphic-Twin的内部逻辑（如链路等级的判定标准），因为那是Core的职责
Polymorphic-Twin侧：
• 检疫逻辑只基于EvidencePackage的格式和DomainPack的约束卡片，不依赖“数据来自SynthGen Core”这一假设
• 不要求EvidencePackage中包含SynthGen Core特有的字段（如LLM生成的语义标注），这些可以作为可选扩展字段
￼
这个定位清晰了：SynthGen Core和Polymorphic-Twin是两个独立的系统，通过标准协议握手。一个负责制造数据，一个负责审判数据。两者可以独立发展、独立销售、独立服务客户，也可以组合成完整方案。