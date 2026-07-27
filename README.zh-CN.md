<div align="center">

# SynthGen Core

**世界模型的数据基础设施。定义物理规则——SynthGen 生成每一个合理的版本，全部可追溯。**

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://isocpp.org)
[![Tests](https://img.shields.io/badge/tests-1453%20passed-green)](https://github.com/datanaan/synthgen-core)

</div>

**中文** | [English](README.md)

---

## 问题

大多数"合成数据"工具只是 numpy/scipy 的薄封装。它们从分布中采样，称之为"合成数据"，然后走人。结果是**统计上貌似合理，物理上毫无意义**——而且完全不可追溯。

如果你在构建**世界模型**或**物理仿真**，你需要根本不同的东西：

- 数据必须尊重**真实的物理约束**（不仅仅是统计分布）
- 数据必须**完全可追溯**——每条记录都知道它从哪里来，满足哪些约束
- 需要**自定义 DSL**，让领域专家不用写 C++ 就能定义模式和约束
- 需要**不可变审计追踪**，证明数据未被篡改

**SynthGen Core 是一个 C++17 数据库内核，专为此而生。** 它不是 Python 库。它是一个引擎。

---

## 运行时实际流程

```
用户写 SynthLang DSL → 解析器生成 AST → Schema/约束注册
    → RectangularSampler 生成数据（Gaussian/Uniform/KDE）
    → ValueRangeValidator 逐行检查约束
    → EvidencePackageBuilder 打包结果 + tail_report + provenance
    → AuditLog 追加哈希链记录
    → Storage (Arrow/Parquet) 持久化，带版本链 + 时间旅行
```

### 为什么它是"世界模型的数据基础设施"？

世界模型的核心能力是模拟"what if"——不是在内插现有数据点。SynthGen Core 被设计为**世界模型的数据生成层**：

| 能力 | 意义 |
|------|------|
| **物理优先采样** | Gaussian、Uniform、KDE——不仅仅是"随机数" |
| **约束卡** | 值域、行间、聚合、DURING/WHEN——真实物理规则 |
| **tail_report** | 每条数据知道它满足/近似/违反了哪些约束 |
| **哈希链审计** | 不可变溯源——证明每条数据从哪里来 |
| **时间旅行** | 查询任意版本——`AS OF` 语义 |
| **漂移检测** | 版本间 KS 统计量分布对比 |
| **自定义 DSL** | 领域专家写 SynthLang，不用写 C++ |
| **1453+ 测试** | 100% 通过率——单元、集成、E2E、混沌、性能 |

### 具体例子

```synthlang
-- 定义传感器数据模式
DEFINE TYPE sensor_log {
    timestamp: DATETIME NOT NULL ORDER,
    wind_speed: FLOAT [0.0, 50.0],
    temperature: FLOAT [-50.0, 80.0],
    status: ENUM('normal', 'warning', 'fault')
};

-- 定义物理约束
DEFINE CONSTRAINT wind_safety ON sensor_log {
    wind_speed BETWEEN 0 AND 25 DURING status = 'normal_operation',
    temperature > -40 WHEN wind_speed > 20,
    vibration[t] - vibration[t-1] < 5.0,
    AVG(temperature) OVER (INTERVAL 1 HOUR) <= 40.0
};
```

---

## 架构

```
src/
├── parser/          # SynthLang DSL：词法分析 → 语法分析 → AST
├── schema/          # Schema 定义 + 注册 + 构建
├── engine/
│   ├── physics/     # RectangularSampler, Gaussian/Uniform/KDE
│   ├── constraint/  # 值域、行间、聚合约束验证
│   ├── evidence/    # EvidencePackage + TailReport + Provenance
│   ├── router/      # 约束分类器 + 执行路由
│   ├── postfilter/  # 生成后过滤
│   └── alignment/   # 漂移检测 + 持续对齐
├── storage/
│   ├── audit/       # 哈希链不可变审计日志
│   ├── version/     # 模型版本链 + GC
│   ├── timetravel/  # AS OF 时间旅行查询
│   └── gc/          # 压缩 + 偏差报告
├── api/             # REST API (cpp-httplib)
├── sdk/             # Python 绑定 (pybind11)
└── scaffold/        # Explain, Trace, Metrics
```

---

## 快速开始

```bash
# 构建
git clone https://github.com/datanaan/synthgen-core
cd synthgen-core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 测试（1453+ 全部通过）
cd build && ctest

# 启动 API 服务
./src/api/synthgen_api
```

### Python SDK

```python
import synthgen_core as sc

# 定义 schema
schema = sc.define_type("sensor_log", {
    "timestamp": "DATETIME NOT NULL ORDER",
    "temperature": "FLOAT [-50.0, 80.0]",
})

# 生成 1000 行
result = sc.generate(schema, constraints, limit=1000)
print(result.tail_report)  # 哪些约束被满足？
```

---

## License

Apache 2.0 — 详见 [LICENSE](LICENSE)。
