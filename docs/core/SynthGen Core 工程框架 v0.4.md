SynthGen Core 工程框架 v0.4
生成原生数据库
文档版本：v0.4
上一版本：v0.3
修订性质：执行正确性修复 + 质量提升
核心改进：聚合两阶段值域约束不遗漏、行间约束排序列绑定、窗口类型扩展预留、DURING 语法绑定、协议错误处理

一、架构总览
text
用户 / SDK / 应用
           │
           ▼
┌─────────────────────────────────────────┐
│              接口层                      │
│  SynthLang 解析器 (自研 parser 扩展)     │
│  类型/Schema 系统 (DDL)                  │
│    └─ 默认排序列声明                     │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────┴─────────────────────┐
│              生成引擎                    │
│                                         │
│  约束分类器 (编译阶段)                   │
│  ├─ 值域约束 → 逐行路径（最高优先级）     │
│  ├─ 行间约束 → batch 有状态路径          │
│  └─ 聚合约束 → 两阶段路径（阶段一含值域） │
│                                         │
│  执行路由器 (退化路径 + 约束类型矩阵)     │
│  后筛选保障 (排除率预估 + 超时/行数截断)  │
│  引擎适配层 (物理/数据/约束引擎)          │
│                                         │
│  EvidencePackage 构建器                  │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────┴─────────────────────┐
│              存储引擎                    │
│                                         │
│  存储抽象层 (StorageBackend 接口)        │
│  ├─ atomic_write 事务语义               │
│  └─ 错误传播协议                        │
│  基表层 (INSERT ONLY)                    │
│  快照层 (不可变)                         │
│  模型层 (版本链 + GC 自动 compaction)     │
│  审计日志 (哈希链 + WORM)                │
└─────────────────────────────────────────┘
二、接口层：SynthLang
2.1 设计原则
自研 parser，不模仿任何现有 SQL 方言

Schema 强制：类型系统是 DDL 的一等公民。排序列在 Schema 级别声明，避免每个约束卡片重复指定

物理约束以命名卡片形式定义，可组合、可版本化

关键字和语义完全为生成场景定制

2.2 核心语法
synthlang
-- Schema 定义（ORDER 关键字声明默认排序列）
DEFINE TYPE sensor_log {
    timestamp: DATETIME NOT NULL ORDER,
    wind_speed: FLOAT [0.0, 50.0],
    temperature: FLOAT [-50.0, 80.0],
    vibration: FLOAT,
    status: ENUM('normal', 'warning', 'fault')
};

-- 导入数据
LOAD DATA INTO sensor_log FROM '/data/sensors/*.parquet';

-- 定义约束卡片
DEFINE CONSTRAINT wind_safety ON sensor_log {
    -- 值域约束
    wind_speed BETWEEN 0 AND 25 DURING status = 'normal_operation',
    temperature > -40 WHEN wind_speed > 20,
    -- 行间约束（排序列已在 Schema 中声明）
    vibration[t] - vibration[t-1] < 5.0,
    -- 聚合约束（v1 支持 INTERVAL；v2 扩展 ROWS、PARTITION BY）
    AVG(temperature) OVER (INTERVAL 1 HOUR) <= 40.0
};

-- 生成
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
2.3 窗口类型扩展预留
窗口类型	语法	支持版本
时间窗口	OVER (INTERVAL 1 HOUR)	v1
行数窗口	OVER (ROWS 100)	v2
分组时间窗口	OVER (PARTITION BY col, INTERVAL 1 HOUR)	v2
会话窗口	OVER (SESSION BY col, GAP 5 MINUTES)	v3
v1 仅实现时间窗口。语法解析器预留其余关键字，不报错但标记 unsupported_in_v1。

三、约束系统：三类执行模型
3.1 约束类型分层
层次	约束类型	示例	执行模型	执行优先级
层次一	值域约束	wind_speed BETWEEN 0 AND 25	逐行检查	第一优先
层次二a	行间约束	vibration[t] - vibration[t-1] < 5.0	batch 内有状态检查	第二优先
层次二b	聚合约束	AVG(temperature) OVER 1 HOUR <= 40.0	两阶段执行	最终阶段
核心原则：值域约束是最底层约束。任何执行路径中，值域约束必须最先执行，不可被跳过。

3.2 三类约束的执行语义
类型一：值域约束

执行方式：逐行检查，与前后行无关

执行优先级：最高。在所有其他约束之前执行

依赖：无跨行依赖

并行能力：无限制

类型二：行间约束

执行方式：batch 内按 Schema 声明的 ORDER 列排序后逐行检查

排序列来源：Schema 定义中 ORDER 关键字声明的列。无需在每个约束卡片中重复指定

状态管理：引擎维护 frame buffer，保存前一行数据

batch 边界处理：batch N 的第一行与 batch N-1 的最后一行继续校验。生成引擎负责跨 batch 状态传递

并行限制：数据引擎禁止并行生成。所有 batch 必须顺序交付

DURING column = value 语义：约束仅在指定列等于指定值时生效

WHEN condition THEN constraint 语义：条件为逐行布尔表达式。条件满足时约束生效，不满足时跳过检查

类型三：聚合约束

执行方式：两阶段

阶段一：逐行过滤值域约束。如有行间约束，在阶段一也完成。不是"无约束过滤"
阶段二：按窗口聚合，检查聚合约束。不满足约束的窗口内数据行全部排除
两阶段不等于放弃值域约束。 值域约束必须在阶段一执行，避免越界值污染聚合计算

后筛选粒度从"行"变为"窗口"

排除率含义变化：不是"丢弃的行占比"，而是"丢弃的窗口占比"

最后一个不满窗口的数据返回但不参与聚合约束检查（标记 partial_window_excluded）

3.3 编译阶段的约束分类
text
编译阶段约束分类逻辑:
  1. 识别所有值域约束 → 标记为 PHASE_ONE，最高优先级
  2. 识别所有行间约束 → 标记为 PHASE_ONE（如有），或有状态执行
  3. 识别所有聚合约束 → 标记为 PHASE_TWO
  
  IF 存在聚合约束:
    阶段一执行 PHASE_ONE 标记的全部约束（值域 + 行间）
    阶段二执行 PHASE_TWO 标记的聚合约束
  ELIF 存在行间约束:
    batch 有状态执行，逐行检查值域 + 行间
  ELSE:
    逐行检查值域约束
3.4 约束类型与执行路由器联动
约束完备性	数据充足性	约束类型	执行模式	阶段一	阶段二
完备	充足	仅值域	逐行过滤	值域过滤	—
完备	充足	含行间	顺序生成 + 跨 batch 状态	值域+行间逐行检查	—
完备	充足	含聚合	两阶段	值域+行间逐行过滤	窗口聚合验证
完备	不足	任意	纯物理	物理引擎采样	—
四、生成引擎
4.1 执行路由器
条件	路径	说明
约束完备 + 数据充足 + 融合可用	全功能	约束分类决定执行模式
约束完备 + 数据充足 + 融合不可用	后筛选	拒绝采样，排除率受监控
约束完备 + 数据不足	纯物理	物理引擎在约束域内采样
约束不完备 + 数据充足	统计生成	身份切换：统计相似生成器
约束不完备 + 数据不足	退化	KDE 扰动。身份切换：格式化扰动生成器
约束完备度连续化说明：

当前 v1 用布尔值判断（完备/不完备）。v2 计划引入约束完备度评分（0.0-1.0），基于已覆盖约束维度的加权比例。v1 的布尔判断作为完备度评分在阈值 1.0 时的特例。

4.2 后筛选运行时保障
编译阶段预估算：

采用数据库优化器标准做法：基于直方图/采样的 selectivity 估计 + 保守偏向

设计偏向声明：宁可高估排除率，不可低估。预估排除率 > 0.8 时拒绝走后筛选路径，强制纯物理或要求用户放宽约束

体积比无法计算时的回退策略：

场景	回退
约束域非凸	保守估计：视为排除率 > 0.8
数据分布多模态	对每个模态分别估计，取最保守值
高维空间	逐维度估计取并集。超半维度无法估计 → 视为 > 0.8
条件约束	条件成立区间的体积比单独计算
运行时保障：

机制	触发条件	行为
超时截断	超过用户指定超时	返回已生成部分 + 排除率报告 + data_grade: truncated
行数下限	合法行数 < 请求行数 10%	同超时截断
排除率实时监控	实际排除率持续上升	通知当前进度和预估完成时间
排除率与 data_grade 联动（含误差界）：

排除率	data_grade	双变量相关误差界	自相关误差界	说明
0-30%	physics_guaranteed	±0.2	±1 lag	理论原始承诺
30-70%	statistics_guaranteed	±0.4	±2 lag	排除率中等，误差界放宽
70-90%	limited_fidelity	不保证	不保证	条件保证降级
>90%	拒绝生成	—	—	不走后筛选路径
EvidencePackage 同时包含：理论误差界（查表） + 实际测量误差（生成后与训练数据对比）。偏差 > 0.1 时标记 fidelity_mismatch。预估与实际排除率的偏差也记录在 EvidencePackage，偏差 > 0.3 时提示用户调整预估参数。

五、存储引擎
5.1 存储抽象层接口
text
StorageBackend {
    // 写入
    append(table_id, batch: ArrowBatch) -> SnapshotRef
    append_model(model_id, checkpoint: bytes) -> VersionRef
    
    // 读取
    scan(table_id, snapshot_id, columns, predicate) -> ArrowBatchIterator
    load_model(model_id, version_id) -> CheckpointStream
    
    // 版本
    list_versions(table_id) -> [VersionMeta]
    get_snapshot(table_id, version_tag) -> SnapshotRef
    
    // 生命周期
    compact(table_id, version_range, strategy) -> CompactResult
    verify_audit_chain(from_seq) -> VerifyResult
    
    // 事务
    atomic_write(operations: [WriteOp]) -> AtomicResult
}
事务语义说明：

atomic_write 保证多个写操作（如快照数据 + provenance + 审计日志记录）的原子性：全部成功或全部回滚。v1 由元数据层负责事务协调，使用两阶段提交：

先写数据到对象存储（幂等，失败可安全重试）

写元数据层（原子操作）

提交审计日志（在元数据事务内完成）

中断恢复：以元数据层状态为准。对象存储上的孤立数据由后台 GC 清理。

5.2 v1 默认后端
对象存储 + Parquet + 自研元数据层。

对象存储：天然不可变，WORM 合规保留策略

Parquet：列式存储，高效压缩，Arrow 互操作

元数据层：自研，管理版本/Snapshot/模型索引，承担事务协调

5.3 存储分层
text
审计日志层: 哈希链 + WORM 存储
快照层:    生成数据 + provenance + tail_report (写入后不可变)
模型层:    参数/检查点 | 版本链 | GC 自动 compaction
基表层:    原始输入 (INSERT ONLY) | 约束卡片 DDL
5.4 审计日志
与 WAL 彻底分离。独立的不可变记录系统。

记录格式：

text
{
  sequence: 1042,
  timestamp: "2026-05-10T09:17:00Z",
  actor: {component: "ingestion", caller_id: "user_abc"},
  operation: "LOAD_DATA",
  target: "sensor_log",
  parameters: {source: "/data/20260510.parquet"},
  prev_hash: "a3f2b8c1...",
  current_hash: "SHA256(this_record_without_current_hash)"
}
实现细节：

细节项	定义
创世记录	prev_hash = SHA256("SYNTHGEN_GENESIS")
写入时验证	写入前检查新记录 prev_hash 是否与链尾 current_hash 一致。不一致→拒绝写入+报警
分叉检测	相同 prev_hash 出现多条记录时报错+人工介入
每日全链校验	后台任务遍历整条链，验证所有 prev_hash → current_hash 链接
存储介质	WORM 合规保留的对象存储
5.5 模型版本 GC（自动 compaction）
保护条件（被以下任一条件保护则不可回收）：

被任何现有快照引用

被用户标记为 anchored

在最近 N 个版本内（N 可配置，默认 10）

Compaction 自动执行，不需要用户许可。三个保护条件全部不满足时自动触发。

时间旅行的退化行为：

当请求的版本已被 compaction 删除时，不报错：

text
请求: AS OF MODEL VERSION gen_model_v2.3
v2.3 已被合并入 v2 (合并范围: v2.1 - v2.5)
返回: v2 的数据 + 偏差报告

偏差报告: {
    requested: "v2.3",
    returned: "v2",
    reason: "compacted",
    merged_from: ["v2.1", ..., "v2.5"],
    training_data_range: "2026-04-01 ~ 2026-05-01",
    fidelity_score_range: [0.85, 0.91, 0.88, 0.90, 0.92],
    version_mismatch: true
}
合并后保留的元数据：

合并前所有版本的清单

每个版本的训练数据时间范围

每个版本的 fidelity score 和排除率

合并版本的模型参数 hash

这些元数据足以重建证明链，即使无法重新生成已删除版本的数据。

六、引擎间协议
6.1 接口层 → 生成引擎：查询协议
协议项	定义
请求格式	解析后的语法树（约束已分类：值域/行间/聚合）
执行模式标记	含聚合约束 → two_phase；含行间约束 → stateful_batch；否则 row_by_row
错误处理	解析错误/类型错误/约束引用不存在 → 立即返回错误，不进入生成引擎。错误写入审计日志
6.2 生成引擎 → 数据引擎：生成协议
协议项	定义
请求格式	{model_id, num_samples, seed, batch_size, execution_mode, output_columns}
返回方式	流式返回（batch 迭代器）
进度报告	每个 batch 返回 {batch_index, rows_generated, elapsed_ms}
背压机制	约束引擎处理慢时暂停 batch 请求
跨 batch 状态	execution_mode=stateful_batch 时，数据引擎传递 frame buffer（前一批最后一行）
错误处理	模型推理失败 → 返回 {error, batch_index} + 审计日志记录。生成引擎按退化路径回退
6.3 生成引擎 ↔ 存储引擎：数据协议
协议项	定义
读基表	流式读取，列式 Arrow 格式
写快照	整快照写入（atomic_write 保证原子性），写入后不可变
读模型	按需加载检查点，支持流式加载
写模型	新版本写入后不可变，写入后触发 GC 检查
流控	存储引擎可返回背压信号
错误处理	存储读写失败 → 请求失败 + 审计日志记录 + EvidencePackage 标记 failed
6.4 默认错误处理策略
任何组件内部不可恢复的错误 → 请求失败 + 审计日志记录 + EvidencePackage 标记 failed: true + failure_reason。不回退，不重试，不静默。

七、与理论框架的对应关系
理论框架要求	工程实现位置
输入诚实性（格式忠实性）	接口层 DDL 类型系统
统计签名条件保证+误差界	EvidencePackage 构建器
物理优先	执行路由器退化路径
物理优先认识论偏差	tail_report
后筛选排除率阈值	生成引擎运行时保障
漂移/演化区分+身份联动	执行路由器 + 约束完备性检查
失败模式一：基准冲突	接口层 Schema 校验，硬拒绝
失败模式二：约束真空	约束验证器，降级标记
失败模式三：模仿崩溃	执行路由器，自动回退纯物理
约束缺失身份切换+服务分级	执行路由器 + data_grade
新数据来源边界	LOAD DATA 的 data_origin 校验
审计不可变	哈希链审计日志 + WORM
可追溯性	EvidencePackage provenance + 模型版本链
约束类型区分	约束分类器（值域/行间/聚合）
约束执行优先级	值域优先，聚合最后
八、自研范围
自研（产品核心）	可借鉴的参考实现
SynthLang parser（自研扩展）	PostgreSQL gram.y、ANTLR
类型系统	PostgreSQL DDL engine
执行路由器 + 退化路径	PostgreSQL optimizer path selection
后筛选保障机制	PostgreSQL Hash Join spill-to-disk 逻辑
约束分类器 + 三类执行模型	PostgreSQL constraint executor
EvidencePackage 构建器	PostgreSQL COPY binary 格式
哈希链审计日志	Git object model
存储抽象层	PostgreSQL smgr、SQLite VFS
模型版本管理 + GC	PostgreSQL vacuum / MVCC
所有组件使用系统级语言构建（C/C++/Rust），模型推理走 ONNX Runtime 或 TensorRT。

文档结束