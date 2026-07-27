# CLAUDE.md

This file provides guidance to AI agents when working with code in this repository.

## Project Overview

SynthGen Core is a **physics-first intelligent synthetic data generation engine** — a generation-native database kernel that produces physically valid, statistically plausible, fully traceable synthetic data within known constraints.

This is a C++17 project with:
- Complete engine in `src/` (97+ source files)
- 1453+ tests in `tests/` (100% pass rate)
- Build system: CMake 3.20+
- Design docs in `docs/` (Chinese language)

## Quick Start

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest
```

## Architecture

Three-layer, single-direction dependency:

- **Interface Layer**: SynthLang parser, Schema DDL, Python SDK, REST API
- **Engine Layer**: Constraint classifier, Execution router, Physics/Constraint/Evidence engines
- **Storage Layer**: StorageBackend (Arrow/Parquet), Audit log (hash chain), Version chain, Time travel

## Key Engineering Principles

1. **Honesty over power**: Never claim "trusted data" without qualification
2. **Capability milestones over horizontal layering**: Each version has clear capability boundaries
3. **Database kernel, not application service**: Schema enforcement, immutable audit, time-travel must be native
4. **Scaffolding is first-class**: Explain, Trace, Metrics have equal status with features

## Language

Documentation is in **Chinese** (Simplified). Code comments and identifiers are in English.
