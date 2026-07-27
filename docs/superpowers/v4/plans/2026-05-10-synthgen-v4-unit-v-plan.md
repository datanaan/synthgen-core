SynthGen Core v4 Unit V 实施计划：会话窗口
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit V 设计规范 v1.0
估算：1.5 周
依赖：Unit U (#25+#26)

---

## 概述

Unit V 交付会话窗口——基于活动间隔的自动窗口切分，完成 v4 窗口类型扩展。

---

## Task 1：SessionWindowDef 数据结构

### Step 1.1：SessionWindowDef 实现

**产出**：`src/window/session_window_def.h`, `src/window/session_window_def.cpp`

**验收**：SessionWindowDef 构造和序列化正确，gap_ms=0/-1 返回 kInvalidGapMs

### Step 1.2：WindowTypeV2 枚举扩展

**产出**：修改 `src/window/window_type_v2.h`

**验收**：新增 kSession 枚举值，与 kInterval/kRows/kPartitionBy 兼容

---

## Task 2：SessionWindowEngine 核心

### Step 2.1：会话切分算法

**产出**：`src/window/session_window_engine.h`, `src/window/session_window_engine.cpp`

**验收**：按 GAP 正确切分会话，同一会话内事件间隔 < GAP

### Step 2.2：会话内聚合计算

**验收**：聚合函数在每个会话内正确计算，结果包含会话 ID 和起止时间

### Step 2.3：多分组键支持

**验收**：多分组键独立切分，结果正确合并

---

## Task 3：边界和异常处理

### Step 3.1：GAP 边界处理

**验收**：GAP = 1ms / 1年 / 恰好=间隔 均正确处理

### Step 3.2：NULL 值和空会话

**验收**：session_column 含 NULL 正确处理，单事件会话正确

### Step 3.3：阈值控制

**验收**：会话数 > 100000 返回 kTooManySessions

---

## Task 4：与窗口系统统一集成

### Step 4.1：WindowTypeV2 路由适配

**产出**：修改 `src/window/window_router.h/cpp`（如存在）或聚合引擎路由逻辑

**验收**：kSession 类型正确路由到 SessionWindowEngine

### Step 4.2：与 Unit U 行数/分组窗口的统一接口

**验收**：三种窗口类型通过统一接口调度，互不干扰

---

## Task 5：测试

### Step 5.1：错误路径测试清单

**验收**：以下 7 个 ErrorCode 全部有对应测试用例：

| ErrorCode | 测试场景 | 用例数 |
|-----------|---------|--------|
| kInvalidGapMs | gap_ms=0, gap_ms=-1 | 2 |
| kTooManySessions | 会话数 > 100000 | 1 |
| kNullSessionColumn | 会话列含 NULL | 1 |
| kEmptyInput | 无事件输入 | 1 |
| kSessionOverflow | 单会话超过内存限制 | 1 |
| kIncompatibleWindowType | 路由器不支持 kSession | 1 |
| kSerializationError | 序列化/反序列化失败 | 1 |

### Step 5.2：功能测试

**产出**：`tests/unit/session_window_test.cpp`

**验收**：22+ 测试通过，错误测试占比 ≥ 30%

**测试分类**：
- 单元测试：16+（正常切分 8，边界 GAP 4，多分组键 4）
- 错误测试：6+（对应上述 ErrorCode）
- GAP 精确测试：3（GAP=1ms，GAP=1年，恰好=间隔）

### Step 5.3：集成测试

**产出**：`tests/integration/v4_window_types_test.cpp`

**验收**：三种窗口类型（行数/分组/会话）集成正确

**集成场景**：
- 会话窗口 + 行数窗口混合查询
- 会话窗口 + 分组窗口混合查询
- 窗口类型路由器统一调度验证

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1 | 0.125w | ⬜ |
| Task 2 | 0.5w | ⬜ |
| Task 3 | 0.25w | ⬜ |
| Task 4 | 0.25w | ⬜ |
| Task 5 | 0.375w | ⬜ |
| **合计** | **1.5w** | — |

---

## 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| 会话切分内存消耗 | 大数据集 OOM | kTooManySessions 阈值 + 流式处理 |
| GAP 边界语义歧义 | 恰好=间隔时行为不一致 | 显式定义：= GAP 切分 |
| 与 Unit U 接口变更 | 集成延迟 | 提前对齐 WindowTypeV2 定义 |
