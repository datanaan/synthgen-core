<div align="center">

# SynthGen Core

**The data infrastructure for world models — define a universe, generate every plausible version of it.**

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![GitHub stars](https://img.shields.io/github/stars/datanaan/synthgen-core?style=social)](https://github.com/datanaan/synthgen-core)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://isocpp.org)
[![CMake](https://img.shields.io/badge/CMake-3.20+-brightgreen)](https://cmake.org)
[![Tests](https://img.shields.io/badge/tests-1453%20passed-green)](https://github.com/datanaan/synthgen-core)

</div>

---

<p align="center">
  <b>🇨🇳 中文</b> | <a href="#english">🇬🇧 English</a>
</p>

---

<h2>🇨🇳 什么是 SynthGen Core？</h2>

**大多数"合成数据"工具只是 numpy 的封装。SynthGen Core 是一个数据库内核——自研 DSL、不可变审计日志、时间旅行查询，专为生成"物理世界模型"的数据而设计。**

它不是随机数据生成器——你定义物理规则和约束，SynthGen 生成每一个合理的版本，全部可追溯。

### 核心架构：三层单向依赖

```
┌─────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│   Interface      │     │     Engine        │     │     Storage       │
│                  │     │                  │     │                  │
│  SynthLang DSL   │ ──▶ │  Constraint       │ ──▶ │  Arrow/Parquet   │
│  Schema DDL      │     │  Classifier       │     │  Base Table      │
│  Python SDK      │     │  Physics Engine   │     │  (INSERT ONLY)   │
│  REST API        │     │  Router + Filter  │     │  Audit Log       │
└─────────────────┘     │  EvidencePackage   │     │  (Hash Chain)    │
                        └──────────────────┘     │  Time Travel      │
                                                 └──────────────────┘
```

### 为什么不是又一个 numpy 封装？

| 传统做法 | SynthGen Core |
|---------|---------------|
| Python 脚本调 numpy/scipy | **C++17 数据库内核**，原生性能 |
| 生成数据不可追溯 | **哈希链审计日志**，WORM 语义 |
| "这数据质量如何？" — 不知道 | **tail_report** — 每条数据告诉你它满足/近似/违反了哪些约束 |
| 只能看当前数据 | **时间旅行** — 查询任意版本历史 |
| 用 SQL 定义约束 | **SynthLang 自定义 DSL** — 物理约束、行间约束、聚合约束 |

### 快速开始

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

### 版本路线

| 版本 | 聚焦 | 状态 |
|------|------|------|
| v1 | 基础引擎：解析器、Schema、值域约束、审计 | ✅ 完成 |
| v2 | 约束完备：行间约束、聚合约束、DURING/WHEN | ✅ 完成 |
| v3 | 时间智能：版本链、GC、时间旅行、对齐 | ✅ 完成 |
| v4 | 高级分析：窗口、完备性评分 | 🚧 进行中 |

---

<h2 id="english">🇬🇧 What is SynthGen Core?</h2>

**Most "synthetic data" tools are Python wrappers around numpy. SynthGen Core is a database kernel — custom DSL, immutable audit log, time-travel queries, purpose-built for generating physically valid world model data.**

It's not a random data generator. You define the physics and constraints — SynthGen generates every plausible version, fully traceable.

### Architecture: Three-Layer, Single-Direction Dependency

| Layer | Components |
|-------|-----------|
| **Interface** | SynthLang parser, Schema DDL, Python SDK (pybind11), REST API (cpp-httplib) |
| **Engine** | Constraint classifier, Execution router (degradation paths), Physics engine (Gaussian/uniform/KDE), Post-filter, EvidencePackage builder |
| **Storage** | StorageBackend (Arrow/Parquet), Base table (INSERT ONLY), Snapshot (immutable), Model store (version chain + GC), Audit log (hash chain + WORM), Time travel engine |

### Why Not Another numpy Wrapper?

| Traditional Approach | SynthGen Core |
|--------------------|---------------|
| Python scripts calling numpy/scipy | **C++17 database kernel**, native performance |
| Generated data has no provenance | **Hash-chain audit log** — WORM semantics |
| "How good is this data?" — unknown | **tail_report** — every record tells you which constraints it satisfies/approximates/breaks |
| Current state only | **Time travel** — query any version in history |
| SQL for constraints | **SynthLang DSL** — physical, inter-row, aggregate constraints |

### Quick Start

```bash
# Build
git clone https://github.com/datanaan/synthgen-core
cd synthgen-core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Test (1453+ all pass)
cd build && ctest

# Start API server
./src/api/synthgen_api
```

### Version Roadmap

| Version | Focus | Status |
|---------|-------|--------|
| v1 | Basic engine: parser, schema, value-range, audit | ✅ Complete |
| v2 | Constraint complete: inter-row, aggregate, DURING/WHEN | ✅ Complete |
| v3 | Time intelligence: version chain, GC, time travel, alignment | ✅ Complete |
| v4 | Advanced analysis: windows, completeness scoring | 🚧 In progress |

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
