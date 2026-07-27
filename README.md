<div align="center">

# SynthGen Core

**The data infrastructure for world models. Define a universe with physical laws — SynthGen generates every plausible version of it, fully traceable.**

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![GitHub stars](https://img.shields.io/github/stars/datanaan/synthgen-core?style=social)](https://github.com/datanaan/synthgen-core)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://isocpp.org)
[![Tests](https://img.shields.io/badge/tests-1453%20passed-green)](https://github.com/datanaan/synthgen-core)

</div>

[**中文文档**](README.zh-CN.md) | **English**

---

## The Problem

Most "synthetic data" tools are thin wrappers around numpy/scipy. They sample from distributions, call it "synthetic data," and walk away. The result is **statistically plausible but physically meaningless** — and completely untraceable.

If you're building a **world model** or a **physics simulation**, you need something fundamentally different:

- Data that respects **real physical constraints** (not just statistical distributions)
- Data that is **fully traceable** — every record tells you where it came from and which constraints it satisfies
- A **custom DSL** that lets domain experts define schemas and constraints without writing C++
- An **immutable audit trail** that proves data hasn't been tampered with

**SynthGen Core is a C++17 database kernel purpose-built for this.** It's not a Python library. It's an engine.

---

## What It Actually Does

### Core Flow

```
User writes SynthLang DSL → Parser creates AST → Schema/Constraints registered
    → RectangularSampler generates data (Gaussian/Uniform/KDE)
    → ValueRangeValidator checks every row against constraints
    → EvidencePackageBuilder packages result + tail_report + provenance
    → AuditLog appends hash-chain record
    → Storage (Arrow/Parquet) persists with version chain + time travel
```

### Concrete Example

```synthlang
-- Define a sensor reading schema
DEFINE TYPE sensor_log {
    timestamp: DATETIME NOT NULL ORDER,
    wind_speed: FLOAT [0.0, 50.0],
    temperature: FLOAT [-50.0, 80.0],
    status: ENUM('normal', 'warning', 'fault')
};

-- Define physical constraints
DEFINE CONSTRAINT wind_safety ON sensor_log {
    wind_speed BETWEEN 0 AND 25 DURING status = 'normal_operation',
    temperature > -40 WHEN wind_speed > 20,
    vibration[t] - vibration[t-1] < 5.0,
    AVG(temperature) OVER (INTERVAL 1 HOUR) <= 40.0
};
```

### What Happens at Runtime

1. **SynthLang parser** (`src/parser/`) — Lexer → Parser → AST. Custom DSL, not SQL. Supports types, enums, ORDER columns, constraint cards with DURING/WHEN semantics.

2. **Physics engine** (`src/engine/physics/`) — `RectangularSampler` orchestrates `GaussianSampler`, `UniformSampler`, `RangeExtractor`, and `SeedController`. Generates Arrow tables directly — no pandas intermediate.

3. **Constraint engine** (`src/engine/constraint/`) — `ValueRangeValidator` checks value-range, inter-row (DURING/WHEN), and aggregate constraints. **Safety-critical violations interrupt immediately** — no further checks after a safety fail.

4. **Evidence package** (`src/engine/evidence/`) — Every generation produces an `EvidencePackageV1` containing: the generated data, `TailReportV1` (tells you which constraints each record satisfies/approximates/breaks), `ProvenanceV1` (seed, version, timestamp), and schema hash.

5. **Audit log** (`src/storage/audit/`) — Hash-chain immutable log. Each `AuditRecord` has `prev_hash`, `content_hash`, `chain_hash`. Supports `verify_chain()` and `detect_forks()` — **WORM semantics** (write-once, read-many).

6. **Version chain** (`src/storage/version/`) — Model version chain with GC compaction. `TimeTravelEngine` supports `AS OF` queries. `ContinuousAlignment` detects drift between versions using KS statistics.

7. **REST API** (`src/api/`) — `SynthGenService` exposes `define_type`, `load_data`, `define_constraint`, `generate`, `explain`. Built on cpp-httplib.

8. **Python SDK** (`src/sdk/`) — pybind11 bindings for all core operations.

---

## Why This Matters for World Models

A **world model** needs to simulate "what if" — not just interpolate between existing data points. SynthGen Core is designed as the **data generation layer** for this:

| Capability | What It Means |
|-----------|---------------|
| **Physics-first sampling** | Gaussian, uniform, KDE — not just "random" |
| **Constraint cards** | Value-range, inter-row, aggregate, DURING/WHEN — real physical rules |
| **tail_report** | Every record knows which constraints it satisfies, approximates, or breaks |
| **Hash-chain audit** | Immutable provenance — prove where every data point came from |
| **Time travel** | Query data at any version — `AS OF` semantics |
| **Drift detection** | KS statistic on distributions between versions |
| **Custom DSL** | Domain experts write SynthLang, not C++ |
| **1453+ tests** | 100% pass rate — unit, integration, e2e, chaos, performance |

---

## Quick Start

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

### Python SDK

```python
import synthgen_core as sc

# Define schema
schema = sc.define_type("sensor_log", {
    "timestamp": "DATETIME NOT NULL ORDER",
    "temperature": "FLOAT [-50.0, 80.0]",
    "status": "ENUM('normal', 'warning', 'fault')"
})

# Generate 1000 rows
result = sc.generate(schema, constraints, limit=1000)
print(result.tail_report)  # Which constraints are satisfied?
```

---

## Project Structure

```
src/
├── parser/          # SynthLang DSL: Lexer → Parser → AST
├── schema/          # Schema definition + registry + builder
├── engine/
│   ├── physics/     # RectangularSampler, Gaussian/Uniform/KDE
│   ├── constraint/  # Value-range, inter-row, aggregate validation
│   ├── evidence/    # EvidencePackage + TailReport + Provenance
│   ├── router/      # Constraint classifier + execution routing
│   ├── postfilter/  # Post-generation filtering
│   └── alignment/   # Drift detection + continuous alignment
├── storage/
│   ├── audit/       # Hash-chain immutable audit log
│   ├── version/     # Model version chain + GC
│   ├── timetravel/  # AS OF queries
│   └── gc/          # Compaction + bias reporting
├── api/             # REST API (cpp-httplib)
├── sdk/             # Python bindings (pybind11)
└── scaffold/        # Explain, Trace, Metrics
```

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
