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

[**中文文档**](README.zh-CN.md) | **English**

---

## What is SynthGen Core?

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
