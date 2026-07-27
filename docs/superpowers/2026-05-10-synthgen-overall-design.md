SynthGen Core 整体设计规范
文档性质：项目级架构约束——所有版本和实施计划的共同基础
版本：v1.0
日期：2026-05-10
适用范围：SynthGen Core 全生命周期（v1-v4）
上游文档：路线图 v1.4、工程框架 v0.4、理论框架 v1.3、工程执行守则 v1.0

---

## 一、为什么需要这份规范

路线图定义了"做什么"，理论框架定义了"为什么"，工程框架定义了"怎么做"。

但有三件事横跨所有版本，必须在第一天就定死，否则每个 Unit 的 spec 都会重复定义或互相矛盾：
1. **架构分层和模块边界**——什么代码放在哪里
2. **技术栈和编码约定**——用什么语言、什么风格
3. **跨切关注点**——Trace/Explain/Metrics/测试如何贯穿所有组件

本规范就是这些"第一天定死"的决策。

---

## 二、架构分层

### 2.1 三层架构

```
┌─────────────────────────────────────────────────┐
│                  接口层 (Interface)               │
│  SynthLang Parser | Type System / Schema DDL     │
│  Python SDK      | REST API                      │
└───────────────────────┬─────────────────────────┘
                        │
┌───────────────────────┴─────────────────────────┐
│                生成引擎层 (Engine)                 │
│  约束分类器 | 执行路由器 | 后筛选保障              │
│  物理引擎   | 数据引擎   | 约束引擎               │
│  EvidencePackage 构建器                          │
└───────────────────────┬─────────────────────────┘
                        │
┌───────────────────────┴─────────────────────────┐
│                存储引擎层 (Storage)                │
│  存储抽象层 (StorageBackend)                      │
│  基表层 | 快照层 | 模型层 | 审计日志              │
└─────────────────────────────────────────────────┘
```

### 2.2 层间依赖规则

1. **单向依赖**：接口层 → 生成引擎层 → 存储引擎层。禁止反向依赖。
2. **同层可依赖**：物理引擎可依赖约束引擎（同在生成引擎层）。
3. **跨层仅通过接口**：上层依赖下层时，仅通过 StorageBackend 等抽象接口，不依赖具体实现。
4. **路由器特殊地位**：ExecutionRouter 位于生成引擎层，但作为引擎层内各组件的"调度中枢"，同层其他引擎（如 ContinuousAlignmentEngine）可依赖它。路由器不依赖同层其他引擎的业务逻辑，仅通过配置驱动调度决策。这种单向调度关系使得路由器在引擎层内具有"准上层"地位，类似操作系统中的调度器。

### 2.3 模块边界原则

- **每个模块有独立的目录和命名空间**——`synthgen::parser`、`synthgen::storage`、`synthgen::engine`
- **模块间通信通过定义的协议**——不直接访问其他模块的内部数据结构
- **模块可独立测试**——mock 掉依赖模块的接口即可测试

### 2.4 Spec 与 Plan 接口定义约定

- **阶段设计规范定义"契约级接口"**：公开 API、跨模块协议、对外暴露的数据结构——这些是所有 Unit 必须遵守的契约
- **Unit spec 仅补充"实现级接口"**：内部数据结构、算法选择、私有辅助函数——避免与阶段设计规范重复定义
- **Unit plan 引用阶段设计规范的接口**：仅描述实现步骤和验收标准，不重新定义接口签名
- **消除重复原则**：当阶段设计规范和 Unit spec 对同一接口有定义时，以阶段设计规范为准，Unit spec 仅标注差异或补充

---

## 三、目录结构

```
synthgen-core/
├── src/
│   ├── parser/              # SynthLang 解析器
│   │   ├── lexer.h/cpp
│   │   ├── ast.h/cpp        # 抽象语法树
│   │   ├── parser.h/cpp     # 语法分析
│   │   └── constraint_classifier.h/cpp  # 约束分类器（v2）
│   │
│   ├── schema/              # 类型系统 + Schema DDL
│   │   ├── types.h/cpp      # 数据类型定义
│   │   ├── schema.h/cpp     # Schema 管理
│   │   └── order_decl.h/cpp # ORDER 声明（v1 预留）
│   │
│   ├── engine/              # 生成引擎
│   │   ├── physics/         # 物理引擎
│   │   │   ├── sampler.h/cpp       # 采样器（均匀/高斯）
│   │   │   ├── rectangular.h/cpp   # 矩形域采样（v1）
│   │   │   └── rejection.h/cpp     # 拒绝采样/MCMC（v2）
│   │   ├── constraint/      # 约束引擎
│   │   │   ├── value_range.h/cpp   # 值域约束验证器
│   │   │   ├── inter_row.h/cpp     # 行间约束引擎（v2）
│   │   │   └── aggregate.h/cpp     # 聚合约束引擎（v2）
│   │   ├── router/          # 执行路由器
│   │   │   ├── classifier.h/cpp    # 约束分类器（v2）
│   │   │   ├── router.h/cpp        # 执行路由器（v2 重构）
│   │   │   └── degradation.h/cpp   # 退化路径（v2）
│   │   ├── data/            # 数据引擎
│   │   │   └── kde.h/cpp           # KDE 密度估计（v2）
│   │   ├── postfilter/      # 后筛选
│   │   │   └── postfilter.h/cpp    # 后筛选保障（v2 完整版）
│   │   └── evidence/        # EvidencePackage
│   │       ├── builder.h/cpp       # 构建器
│   │       ├── tail_report.h/cpp   # tail_report
│   │       └── schema.h/cpp        # EvidencePackage Schema 定义
│   │
│   ├── storage/             # 存储引擎
│   │   ├── backend.h/cpp           # StorageBackend 抽象接口
│   │   ├── object_store.h/cpp      # 对象存储适配
│   │   ├── parquet_io.h/cpp        # Parquet 读写
│   │   ├── metadata.h/cpp          # 元数据层
│   │   ├── base_table.h/cpp        # 基表层
│   │   ├── snapshot.h/cpp          # 快照层
│   │   ├── model_store.h/cpp       # 模型层（v3）
│   │   ├── version_chain.h/cpp     # 版本链（v3）
│   │   ├── gc.h/cpp                # GC compaction（v3）
│   │   └── audit.h/cpp             # 审计日志（v2）
│   │
│   ├── api/                 # REST API
│   │   ├── server.h/cpp
│   │   └── handlers.h/cpp
│   │
│   ├── sdk/                 # Python SDK（C 绑定）
│   │   └── bindings.cpp
│   │
│   ├── scaffold/            # 脚手架设施
│   │   ├── explain.h/cpp           # Explain
│   │   ├── trace.h/cpp             # Trace (span 定义 + 写入)
│   │   ├── metrics.h/cpp           # 可观测性 (metrics 端点)
│   │   └── test_helpers.h          # 测试辅助（头文件库）
│   │
│   └── common/              # 公共设施
│       ├── error.h/cpp             # 错误类型 + 错误传播
│       ├── result.h                # Result<T> 类型
│       ├── seed.h/cpp              # 种子控制
│       └── config.h/cpp            # 配置管理
│
├── tests/
│   ├── unit/                # 单元测试
│   ├── integration/         # 集成测试
│   ├── e2e/                 # 端到端测试
│   ├── fixtures/            # 测试数据集
│   │   └── sensor_1000.parquet   # 标准测试数据集
│   └── snapshots/           # 参考输出快照
│
├── tools/                   # 开发辅助工具（工具线）
│   ├── scaffold_templates/  # 组件模板引擎模板
│   ├── schema_checker/      # Schema 一致性校验器（Python）
│   └── trace_analyzer/      # Trace 分析工具（Python）
│
├── scripts/                 # 构建/CI 脚本
├── docs/                    # 文档
├── CMakeLists.txt
└── README.md
```

### 3.1 命名约定

| 类别 | 约定 | 示例 |
|------|------|------|
| 目录 | snake_case | `value_range/`, `post_filter/` |
| 类 | PascalCase | `RectangularSampler`, `ValueRangeValidator` |
| 函数 | snake_case | `validate_row()`, `generate_batch()` |
| 常量 | kPascalCase | `kDefaultSeed`, `kMaxExclusionRate` |
| 枚举 | PascalCase 枚举名 + kPascalCase 值 | `ExecutionMode::kRowByRow` |
| 命名空间 | synthgen::模块名 | `synthgen::engine::physics` |
| 文件 | snake_case | `value_range_validator.h/cpp` |
| 测试文件 | 模块名_test.cpp | `value_range_validator_test.cpp` |

### 3.2 头文件约定

```cpp
// 每个 .h 文件必须包含：
#pragma once

#include "synthgen/common/result.h"  // 使用 Result<T> 替代异常

namespace synthgen::engine::constraint {

class ValueRangeValidator {
public:
    // 构造函数
    explicit ValueRangeValidator(const Schema& schema);

    // 核心接口——返回 Result，不抛异常
    Result<ValidationResult> validate_row(const Row& row) const;

    // Explain 支持
    ExplainInfo explain() const;

    // Trace 支持——每个公开方法创建 span
    // Trace 内部由 RAII span guard 实现，不侵入业务逻辑

private:
    // 内部实现细节
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace synthgen::engine::constraint
```

---

## 四、技术栈

### 4.1 核心技术栈

| 层 | 技术 | 理由 |
|----|------|------|
| 核心语言 | C++17 | 团队主力语言，系统级性能 |
| 备选语言 | Rust（可选模块） | 内存安全，适合存储层和解析器 |
| 构建 | CMake 3.20+ | 业界标准，跨平台 |
| 测试 | Google Test + Google Mock | C++ 标准测试框架 |
| 列式存储 | Apache Arrow + Parquet | 高效列式 IO，生态成熟 |
| 模型推理 | ONNX Runtime / TensorRT | 不走 PyTorch Python 绑定 |
| HTTP | crow.h / cpp-httplib | 轻量 C++ HTTP 库 |
| Python 绑定 | pybind11 | C++ → Python 的标准方案 |
| CI | GitHub Actions / GitLab CI | 自动化测试 |

### 4.2 工具线技术栈

| 工具 | 技术 | 理由 |
|------|------|------|
| 组件模板引擎 | inja (C++ 头文件库) 或 Jinja2 (Python) | 模板展开，无 AI |
| 测试辅助库 | C++ 头文件库 | 与 Google Test 集成 |
| Schema 校验器 | Python (CI 脚本) | JSON/YAML 比对生态成熟 |
| Trace 分析工具 | Python (独立工具) | 规则引擎 + 终端高亮 |

### 4.3 依赖管理原则

1. **最小依赖**：每个模块只依赖必须的库
2. **头文件库优先**：inja、cpp-httplib 等零构建依赖优先
3. **系统级库允许**：Arrow、Parquet、ONNX Runtime 等必须的系统级库
4. **禁止 Python 产品代码**：Python 仅用于 SDK 绑定和工具线，不进入核心代码路径

---

## 五、跨切关注点

### 5.1 Trace（执行过程可追踪）

**所有版本、所有组件必须遵守的 Trace 规范**：

```cpp
// Trace span 定义
struct TraceSpan {
    std::string trace_id;      // = package_id（全局唯一）
    std::string span_id;       // 组件内唯一
    std::string parent_span_id; // 上游组件的 span_id
    std::string component;     // "parser" | "physics_engine" | "validator" | ...
    std::string operation;     // "parse" | "generate_batch" | "validate_row" | ...
    Timestamp start_time;
    Timestamp end_time;
    std::string status;        // "ok" | "error" | "timeout"
    std::map<std::string, std::string> attributes;  // 组件特定属性
};
```

**实现方式**：
- RAII SpanGuard：构造时创建 span，析构时写入
- 不侵入业务逻辑——SpanGuard 在函数入口创建，业务代码不变
- 每个 span 写入 EvidencePackage provenance

**v1 最小版**：每个组件产生 span，trace_id = package_id
**v2 增强**：后筛选路径实时排除率变化
**v3 增强**：持续对齐模型更新前后分布变化

### 5.2 Explain（执行计划可解释）

**所有版本、所有组件必须遵守的 Explain 规范**：

```cpp
// Explain 信息定义
struct ExplainInfo {
    ExecutionMode execution_mode;  // row_by_row | stateful_batch | two_phase
    std::string path;              // physics_sampling | constrained_fusion | ...
    ConstraintClassification constraint_classification;
    // 版本递增字段...
};
```

**实现方式**：
- 每个组件提供 `explain() const` 方法
- 生成请求前可调用 `explain()` 预览执行计划
- Explain 输出结构一旦定义，即成隐式 API，不可随意改

### 5.3 Metrics（可观测性）

**所有版本、所有组件必须遵守的 Metrics 规范**：

```cpp
// 进程内 metrics（v1）
struct Metrics {
    Counter generation_throughput;    // 生成吞吐量（行/秒）
    Histogram request_latency_ms;     // 请求完成延迟
    Gauge memory_usage_bytes;         // 内存占用
    // 版本递增字段...
};
```

**实现方式**：
- v1：进程内计数器，/metrics HTTP 端点暴露
- v2+：Prometheus 格式导出

### 5.4 错误处理

**全项目统一的错误处理策略**：

```cpp
// Result<T> 替代异常
template<typename T>
class Result {
    T value_;
    Error error_;
    bool ok_;
public:
    bool ok() const;
    const T& value() const;      // ok() 时调用
    const Error& error() const;  // !ok() 时调用
};

// Error 类型
struct Error {
    ErrorCode code;           // 机器可读
    std::string message;      // 人类可读
    std::string component;    // 错误来源组件
    std::optional<std::string> detail;  // 上下文详情
};
```

**默认错误处理策略**（与工程框架 v0.4 对齐）：
- 任何组件内部不可恢复的错误 → 请求失败 + 审计日志记录 + EvidencePackage 标记 failed
- 不回退，不重试，不静默

### 5.5 种子控制

**所有涉及随机性的组件必须遵守的种子规范**：

```cpp
struct SeedConfig {
    uint64_t global_seed;     // 用户指定或自动生成
    uint64_t request_seed;    // 每个生成请求派生的种子
    uint64_t batch_seed;      // 每个 batch 派生的种子
};
```

- 种子派生：`batch_seed = hash(request_seed + batch_index)`
- 确定性保证：相同 global_seed + 相同请求 → 相同输出
- 参考快照：固定 seed 生成的输出保存为快照，CI 自动比对

---

## 六、EvidencePackage Schema 版本策略

### 6.1 版本化规则

- Schema 版本号与产品版本号独立：`evp_v1`, `evp_v2`, `evp_v3`
- 每个版本新增字段，不修改已有字段语义
- 字段适用性标注：`always` | `data_engaged` | `aggregation_present` | `drift_available` | `post_filter_engaged` | `not_applicable`

### 6.1a EvidencePackage 版本演进策略

- C++ 端使用 struct 组合（非继承）传递版本字段：每个版本的 EP 包含上游版本的完整字段副本
- JSON Schema 端独立演进：每个版本有独立的 JSON Schema 文件，新增字段用 `"required": false`
- 演进约束：新增字段不修改已有字段的语义和名称；废弃字段标注 `"deprecated": true` 但至少保留一个版本
- 适用性枚举只增不减：新增的适用性类型（如 v2 的 `post_filter_engaged`）追加到枚举末尾

### 6.2 v1 Schema 定义

```json
{
  "$schema": "EvidencePackage/v1",
  "schema_hash": "SHA256(schema_definition)",
  "constraint_summary": {
    "type": "value_range",
    "details": [
      {"column": "temperature", "min": -10.0, "max": 45.0}
    ]
  },
  "exclusion_rate": 0.0,
  "data_grade": "physics_guaranteed",
  "row_count": 1000,
  "provenance": {
    "data_source": "/data/sensors.parquet",
    "constraints": ["safe_range"],
    "generation_params": {"seed": 42, "distribution": "uniform"},
    "trace_spans": ["..."],
    "generator_identity": "physics_sampler"
  },
  "conservative_tail_report": {
    "epistemological_bias": "physical_first",
    "tail_exclusion_statement": "Tail events systematically excluded by value range constraints",
    "exclusion_rate_by_constraint": [
      {"constraint": "safe_range", "rate": 0.0}
    ]
  },
  "audit_immutability": "not_applicable",
  "statistical_fidelity": "not_applicable",
  "drift_detection": "not_applicable",
  "constraint_type_breakdown": "not_applicable"
}
```

### 6.3 适用性标注规则

| 字段 | v1 适用性 | 说明 |
|------|----------|------|
| schema_hash | always | ✅ |
| constraint_summary | always | ✅ 仅值域 |
| exclusion_rate | always | ✅ 纯物理应为 0 |
| data_grade | always | ✅ physics_guaranteed |
| row_count | always | ✅ |
| provenance | always | ✅ 基础版 |
| conservative_tail_report | always | ✅ 偏差声明 |
| audit_immutability | always | ⬜ not_applicable |
| statistical_fidelity | data_engaged | ⬜ not_applicable |
| drift_detection | drift_available | ⬜ not_applicable |
| constraint_type_breakdown | aggregation_present | ⬜ not_applicable |
| post_filter_info | post_filter_engaged | ⬜ not_applicable |

---

## 七、三线并行原则

### 7.1 明线（功能组件）

路线图定义的 #1-#30 功能组件。验收标准：功能正确、接口可用。

### 7.2 暗线（脚手架工程）

六类脚手架设施。验收标准：**与功能组件享有同等地位，脚手架不过=版本不交付**。

| 脚手架 | v1 交付 | v2 增强 | v3 增强 | v4 增强 |
|--------|---------|---------|---------|---------|
| Explain | 路由决策+约束分类+执行模式 | 排除率预估+体积比 | compaction影响 | 完备度评分影响 |
| Trace | span结构+trace_id | 后筛选实时排除率 | 模型更新变化 | 反例搜索轨迹 |
| 可观测性 | 吞吐量+延迟+内存 | 排除率趋势+退化路径命中率 | 版本链状态+GC历史 | — |
| 确定性测试 | seed固定+快照+Schema验证 | 5条退化路径回归 | compaction一致性 | — |
| CI/CD | 基础设施+标准数据集 | — | — | — |
| 错误注入 | — | 后筛选+数据引擎故障 | compaction冲突 | — |

### 7.3 工具线（开发辅助工具）

四个开发辅助工具。验收标准：产出必须可验证。

| 工具 | v1 | v2 | v3 | v4 |
|------|----|----|----|-----|
| 组件模板引擎 | v0.1 | v0.2 | v0.3 | — |
| 测试辅助库 | v0.1 | v0.2 | v0.3 | — |
| Schema 校验器 | — | v1.0 | v1.1 | — |
| Trace 分析工具 | — | v0.1 | v0.2 | v0.3 |

---

## 八、测试策略

### 8.1 测试分层

| 层级 | 内容 | 运行时机 |
|------|------|---------|
| 单元测试 | 每个类的公开方法 | 每次 PR |
| 单元测试（错误路径） | 非法参数、边界条件、资源缺失 | 每次 PR |
| 集成测试 | 模块间协议正确性 | 每次 PR |
| 集成测试（错误传播） | 错误跨模块传播、降级行为 | 每次 PR |
| 端到端测试 | 完整生成流程（正向） | 每次 PR + 每夜 |
| 端到端测试（错误场景） | 非法请求、超时、资源耗尽 | 每次 PR |
| 参考快照 | 固定 seed 输出比对 | 每次 PR |
| Schema 验证 | EvidencePackage 结构正确性 | 每次 PR |
| 边界条件测试 | 空输入、极大/极小值、溢出 | 每次 PR |
| 负面测试 | 非法状态、恶意输入、并发冲突 | 每次 PR |

### 8.1a 错误测试规范（所有版本必须遵守）

**每个组件的测试必须覆盖以下错误场景**：

| 错误类别 | 测试场景 | 验收标准 |
|---------|---------|---------|
| **非法参数** | NULL/空指针、空字符串、负数（要求正数时）、超大值 | 返回明确的 ErrorCode，不崩溃 |
| **边界条件** | 最小值-ε、最小值、最大值、最大值+ε、零值、空集合 | 行为符合预期（拒绝或正确处理） |
| **资源缺失** | 文件不存在、目录无权限、磁盘满、内存不足 | 返回对应 ErrorCode，资源泄漏检测 |
| **状态错误** | 未初始化调用、重复初始化、已关闭后调用 | 返回 kInvalidState，不崩溃 |
| **并发冲突** | 多线程同时读写、读写竞争 | 线程安全，数据一致 |
| **数据损坏** | 非法格式、CRC 失败、截断文件 | 返回 kDataCorruption，不静默继续 |
| **超时/性能** | 超长输入、无限循环检测、慢查询 | 可超时中断，返回 kTimeout |

**错误测试占比要求**：
- 单元测试中错误路径测试用例占比 ≥ 30%
- 每个公开方法至少 1 个错误路径测试用例
- 每个 ErrorCode 至少 1 个测试用例触发

### 8.2 标准测试数据集

- **sensor_1000.parquet**：1000 行传感器数据，包含 timestamp/temperature/pressure/vibration/status 列
- 每个版本使用相同的标准数据集
- 新增测试数据集需在 v1 spec 中定义

### 8.3 确定性保证

- 固定 seed → 固定输出（逐行可比对）
- 参考快照存入 `tests/snapshots/`，CI 自动比对
- 快照不一致 = 测试失败

---

## 九、与工程执行守则的关系

本规范定义技术层面的"怎么做"。工程执行守则定义文化层面的"在压力下怎么选"。

两者互补，缺一不可：
- 本规范的约束是技术性的——违反会导致编译错误或测试失败
- 守则的约束是文化性的——违反不会导致编译错误，但会导致体系瓦解

**关键交叉点**：

| 守则底线 | 本规范对应 | 违反检测 |
|---------|-----------|---------|
| 诚实优先 | EvidencePackage 字段适用性标注 | Schema 校验器可自动检测 |
| 能力里程碑 | 版本间依赖图 | CI 只测当前版本范围 |
| 数据库内核 | 架构分层（禁止反向依赖） | 代码审查 |
| 理论确定项不可修改 | EvidencePackage Schema 版本策略 | Schema 校验器可检测字段变更 |
| 暗线不被挤占 | 测试策略（脚手架测试=功能测试） | CI 脚手架测试失败=构建失败 |

---

## 十、规范更新规则

1. 本规范一旦定稿，后续修改需经过与路线图 v1.4 同等的审查
2. 新增版本（v2/v3/v4）可在本规范基础上新增章节，不修改已有章节语义
3. 紧急修正（如发现技术选型错误）走理论框架修订流程
