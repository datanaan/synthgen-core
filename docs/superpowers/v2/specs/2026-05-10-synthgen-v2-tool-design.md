SynthGen Core v2 工具线设计规范
文档性质：工具线级设计规范 [COORDINATE]
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：v2 工具线实施计划
组件：v2 工具线 4 项
估算：1 周
依赖：v2 功能组件
协调项：C7（Python 工具链团队接受度）

---

## 一、本 Unit 交付什么

v2 工具线在 v1 基础上新增/增强 4 项工具：

| 工具 | v2 交付内容 | 估算 |
|------|-----------|------|
| 组件模板引擎 v0.2 | 增加 v2 新组件类型模板 | 0.25w |
| 测试辅助库 v0.2 | 退化路径参数化测试宏 | 0.25w |
| Schema 校验器 v1.0 | 编译期校验 + 三方 diff | 0.5w |
| Trace 分析工具 v0.1 | 规则引擎 + span 异常检测 | 0.5w |

---

## 二、组件模板引擎 v0.2

### 2.1 v0.1 → v0.2 变更

- 新增行间引擎模板
- 新增聚合引擎模板
- 新增约束分类器模板
- 新增执行路由器模板
- 新增数据引擎模板
- 新增审计日志模板

### 2.2 验收标准

- [ ] 从 #10-#17 任一新组件接口描述生成骨架代码
- [ ] 骨架代码能通过编译和基础 CI

---

## 三、测试辅助库 v0.2

### 3.1 新增宏

```cpp
// 退化路径参数化测试
TEST_DEGRADATION_PATH(router, schema, constraints, expected_path);

// 后筛选排除率断言
ASSERT_EXCLUSION_RATE_BAND(result, expected_band);

// 审计日志哈希链完整性断言
ASSERT_AUDIT_CHAIN_VALID(audit_log);

// 数据引擎 KDE 精度断言
ASSERT_KDE_DISTRIBUTION_CLOSE(engine, expected_mean, expected_std, tolerance);
```

### 3.2 验收标准

- [ ] TEST_DEGRADATION_PATH 宏能自动测试 5 条退化路径路由正确性
- [ ] 新增宏在标准数据集上通过

---

## 四、Schema 校验器 v1.0

### 4.1 核心功能

三方 diff：代码接口注册 ↔ EvidencePackage Schema ↔ 理论框架承诺清单

### 4.2 接口注册机制

每个组件编译时自动生成接口描述 .json：

```json
{
  "component": "InterRowEngine",
  "namespace": "synthgen::engine::constraint",
  "methods": [
    {"name": "execute_batch", "params": ["ArrowBatch", "InterRowState"], "returns": "InterRowResult"}
  ],
  "errors": ["kOrderColumnRequired", "kUndefinedColumn", ...],
  "spans": ["execute_batch"],
  "metrics": ["inter_row_rows_checked", "inter_row_filter_rate"]
}
```

### 4.3 校验规则

| 校验项 | 规则 | 错误级别 |
|--------|------|---------|
| 字段名拼写 | 代码接口注册 vs Schema 定义 | 高 |
| 必选字段存在性 | Schema 定义 vs 实际填充 | 高 |
| 枚举值匹配 | 适用性标注 vs 实际值 | 中 |
| 适用性标记缺失 | always 字段必须有填充逻辑 | 高 |

### 4.4 [COORDINATE] Python 工具链接受度

Schema 校验器用 Python 实现（CI 脚本）。C++ 团队维护 Python 脚本的认知负担需纳入风险。

> 如果团队不接受 Python 工具链，可用 C++ 实现简化版（仅做编译期静态断言，不做三方 diff）。

### 4.5 验收标准

- [ ] 字段 diff 能发现人为引入的接口注册与 Schema 定义的不一致
- [ ] 故意拼错一个字段名，校验器能检出
- [ ] 集成为 CI 步骤，编译后自动运行

---

## 五、Trace 分析工具 v0.1

### 5.1 规则引擎

4 条核心规则：

| 规则 | 条件 | 标记 |
|------|------|------|
| R1 | span.status == "error" | 🔴 标红 |
| R2 | span.duration > P99_threshold | 🟡 标黄 |
| R3 | exclusion_rate 连续 3 个 span 上升 | 🔴 标红（排除率飙升） |
| R4 | span.path != expected_path | 🟡 标黄（退化路径意外命中） |

### 5.2 输出

- 终端高亮 + JSON 报告
- 输入：EvidencePackage 中的 trace 字段（JSON）

### 5.3 验收标准

- [ ] 构造排除率飙升的 Trace，工具能标红对应 span
- [ ] 构造 duration 超阈值 span，工具能标黄
- [ ] JSON 报告格式正确

---

## 六、[COORDINATE] Python 工具链风险评估

| 工具 | 实现语言 | 风险 |
|------|---------|------|
| 组件模板引擎 | inja(C++) 或 Jinja2(Python) | 低（inja 无 Python 依赖）|
| 测试辅助库 | C++ | 无 |
| Schema 校验器 | Python | 中（CI 脚本，非产品代码）|
| Trace 分析工具 | Python | 中（独立工具，非产品代码）|

**缓解**：
- Python 工具限于 CI 脚本和独立工具，不涉及产品代码
- 模板引擎优先用 inja(C++)
- Schema 校验器可回退到 C++ 编译期静态断言
