# 快照管理器 (SnapshotManager)

> 类型：组件

## 定义

SynthGen Core 确定性测试框架的核心组件。负责参考快照的保存、加载和比对。保证相同 seed 始终产生相同输出，是生成引擎确定性保证的验证机制。

## 核心接口

```cpp
// src/scaffold/snapshot.h
class SnapshotManager {
public:
    Result<void> save(const ArrowBatch& batch, const std::string& name);
    Result<ArrowBatch> load(const std::string& name);
    Result<bool> compare(const ArrowBatch& batch, const std::string& name);
};
```

## v1 参考快照

| 快照文件 | 内容 | 说明 |
|---------|------|------|
| `physics_seed42_1000rows_uniform.parquet` | 均匀分布 1000 行 | 物理引擎确定性基准 |
| `physics_seed42_1000rows_gaussian.parquet` | 高斯分布 1000 行 | 高斯采样确定性基准 |
| `evidence_package_v1_seed42.json` | EvidencePackage v1 | 证据包确定性基准 |

## 确定性测试场景

- seed=42，生成 1000 行 -> 与快照逐行一致
- seed=42，生成 1000 行（第 2 次）-> 与快照逐行一致
- seed=43 -> 与 seed=42 不同
- 相同 seed + 相同请求，100 次 -> 全部一致

## 存储格式

- ArrowBatch -> Parquet 格式（使用 Arrow 标准格式，保证跨平台一致）
- EvidencePackage -> JSON 格式

## 关联实体

- [[scaffolding]] — SnapshotManager 是脚手架确定性测试框架的核心
- [[seed-controller]] — 种子控制器保证确定性
- [[physics-engine]] — 物理引擎是确定性验证的主要目标
