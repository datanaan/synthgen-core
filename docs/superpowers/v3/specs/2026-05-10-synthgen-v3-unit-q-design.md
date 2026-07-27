SynthGen Core v3 Unit Q 设计规范：模型版本链
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v3 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit Q 实施计划
组件：#18 模型版本链
估算：1 周
依赖：v1 #4 存储+元数据层

---

## 一、本 Unit 交付什么

**Unit Q 是 v3 的基础**——模型版本链为时间旅行和持续对齐提供版本管理能力。

交付物：
1. **ModelVersionChain**：创建/引用/列表 + 不可变写入
2. **ModelVersion**：版本元数据（训练数据范围/fidelity_score）
3. **不可变保证**：已写入版本不可修改/删除

---

## 二、#18 模型版本链

### 2.1 核心语义

模型版本链是一个不可变的、链式的版本管理系统：

- 每个模型版本有唯一 ID 和父版本引用
- 版本一旦写入不可修改（不可变保证）
- 版本元数据包含训练数据范围、保真度评分等

### 2.2 接口定义

（定义见 v3 阶段设计规范 3.1 节）

### 2.3 错误处理

```cpp
enum class VersionChainErrorCode {
    kVersionNotFound,         // 版本不存在
    kParentNotFound,          // 父版本不存在
    kImmutableViolation,      // 修改已写入版本
    kDuplicateVersionId,      // 版本 ID 重复
    kVersionChainCycle,       // 版本链循环
    kModelNotFound,           // 模型不存在
    kInvalidVersionId,         // 无效版本 ID 格式
};
```

---

## 三、Unit Q 验收标准

### 3.1 功能验收

- [ ] 版本创建正确，父版本引用正确
- [ ] 版本引用返回正确数据
- [ ] 版本列表按时间排序
- [ ] 不可变保证：修改已写入版本返回 kImmutableViolation
- [ ] 版本元数据（训练数据范围/fidelity_score）正确存储

### 3.2 错误测试验收

- [ ] kVersionNotFound
- [ ] kParentNotFound
- [ ] kImmutableViolation
- [ ] kDuplicateVersionId
- [ ] kVersionChainCycle
- [ ] kModelNotFound
- [ ] kInvalidVersionId

### 3.3 边界条件测试

- [ ] 首个版本（无父版本）
- [ ] 100 个版本的性能
- [ ] 版本链深度 >50
- [ ] 空模型名称
- [ ] 极长版本 ID

### 3.4 测试验收

- [ ] 至少 15 个测试用例
- [ ] 错误测试占比 ≥ 30%

---

## 四、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `ModelVersionChain::create_version()` | Unit R (GC), Unit S (对齐) | 创建版本 |
| `ModelVersionChain::get_version()` | Unit R, S, T | 引用版本 |
| `ModelVersion` | Unit S (时间旅行), Unit T (偏差报告) | 版本元数据 |
