SynthGen Core 工程框架 v0.2
生成原生数据库（Generation-Native Database）
文档版本：v0.2
上一版本：v0.1（已驳回）
修订性质：根本性架构重构
核心决策：不寄生在任何现有数据库上。继承数据库范式的核心智慧（声明式接口、不可变日志、版本化存储），但以生成操作为一等公民重新设计全部内核。

一、架构总览
text
                     用户 / SDK / 应用
                           │
                           ▼
┌──────────────────────────────────────────────────┐
│                  接口层                           │
│  SynthLang 解析器    │    类型/Schema 系统        │
└───────────────────────┬──────────────────────────┘
                        │
┌───────────────────────┴──────────────────────────┐
│                  生成引擎                         │
│  执行路由器    │   后筛选运行时保障   │  引擎适配层 │
│  (退化路径)   │   (排除率/超时控制)  │ (物理/数据/约束) │
└───────────────────────┬──────────────────────────┘
                        │
┌───────────────────────┴──────────────────────────┐
│                  存储引擎                         │
│  基表层 (INSERT ONLY)    快照层 (不可变)          │
│  模型层 (版本链+GC)      审计日志 (哈希链+WORM)   │
└──────────────────────────────────────────────────┘
二、接口层：SynthLang
2.1 设计原则
借鉴SQL的声明式语义，但关键字和语义完全为生成场景定制

不声称与任何SQL方言兼容

Schema强制：输入数据的类型定义即Schema

物理约束以命名卡片形式定义，可组合、可版本化

2.2 核心语法
synthlang
-- Schema定义（从输入数据自动推断，用户可修改）
DEFINE TYPE sensor_log {
    timestamp: DATETIME NOT NULL,
    wind_speed: FLOAT [0.0, 50.0],
    temperature: FLOAT [-50.0, 80.0],
    vibration: FLOAT,
    status: ENUM('normal', 'warning', 'fault')
};

-- 导入数据
LOAD DATA INTO sensor_log FROM '/data/sensors/*.parquet';

-- 定义约束卡片（可命名、可组合、可版本化）
DEFINE CONSTRAINT wind_safety ON sensor_log {
    wind_speed BETWEEN 0 AND 25 DURING 'normal_operation',
    temperature > -40 WHEN wind_speed > 20
};

-- 生成数据
GENERATE TABLE gen_samples
FROM sensor_log
WITH CONSTRAINTS wind_safety
MODE = constrained (fallback = physics_only)
LIMIT 1000
INCLUDE tail_report;

-- 持续对齐
UPDATE GENERATION MODEL gen_model_v1
INCORPORATE DATA FROM sensor_log
WHERE timestamp > '2026-05-01'
WITH DRIFT_CHECK = auto
SAVE AS gen_model_v2;

-- 时间旅行
SELECT * FROM gen_samples
AS OF MODEL VERSION gen_model_v1;
三、生成引擎
3.1 执行路由器
条件	路径	说明
约束完备 + 数据充足 + 融合可用	全功能	物理提供约束域，数据生成分布，融合后筛选
约束完备 + 数据充足 + 融合不可用	后筛选	数据全量生成，约束引擎过滤
约束完备 + 数据不足	纯物理	物理引擎在约束域内采样
约束不完备 + 数据充足	统计生成	身份切换：统计相似生成器
约束不完备 + 数据不足	退化	KDE扰动 + 格式校验。身份切换：格式化扰动生成器
3.2 后筛选运行时保障
后筛选使用拒绝采样，面临排除率爆炸风险。以下机制在运行时管理此风险：

编译阶段预估算：利用约束域与估计分布的体积比预估排除率。预估 > 0.9 时，拒绝走后筛选路径，强制纯物理或要求用户放宽约束。

运行时保障表：

机制	触发条件	行为
超时截断	生成超过用户指定超时	返回已生成部分 + 排除率报告 + data_grade: truncated
行数下限	合法行数 < 请求行数的10%	同超时截断
排除率实时监控	实际排除率持续上升	通知用户当前进度和预估完成时间
排除率与 data_grade 联动：

排除率	data_grade	含义
0-30%	physics_guaranteed	约束有效，生成高效
30-70%	statistics_guaranteed	约束与分布有中度假定差异
70-90%	limited_fidelity	效率显著下降
>90%	拒绝生成	要求用户调整或切换模式
3.3 引擎适配层
三个引擎各自独立部署和扩展，生成引擎通过统一接口调用：

引擎	输入	输出
物理引擎	约束卡片、采样参数	数据行（满足约束）
数据引擎	训练数据表、生成数量、seed	数据行（统计相似）
约束引擎	候选数据行、约束卡片	过滤后数据行 + 排除报告
3.4 生成引擎与数据引擎之间的协议
协议项	定义
请求格式	{model_id, num_samples, seed, batch_size, output_columns}
返回方式	流式返回（batch迭代器），非一次性全量
进度报告	每个batch返回 {batch_index, rows_generated, elapsed_ms}
背压机制	约束引擎处理速度低于生成速度时，暂停数据引擎batch请求
错误处理	模型推理失败时，数据引擎返回 {error, batch_index}，生成引擎决定回退或重试
四、存储引擎
4.1 存储分层
text
审计日志层: 哈希链不可变追加 | WORM | 可独立验证
快照层:    生成数据 + provenance + tail_report | 写入后不可变
模型层:    参数/检查点 | 版本链 | GC策略
基表层:    原始输入 (INSERT ONLY) | 约束卡片定义
4.2 审计日志
与WAL彻底分离。审计日志是独立的不可变记录系统。

记录格式：

text
{
  "sequence": 1042,
  "timestamp": "2026-05-09T14:32:17Z",
  "actor": {"component": "ingestion", "caller_id": "user_abc"},
  "operation": "LOAD_DATA",
  "target": "sensor_log",
  "parameters": {"source": "/data/20260509.parquet"},
  "prev_hash": "a3f2b8c1...",
  "current_hash": "SHA256(this_record_without_current_hash)"
}
不可变性保证：

每条记录的 current_hash 覆盖除自身外的所有字段

每条记录的 prev_hash 指向前一条，形成链

存储底层使用WORM兼容的对象存储

审计链可被独立工具验证，不依赖数据库进程

基表层只允许INSERT：拒绝UPDATE/DELETE。表分区按月组织，旧分区设为只读。

4.3 模型版本GC
规则	说明
默认保留数	最近N个版本（N可配置，默认10）
锚定保护	用户标记为"锚定"的版本永不回收
快照依赖保护	被现有快照引用的版本不回收
Compaction	连续微版本可在用户许可后合并。合并后保留元数据（数据范围、指标、hash），微版本参数可物理删除
物理删除前提	未被锚定 + 未被快照引用 + 元数据已继承
五、约束系统
5.1 两层约束架构
SQL CHECK只能处理单列范围和简单跨列条件。复杂约束需要独立的约束描述和验证层。

约束层次	承载形式	覆盖类型
层次一：值域约束	类型系统 + CHECK等价逻辑	单列范围、枚举、NOT NULL
层次二：复杂约束	SynthLang CONSTRAINT块 + 约束验证器	跨列条件、时序约束、聚合约束、物理方程
层次二约束示例：

synthlang
DEFINE CONSTRAINT temporal_safety ON sensor_log {
    timestamp MONOTONIC INCREASING,
    ABS(vibration[t] - vibration[t-1]) < 5.0,
    AVG(temperature) OVER (INTERVAL 1 HOUR) <= 40.0
};
约束验证器在执行时按顺序检查：先层次一（类型系统自动执行），再层次二（约束验证器逐条执行）。失败的数据行被排除并记录在tail_report中。

六、三大模块间的内部协议
6.1 接口层 → 生成引擎：查询协议
协议项	定义
请求格式	解析后的语法树（AST），包含操作类型、目标表、约束引用、模式、限制参数
错误传播	解析错误、类型错误、约束引用不存在 → 立即返回错误，不进入生成引擎
语义检查	Schema校验、约束存在性检查在接口层完成，不传递无效请求
6.2 生成引擎 ↔ 存储引擎：数据协议
协议项	定义
读基表	流式读取，列式格式（Apache Arrow），批量大小可配置
写快照	一次性写入，包含数据+provenance+tail_report，写入后不可变
读模型	按需加载模型检查点，支持mmap和流式加载两种模式
写模型	新版本写入后不可变，触发GC检查
流控	存储引擎可返回背压信号，暂停数据传输
6.3 生成引擎 ↔ 数据引擎：生成协议
同3.4节定义。

七、与API+微服务方案的公平对比
维度	生成原生数据库方案	API+微服务方案
核心功能适配	✅ 生成是一等公民，语法和引擎为生成设计	⚠️ 生成隐藏在POST /generate后，无语言层表达力
Schema约束	✅ 类型系统和SynthLang双重强制	⚠️ 需自行实现校验层
约束描述能力	✅ 独立约束卡片语言，超越SQL CHECK	⚠️ 需自行设计约束DSL
版本管理	✅ 内建模型版本链和快照时间旅行	⚠️ 需自行设计
审计不可变	✅ 哈希链审计日志，独立于运行状态	⚠️ 需自行设计
部署	独立二进制，无外部依赖	标准Docker容器
开发速度	需自研接口层和部分基础设施	标准Web开发，初期更快
人才可得性	需理解系统设计的开发者	标准后端开发者，更易招聘
存储后端灵活性	可插拔（PG/Iceberg/对象存储）	每个服务独立选择
八、暂不承诺与理论框架的时效性对齐
暂不承诺项	对理论框架的影响	缓解措施
第一版不引入流式实时生成	持续对齐为批量模式，漂移检测延迟 = 批量间隔	最小批量间隔定义为1小时。低于此频率，漂移检测声明为 degraded，系统以标记 drift_detection_lag 运行
第一版不引入分布式/水平扩展	无理论冲突	单机部署可满足初期场景
最小可行对齐周期：1小时。低于1小时的持续对齐属于后续版本。

九、与理论框架的对应关系
理论框架要求	工程实现位置
输入诚实性（层次一：格式忠实性）	接口层类型系统
输入诚实性（层次二：统计签名条件保证）	EvidencePackage构建器
物理优先	执行路由器退化路径判定
物理优先认识论偏差声明	tail_report
后筛选排除率阈值	生成引擎运行时保障
漂移/演化区分+身份联动	执行路由器 + 约束完备性检查
失败模式一：基准冲突	接口层Schema校验，硬拒绝
失败模式二：约束真空	约束验证器，降级标记
失败模式三：模仿崩溃	执行路由器，自动回退纯物理
约束缺失身份切换+服务分级	执行路由器 + data_grade标记
新数据来源边界	基表层LOAD DATA的data_origin校验
审计不可变	哈希链审计日志 + WORM
可追溯性	EvidencePackage provenance + 模型版本链
十、自研范围与复用边界
自研（产品核心）	复用（不重新发明）
SynthLang解析器和类型系统	Apache Arrow（列式内存格式）
执行路由器和退化路径逻辑	Apache Parquet（列式持久化格式）
后筛选运行时保障机制	对象存储（WORM合规保留）
约束卡片语言和验证器	可选：PostgreSQL（仅作为基表存储后端之一）
引擎适配层（物理/数据/约束）	可选：Apache Iceberg（快照管理）
EvidencePackage构建器	
哈希链审计日志	
模型版本管理和GC	
文档结束

工程框架 v0.2 完成。继承数据库范式的核心智慧，但不寄生在任何现有数据库上。生成操作为一等公民，全部内核为生成场景自研设计。