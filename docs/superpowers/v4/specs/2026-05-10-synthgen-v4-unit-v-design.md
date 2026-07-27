SynthGen Core v4 Unit V 设计规范：会话窗口
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v4 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit V 实施计划
组件：#27 会话窗口
估算：1.5 周
依赖：Unit U (#25+#26)

---

## 一、本 Unit 交付什么

**Unit V 是 v4 窗口类型扩展的完成**——会话窗口基于活动间隔自动切分，无需预定义窗口边界。

交付物：
1. **SessionWindowEngine**：基于 GAP 的会话窗口切分和聚合
2. **SessionWindowDef**：会话窗口定义（分组键 + GAP 阈值 + 聚合函数）
3. **会话切分算法**：按时间间隔自动识别会话边界
4. **与行数/分组窗口的统一接口**：通过 WindowTypeV2 统一调度

---

## 二、#27 会话窗口

### 2.1 核心语义

会话窗口（SESSION）按活动间隔自动划分——两次事件间隔超过 GAP 则切分为新会话：

- `OVER (SESSION user_id, GAP 30 MINUTES)` —— 同一用户 30 分钟内的活动归为一个会话
- 会话窗口不需要预定义边界，窗口大小由数据本身决定
- 适合用户行为分析、交易流水分析等场景

**与其他窗口类型的对比**：

| 维度 | 区间窗口 | 行数窗口 | 会话窗口 |
|------|---------|---------|---------|
| 划分依据 | 固定时间跨度 | 固定行数 | 数据驱动（GAP） |
| 窗口大小 | 固定 | 固定 | 可变 |
| 窗口边界 | 预定义 | 预定义 | 数据驱动 |
| 空窗口 | 可能 | 不可能 | 不可能 |

**切分算法**：

```
1. 按 session_column 排序分组
2. 在每个分组内，按时间排序
3. 遍历事件：相邻事件间隔 > GAP → 新会话
4. 每个会话内应用聚合函数
```

### 2.2 接口定义

（定义见 v4 阶段设计规范 3.2 节）

### 2.3 错误处理

```cpp
enum class SessionWindowErrorCode {
    kInvalidGapMs,               // gap_ms <= 0
    kUndefinedSessionColumn,     // session_column 不存在
    kEmptyBatch,                 // 空批次
    kNonTimestampSessionColumn,  // session_column 非时间类型（用于 GAP 计算）
    kAggregateFunctionMismatch,  // 聚合函数与列类型不匹配
    kTooManySessions,            // 会话数超过阈值（默认 100000）
    kSessionColumnNotSortable,   // 会话列不可排序
};
```

---

## 三、Unit V 验收标准

### 3.1 功能验收

- [ ] SESSION + GAP 正确识别会话边界
- [ ] 同一会话内事件间隔 < GAP
- [ ] 不同会话间事件间隔 >= GAP
- [ ] 单事件构成单事件会话
- [ ] 多分组键独立切分
- [ ] 聚合函数在每个会话内正确计算
- [ ] 结果包含会话 ID 和起止时间

### 3.2 错误测试验收

- [ ] kInvalidGapMs（gap_ms = 0 / -1）
- [ ] kUndefinedSessionColumn
- [ ] kEmptyBatch
- [ ] kNonTimestampSessionColumn
- [ ] kAggregateFunctionMismatch
- [ ] kTooManySessions
- [ ] kSessionColumnNotSortable

### 3.3 边界条件测试

- [ ] GAP = 1ms（极小 GAP，几乎每事件一个会话）
- [ ] GAP = 1 年（极大 GAP，几乎只有一个会话）
- [ ] 所有事件间隔 = GAP（恰好切分边界）
- [ ] 单事件批次
- [ ] 两个事件间隔恰好 = GAP（边界条件）
- [ ] session_column 含 NULL
- [ ] 100000+ 会话的性能
- [ ] 同一分组 1000 个会话

### 3.4 测试验收

- [ ] 至少 18 个测试用例
- [ ] 错误测试占比 ≥ 30%

---

## 四、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `SessionWindowEngine::compute_session_windows()` | 聚合引擎, 路由器 | 会话窗口聚合 |
| `WindowTypeV2::kSession` (新增) | 路由器 | 窗口类型判断 |
| `SessionWindowDef` | Explain | 解释会话窗口配置 |
