SynthGen Core v1 Unit D 设计规范：Physics Engine v1
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0、Unit A 设计规范
下游文档：Unit D 实施计划
组件：#5 物理引擎 v1（矩形域采样）
估算：1.5 周
依赖：Unit A (Parser + Type System)

---

## 一、本 Unit 交付什么

Unit D 实现 v1 的物理引擎——在矩形约束域内按基础分布采样，生成物理合法的合成数据。

**v1 限制（诚实声明）**：
- 仅支持矩形约束域（BETWEEN/MIN/MAX 定义的超矩形）
- 不支持非矩形约束域（DURING/WHEN 产生的条件约束空间）
- 不支持行间约束、聚合约束
- 传入非矩形约束时返回 `unsupported_in_v1` 错误

交付物：
1. **RectangularSampler**：矩形域采样器（均匀/高斯分布）
2. **SeedController**：种子控制（全局种子 → 请求种子 → batch 种子）
3. **DistributionEngine**：基础分布实现（均匀、高斯）
4. **BatchGenerator**：批量生成（支持指定 batch_size）

---

## 二、核心接口

```cpp
namespace synthgen::engine::physics {

// 生成请求
struct GenerationRequest {
    const Schema& schema;
    std::vector<ConstraintDef> constraints;  // v1 仅值域约束
    int64_t limit;                           // 请求行数
    uint64_t seed;                           // 用户指定或系统生成
    std::string distribution = "uniform";    // "uniform" | "gaussian"
    int64_t batch_size = 1000;               // 每批生成行数
};

// 生成结果
struct GenerationResult {
    ArrowBatch data;
    GenerationStats stats;
};

struct GenerationStats {
    int64_t rows_generated;
    int64_t rows_requested;
    double exclusion_rate;      // v1 纯物理路径应为 0.0
    int64_t elapsed_ms;
    int64_t batch_count;
    std::string distribution_used;
};

// 矩形域采样器
class RectangularSampler {
public:
    explicit RectangularSampler(const Schema& schema);

    Result<GenerationResult> generate(const GenerationRequest& request);

    // Explain 支持
    ExplainInfo explain(const GenerationRequest& request) const;

    // 预检查：约束是否可执行
    Result<void> validate_request(const GenerationRequest& request) const;

private:
    // 从约束中提取每列的采样范围
    Result<std::vector<ColumnRange>> extract_ranges(
        const std::vector<ConstraintDef>& constraints) const;

    // 生成单 batch
    Result<ArrowBatch> generate_batch(
        const std::vector<ColumnRange>& ranges,
        int64_t batch_rows,
        uint64_t batch_seed,
        const std::string& distribution);
};

// 列采样范围
struct ColumnRange {
    std::string column_name;
    DataType type;
    double min_value;
    double max_value;
    // v1：范围直接来自约束。无约束的列使用 Schema 声明的范围。
};

}  // namespace synthgen::engine::physics
```

---

## 三、矩形域采样算法

### 3.1 采样范围确定

```cpp
// 对每列，确定采样范围：
// 1. 如果有约束（BETWEEN/MIN/MAX），使用约束范围
// 2. 如果没有约束，使用 Schema 声明的 [range_min, range_max]
// 3. 如果 Schema 也没有声明范围，使用类型默认值：
//    FLOAT: [-1e6, 1e6]
//    INT:   [-1e9, 1e9]
//    DATETIME: [1970-01-01, 2100-01-01]
//    STRING: 长度 [0, 256]
//    ENUM: 从 enum_values 中均匀选择
```

### 3.2 均匀分布采样

```cpp
// 对 FLOAT/INT 列：
value = min + random(seed) * (max - min)

// 对 DATETIME 列：
timestamp = min_epoch_us + random(seed) * (max_epoch_us - min_epoch_us)

// 对 STRING 列：
// 生成长度在 [0, max_len] 的随机字符串（字母数字）

// 对 ENUM 列：
// 从 enum_values 中均匀选择
```

### 3.3 高斯分布采样

```cpp
// 对 FLOAT/INT 列：
mean = (min + max) / 2
stddev = (max - min) / 6  // 6σ 覆盖 99.7%
value = gaussian_random(seed, mean, stddev)

// 如果值超出 [min, max]，截断到边界
if value < min: value = min
if value > max: value = max

// 注意：截断导致尾部事件被排除 → 记录在 tail_report 中
```

### 3.4 种子控制

```cpp
// 种子派生链：
global_seed = user_provided or random()
request_seed = hash(global_seed + request_id)
batch_seed = hash(request_seed + batch_index)
row_seed = hash(batch_seed + row_index)

// 确定性保证：
// 相同 global_seed + 相同请求 → 相同输出（逐行可比对）
```

---

## 四、错误处理

| 错误场景 | 错误码 | 行为 |
|---------|--------|------|
| 空 Schema | kInvalidArgument | 失败 |
| limit < 0 | kInvalidArgument | 失败 |
| limit = 0 | （允许） | 返回空结果 |
| 不存在的 distribution | kInvalidArgument | 失败 |
| 约束范围与 Schema 范围冲突 | kInvalidRange | 失败 |
| 非矩形约束（DURING/WHEN） | kUnsupportedInV1 | 失败 |
| 空约束列表 | （允许） | 使用 Schema 默认范围 |
| 约束引用不存在的列 | kUndefinedColumn | 失败 |
| seed = UINT64_MAX | （允许） | 作为普通种子处理 |
| batch_size <= 0 | kInvalidArgument | 失败 |
| 内存不足 | kOutOfMemory | 失败 |

---

## 五、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `RectangularSampler::generate()` | Unit E (Validation) | 生成数据供验证 |
| `RectangularSampler::explain()` | Unit H (Explain) | 预览执行计划 |
| `GenerationResult` | Unit F (EvidencePackage) | 构建证据包 |
| `GenerationStats` | Unit H (Metrics) | 暴露生成统计 |

---

## 六、Unit D 验收标准

### 6.1 功能验收

- [ ] 矩形域内均匀采样，值在 [min, max] 内
- [ ] 矩形域内高斯采样，值在 [min, max] 内（截断后）
- [ ] 种子固定 → 输出确定（逐行可比对）
- [ ] 不同 seed → 不同输出
- [ ] 空约束列表使用 Schema 默认范围
- [ ] 多 batch 生成，batch 间顺序正确
- [ ] FLOAT/INT/DATETIME/STRING/ENUM 全部类型支持
- [ ] 生成统计正确（行数、排除率、耗时）

### 6.2 错误测试验收

- [ ] 空 Schema 返回 kInvalidArgument
- [ ] limit < 0 返回 kInvalidArgument
- [ ] limit = 0 返回空结果（0行）
- [ ] 不存在的 distribution 返回 kInvalidArgument
- [ ] 约束范围与 Schema 范围冲突返回 kInvalidRange
- [ ] DURING 约束返回 kUnsupportedInV1
- [ ] WHEN 约束返回 kUnsupportedInV1
- [ ] 行间约束返回 kUnsupportedInV1
- [ ] 聚合约束返回 kUnsupportedInV1
- [ ] 约束引用不存在的列返回 kUndefinedColumn
- [ ] batch_size <= 0 返回 kInvalidArgument
- [ ] 非法种子值（UINT64_MAX）行为确定
- [ ] 内存不足返回 kOutOfMemory

### 6.3 边界条件测试

- [ ] 1 行生成
- [ ] 0 行生成（limit = 0）
- [ ] 1000000 行生成
- [ ] 1 列 Schema
- [ ] 1000 列 Schema
- [ ] 最小 FLOAT 值（-DBL_MAX）采样
- [ ] 最大 FLOAT 值（DBL_MAX）采样
- [ ] 范围宽度为 0（min == max）→ 始终返回该值
- [ ] 范围宽度极小（ε）→ 行为确定
- [ ] 高斯分布 6σ 截断后排除率统计正确
- [ ] batch_size = 1
- [ ] batch_size = limit
- [ ] batch_size > limit

### 6.4 确定性测试

- [ ] seed = 42，生成 1000 行 → 保存参考快照
- [ ] seed = 42，再次生成 1000 行 → 与快照逐行一致
- [ ] seed = 43，生成 1000 行 → 与 seed=42 不同
- [ ] 相同 seed + 相同请求 → 100 次生成全部一致

### 6.5 脚手架验收

- [ ] generate 产生 Trace span（component="physics_engine", operation="generate_batch"）
- [ ] explain() 返回 execution_mode=row_by_row, path=physics_sampling
- [ ] /metrics 暴露 generation_total / generation_rows / generation_duration_ms
- [ ] 生成代码可作为模板引擎 v0.1 素材

### 6.6 测试验收

- [ ] 单元测试：均匀采样 + 高斯采样 + 种子控制
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 每个 ErrorCode 至少 1 个测试用例触发
- [ ] 至少 30 个测试用例
- [ ] 参考快照测试通过
- [ ] CI 自动运行
