SynthGen Core 开发路线图 v1.0
文档版本：v1.0
日期：2026-05-10
状态：待审核

方法：能力里程碑——按"系统能做什么"划分版本，每个版本交付可演示的产品能力，内部含跨层技术拆解。
交付粒度：按功能里程碑，时间仅作估算。
与理论框架对齐：v1.3 约束分层（值域/行间/聚合）→ 路线图版本递进完全对齐。

---

## 总览

| 版本 | 产品能力 | 核心新组件数 | 估算 |
|------|---------|------------|------|
| **v1** 最小可运行 | Schema定义→数据导入→值域约束生成→证据包输出 | 8 | 5-6周 |
| **v2** 约束完整 | 三类约束全做+执行路由器+审计日志 | 7 | 5-6周 |
| **v3** 时间智能 | 时间旅行+持续对齐+模型版本管理 | 7 | 4-5周 |
| **v4** 高级分析 | 多窗口+完备度评分+反例搜索 | 6 | 4-5周 |

**总计**：28个组件，18-22周

---

## v1: 最小可运行（5-6周）

**产品故事**：用5行SynthLang定义你的数据域，导入真实数据，获得物理合法的合成数据和完整的证据包。

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 1 | SynthLang Parser 核心语法 | DEFINE TYPE / LOAD DATA / DEFINE CONSTRAINT(仅值域) / GENERATE TABLE | 无 | 1.5周 |
| 2 | 类型系统 + Schema DDL | 数据类型、ORDER声明、值域范围声明([min,max])、ENUM | #1 Parser | 1周 |
| 3 | 数据导入(LOAD DATA) | 读取Parquet，写入基表层，Schema校验 | #2 类型系统 + #4 存储 | 0.5周 |
| 4 | 基础存储引擎 | 对象存储+Parquet读写+自研元数据层v1(版本/Snapshot索引) | 无 | 1.5周 |
| 5 | 值域约束引擎 | 逐行检查 BETWEEN/MIN/MAX，输出合法行 | #2 Schema + #3 数据导入 | 1周 |
| 6 | 后筛选基础版 | 排除率计算+行数下限截断+data_grade(physics_guaranteed/statistics_guaranteed/limited_fidelity) | #5 值域引擎 | 0.5周 |
| 7 | EvidencePackage 构建器 v1 | schema_hash/constraint_summary/exclusion_rate/data_grade/row_count/provenance基础 | #6 后筛选 + #5 值域引擎 | 1周 |
| 8 | Python SDK + REST API | 客户端封装，SynthLang作为内部IR，用户用SDK调用 | 全部 | 1周 |

### 依赖链

```
#1 Parser → #2 类型系统 → Schema DDL
                              ↓
#4 基础存储 ← #3 数据导入 ←───┘
      ↓
#5 值域约束引擎 → #6 后筛选基础版 → #7 EvidencePackage v1
                                              ↓
                                   #8 Python SDK + REST API
```

### 可并行开发

- #1 Parser + #4 存储：无依赖，可同时开工
- #7 EvidencePackage：接口先定义，与 #5/#6 并行开发

### 演示场景

```synthlang
DEFINE TYPE sensor_log {
    timestamp: DATETIME NOT NULL ORDER,
    temperature: FLOAT [-50.0, 80.0],
    pressure: FLOAT [900.0, 1100.0]
};

LOAD DATA INTO sensor_log FROM '/data/sensors.parquet';

DEFINE CONSTRAINT safe_range ON sensor_log {
    temperature BETWEEN -10 AND 45,
    pressure BETWEEN 980 AND 1040
};

GENERATE TABLE gen_samples
FROM sensor_log
WITH CONSTRAINTS safe_range
LIMIT 1000;
```

**输出**：1000行合成数据 + EvidencePackage（含排除率、data_grade、约束摘要）

### v1 验收标准

- [ ] SynthLang 核心语法解析无错误
- [ ] Parquet 文件导入成功，Schema 校验通过
- [ ] 值域约束逐行过滤，排除率正确计算
- [ ] EvidencePackage 包含所有必需字段
- [ ] Python SDK 可端到端调用
- [ ] 排除率 >90% 时拒绝生成

---

## v2: 约束完整（5-6周）

**产品故事**：行间依赖、窗口聚合、退化路径——完整的三类约束体系，每条生成路径都有审计记录。

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 9 | 行间约束引擎 | batch有状态执行+frame buffer+跨batch状态传递+排序列绑定(来自Schema ORDER) | v1 #5 值域引擎 | 1.5周 |
| 10 | 聚合约束引擎 | 两阶段执行（阶段一：值域+行间逐行过滤；阶段二：时间窗口聚合验证）+排除率窗口语义+partial_window_excluded标记 | v1 #5 值域 + #9 行间 | 1.5周 |
| 11 | 约束分类器(编译时) | 识别值域/行间/聚合→标记PHASE_ONE/PHASE_TWO+执行模式标记(row_by_row/stateful_batch/two_phase) | Parser扩展 | 1周 |
| 12 | 执行路由器 | 5条退化路径（全功能/后筛选/纯物理/统计生成/KDE扰动）+约束完备性检查+身份切换+体积比预估 | #11 分类器 + #9/#10 三种引擎 | 1.5周 |
| 13 | 后筛选完整版 | 排除率预估(直方图/采样+保守偏向)+超时截断+排除率实时监控+误差界联动表(0-30%/30-70%/70-90%) | #12 执行路由器 | 1周 |
| 14 | 哈希链审计日志 | 创世记录+写入验证(prev_hash一致性)+分叉检测+每日全链校验+WORM存储 | v1 #4 存储 | 1周 |
| 15 | DURING/WHEN语义 | 条件约束：DURING column=value + WHEN condition THEN constraint | #11 分类器 + 引擎 | 0.5周 |

### 依赖链

```
#11 约束分类器 ← Parser扩展(v2语法)
      ↓
#9 行间约束引擎 ←── v1值域引擎
      ↓                       ↓
#10 聚合约束引擎(两阶段) ──→ #13 后筛选完整版
      ↓
#12 执行路由器(5路径) ──→ 身份切换(统计/格式化生成器)
      ↓
#14 哈希链审计日志（独立，可与引擎并行）
#15 DURING/WHEN（串在分类器后）
```

### 可并行开发

- #14 审计日志：与引擎开发并行（仅依赖基础存储）
- #9 行间 + #10 聚合：行间先行，聚合依赖行间
- #12 路由器 + #13 后筛选：路由器先行

### 演示场景

```synthlang
DEFINE TYPE sensor_log {
    timestamp: DATETIME NOT NULL ORDER,
    wind_speed: FLOAT [0.0, 50.0],
    temperature: FLOAT [-50.0, 80.0],
    vibration: FLOAT,
    status: ENUM('normal', 'warning', 'fault')
};

DEFINE CONSTRAINT wind_safety ON sensor_log {
    wind_speed BETWEEN 0 AND 25 DURING status = 'normal',
    vibration[t] - vibration[t-1] < 5.0,
    AVG(temperature) OVER (INTERVAL 1 HOUR) <= 40.0
};

GENERATE TABLE gen_samples FROM sensor_log
WITH CONSTRAINTS wind_safety
LIMIT 1000
INCLUDE tail_report;
```

**输出**：
- 三类约束过滤后的数据
- EvidencePackage（含误差界、排除率）
- tail_report
- 审计日志（哈希链可验证）

### v2 验收标准

- [ ] 行间约束跨batch状态传递正确
- [ ] 聚合约束两阶段执行，阶段一包含值域+行间
- [ ] 约束分类器正确标记PHASE_ONE/PHASE_TWO
- [ ] 执行路由器5条退化路径全部可达
- [ ] 排除率>80%时走保守偏向，>90%时拒绝后筛选
- [ ] 审计日志哈希链完整，可验证无篡改
- [ ] DURING/WHEN条件约束正确生效

---

## v3: 时间智能（4-5周）

**产品故事**：数据在演化，模型在进化。时间旅行回到任意版本，持续对齐保持数据与时偕行。

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 16 | 模型版本链 | 版本创建/引用/列表+不可变写入+版本元数据(训练数据范围/fidelity_score) | v1 #4 存储+元数据层 | 1周 |
| 17 | 模型版本GC | 3保护条件(快照引用/anchored/N版本内)+自动compaction+合并元数据保留 | #16 版本链 | 1周 |
| 18 | 时间旅行(AS OF) | 按版本读取快照+compaction退化行为(返回最近版本+偏差报告) | #16 版本链 + #17 GC | 0.5周 |
| 19 | 持续对齐(UPDATE MODEL) | 新数据纳入+漂移检测(auto)+SAVE AS新版本+身份延续 | #16 版本链 + v2 #12 执行路由器 | 1.5周 |
| 20 | tail_report | 排除率与data_grade联动+双变量/自相关误差界+fidelity_mismatch标记 | v2 #13 后筛选完整版 | 1周 |
| 21 | 存储模型层 | 检查点存储+流式加载+版本索引+atomic_write事务(两阶段提交:先写数据→写元数据→提交审计) | v1 #4 元数据层 | 1周 |
| 22 | 偏差报告 | compaction偏差(requested/returned/reason/merged_from/training_data_range/fidelity_score_range/version_mismatch) | #17 GC + #16 版本链 | 0.5周 |

### 依赖链

```
#21 存储模型层(检查点+事务)
      ↓
#16 模型版本链 → #17 GC compaction → #18 时间旅行(AS OF)
      ↓                              ↓
#19 持续对齐(UPDATE MODEL) ←─────────┘
      ↓
#22 偏差报告 ← #17 GC

#20 tail_report ← v2后筛选（独立，可并行）
```

### 可并行开发

- #20 tail_report：与版本链开发并行
- #21 存储模型层：与版本链开发并行
- #16/#17/#18 串行，但 #19 和 #22 可并行

### 演示场景

```synthlang
-- 初始生成
GENERATE TABLE gen_samples FROM sensor_log
WITH CONSTRAINTS wind_safety
LIMIT 10000;

-- 持续对齐：新数据到来，模型进化
UPDATE GENERATION MODEL gen_model_v1
INCORPORATE DATA FROM sensor_log
WHERE timestamp > '2026-05-01'
WITH DRIFT_CHECK = auto
SAVE AS gen_model_v2;

-- 时间旅行：回到旧版本
SELECT * FROM gen_samples
AS OF MODEL VERSION gen_model_v1;

-- v2.3 已被compaction合并到 v2
-- 返回 v2 数据 + 偏差报告
```

**输出**：
- 新版本模型（gen_model_v2）
- 时间旅行数据（含偏差报告）
- EvidencePackage（含模型版本链provenance）
- tail_report（误差界+fidelity_mismatch标记）

### v3 验收标准

- [ ] 模型版本链创建、引用、列表正确
- [ ] GC compaction 3保护条件全部生效
- [ ] AS OF 读取正确版本，compaction退化返回偏差报告
- [ ] UPDATE MODEL 新数据纳入，漂移检测工作
- [ ] atomic_write 事务：中断恢复以元数据层状态为准
- [ ] tail_report 排除率与data_grade联动正确
- [ ] 偏差报告字段完整，证明链可重建

---

## v4: 高级分析（4-5周）

**产品故事**：行数窗口、分组聚合、会话切分——窗口类型全面扩展。约束完备度从布尔走向连续评分，反例搜索揭示数据盲区。

### 组件清单

| # | 组件 | 功能 | 依赖 | 估算 |
|---|------|------|------|------|
| 23 | 行数窗口(ROWS) | OVER (ROWS 100) 语法+执行+行数滑动窗口聚合 | v2 #10 聚合引擎 | 1周 |
| 24 | 分组时间窗口(PARTITION BY) | OVER (PARTITION BY col, INTERVAL 1 HOUR) 语法+执行+分组聚合 | v2 #10 聚合引擎 | 1周 |
| 25 | 会话窗口(SESSION) | OVER (SESSION BY col, GAP 5 MINUTES) 语法+执行+会话切分 | #23/#24 窗口基础 | 1.5周 |
| 26 | 约束完备度连续化评分 | 0.0-1.0评分+基于已覆盖约束维度加权+布尔判断作为阈值1.0特例+执行路由器联动(评分<1.0时退化路径选择) | v2 #12 执行路由器 | 1周 |
| 27 | 反例搜索 | 给定约束集合，搜索输入空间中不满足约束的区域+指导用户放宽约束或补充数据 | v2 #12 路由器 + #13 后筛选 | 1.5周 |
| 28 | EvidencePackage v2 | 新增模型版本链provenance+完备度评分+反例区域+偏差报告引用 | v3 EvidencePackage + #26/#27 | 0.5周 |

### 依赖链

```
#23 行数窗口 ←── v2聚合引擎
#24 分组时间窗口 ←── v2聚合引擎
#25 会话窗口 ←── #23/#24

#26 完备度评分 ←── v2执行路由器
#27 反例搜索 ←── v2路由器 + 后筛选

#28 EvidencePackage v2 ←── #26 + #27
```

### 可并行开发

- #23/#24 可并行（同层级，不同窗口类型）
- #25 依赖 #23/#24，但可在两者后期开始
- #26/#27 可并行
- #28 在 #26/#27 完成后

### 演示场景

```synthlang
-- 行数窗口
DEFINE CONSTRAINT vibration_check ON sensor_log {
    AVG(vibration) OVER (ROWS 100) <= 2.0
};

-- 分组时间窗口
DEFINE CONSTRAINT regional_temp ON sensor_log {
    AVG(temperature) OVER (PARTITION BY region, INTERVAL 1 HOUR) <= 35.0
};

-- 会话窗口
DEFINE CONSTRAINT session_stability ON user_sessions {
    COUNT(*) OVER (SESSION BY user_id, GAP 30 MINUTES) <= 1000
};
```

**输出**：
- 多窗口类型约束执行结果
- 约束完备度评分（0.0-1.0）
- 反例区域报告
- EvidencePackage v2（含完备度+反例+版本链provenance）

### v4 验收标准

- [ ] ROWS/PARTITION BY/SESSION 三种窗口语法解析正确
- [ ] 窗口聚合计算正确（边界、分组、会话切分）
- [ ] 完备度评分 0.0-1.0，布尔判断作为1.0特例
- [ ] 评分<1.0时路由器选择正确退化路径
- [ ] 反例搜索返回不满足约束的区域
- [ ] EvidencePackage v2 包含所有新字段

---

## 版本间依赖总图

```
v1 最小可运行
│  #1 Parser
│  #2 类型系统
│  #3 数据导入
│  #4 基础存储
│  #5 值域引擎
│  #6 后筛选基础版
│  #7 EvidencePackage v1
│  #8 SDK+REST
│
├──→ v2 约束完整
│    #9 行间引擎 ←── v1#5
│    #10 聚合引擎 ←── v1#5 + #9
│    #11 约束分类器 ←── Parser扩展
│    #12 执行路由器 ←── #9/#10/#11
│    #13 后筛选完整版 ←── #12
│    #14 哈希链审计 ←── v1#4
│    #15 DURING/WHEN ←── #11
│
├──→ v3 时间智能
│    #16 模型版本链 ←── v1#4
│    #17 GC compaction ←── #16
│    #18 时间旅行 ←── #16 + #17
│    #19 持续对齐 ←── #16 + v2#12
│    #20 tail_report ←── v2#13
│    #21 存储模型层 ←── v1#4
│    #22 偏差报告 ←── #17 + #16
│
└──→ v4 高级分析
     #23 行数窗口 ←── v2#10
     #24 分组时间窗口 ←── v2#10
     #25 会话窗口 ←── #23/#24
     #26 完备度评分 ←── v2#12
     #27 反例搜索 ←── v2#12 + v2#13
     #28 EvidencePackage v2 ←── #26 + #27
```

---

## 与工程框架 v0.4 的对齐验证

| 工程框架 v0.4 组件 | 路线图覆盖 |
|-------------------|-----------|
| SynthLang 解析器 | v1 #1 + v2 #11(Parser扩展) |
| 类型/Schema 系统 | v1 #2 |
| 约束分类器 | v2 #11 |
| 值域约束 → 逐行路径 | v1 #5 |
| 行间约束 → batch有状态路径 | v2 #9 |
| 聚合约束 → 两阶段路径 | v2 #10 |
| 执行路由器 + 退化路径 | v2 #12 |
| 后筛选保障 | v1 #6 + v2 #13 |
| 引擎适配层 | v2 #12(路由器内含) |
| EvidencePackage 构建器 | v1 #7 + v4 #28 |
| 存储抽象层(StorageBackend) | v1 #4 + v3 #21 |
| 基表层(INSERT ONLY) | v1 #3 |
| 快照层(不可变) | v1 #4 |
| 模型层(版本链+GC) | v3 #16 + #17 |
| 审计日志(哈希链+WORM) | v2 #14 |
| 模型版本GC | v3 #17 |
| 时间旅行(AS OF) | v3 #18 |
| 持续对齐(UPDATE MODEL) | v3 #19 |
| tail_report | v3 #20 |
| 排除率与data_grade联动 | v2 #13 + v3 #20 |
| DURING/WHEN语义 | v2 #15 |
| 窗口类型扩展(ROWS/PARTITION/SESSION) | v4 #23/#24/#25 |
| 约束完备度连续化评分 | v4 #26 |
| 反例搜索 | v4 #27 |

**覆盖率**：28/28 = 100%

---

## 与理论框架 v1.3 的对齐验证

| 理论框架 v1.3 要求 | 路线图覆盖 |
|-------------------|-----------|
| 值域约束逐行过滤 | v1 #5 |
| 行间约束有状态过滤 | v2 #9 |
| 聚合约束两阶段执行 | v2 #10 |
| 后筛选是最终防线 | v1 #6 + v2 #13 |
| 物理优先认识论偏差 | v3 #20 tail_report |
| 排除率窗口语义 | v2 #10 |
| 代偿收敛时限 | v3 #19(持续对齐中实现) |
| 记忆策略退化分析 | v2 #12 执行路由器(身份切换) |
| 审计不可变 | v2 #14 |
| 可追溯性 | v1 #7 + v3 #16 版本链 |

**覆盖率**：10/10 = 100%

---

## 人员假设

基于 aboutme.md：专业数据库团队，C/C++/Rust 技术栈。

- **Parser + 类型系统**：1人（编译原理背景）
- **存储引擎**：1-2人（存储/文件系统背景）
- **约束引擎 + 路由器**：1-2人（数据库执行引擎背景）
- **SDK + API**：1人（应用层）
- **总团队**：3-5人

v1 最小可运行阶段，2-3人即可并行（Parser+存储→约束+SDK）。

---

## 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| v1 Parser 子集在 v2 扩展时重构 | 延迟1-2周 | 工程框架已设计预留（unsupported_in_v1标记），Parser 架构从一开始就支持扩展 |
| 聚合约束两阶段语义理解偏差 | v2 延迟 | v1 阶段就完成两阶段语义的设计文档审查 |
| 元数据层事务协调复杂度 | v3 延迟 | v1 用简化版（无事务），v3 引入两阶段提交 |
| ONNX Runtime/TensorRT 集成 | 全局 | 物理引擎作为独立模块，可先 mock 再集成 |

---

文档结束
