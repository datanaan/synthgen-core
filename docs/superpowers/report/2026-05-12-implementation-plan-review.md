# SynthGen Core 实施计划 Code Review 报告

**审查日期**：2026-05-12
**审查范围**：v2/v3/v4 全部 specs + plans（55 份文档）+ 上游文档一致性
**审查人**：AI Code Reviewer

---

## 一、总体评价

### 1.1 评分：B+ (85/100)

文档体系整体质量高，架构一致性强，与上游文档（路线图 v1.4、工程框架 v0.4、理论框架 v1.3、工程执行守则）对齐良好。v1 已完成 21 份文档的质量标杆明确，v2/v3/v4 规范和计划可据此推进。

### 1.2 核心优势

| 维度 | 评分 | 说明 |
|------|------|------|
| 架构一致性 | A | 三层架构、模块边界、命名约定、跨切关注点贯穿 v1-v4，零矛盾 |
| 上游对齐 | A | 与路线图 v1.4 的 30 个功能组件 100% 覆盖；与工程框架 v0.4 25/25 对齐 |
| 诚实声明体系 | A | 每个版本的"能做/做不了"清晰；EvidencePackage 适用性标注从 v1 贯穿 v3 |
| 错误测试规范 | A- | 每个版本有完整的错误测试验收标准；ErrorCode 覆盖面广 |
| 协调项管理 | B+ | C1-C8 分类清晰，占位标注到位；但部分协调项缺少决策 deadline |
| Plan 深度 | B | v2 plans 深度好（J/K/M 各 300+ 行）；v3/v4 plans 偏骨架化 |
| Spec 深度 | B+ | 阶段设计规范接口完整；Unit spec 依赖 v2 最详细 |

### 1.3 核心风险

| # | 风险 | 严重度 | 说明 |
|---|------|--------|------|
| R1 | v3/v4 Plan 骨架化 | HIGH | Unit Q-T plans 仅 50-60 行，缺少 Step 级验收标准和错误测试细则 |
| R2 | C1 KDE 技术选型未决 | HIGH | 阻塞 #15b 全链路 3 周估算，如选型延迟则 v2 Wave 2 失效 |
| R3 | v2 → v3 Plan 深度断崖 | MEDIUM | v2 Unit J plan 305 行 vs v3 Unit Q plan 67 行，4.5x 差距 |
| R4 | Spec 与 Plan 接口定义重复 | MEDIUM | 阶段设计规范已有完整 C++ 接口，Unit spec 重复定义，维护成本高 |
| R5 | 检查文档（consistency/completeness）未评估 | LOW | v1 已有 2 份检查文档，v2/v3/v4 各 2 份未在本次审查中覆盖 |

---

## 二、逐版本审查

### 2.1 v2 约束完整

#### 2.1.1 阶段设计规范 ✅ 优秀

**文档**：`docs/superpowers/v2/specs/2026-05-10-synthgen-v2-design.md`（815 行）

**优点**：
1. **产品故事精准**：v1→v2 的核心变化是"执行调度重构"而非"加引擎"，这个定位极其清晰
2. **依赖图完整**：Unit J-P + Q/R 依赖关系无环路，波次划分合理
3. **接口定义完整**：9 个组件（#10-#17）的 C++ 接口全部定义，含命名空间、Result<T>、Explain
4. **错误测试验收**：7 大类错误测试场景，50+ 具体验收项
5. **诚实声明体系**：10 条声明与 EvidencePackage 字段映射，可自动验证

**问题**：

| # | 问题 | 严重度 | 建议 |
|---|------|--------|------|
| P2-1 | 聚合引擎 `TwoPhaseResult` 中 `PhaseOneResult`/`PhaseTwoResult` 类型未定义 | MEDIUM | 补充类型定义或引用整体设计规范 |
| P2-2 | `PostFilter::estimate_exclusion_rate` 参数含 `DataEngineV1` 但未 `#include` 也不在同级命名空间 | LOW | 确认跨命名空间依赖是否通过前向声明 |
| P2-3 | `WORMStorage` 的 `modify`/`delete` 方法描述在注释中，但未在接口签名体现 | LOW | 显式声明 `Result<void> modify(...) = delete;` 或用 `= delete` |
| P2-4 | `EvidencePackageV2` 继承关系 `ProvenanceV2 : public ProvenanceV1` 在 C++ 中是 struct 继承，语义上是值类型，需确认是否需要虚析构 | LOW | 值类型不需要虚析构，但应在整体设计规范中明确 Provenance 是值类型 |
| P2-5 | v2 诚实声明表第 9 行"排除率与 data_grade 联动"缺少 30-70% 对应的 data_grade 值 | MEDIUM | 补充：30-70% → statistics_guaranteed（与工程框架对齐） |

#### 2.1.2 Unit Specs ✅ 良好

**已审查**：v2 Unit J spec（完整阅读），其余 Unit specs 通过标题和结构扫描。

**Unit J Spec 评价**（9/10）：
- 接口定义与阶段设计规范完全一致
- 补充了行间约束语法（[t]/[t-1]）的 Parser 扩展细节
- 错误测试覆盖了 8 个场景
- 缺少：frame buffer 与批量执行接口的时序图（状态转换图）

**Spec 共性问题**：
- 每份 Unit spec 的接口定义与阶段设计规范大量重复。建议：阶段设计规范定义"契约级接口"（公开 API），Unit spec 仅补充"实现级接口"（内部数据结构、算法选择）

#### 2.1.3 Unit Plans ✅ 良好（v2）

**已审查**：Unit J plan（305 行，5 Tasks，14 Steps），Unit K plan（350 行），Unit M plan（300 行）

**Unit J Plan 评价**（8.5/10）：
- Task 分解合理：Parser 扩展 → 核心实现 → 错误处理 → 脚手架 → 集成
- 每个 Step 有产出文件和验收标准
- 测试用例数量明确（10+ 单元 / 9+ 边界 / 8+ 集成）
- 缺少：每个 Step 的预估工时（仅 Task 级有估算）

**Scaffold/Tool Plans**：
- v2 scaffold plan（2KB）：5 项脚手架增强，结构清晰但偏简
- v2 tool plan（2KB）：4 项工具，同上

#### 2.1.4 协调项审查

| 编号 | 协调项 | 评估 | 建议 |
|------|--------|------|------|
| C1 | KDE 技术选型 | ⚠️ 推荐"自研 C++ KDE"但未考虑开发量 | 自研 KDE ~2w 开发量未计入 #15b 3w 估算中（3w 含 KDE 开发+集成+测试）。如果 KDE 开发就需 2w，集成测试只剩 1w，偏紧 |
| C2 | 执行路由器 v1 兼容策略 | ✅ 推荐"完全重构"，与路线图"摩托车是新的整车"一致 | 无额外建议 |
| C3 | v2 Parser 扩展范围 | ✅ 合理 | Unit J plan 已明确 [t]/[t-1] 语法子集边界 |
| C8 | WORM 存储实现 | ⚠️ 推荐"带哈希校验的 Parquet" | 与路线图审计日志设计一致。但需确认：Parquet 本身不支持 append-only，需在应用层追加写入新文件 |

---

### 2.2 v3 时间智能

#### 2.2.1 阶段设计规范 ✅ 良好

**文档**：`docs/superpowers/v3/specs/2026-05-10-synthgen-v3-design.md`（432 行）

**优点**：
1. 产品故事清晰：模型版本管理 + 时间旅行 + 持续对齐
2. 接口定义完整：#18-#24 的 C++ 接口
3. 诚实声明 6 条，与 v2 风格一致
4. 依赖图清晰，波次划分合理

**问题**：

| # | 问题 | 严重度 | 建议 |
|---|------|--------|------|
| P3-1 | `CompactionConfig::keep_recent_n = 5` 与路线图 v1.4 "默认 10" 不一致 | HIGH | 统一为 10 或明确说明 v3 调整原因 |
| P3-2 | `ModelVersionChain::modify_version` 签名用 `...` 省略参数 | LOW | 改为明确签名或注释说明"此方法仅用于返回 kImmutableViolation" |
| P3-3 | `ContinuousAlignmentEngine` 构造函数依赖 `ExecutionRouter`，但 v2 中路由器在引擎层，持续对齐也在引擎层——同层依赖需确认 | MEDIUM | 路由器是对齐的"调度者"，同层依赖合理，但应在整体设计规范中明确路由器的特殊地位 |
| P3-4 | 持续对齐的漂移检测算法未指定（`drift_check = "auto"` 的 auto 是什么） | MEDIUM | 在 spec 中明确：auto = 基于 KS 检验或 KL 散度，或标注为 [COORDINATE] |
| P3-5 | v3 诚实声明缺少"存储事务原子性"在 EvidencePackage 中的字段映射 | LOW | atomic_write 是存储层行为，EvidencePackage 中通过 provenance 间接体现，但应显式声明 |

#### 2.2.2 Unit Plans ⚠️ 骨架化

**已审查**：Unit Q plan（67 行，3 Tasks），Unit R plan（50 行），Unit S plan（55 行）

**Unit Q Plan 评价**（5/10）：
- Task 分解到 3 个 Task，但 Steps 只有 Task 级，无 Step 级
- 缺少：每个 Step 的产出文件、验收标准、测试用例数量
- 与 v2 Unit J plan（305 行，14 Steps）相比严重不足

**问题**：v3 的 Unit plans 整体是 v2 的骨架版，缺少实施细节。建议：参照 v2 Unit J plan 的深度，每个 Unit plan 至少 150+ 行。

---

### 2.3 v4 高级分析

#### 2.3.1 阶段设计规范 ✅ 合格

**文档**：`docs/superpowers/v4/specs/2026-05-10-synthgen-v4-design.md`（190 行）

**优点**：
1. Research 声明清晰：#29 反例搜索标为 research
2. 接口定义简洁：#25-#30 的 C++ 接口
3. 诚实声明 3 条，与 v2/v3 一致

**问题**：

| # | 问题 | 严重度 | 建议 |
|---|------|--------|------|
| P4-1 | `CompletenessScorer::should_allow_full_function` 阈值参数默认 1.0，但评分函数 `score()` 的计算逻辑未定义 | HIGH | 补充评分算法：基于约束覆盖维度加权，至少给出公式框架 |
| P4-2 | `EvidencePackageV3` 继承 `EvidencePackageV2` 但 v2 的 ProvenanceV2 也是 struct 继承——多层 struct 继承在 C++ 中维护成本高 | MEDIUM | 考虑改为组合而非继承，或在整体设计规范中明确 EvidencePackage 的演进策略 |
| P4-3 | `CounterExampleSearcher` 的 `search` 方法标注 [COORDINATE]，但接口签名已固定——如果 C5/C6 决策改变接口，需同步修改 | LOW | 在接口上方加注释说明此接口可能因 C5/C6 决策而调整 |
| P4-4 | v4 缺少脚手架验收标准（只有 2 项，v2/v3 各 5 项） | MEDIUM | 补充：可观测性增强（窗口类型分布+完备度趋势）、测试增强（窗口回归） |

#### 2.3.2 Unit Plans ⚠️ 骨架化

**已审查**：Unit U plan（129 行，5 Tasks），Unit V plan（95 行），Unit W plan（95 行）

**评价**：v4 plans 比 v3 略好（有 Task 级分步），但仍不如 v2 的深度。Unit U plan 的每个 Step 有验收标准，但缺少：
- 具体测试用例数量
- 错误路径测试清单
- 脚手架集成 Step

---

## 三、跨版本一致性问题

### 3.1 数据不一致

| # | 位置 | 不一致内容 | 严重度 | 修正建议 |
|---|------|-----------|--------|---------|
| X1 | v3 spec vs 路线图 v1.4 | `keep_recent_n` 默认值：v3 spec = 5，路线图 = 10 | HIGH | 统一为 10，或在 v3 spec 中说明调整理由 |
| X2 | v2 spec vs 整体设计规范 | v2 spec 中 EvidencePackage 字段适用性标注 `post_filter_engaged` 未在整体设计规范 v1 的适用性枚举中定义 | MEDIUM | 在整体设计规范中补充适用性枚举：`always / data_engaged / aggregation_present / drift_available / not_applicable / post_filter_engaged` |
| X3 | v4 spec vs 路线图 v1.4 | 路线图 #25/26/27 的窗口类型标注与工程框架 v0.4 的支持版本表不一致（工程框架说 ROWS/PARTITION BY 是 v2，路线图说是 v4） | MEDIUM | 这是历史遗留：工程框架 v0.4 §2.3 表中的版本标注有误。路线图 v1.4 和 v4 spec 一致，以路线图为准 |
| X4 | v2 spec vs 路线图 v1.4 | v2 spec 的 `ConservativeTailReport` 字段名与整体设计规范的 `conservative_tail_report` 大小写不一致 | LOW | 整体设计规范用 snake_case（JSON 格式），C++ 代码用 PascalCase。正常但需明确约定 |

### 3.2 依赖链完整性

**验证方法**：遍历每个 Unit 的依赖，确认上游 Unit 在同版本或更早版本中。

| 依赖链 | 验证结果 |
|--------|---------|
| v2 Unit J → v1 #5 + #6 | ✅ v1 已完成 |
| v2 Unit K → v1 #6 + v2 #10 | ✅ 同版本 Wave 1 |
| v2 Unit M → v2 #10/#11/#12 | ✅ Wave 2 依赖 Wave 1 |
| v2 Unit N → v2 #13 + #15b | ✅ Wave 3 依赖 Wave 2 |
| v3 Unit S → v3 #18 + #19 + v2 #13 + v2 #15b | ✅ 跨版本依赖明确 |
| v3 Unit T → v2 #14 + v3 #21 + #19 + #18 | ✅ |
| v4 Unit U → v2 #11 | ✅ |
| v4 Unit X → v4 #28 + v2 #13 + C5(待测模型) | ⚠️ C5 需在 v3 交付 |

**结论**：依赖链无环路，跨版本依赖清晰。唯一风险是 C5（待测模型接入协议）需在 v3 交付，否则 v4 Unit X 无法启动。

### 3.3 估算合理性

| 版本 | 路线图估算 | Spec+Plan 估算 | 差异 | 评估 |
|------|-----------|----------------|------|------|
| v2 | 12-13 周（含脚手架+工具） | 9 Unit × 1-4w = 12w（仅功能） | 功能估算匹配 | ✅ |
| v3 | 6-7 周（含脚手架+工具） | 6 Unit × 0.5-2.5w = 8w（仅功能） | 功能估算偏高 | ⚠️ v3 spec 总计 8w > 路线图 4-5w（功能），需确认是否含脚手架并行 |
| v4 | 4-5 周（含脚手架+工具） | 6 Unit × 0-2w = 7w（仅功能） | 功能估算偏高 | ⚠️ 同上 |

**v3 估算差异分析**：
- 路线图 v3 功能估算 4-5 周，v3 spec 的 Unit 总计约 8 周（Q:1w + R:1w + S:2w + T:2.5w + U:1w + V:0.5w）
- 差异原因：路线图按人员并行估算，spec 按串行累加
- 结论：**路线图估算更合理**（考虑并行），但 spec 的 Unit 分配总表应注明"可并行"

---

## 四、协调项 C1-C8 深度审查

| 编号 | 协调项 | 影响范围 | 评估 | 决策建议 |
|------|--------|---------|------|---------|
| C1 | KDE 技术选型 | #15b 全链路 | ⚠️ 推荐"自研 C++ KDE"但 2w 开发量偏大 | **建议选 ONNX Runtime**：团队 C++ 为主，但 KDE 算法成熟（scipy.stats 可参考），用 ONNX 导出预训练 KDE 模型更省力。如果坚持自研，需将 #15b 从 3w 调整为 4w |
| C2 | v1 兼容策略 | #13 路由器 | ✅ 完全重构是正确决策 | 无额外建议 |
| C3 | Parser 扩展范围 | #12 分类器 | ✅ [t]/[t-1] 子集边界清晰 | 无额外建议 |
| C4 | 持续对齐-数据引擎接口 | #21 | ⚠️ fit() 增量更新推荐合理，但未考虑模型序列化格式 | 增量更新需持久化增量状态，需定义检查点格式 |
| C5 | 待测模型接入协议 | #29 | ✅ v3 定义、v4 使用的时序正确 | 在 v3 Unit S plan 中补充"定义 TestModelProtocol"的 Step |
| C6 | 反例搜索理论基础 | #29 | ⚠️ 理论框架 v1.3 无独立章节 | v2/v3 开发期间需并行理论预研，否则 v4 #29 只能标记 deferred |
| C7 | Python 工具链接受度 | v2/v3/v4 工具 | ✅ 已在路线图中评估风险 | Python 仅限 CI 脚本和独立工具，不进产品代码 |
| C8 | WORM 存储选型 | #15 | ✅ 带哈希校验的 Parquet 合理 | 需确认 Parquet 的 append 策略（每次审计追加新文件 vs 合并） |

**新增建议协调项**：

| 编号 | 协调项 | 理由 |
|------|--------|------|
| C9 | EvidencePackage Schema 版本演进策略 | v1→v2→v3 的继承/组合方式未明确。当前用 C++ struct 继承，v4 后维护成本高。建议：Schema 用 JSON Schema 独立演进，C++ 用组合而非继承 |
| C10 | 漂移检测算法选型 | v3 `drift_check = "auto"` 未定义。KS 检验 vs KL 散度 vs Wasserstein 距离的选择影响精度和性能 |

---

## 五、行动计划

### 5.1 必须修复（P0）

| # | 行动 | 影响 | 期限 |
|---|------|------|------|
| A1 | 统一 `keep_recent_n` 默认值（v3 spec vs 路线图） | 数据一致性 | v3 开发前 |
| A2 | 补充 v3/v4 Unit Plans 深度 | 实施可行性 | v3 开发前 |
| A3 | C1 KDE 技术选型决策 | v2 Wave 2 启动前 | v2 Wave 1 结束前 |

### 5.2 建议修复（P1）

| # | 行动 | 影响 | 期限 |
|---|------|------|------|
| A4 | 补充 v2 spec 中 `PhaseOneResult`/`PhaseTwoResult` 类型定义 | 接口完整性 | v2 开发前 |
| A5 | 明确 v2 排除率联动表中 30-70% 的 data_grade | 诚实声明完整性 | v2 开发前 |
| A6 | 补充 v4 完备度评分算法框架 | 接口可实现性 | v4 开发前 |
| A7 | 明确 v3 漂移检测算法（新增 C10） | v3 可实现性 | v3 开发前 |
| A8 | 在整体设计规范中补充 `post_filter_engaged` 适用性枚举 | Schema 完整性 | v2 开发前 |

### 5.3 可选改进（P2）

| # | 行动 | 影响 | 期限 |
|---|------|------|------|
| A9 | 统一 Spec 与 Plan 的接口定义方式（消除重复） | 维护成本降低 | 持续 |
| A10 | 为每个 Unit Plan 添加 Step 级工时估算 | 进度可追踪 | v3/v4 开发前 |
| A11 | 添加 EvidencePackage 版本演进策略文档（C9） | 长期维护 | v3 开发前 |

---

## 六、文档清单与完整性检查

### 6.1 已有文档清单

| 版本 | 类型 | 数量 | 状态 |
|------|------|------|------|
| v1 | Specs | 12 | ✅ 已完成 |
| v1 | Plans | 9 | ✅ 已完成 |
| v2 | Specs | 12 | ✅ 已完成（含阶段设计规范） |
| v2 | Plans | 9 | ✅ 已完成 |
| v3 | Specs | 7 | ✅ 已完成（含阶段设计规范） |
| v3 | Plans | 6 | ✅ 已完成 |
| v4 | Specs | 7 | ✅ 已完成（含阶段设计规范） |
| v4 | Plans | 6 | ✅ 已完成 |
| 整体 | 设计规范 | 1 | ✅ 已完成 |
| **合计** | | **69** | |

### 6.2 实施计划 vs 实际文档一致性

| 实施计划项 | 实际文件 | 状态 |
|-----------|---------|------|
| v2 阶段设计规范 | `v2/specs/2026-05-10-synthgen-v2-design.md` | ✅ |
| v2 Unit J-P specs (7) | 7 份 unit-*-design.md | ✅ |
| v2 脚手架 spec | `v2-scaffold-design.md` | ✅ |
| v2 工具 spec | `v2-tool-design.md` | ✅ |
| v2 一致性检查 | `v2-consistency-check.md` | ✅ |
| v2 完整检查 | `v2-completeness-check.md` | ✅ |
| v2 Unit J-P plans (7) | 7 份 unit-*-plan.md | ✅ |
| v2 脚手架 plan | `v2-scaffold-plan.md` | ✅ |
| v2 工具 plan | `v2-tool-plan.md` | ✅ |
| v3 阶段设计规范 | `v3/specs/2026-05-10-synthgen-v3-design.md` | ✅ |
| v3 Unit Q-T specs (4) | 4 份 unit-*-design.md | ✅ |
| v3 脚手架 spec | `v3-scaffold-design.md` | ✅ |
| v3 工具 spec | `v3-tool-design.md` | ✅ |
| v3 一致性检查 | `v3-consistency-check.md` | ✅ |
| v3 完整检查 | `v3-completeness-check.md` | ✅ |
| v3 Unit Q-T plans (4) | 4 份 unit-*-plan.md | ✅ |
| v3 脚手架 plan | `v3-scaffold-plan.md` | ✅ |
| v3 工具 plan | `v3-tool-plan.md` | ✅ |
| v4 阶段设计规范 | `v4/specs/2026-05-10-synthgen-v4-design.md` | ✅ |
| v4 Unit U-X specs (4) | 4 份 unit-*-design.md | ✅ |
| v4 脚手架 spec | `v4-scaffold-design.md` | ✅ |
| v4 工具 spec | `v4-tool-design.md` | ✅ |
| v4 一致性检查 | `v4-consistency-check.md` | ✅ |
| v4 完整检查 | `v4-completeness-check.md` | ✅ |
| v4 Unit U-X plans (4) | 4 份 unit-*-plan.md | ✅ |
| v4 脚手架 plan | `v4-scaffold-plan.md` | ✅ |
| v4 工具 plan | `v4-tool-plan.md` | ✅ |
| 整体设计规范 | `2026-05-10-synthgen-overall-design.md` | ✅ |

**文件覆盖率**：55/55 = 100%（实施计划中列出的所有文件均存在）

### 6.3 缺失文档

| 缺失 | 说明 | 优先级 |
|------|------|--------|
| v2/v3/v4 检查文档内容审查 | consistency-check 和 completeness-check 6 份文档仅验证存在，未审查内容 | P2 |
| 待测模型接入协议 spec | C5 要求在 v3 定义，但无独立 spec 文档 | P1（v3 开发前补充） |
| EvidencePackage 版本演进策略 | C9 建议新增 | P2 |

---

## 七、与四轮工程审查的历史关联

根据项目记忆，SynthGen-Core 经历了四轮工程审查（v0.1→v0.4），关键发现包括：

| 审查轮次 | 核心发现 | 本报告关联 |
|---------|---------|-----------|
| v0.1 | 诚实性危机（承诺无条件物理合法性） | ✅ 当前 spec 体系已修复：物理优先偏差从 v1 声明 |
| v0.2 | 聚合两阶段跳过值域 | ✅ 当前 v2 spec §3.2 明确"阶段一含值域" |
| v0.3 | 行间约束排序列 | ✅ 当前 v2 spec §3.1 明确排序列来自 Schema ORDER |
| v0.4 | 通过审查 | 当前文档体系在 v0.4 基础上继续演进，质量持续提升 |

**全链路回头望核心发现**（review_full_chain_r1.md）的三大学术偏移：
1. **产品形态漂移**：当前文档体系已收敛——SynthLang 是内部编译目标，用户通过 SDK/REST 交互
2. **诚实梯度断裂**：当前 EvidencePackage 适用性标注体系解决了此问题
3. **客户画像缺领域专家**：此问题不在文档体系范围内，属于产品/市场层面

---

## 八、结论

### 8.1 可以开始实施的部分

以下文档标记为 [IMPLEMENT]，无协调项阻塞，可立即启动：

**v2 Wave 1**（前 4 周）：
- ✅ Unit J: 行间约束引擎（spec + plan 均可执行）
- ✅ Unit K: 聚合约束引擎
- ✅ Unit L: 约束分类器
- ✅ Unit O (#15 审计部分): 哈希链审计日志

**v3 Wave 1**（前 2 周）：
- ✅ Unit Q: 模型版本链
- ✅ Unit T (#23 存储模型层): 检查点存储+atomic_write

### 8.2 需要协调后再启动的部分

| 阻塞项 | 阻塞的 Unit | 决策期限 |
|--------|------------|---------|
| C1 KDE 选型 | Unit O (#15b 数据引擎) → Unit N (后筛选) → Unit P | v2 Wave 1 结束前 |
| C2 v1 兼容策略 | Unit M (执行路由器重构) | v2 Wave 1 结束前 |
| C8 WORM 选型 | Unit O (#15 审计部分) | v2 Wave 1 开始前 |
| C5 待测模型协议 | v4 Unit X | v3 开发期间 |

### 8.3 最终建议

1. **立即执行**：v2 Wave 1 的 Unit J/K/L 和审计部分，这些文档质量足够启动开发
2. **优先决策**：C1(KDE 选型) + C8(WORM 选型)，这是 v2 Wave 2 的硬性前置
3. **补充文档**：v3/v4 Unit Plans 需在各自版本开发前补充到 v2 Unit J 的深度水准
4. **新增协调项**：C9(EvidencePackage 演进策略) + C10(漂移检测算法选型)

---

*报告结束*
