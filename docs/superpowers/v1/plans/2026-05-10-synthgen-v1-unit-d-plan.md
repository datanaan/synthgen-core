SynthGen Core v1 Unit D 实施计划：Physics Engine v1
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit D 设计规范 v1.0
估算：1.5 周
依赖：Unit A (Parser + Type System)

---

## 概述

Unit D 实现 v1 物理引擎——矩形域采样器。在 BETWEEN/MIN/MAX 定义的超矩形内按均匀/高斯分布采样。

---

## Task 1：SeedController

**目标**：实现种子控制链

### Step 1.1：种子派生实现

**做什么**：实现 global_seed → request_seed → batch_seed → row_seed 的派生链

**产出**：`src/engine/physics/seed_controller.h`, `src/engine/physics/seed_controller.cpp`

**关键逻辑**：
- 使用 std::hash 或自定义哈希函数
- 确定性：相同输入 → 相同输出
- 独立性：不同 batch 的种子不相关

```cpp
class SeedController {
public:
    explicit SeedController(uint64_t global_seed);
    uint64_t request_seed(uint64_t request_id);
    uint64_t batch_seed(uint64_t request_seed, int64_t batch_index);
    uint64_t row_seed(uint64_t batch_seed, int64_t row_index);
};
```

**验收**：
- [ ] 相同 global_seed + 相同 request_id → 相同 request_seed
- [ ] 相同 request_seed + 相同 batch_index → 相同 batch_seed
- [ ] 不同 request_id → 不同 request_seed
- [ ] 不同 batch_index → 不同 batch_seed

### Step 1.2：随机数生成器

**做什么**：实现基于种子的确定性随机数生成器

**产出**：`src/engine/physics/random.h`, `src/engine/physics/random.cpp`

**关键逻辑**：
- 使用 std::mt19937_64（Mersenne Twister）
- uniform_real_distribution [0.0, 1.0]
- normal_distribution (mean, stddev)

**验收**：
- [ ] 相同种子 → 相同随机数序列
- [ ] 不同种子 → 不同随机数序列
- [ ] 均匀分布统计检验（Chi-square，p > 0.05）
- [ ] 高斯分布统计检验（Kolmogorov-Smirnov，p > 0.05）

### Step 1.3：种子控制测试

**做什么**：编写种子控制单元测试

**产出**：`tests/unit/seed_controller_test.cpp`

**测试用例**（至少 10 个）：
- 相同种子 → 相同序列
- 不同种子 → 不同序列
- 种子派生链正确性
- **错误测试**：UINT64_MAX 作为种子
- **边界测试**：种子 = 0
- **边界测试**：种子 = 1
- **统计测试**：均匀分布 Chi-square（10000 样本）
- **统计测试**：高斯分布 KS 检验（10000 样本）
- **边界测试**：batch_index = 0
- **边界测试**：batch_index = INT64_MAX

**验收**：10+ 测试用例全通过

---

## Task 2：DistributionEngine

**目标**：实现基础分布采样

### Step 2.1：均匀分布采样

**做什么**：实现 FLOAT/INT/DATETIME/STRING/ENUM 的均匀采样

**产出**：`src/engine/physics/uniform_sampler.h`, `src/engine/physics/uniform_sampler.cpp`

**关键逻辑**：
- FLOAT: min + random() * (max - min)
- INT: floor(min + random() * (max - min + 1))
- DATETIME: min_epoch + random() * (max_epoch - min_epoch)
- STRING: 随机长度 [0, max_len]，随机字符
- ENUM: 从 enum_values 中均匀选择

**验收**：
- [ ] FLOAT 采样在 [min, max] 内
- [ ] INT 采样在 [min, max] 内
- [ ] DATETIME 采样在范围内
- [ ] STRING 长度在 [0, max_len] 内
- [ ] ENUM 值在 enum_values 中

### Step 2.2：高斯分布采样

**做什么**：实现 FLOAT/INT 的高斯采样（含截断）

**产出**：`src/engine/physics/gaussian_sampler.h`, `src/engine/physics/gaussian_sampler.cpp`

**关键逻辑**：
- mean = (min + max) / 2
- stddev = (max - min) / 6
- value = gaussian_random(seed, mean, stddev)
- 截断：value < min → min, value > max → max
- 记录截断次数 → tail_report

**验收**：
- [ ] 采样值在 [min, max] 内（截断后）
- [ ] 截断次数统计正确
- [ ] 均值接近 (min+max)/2
- [ ] 标准差接近 (max-min)/6

### Step 2.3：分布测试

**做什么**：编写分布采样单元测试

**产出**：`tests/unit/distribution_test.cpp`

**测试用例**（至少 15 个）：
- FLOAT 均匀采样范围
- INT 均匀采样范围
- DATETIME 均匀采样范围
- STRING 均匀采样长度
- ENUM 均匀采样值
- FLOAT 高斯采样范围（截断后）
- INT 高斯采样范围（截断后）
- 高斯截断统计
- **错误测试**：min > max 返回 kInvalidRange
- **错误测试**：空 enum_values 返回 kInvalidArgument
- **边界测试**：范围宽度 = 0（min == max）
- **边界测试**：范围宽度极小（ε）
- **边界测试**：最小 FLOAT 值
- **边界测试**：最大 FLOAT 值
- **统计测试**：10000 样本的分布检验

**验收**：15+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 3：RectangularSampler

**目标**：实现矩形域采样器

### Step 3.1：采样范围提取

**做什么**：从约束中提取每列的采样范围

**产出**：`src/engine/physics/range_extractor.h`, `src/engine/physics/range_extractor.cpp`

**关键逻辑**：
- 解析约束（BETWEEN / > / < / >= / <=）
- 对每列，合并所有约束得到最终范围
- 无约束列使用 Schema 默认范围
- 约束范围与 Schema 范围冲突 → kInvalidRange

**验收**：
- [ ] BETWEEN 约束正确提取范围
- [ ] > / < 约束正确提取范围
- [ ] 多约束合并正确（取交集）
- [ ] 无约束列使用 Schema 默认范围
- [ ] 冲突范围返回 kInvalidRange

### Step 3.2：BatchGenerator

**做什么**：实现批量生成

**产出**：`src/engine/physics/batch_generator.h`, `src/engine/physics/batch_generator.cpp`

**关键逻辑**：
- 按 batch_size 分批生成
- 每 batch 使用独立种子
- 组装为 ArrowBatch

**验收**：
- [ ] 单 batch 生成正确
- [ ] 多 batch 生成正确
- [ ] batch 顺序正确
- [ ] 最后 batch 可能不足 batch_size

### Step 3.3：RectangularSampler 集成

**做什么**：集成所有组件

**产出**：`src/engine/physics/rectangular_sampler.h`, `src/engine/physics/rectangular_sampler.cpp`

**验收**：
- [ ] generate() 返回正确结果
- [ ] explain() 返回正确信息
- [ ] validate_request() 正确检查
- [ ] 生成统计正确

### Step 3.4：采样器集成测试

**做什么**：编写端到端采样测试

**产出**：`tests/integration/sampler_integration_test.cpp`

**测试用例**（至少 15 个）：
- 完整流程：Schema + 约束 → 生成 1000 行
- 均匀分布生成
- 高斯分布生成
- 多列 Schema 生成
- 无约束生成（使用 Schema 默认范围）
- 多 batch 生成
- **错误测试**：DURING 约束 → kUnsupportedInV1
- **错误测试**：WHEN 约束 → kUnsupportedInV1
- **错误测试**：行间约束 → kUnsupportedInV1
- **错误测试**：聚合约束 → kUnsupportedInV1
- **错误测试**：limit < 0 → kInvalidArgument
- **错误测试**：空 Schema → kInvalidArgument
- **错误测试**：不存在的 distribution → kInvalidArgument
- **边界测试**：1 行生成
- **边界测试**：0 行生成

**验收**：15+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 4：确定性验证

**目标**：验证种子固定 → 输出一致

### Step 4.1：参考快照生成

**做什么**：用固定 seed 生成参考输出并保存

**产出**：`tests/snapshots/physics_seed42_1000rows.parquet`

**配置**：
- seed = 42
- Schema: sensor_log（5列）
- 约束: safe_range（temperature [-10, 45], pressure [980, 1040]）
- distribution: uniform
- limit: 1000

**验收**：快照文件可读取，1000 行

### Step 4.2：快照比对测试

**做什么**：验证相同 seed → 相同输出

**产出**：`tests/integration/determinism_test.cpp`

**测试用例**：
- seed=42，生成 1000 行 → 与快照逐行一致
- seed=42，生成 1000 行（第 2 次）→ 与快照逐行一致
- seed=43，生成 1000 行 → 与 seed=42 不同
- 相同 seed + 相同请求，100 次 → 全部一致

**验收**：所有确定性测试通过

---

## Task 5：脚手架集成

**目标**：为物理引擎添加 Trace/Explain/Metrics

### Step 5.1：Trace span

**做什么**：为 generate 添加 span

- generate → span(component="physics_engine", operation="generate", attributes={limit, distribution, batch_count})
- generate_batch → span(component="physics_engine", operation="generate_batch", attributes={batch_index, batch_rows})

**验收**：每次 generate 产生 span

### Step 5.2：Explain 接口

**做什么**：实现 explain() 方法

```cpp
ExplainInfo RectangularSampler::explain(const GenerationRequest& request) const {
    return {
        .execution_mode = ExecutionMode::kRowByRow,
        .path = "physics_sampling",
        .constraint_classification = {value_range: N, inter_row: 0, aggregate: 0},
        .distribution = request.distribution,
        .estimated_exclusion_rate = 0.0,  // v1 纯物理路径
    };
}
```

**验收**：explain() 返回正确信息

### Step 5.3：Metrics 注册

**做什么**：注册物理引擎 metrics

```
generation_total        — 生成调用次数
generation_rows         — 生成总行数
generation_duration_ms  — 生成耗时
generation_batches      — 生成 batch 数
```

**验收**：metrics 端点暴露上述指标

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: SeedController | 3 | 0.3w | ⬜ |
| Task 2: DistributionEngine | 3 | 0.4w | ⬜ |
| Task 3: RectangularSampler | 4 | 0.5w | ⬜ |
| Task 4: 确定性验证 | 2 | 0.2w | ⬜ |
| Task 5: 脚手架 | 3 | 0.1w | ⬜ |
| **合计** | **15** | **1.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 高斯分布截断导致统计偏差 | 记录截断次数，在 tail_report 中声明 |
| 大 limit 内存不足 | batch 生成，控制内存使用 |
| 种子确定性跨平台不一致 | 使用 std::mt19937_64，行为跨平台一致 |
| 分布统计检验偶尔失败 | 使用固定种子 + 大样本（10000），降低随机性 |
