# SynthGen Core

> **Physics-first intelligent synthetic data generation engine.**

SynthGen Core is a generation-native database kernel that produces physically valid, statistically plausible, fully traceable synthetic data within known constraints. Given a user-defined data world framework, it generates diverse plausible representations of what that data could look like.

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

---

## What is SynthGen Core?

SynthGen Core is an **independent, standalone product** for synthetic data generation. It optionally integrates with Polymorphic Twin (digital twin governance infrastructure) via the EvidencePackage protocol.

### Architecture

Three-layer, single-direction dependency (Interface → Engine → Storage):

```
Interface Layer          Engine Layer              Storage Layer
┌──────────────┐     ┌──────────────────┐     ┌──────────────────┐
│ SynthLang    │     │ Constraint        │     │ StorageBackend   │
│   Parser     │ ──▶ │   Classifier      │ ──▶ │   (Arrow/Parquet)│
│ Schema DDL   │     │ Execution Router  │     │ Base Table       │
│ Python SDK   │     │ Physics Engine    │     │   (INSERT ONLY)  │
│ REST API     │     │ Constraint Engine │     │ Snapshot         │
└──────────────┘     │ Post-filter       │     │ Model Store      │
                     │ EvidencePackage   │     │ Audit Log        │
                     └──────────────────┘     │   (Hash Chain)   │
                                              └──────────────────┘
```

### Key Capabilities

- **Physical validity**: Generated data respects physical laws and constraints
- **Statistical plausibility**: Realistic distributions based on KDE and other methods
- **Full traceability**: Every data point has a complete provenance chain
- **Immutable audit**: Hash-chain audit log with WORM semantics
- **Time travel**: Query data as it existed at any point in history
- **Custom DSL**: SynthLang for schema and constraint definition

---

## Quick Start

### Prerequisites

- C++17 compiler
- CMake 3.20+
- Apache Arrow + Parquet
- Google Test (for testing)

### Build

```bash
git clone https://github.com/datanaan/synthgen-core
cd synthgen-core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Test

```bash
cd build
ctest --output-on-failure
```

### Run

```bash
# Start the REST API server
./build/src/api/synthgen_api

# Use the CLI
./build/tools/synthgen --help
```

---

## Project Structure

```
├── CMakeLists.txt              # Top-level build configuration
├── src/
│   ├── common/                 # Common types and utilities
│   ├── parser/                 # SynthLang DSL parser
│   ├── schema/                 # Schema DDL
│   ├── engine/                 # Generation engine
│   │   ├── physics/            # Physics sampling (Gaussian, uniform, etc.)
│   │   ├── constraint/         # Constraint engine (value-range, inter-row, aggregate)
│   │   ├── evidence/           # EvidencePackage builder
│   │   ├── router/             # Execution router
│   │   ├── postfilter/         # Post-generation filtering
│   │   └── alignment/          # Drift detection and continuous alignment
│   ├── storage/                # Storage layer
│   │   ├── audit/              # Hash-chain audit log
│   │   ├── version/            # Model version chain
│   │   ├── gc/                 # Garbage collection and compaction
│   │   ├── timetravel/         # Time travel engine
│   │   └── model/              # Model storage
│   ├── scaffold/               # Explain, Trace, Metrics
│   ├── api/                    # REST API
│   └── sdk/                    # Python bindings (pybind11)
├── tests/                      # 1453+ tests (unit, integration, e2e)
├── docs/                       # Documentation
├── tools/                      # CLI tools
└── wiki/                       # Knowledge base
```

## Supported Languages

| Language | Purpose | Status |
|----------|---------|--------|
| C++17 | Core engine | ✅ Primary |
| Python | SDK bindings | ✅ (pybind11) |
| SynthLang | Custom DSL for schema/constraints | ✅ Built-in |

---

## Version Roadmap

| Version | Focus | Status |
|---------|-------|--------|
| v1 | Basic engine: parser, schema, value-range constraints, audit | ✅ Complete |
| v2 | Constraint complete: inter-row, aggregate, DURING/WHEN | ✅ Complete |
| v3 | Time intelligence: version chain, GC, time travel, alignment | ✅ Complete |
| v4 | Advanced analysis: windows, constraint completeness scoring | 🚧 In progress |

---

## License

Apache 2.0 — see [LICENSE](LICENSE).
