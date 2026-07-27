SynthGen Core v2 Unit J 实施计划：行间约束引擎
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit J 设计规范 v1.0
估算：1.5 周
依赖：v1 #5 物理引擎 + v1 #6 值域验证器

---

## 概述

Unit J 交付行间约束引擎——v2 三类约束体系中"行间"层的核心实现。包含 batch 有状态执行、frame buffer、ORDER 列绑定、Parser 行间约束语法扩展。

---

## Task 1：Parser 行间约束语法扩展

**目标**：Parser 识别 [t]/[t-1] 语法，输出 InterRowConstraintDef

### Step 1.1：Token 扩展

**做什么**：为行间约束语法添加新 Token

**产出**：`src/parser/token.h`（扩展）

```cpp
// 新增 Token
S_LBRACKET,  // [
S_RBRACKET,  // ]
K_ABS,       // ABS() 函数
```

**验收**：新 Token 类型可被 Lexer 识别

### Step 1.2：行间引用 AST 节点

**做什么**：定义 InterRowRef 和 InterRowConstraintItem AST 节点

**产出**：`src/parser/ast.h`（扩展）

（定义见 Unit J 设计规范 2.4 节）

**验收**：AST 节点覆盖行间约束语法

### Step 1.3：Parser 行间约束解析

**做什么**：实现行间约束语法解析

**关键逻辑**：
- 识别 `column[t]` 语法
- 识别 `column[t-1]` 语法
- 解析 `|x[t] - x[t-1]| < delta_max` 形式
- 解析 `x[t] > x[t-1]` 单调性约束
- 校验列名存在性和类型

**验收**：
- [ ] 合法行间约束正确解析
- [ ] 引用不存在的列返回 kUndefinedColumn
- [ ] 引用非数值列返回 kTypeMismatch
- [ ] [t-3] 等不支持偏移量返回 kInvalidOffset
- [ ] 无 ORDER 列的 Schema 上行间约束返回 kOrderColumnRequired

### Step 1.4：Parser 扩展测试

**做什么**：编写行间约束语法解析测试

**产出**：`tests/unit/inter_row_parser_test.cpp`

**测试用例**（至少 10 个）：
- delta_max 约束解析
- delta_min 约束解析
- 单调性约束解析
- ABS 函数约束解析
- 不存在的列引用
- 非数值列引用
- 不支持的偏移量
- 无 ORDER 列的 Schema
- 多个行间约束同时定义
- 混合值域+行间约束

**验收**：10+ 测试用例全通过

---

## Task 2：InterRowEngine 核心实现

**目标**：实现行间约束引擎的核心逻辑

### Step 2.1：InterRowState 和 FrameBuffer

**做什么**：实现 batch 间状态传递和帧缓冲区

**产出**：`src/engine/constraint/inter_row_state.h`, `src/engine/constraint/inter_row_state.cpp`

**关键逻辑**：
- InterRowState：记录上一 batch 最后一行的值
- FrameBuffer：deque 实现，存储最后 N 行
- push 时检查 max_size，超出则 pop_front
- 清空时重置状态

**验收**：
- [ ] 状态传递在 batch 间正确
- [ ] FrameBuffer 大小限制生效
- [ ] 清空后状态重置

### Step 2.2：ORDER 列排序

**做什么**：在执行行间约束前按 ORDER 列排序

**产出**：`src/engine/constraint/inter_row_engine.cpp`（部分）

**关键逻辑**：
- 从 Schema 获取 ORDER 列名
- 按 ORDER 列升序排序 batch
- ORDER 列有 NULL 值时：
  - NOT NULL 列：报错 kOrderColumnNull
  - 可 NULL 列：NULL 行排最后，不参与行间检查

**验收**：
- [ ] 按 ORDER 列升序排序正确
- [ ] NOT NULL 列有 NULL 时报错
- [ ] 可 NULL 列 NULL 行排最后

### Step 2.3：行间约束检查

**做什么**：实现逐行行间约束检查

**产出**：`src/engine/constraint/inter_row_engine.h`, `src/engine/constraint/inter_row_engine.cpp`

**关键逻辑**：
- 按 ORDER 列排序后逐行遍历
- 每行与前一行比较：
  - kDeltaMax: |current - previous| < delta_max
  - kDeltaMin: |current - previous| > delta_min
  - kMonotoneIncrease: current > previous
  - kMonotoneDecrease: current < previous
- 第一行：使用 incoming_state（如果有）作为 previous
- 最后一行：写入 outgoing_state

**验收**：
- [ ] delta_max 约束检查正确
- [ ] delta_min 约束检查正确
- [ ] 单调性约束检查正确
- [ ] 跨 batch 状态传递正确
- [ ] 过滤率计算正确

### Step 2.4：批量执行接口

**做什么**：实现 execute_batch 接口

**产出**：`src/engine/constraint/inter_row_engine.cpp`（补充）

**关键逻辑**：
- 输入：batch + incoming_states
- 处理：排序 → 逐行检查 → 生成过滤后 batch
- 输出：filtered_batch + outgoing_states + 统计信息

**验收**：
- [ ] execute_batch 接口与设计规范一致
- [ ] 多个行间约束同时生效
- [ ] 统计信息（rows_passed/rows_filtered/filter_rate）正确

---

## Task 3：错误处理和边界条件

**目标**：完善错误处理和边界条件覆盖

### Step 3.1：错误路径实现

**做什么**：实现所有 ErrorCode 对应的错误路径

**产出**：`src/engine/constraint/inter_row_engine.cpp`（补充）

**验收**：
- [ ] kOrderColumnRequired：有行间约束但 Schema 无 ORDER 列
- [ ] kUndefinedColumn：约束列不存在
- [ ] kTypeMismatch：约束列类型非数值
- [ ] kInvalidDelta：delta_max ≤ 0
- [ ] kEmptyBatch：空 batch 输入
- [ ] kOrderColumnNull：NOT NULL 列有 NULL
- [ ] kInvalidOffset：行间引用偏移量不支持

### Step 3.2：边界条件测试

**做什么**：编写边界条件测试用例

**产出**：`tests/unit/inter_row_boundary_test.cpp`

**测试用例**（至少 9 个）：
- batch 大小 = 1
- batch 大小 = 2
- 冷启动（无 incoming_state）
- 所有行过滤（filter_rate = 1.0）
- 没有行被过滤（filter_rate = 0.0）
- delta_max 极小（接近 0）
- delta_max 极大（DBL_MAX）
- ORDER 列值重复
- frame buffer 跨 batch 边界

**验收**：9+ 边界条件测试通过

---

## Task 4：脚手架集成

**目标**：为 InterRowEngine 添加 Trace/Explain/Metrics

### Step 4.1：Trace span 集成

**做什么**：为 execute_batch 添加 span 创建

**实现方式**：RAII SpanGuard

```cpp
Result<InterRowResult> InterRowEngine::execute_batch(...) {
    SpanGuard span("inter_row_engine", "execute_batch", trace_id_);
    // ...
    span.set_attribute("rows_filtered", result.rows_filtered);
    span.set_attribute("filter_rate", result.filter_rate);
    return result;
}
```

**验收**：每次 execute_batch 产生 span

### Step 4.2：Explain 接口

**做什么**：为 InterRowEngine 添加 explain() 方法

```cpp
struct InterRowExplainInfo {
    std::vector<std::string> constraints;
    std::string execution_mode = "stateful_batch";
    std::string order_column;
    int frame_buffer_size;
};

ExplainInfo InterRowEngine::explain() const;
```

**验收**：explain() 返回约束列表 + 执行模式 + ORDER 列 + 帧缓冲区大小

### Step 4.3：Metrics 注册

**做什么**：注册 InterRowEngine 相关 metrics

```
inter_row_rows_checked     — 检查的行数
inter_row_rows_filtered   — 过滤的行数
inter_row_filter_rate      — 过滤率
inter_row_execute_ms       — 执行耗时
```

**验收**：metrics 端点暴露上述指标

---

## Task 5：集成测试

**目标**：端到端行间约束流程测试

### Step 5.1：集成测试

**做什么**：编写行间约束引擎集成测试

**产出**：`tests/integration/inter_row_integration_test.cpp`

**测试用例**（至少 8 个）：
- 完整流程：定义 Schema(ORDER) → 定义行间约束 → 生成 → 检查
- 跨 batch 状态传递
- 混合值域+行间约束
- 后筛选排除率联动
- Trace span 完整性
- Explain 输出正确性
- 与 v1 物理引擎协同
- 5 条退化路径中行间引擎参与的路由测试

**验收**：8+ 集成测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: Parser 扩展 | 4 | 0.25w | ⬜ |
| Task 2: 核心实现 | 4 | 0.75w | ⬜ |
| Task 3: 错误处理 | 2 | 0.25w | ⬜ |
| Task 4: 脚手架 | 3 | 0.125w | ⬜ |
| Task 5: 集成测试 | 1 | 0.125w | ⬜ |
| **合计** | **14** | **1.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 行间约束跨 batch 状态语义理解偏差 | v1 阶段完成行间约束语义设计文档审查 |
| ORDER 列 NULL 值处理策略不一致 | 明确策略：NOT NULL 列报错，可 NULL 列排最后 |
| Parser [t] 语法与已有 [] 冲突 | Lexer 区分：IDENT 后 [ 为行间引用，其他为语法元素 |
| frame buffer 大小不够覆盖复杂行间约束 | 默认 kFrameBufferSize = 2，未来可配置 |
