# 种子控制器 (SeedController)

> 类型：组件

## 定义

SynthGen Core 物理引擎的确定性保证组件。实现 global_seed -> request_seed -> batch_seed -> row_seed 的四级种子派生链，确保相同输入始终产生相同输出。

## 种子派生链

```
global_seed (用户指定或默认)
    └── request_seed(request_id)
            └── batch_seed(batch_index)
                    └── row_seed(row_index)
```

## 核心接口

```cpp
// src/engine/physics/seed_controller.h
class SeedController {
public:
    explicit SeedController(uint64_t global_seed);
    uint64_t request_seed(uint64_t request_id);
    uint64_t batch_seed(uint64_t request_seed, int64_t batch_index);
    uint64_t row_seed(uint64_t batch_seed, int64_t row_index);
};
```

## 设计属性

- **确定性**：相同 global_seed + 相同 request_id -> 相同 request_seed
- **独立性**：不同 batch 的种子不相关
- **跨平台一致**：使用 std::mt19937_64（Mersenne Twister），行为在所有平台一致
- **使用 std::hash 或自定义哈希函数**进行种子派生

## 边界条件处理

- seed = 0：合法
- seed = UINT64_MAX：合法
- batch_index = 0：合法
- batch_index = INT64_MAX：合法

## 关联实体

- [[physics-engine]] — 物理引擎使用 SeedController 保证确定性
- [[rectangular-sampler]] — 采样器通过 SeedController 获取每行种子
