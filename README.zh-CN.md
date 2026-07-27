<div align="center">

# SynthGen Core

**世界模型的数据基础设施——定义宇宙规则，生成每一个合理的版本。**

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://isocpp.org)
[![CMake](https://img.shields.io/badge/CMake-3.20+-brightgreen)](https://cmake.org)
[![Tests](https://img.shields.io/badge/tests-1453%20passed-green)](https://github.com/datanaan/synthgen-core)

</div>

---

**中文** | [English](README.md)

---

## 什么是 SynthGen Core？

**大多数"合成数据"工具只是 numpy 的封装。SynthGen Core 是一个数据库内核——自研 DSL、不可变审计日志、时间旅行查询，专为生成"物理世界模型"的数据而设计。**

它不是随机数据生成器——你定义物理规则和约束，SynthGen 生成每一个合理的版本，全部可追溯。

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

## License

Apache 2.0 — 详见 [LICENSE](LICENSE)。
