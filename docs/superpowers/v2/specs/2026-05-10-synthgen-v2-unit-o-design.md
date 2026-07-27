SynthGen Core v2 Unit O 设计规范：哈希链审计 + 数据引擎 KDE
文档性质：Unit 级设计规范 [COORDINATE]
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit O 实施计划
组件：#15 哈希链审计日志 + #15b 数据引擎 v1(KDE)
估算：4 周
依赖：v1 #4 存储
协调项：C1（KDE 技术选型）、C8（WORM 存储选型）

---

## 一、本 Unit 交付什么

**Unit O 包含两个独立组件**，可并行开发：

1. **#15 哈希链审计日志**（1 周）：审计不可变保证的实现基础
2. **#15b 数据引擎 v1(KDE)**（3 周）：后筛选排除率预估的计算基础

---

## 二、#15 哈希链审计日志

### 2.1 核心语义

哈希链审计日志保证**所有生成操作不可篡改地记录**。每条记录包含前一条记录的哈希，形成链式结构。

**审计保证**：
- 创世记录：系统启动时创建，prev_hash = "0"
- 写入验证：每条新记录的 prev_hash 必须等于前一条的 chain_hash
- 分叉检测：如果两条记录的 prev_hash 相同但 chain_hash 不同，说明有分叉
- 每日校验：全链验证所有 hash 链接正确

### 2.2 接口定义

```cpp
namespace synthgen::storage::audit {

// 审计记录
struct AuditRecord {
    std::string record_id;          // UUID
    std::string operation;          // generate / update_model / compact / ...
    std::string actor_identity;     // 执行者身份（来自 IdentityDeclaration）
    Timestamp timestamp;
    std::string prev_hash;          // 前一条记录的 chain_hash
    std::string content_hash;       // 本记录内容的 SHA-256
    std::string chain_hash;        // SHA-256(prev_hash + content_hash)
    std::map<std::string, std::string> metadata;
    // metadata 可包含：
    //   "constraint_name" → 约束名
    //   "rows_generated" → 生成行数
    //   "degradation_path" → 走的退化路径
    //   "evidence_package_id" → 证据包 ID
};

// 审计日志
class AuditLog {
public:
    explicit AuditLog(StorageBackend& storage);

    // 创世记录（系统启动时调用一次）
    Result<void> create_genesis();

    // 追加记录
    Result<AuditRecord> append(
        const std::string& operation,
        const std::string& actor_identity,
        const std::map<std::string, std::string>& metadata = {});

    // 验证哈希链完整性
    Result<bool> verify_chain();

    // 每日全链校验
    struct ChainVerificationReport {
        bool is_valid;
        int64_t total_records;
        int64_t verified_records;
        std::vector<std::string> broken_links;  // 断裂位置
        std::vector<std::string> fork_points;    // 分叉点
    };
    Result<ChainVerificationReport> daily_verification();

    // 分叉检测
    struct ForkDetection {
        std::string record_id;
        std::string prev_hash;
        std::vector<std::string> competing_next;  // 竞争的下一记录
    };
    Result<std::vector<ForkDetection>> detect_forks();

    // 查询
    Result<AuditRecord> get_latest();
    Result<std::vector<AuditRecord>> scan(
        const std::optional<Timestamp>& from,
        const std::optional<Timestamp>& to,
        int64_t limit = 1000);
};

// [COORDINATE] WORM 存储实现
class WORMStorage {
public:
    Result<void> write(const AuditRecord& record);
    Result<AuditRecord> read(const std::string& record_id);
    Result<std::vector<AuditRecord>> scan(
        const std::optional<Timestamp>& from,
        const std::optional<Timestamp>& to,
        int64_t limit = 1000);

    // WORM 保证
    Result<void> modify(const std::string& record_id, ...);   // → kWriteOnceViolation
    Result<void> remove(const std::string& record_id);          // → kWriteOnceViolation
};

}  // namespace synthgen::storage::audit
```

### 2.3 [COORDINATE] WORM 存储选型

**待决策**：WORM 存储实现方式

| 选项 | 描述 | 推荐度 |
|------|------|--------|
| 带哈希校验的 Parquet | 每条记录含 prev_hash，追加写入 Parquet 文件 | ⭐⭐⭐ 推荐 |
| Append-only 文件 | 纯追加日志文件 | ⭐⭐ |
| 专用 WORM 存储引擎 | 自研 WORM 抽象层 | ⭐ |

**占位推荐**：带哈希校验的 Parquet。

### 2.4 错误处理

```cpp
enum class AuditErrorCode {
    kGenesisAlreadyExists,      // 创世记录已存在
    kWriteOnceViolation,         // 修改/删除已写入记录
    kHashChainBroken,            // 哈希链断裂
    kForkDetected,               // 分叉检测
    kRecordNotFound,             // 记录不存在
    kTimestampOutOfOrder,        // 时间戳乱序
    kInvalidOperation,           // 无效操作名
};
```

---

## 三、#15b 数据引擎 v1(KDE)

### 3.1 核心语义

数据引擎 v1 基于核密度估计(KDE)学习训练数据分布，提供三个核心能力：

1. **密度采样**：从学习到的分布中采样新数据
2. **体积比计算**：约束空间体积 / 数据分布体积
3. **密度估计**：在给定点估计概率密度

**限制声明**：
- 维度限制：中低维（<20维）有效，高维有维度灾难问题
- 带宽选择：默认 Silverman 规则，可手动指定
- 训练数据量限制：默认最大 100 万行

### 3.2 [COORDINATE] KDE 技术选型

**待决策**：KDE 实现方式

| 选项 | 描述 | 估算 | 推荐度 |
|------|------|------|--------|
| 自研 C++ KDE | 无外部依赖，团队技术栈一致 | 3w | ⭐⭐⭐ 推荐 |
| ONNX Runtime 推理 | 训练 Python KDE → 导出 ONNX → C++ 推理 | 2w + Python 工具 | ⭐⭐ |
| scipy-like C++ 移植 | 从 scipy.stats 移植 KDE 逻辑 | 3w + 精度验证 | ⭐⭐ |

**占位推荐**：自研 C++ KDE。理由：团队 C++ 为主，自研可控制维度限制和带宽选择逻辑。

> ⚠️ 本 spec 按自研 C++ KDE 编写。如果团队决策为其他选项，需调整 Task 2 的实现路径。

### 3.3 接口定义

```cpp
namespace synthgen::engine::data {

// KDE 配置
struct KDEConfig {
    std::string kernel = "gaussian";     // 核函数：gaussian / epanechnikov / tophat
    double bandwidth = 0.0;              // 带宽（0 = Silverman 自动选择）
    int max_dimensions = 20;             // 最大维度
    int64_t max_training_rows = 1000000; // 最大训练行数
    double bandwidth_factor = 1.0;       // 带宽调整因子（>1 更平滑）
};

// 模型元数据
struct KDEModelMetadata {
    int dimensions;
    int64_t training_rows;
    std::vector<std::string> column_names;
    std::vector<double> bandwidths;       // 每个维度的带宽
    std::string kernel_type;
    double training_data_range_min;       // 训练数据范围（按 ORDER 列）
    double training_data_range_max;
    double fidelity_score;                // 保真度评分
};

// 数据引擎 v1
class DataEngineV1 {
public:
    explicit DataEngineV1(const KDEConfig& config);

    // === 学习 ===

    // 从训练数据学习 KDE
    Result<void> fit(const ArrowBatch& training_data, const Schema& schema);

    // === 采样 ===

    // 从 KDE 分布中采样
    Result<ArrowBatch> sample(int64_t count, uint64_t seed);

    // === 计算 ===

    // 体积比计算
    Result<double> volume_ratio(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints);

    // 密度估计
    Result<double> estimate_density(const std::vector<double>& point);

    // === 查询 ===

    bool is_fitted() const;
    int dimensions() const;
    const KDEConfig& config() const;
    const KDEModelMetadata& metadata() const;

    // Explain
    ExplainInfo explain() const;

private:
    KDEConfig config_;
    KDEModelMetadata metadata_;
    bool fitted_ = false;

    // KDE 核心数据（fit 后填充）
    // - 训练数据矩阵（存储于 ArrowBatch 或 Eigen 矩阵）
    // - 每个维度的带宽
    // - 核函数实现

    // 带宽选择：Silverman 规则
    Result<std::vector<double>> compute_bandwidth_silverman(
        const ArrowBatch& data, const Schema& schema);

    // 核函数计算
    double kernel_gaussian(double distance, double bandwidth) const;

    // 采样：拒绝采样法
    Result<ArrowBatch> sample_rejection(int64_t count, uint64_t seed);

    // 体积比计算
    // 方法：在约束空间内均匀采样 → 计算 KDE 密度 → 积分
    Result<double> compute_volume_ratio_monte_carlo(
        const Schema& schema,
        const std::vector<ConstraintDef>& constraints,
        int n_samples = 10000);
};

}  // namespace synthgen::engine::data
```

### 3.4 错误处理

```cpp
enum class DataEngineErrorCode {
    kNotFitted,                    // 未 fit 就调用
    kDimensionTooHigh,              // 维度 > 20（警告，不拒绝）
    kEmptyTrainingData,             // 训练数据为空
    kTrainingDataTooLarge,          // 训练行数 > max
    kInvalidBandwidth,              // 带宽为负
    kInvalidArgument,               // count ≤ 0 等
    kSamplingFailed,                // 采样失败
    kVolumeRatioComputationFailed,  // 体积比计算失败
    kEmptyConstraintSpace,          // 约束空间为空
    kUnsupportedKernel,             // 不支持的核函数
};
```

---

## 四、Unit O 验收标准

### 4.1 #15 审计日志功能验收

- [ ] 创世记录创建成功
- [ ] 追加记录哈希链完整
- [ ] 修改已写入记录返回 kWriteOnceViolation
- [ ] 删除已写入记录返回 kWriteOnceViolation
- [ ] 哈希链验证：手动修改一条记录后 daily_verification 返回 false
- [ ] 分叉检测：两条记录 prev_hash 相同但 chain_hash 不同时检测到
- [ ] 时间范围查询正确

### 4.2 #15b 数据引擎功能验收

- [ ] KDE 可学习训练数据分布
- [ ] 采样数据分布与训练数据分布统计特征接近
- [ ] 体积比计算合理（约束空间越大，体积比越大）
- [ ] 密度估计在训练数据范围内值更高
- [ ] Silverman 带宽自动选择
- [ ] 维度 >20 返回 kDimensionTooHigh 警告
- [ ] 数据引擎在中低维数据上 KDE 估计有效

### 4.3 错误测试验收

**审计日志错误测试**：
- [ ] 创世记录重复写入返回 kGenesisAlreadyExists
- [ ] 修改记录返回 kWriteOnceViolation
- [ ] 删除记录返回 kWriteOnceViolation
- [ ] 哈希链断裂检测
- [ ] 分叉检测
- [ ] 空操作名返回 kInvalidOperation

**数据引擎错误测试**：
- [ ] 未 fit 调用 sample 返回 kNotFitted
- [ ] 维度 >20 警告
- [ ] 训练数据为空返回 kEmptyTrainingData
- [ ] 训练数据行数 > max 返回 kTrainingDataTooLarge
- [ ] 带宽为负数返回 kInvalidBandwidth
- [ ] 采样 count ≤ 0 返回 kInvalidArgument
- [ ] 不支持的核函数返回 kUnsupportedKernel

### 4.4 边界条件测试

- [ ] 审计日志 0 条记录
- [ ] 审计日志 1 条记录（仅创世）
- [ ] 审计日志 10000 条记录性能
- [ ] 数据引擎 1 维数据
- [ ] 数据引擎 19 维数据（接近上限）
- [ ] 数据引擎 21 维数据（超上限警告）
- [ ] 数据引擎 1 行训练数据
- [ ] 数据引擎极小带宽
- [ ] 数据引擎极大带宽

### 4.5 测试验收

- [ ] #15 至少 15 个测试用例
- [ ] #15b 至少 25 个测试用例
- [ ] 错误测试占比 ≥ 30%
- [ ] CI 自动运行

---

## 五、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `AuditLog::append()` | Unit M (路由器), Unit P (EvidencePackage) | 审计记录 |
| `AuditLog::daily_verification()` | Unit Q (脚手架) | 审计验证 |
| `DataEngineV1::fit()` | 数据导入流程 | 学习训练数据 |
| `DataEngineV1::sample()` | Unit M (路由器) | 统计生成/KDE 扰动路径 |
| `DataEngineV1::volume_ratio()` | Unit M (路由器), Unit N (后筛选) | 排除率预估 |
| `DataEngineV1::estimate_density()` | Unit P (EvidencePackage) | 密度信息 |
| `KDEModelMetadata` | Unit P (EvidencePackage) | 模型元数据 |
