SynthGen Core v2 Unit K 实施计划：聚合约束引擎
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit K 设计规范 v1.0
估算：1.5 周
依赖：v1 #6 值域验证器 + #10 行间引擎

---

## 概述

Unit K 交付聚合约束引擎——v2 两阶段执行模型的核心实现。包含时间窗口划分、聚合函数计算、窗口排除率、Parser 聚合约束语法扩展。

---

## Task 1：Parser 聚合约束语法扩展

**目标**：Parser 识别 OVER/INTERVAL 语法

### Step 1.1：Token 扩展

**做什么**：为聚合约束语法添加新 Token

**产出**：`src/parser/token.h`（扩展）

```cpp
// 新增 Token
K_AVG, K_SUM, K_MIN, K_MAX, K_COUNT,
K_OVER, K_INTERVAL,
K_HOURS, K_MINUTES, K_SECONDS, K_DAYS,
```

**验收**：新 Token 类型可被 Lexer 识别

### Step 1.2：聚合约束 AST 节点

**做什么**：定义 AggregateConstraintDef AST 节点

**产出**：`src/parser/ast.h`（扩展）

```cpp
struct AggregateConstraintItem {
    std::string column_name;
    AggregateFunction function;
    WindowType window_type;
    std::string window_spec;
    std::optional<double> min_val;
    std::optional<double> max_val;
};
```

**验收**：AST 节点覆盖聚合约束语法

### Step 1.3：Parser 聚合约束解析

**做什么**：实现聚合约束语法解析

**关键逻辑**：
- 识别聚合函数名（AVG/SUM/MIN/MAX/COUNT）
- 解析 OVER (INTERVAL ...) 子句
- 校验列名存在性和类型
- 校验 ORDER 列为 DATETIME 类型

**验收**：
- [ ] 合法聚合约束正确解析
- [ ] 不支持的聚合函数返回错误
- [ ] 窗口语法错误返回错误
- [ ] 聚合列不存在返回错误
- [ ] 时间窗口要求 ORDER 列为 DATETIME

### Step 1.4：Parser 扩展测试

**做什么**：编写聚合约束语法解析测试

**产出**：`tests/unit/aggregate_parser_test.cpp`

**测试用例**（至少 8 个）：
- AVG 约束解析
- SUM/MIN/MAX/COUNT 约束解析
- INTERVAL 语法解析
- 不支持的聚合函数
- 窗口语法错误
- 聚合列不存在
- 时间窗口非 DATETIME
- 混合值域+行间+聚合约束

**验收**：8+ 测试用例全通过

---

## Task 2：时间窗口划分

**目标**：实现时间窗口（INTERVAL）划分

### Step 2.1：时间窗口解析

**做什么**：解析 INTERVAL 规格为秒数

**产出**：`src/engine/constraint/window_spec.h`, `src/engine/constraint/window_spec.cpp`

**关键逻辑**：
- "1 HOUR" → 3600 秒
- "5 MINUTES" → 300 秒
- "30 SECONDS" → 30 秒
- "1 DAY" → 86400 秒

**验收**：时间规格解析正确

### Step 2.2：窗口划分

**做什么**：按时间窗口划分 batch 数据

**产出**：`src/engine/constraint/window_partition.h`, `src/engine/constraint/window_partition.cpp`

**关键逻辑**：
- 按 ORDER 列（DATETIME）排序
- 按 INTERVAL 划分为连续时间窗口
- 头尾不完整窗口标记为 partial
- 每个窗口记录包含的行索引

**验收**：
- [ ] 连续窗口划分正确
- [ ] partial 窗口标记正确
- [ ] 空数据时返回空窗口列表
- [ ] 所有行属于恰好一个窗口

---

## Task 3：聚合函数实现

**目标**：实现 AVG/SUM/MIN/MAX/COUNT 聚合函数

### Step 3.1：聚合函数实现

**做什么**：实现各聚合函数

**产出**：`src/engine/constraint/aggregate_functions.h`, `src/engine/constraint/aggregate_functions.cpp`

```cpp
class AggregateFunctionExecutor {
public:
    static Result<double> avg(const ArrowBatch& batch, int64_t col_idx,
                               const AggregationWindow& window);
    static Result<double> sum(const ArrowBatch& batch, int64_t col_idx,
                               const AggregationWindow& window);
    static Result<double> min(const ArrowBatch& batch, int64_t col_idx,
                               const AggregationWindow& window);
    static Result<double> max(const ArrowBatch& batch, int64_t col_idx,
                               const AggregationWindow& window);
    static Result<double> count(const ArrowBatch& batch, int64_t col_idx,
                                 const AggregationWindow& window);
};
```

**验收**：
- [ ] 各聚合函数计算正确
- [ ] 空窗口返回 kEmptyWindow
- [ ] SUM 溢出返回 kOverflow
- [ ] AVG 在全 NULL 列返回 kEmptyWindow

### Step 3.2：聚合函数测试

**做什么**：编写聚合函数单元测试

**产出**：`tests/unit/aggregate_functions_test.cpp`

**测试用例**（至少 12 个）：
- AVG 基础计算
- SUM 基础计算
- MIN/MAX 基础计算
- COUNT 基础计算
- 空窗口
- 含 NULL 值的窗口
- SUM 溢出
- 全负数 AVG
- 全相同值
- 单行窗口
- 大窗口（1000+行）
- 精度验证（浮点）

**验收**：12+ 测试用例全通过

---

## Task 4：AggregateEngine 核心实现

**目标**：实现两阶段执行引擎

### Step 4.1：阶段一执行

**做什么**：实现阶段一（值域+行间逐行过滤）

**产出**：`src/engine/constraint/aggregate_engine.cpp`（部分）

**关键逻辑**：
- 调用 ValueRangeValidator::validate_batch
- 调用 InterRowEngine::execute_batch
- 合并过滤结果

**验收**：
- [ ] 阶段一正确调用值域+行间引擎
- [ ] 过滤结果正确合并

### Step 4.2：阶段二执行

**做什么**：实现阶段二（窗口聚合验证）

**产出**：`src/engine/constraint/aggregate_engine.cpp`（补充）

**关键逻辑**：
- 在阶段一输出上划分时间窗口
- 对每个窗口计算聚合值
- 检查聚合值是否满足约束
- 计算窗口排除率
- 标记 partial_window_excluded

**验收**：
- [ ] 阶段二聚合验证正确
- [ ] 窗口排除率计算正确
- [ ] partial_window 标记正确
- [ ] 整体排除率计算正确

### Step 4.3：两阶段整合

**做什么**：实现 execute 接口（整合阶段一和阶段二）

**产出**：`src/engine/constraint/aggregate_engine.h`, `src/engine/constraint/aggregate_engine.cpp`

**验收**：
- [ ] execute 接口与设计规范一致
- [ ] 两阶段执行流程正确
- [ ] 结果包含完整的排除率信息

---

## Task 5：错误处理和边界条件

**目标**：完善错误处理和边界条件覆盖

### Step 5.1：错误路径实现

**做什么**：实现所有 ErrorCode 对应的错误路径

**验收**：
- [ ] 全部 AggregateErrorCode 有对应处理路径
- [ ] 每个错误路径至少 1 个测试用例

### Step 5.2：边界条件测试

**做什么**：编写边界条件测试

**产出**：`tests/unit/aggregate_boundary_test.cpp`

**测试用例**（至少 8 个）：
- 窗口大小 = 1
- 窗口大小 = 数据总量
- 所有窗口违反约束
- 没有窗口违反约束
- 极大窗口
- 极小窗口
- 阶段一过滤率极高时阶段二窗口
- 多个聚合约束同时生效

**验收**：8+ 边界条件测试通过

---

## Task 6：脚手架集成

**目标**：为 AggregateEngine 添加 Trace/Explain/Metrics

### Step 6.1：Trace span 集成

**做什么**：为两阶段执行添加 span

- 阶段一：span(component="aggregate_engine", operation="phase_one")
- 阶段二：span(component="aggregate_engine", operation="phase_two")

**验收**：两阶段执行分别产生子 span

### Step 6.2：Explain 接口

**做什么**：为 AggregateEngine 添加 explain() 方法

```cpp
struct AggregateExplainInfo {
    std::vector<std::string> aggregate_constraints;
    std::string execution_mode = "two_phase";
    std::vector<std::string> window_specs;
    int phase_one_constraints;
    int phase_two_constraints;
};
```

**验收**：explain() 返回两阶段信息

### Step 6.3：Metrics 注册

**做什么**：注册 AggregateEngine 相关 metrics

```
aggregate_phase_one_ms       — 阶段一执行耗时
aggregate_phase_two_ms       — 阶段二执行耗时
aggregate_windows_total      — 总窗口数
aggregate_windows_violated   — 违反约束窗口数
aggregate_exclusion_rate     — 窗口排除率
```

**验收**：metrics 端点暴露上述指标

---

## Task 7：集成测试

**目标**：端到端聚合约束流程测试

### Step 7.1：集成测试

**产出**：`tests/integration/aggregate_integration_test.cpp`

**测试用例**（至少 8 个）：
- 完整两阶段流程
- 纯值域约束（无阶段二）
- 值域+行间+聚合混合约束
- 时间窗口排除率验证
- partial_window 边界验证
- Trace span 完整性
- Explain 输出正确性
- 与执行路由器协同（模拟）

**验收**：8+ 集成测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: Parser 扩展 | 4 | 0.25w | ⬜ |
| Task 2: 窗口划分 | 2 | 0.25w | ⬜ |
| Task 3: 聚合函数 | 2 | 0.25w | ⬜ |
| Task 4: 核心实现 | 3 | 0.25w | ⬜ |
| Task 5: 错误处理 | 2 | 0.25w | ⬜ |
| Task 6: 脚手架 | 3 | 0.125w | ⬜ |
| Task 7: 集成测试 | 1 | 0.125w | ⬜ |
| **合计** | **17** | **1.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 两阶段语义理解偏差 | 参考 SQL 窗口函数语义，保持一致 |
| 时间窗口划分与 SQL OVER 语义不完全对齐 | 明确文档声明：v2 仅支持 INTERVAL 窗口，语义参考 PostgreSQL |
| 阶段一过滤后行数极少导致聚合无意义 | 阶段二检查窗口内行数，过少时标记 partial |
| partial_window 语义在各场景下不一致 | 统一定义：窗口不完整 = 开头/结尾窗口 + 过滤后行数不足 |
