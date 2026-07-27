SynthGen Core v3 Unit Q 实施计划：模型版本链
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit Q 设计规范 v1.0
估算：1 周
依赖：v1 #4 存储+元数据层

---

## 概述

Unit Q 交付模型版本链——v3 时间智能的基础版本管理能力。包含 ModelVersion 数据结构、版本链的创建/引用/列表操作、不可变写入保证。此 Unit 是 Unit R(GC)/S(时间旅行+对齐)/T(增强组件) 的共同前置。

---

## Task 1：ModelVersion 数据结构与序列化

**目标**：定义 ModelVersion 值类型，实现构造、校验和序列化

### Step 1.1：ModelVersion 结构体实现

**做什么**：实现 ModelVersion 结构体及其构造/校验逻辑

**产出**：`src/storage/version/model_version.h`, `src/storage/version/model_version.cpp`

**关键逻辑**：
- 字段：version_id, model_name, parent_version_id, created_at, created_by, is_immutable
- 元数据：training_data_range, fidelity_score, training_rows, custom_metadata
- 构造校验：version_id 非空、model_name 非空、created_at 有效
- 第一个版本的 parent_version_id 为空字符串

**验收**：
- [ ] 构造和字段赋值正确
- [ ] version_id 为空时返回 kInvalidVersionId
- [ ] model_name 为空时返回 kInvalidArgument
- [ ] fidelity_score 在 [0.0, 1.0] 范围外时 clamp 或报错

### Step 1.2：序列化和反序列化

**做什么**：实现 ModelVersion 的 Parquet 序列化

**产出**：`src/storage/version/model_version.cpp`（补充）

**关键逻辑**：
- 使用 Arrow SchemaBuilder 构建版本元数据表
- 序列化：ModelVersion → Arrow Row → Parquet 写入
- 反序列化：Parquet 读取 → Arrow Row → ModelVersion
- custom_metadata 用 JSON 字符串存储

**验收**：
- [ ] 序列化后反序列化结果与原始一致
- [ ] 空的 custom_metadata 正确处理
- [ ] 极长 version_id（>256字符）正确处理

---

## Task 2：ModelVersionChain 核心实现

**目标**：实现版本链的创建/引用/列表/不可变保证

### Step 2.1：create_version 实现

**做什么**：实现版本创建逻辑

**产出**：`src/storage/version/model_version_chain.h`, `src/storage/version/model_version_chain.cpp`

**关键逻辑**：
- 生成唯一 version_id（UUID v4）
- 校验 parent_version_id：非空时必须存在且属于同一 model_name
- 校验无循环：新版本不能形成链环
- 写入存储：原子操作，先写版本数据再更新版本索引
- 设置 created_at 为当前时间
- created_by：user / system / auto_compact

**验收**：
- [ ] 版本创建成功，version_id 唯一
- [ ] 父版本不存在时返回 kParentNotFound
- [ ] 父版本属于不同 model_name 时返回 kParentNotFound
- [ ] 重复 version_id 返回 kDuplicateVersionId
- [ ] 首个版本（parent 为空）创建成功

### Step 2.2：get_version 和 list_versions 实现

**做什么**：实现版本引用和列表查询

**产出**：`src/storage/version/model_version_chain.cpp`（补充）

**关键逻辑**：
- get_version：从版本索引查找 → 返回 ModelVersion 常量指针
- list_versions：按 model_name 查找 → 按 created_at 降序排列 → 返回 limit 条
- list_versions 默认 limit=100，最多返回 1000 条

**验收**：
- [ ] get_version 返回正确数据
- [ ] version_id 不存在返回 kVersionNotFound
- [ ] list_versions 按时间降序排列
- [ ] limit=0 返回空列表
- [ ] model_name 不存在返回空列表（不报错）

### Step 2.3：不可变保证

**做什么**：实现 modify_version 方法（永远返回 kImmutableViolation）

**产出**：`src/storage/version/model_version_chain.cpp`（补充）

**关键逻辑**：
- modify_version 显式签名（不接受有效载荷），仅返回错误
- 存储层不提供 update/delete 接口
- 审计日志记录修改尝试

**验收**：
- [ ] modify_version 返回 kImmutableViolation
- [ ] 调用后原始数据不变
- [ ] 审计日志记录修改尝试

---

## Task 3：脚手架集成

**目标**：为 ModelVersionChain 添加 Trace/Explain/Metrics

### Step 3.1：Trace span 集成

**做什么**：为 create_version/get_version/list_versions 添加 span

**实现方式**：RAII SpanGuard

```cpp
Result<ModelVersion> ModelVersionChain::create_version(...) {
    SpanGuard span("version_chain", "create_version", trace_id_);
    span.set_attribute("model_name", model_name);
    // ...
    span.set_attribute("version_id", result.value().version_id);
    return result;
}
```

**验收**：每次操作产生 span，span 含 model_name + version_id

### Step 3.2：Explain 接口

**做什么**：为 ModelVersionChain 添加 explain() 方法

```cpp
struct VersionChainExplainInfo {
    std::string model_name;
    int total_versions;
    int depth;                      // 最深链深度
    std::string latest_version_id;
};
```

**验收**：explain() 返回模型版本概览

### Step 3.3：Metrics 注册

**做什么**：注册版本链相关 metrics

```
version_chain_total_versions    — 版本总数（按模型分标签）
version_chain_create_latency_ms — 创建延迟
version_chain_depth             — 最大链深度
```

**验收**：metrics 端点暴露上述指标

---

## Task 4：错误处理和测试

**目标**：完善错误路径和测试覆盖

### Step 4.1：错误路径实现

**做什么**：实现所有 ErrorCode 对应的错误路径

**ErrorCode 覆盖**：
- kVersionNotFound：get_version/list_versions 找不到版本
- kParentNotFound：create_version 父版本不存在
- kImmutableViolation：modify_version 尝试修改
- kDuplicateVersionId：version_id 冲突
- kVersionChainCycle：循环检测
- kModelNotFound：model_name 不存在
- kInvalidVersionId：version_id 格式无效

**验收**：每个 ErrorCode 至少 1 个触发测试

### Step 4.2：单元测试

**做什么**：编写模型版本链单元测试

**产出**：`tests/unit/model_version_chain_test.cpp`

**测试用例**（至少 15 个）：

功能测试（10+）：
- 创建首个版本
- 创建带父版本的子版本
- 引用已存在版本
- 列出版本按时间排序
- 版本元数据正确存储
- fidelity_score 精度
- custom_metadata 序列化/反序列化
- 多模型并行版本链
- limit 参数生效
- 审计日志记录创建

错误测试（5+）：
- 版本不存在 → kVersionNotFound
- 父版本不存在 → kParentNotFound
- 修改已写入版本 → kImmutableViolation
- 重复 version_id → kDuplicateVersionId
- 无效 version_id → kInvalidVersionId

**验收**：15+ 测试通过，错误测试占比 ≥ 33%

### Step 4.3：边界条件测试

**做什么**：编写边界条件测试

**测试用例**：
- 版本链深度 >50
- 100 个版本列表性能 <100ms
- 空模型名称 → 报错
- 极长 version_id（256 字符）
- 极大 custom_metadata（1MB）

**验收**：5+ 边界条件测试通过

---

## Task 5：集成测试

**目标**：验证版本链与存储层的集成

### Step 5.1：集成测试

**做什么**：编写版本链与存储后端的集成测试

**产出**：`tests/integration/version_chain_integration_test.cpp`

**测试用例**（至少 6 个）：
- 完整流程：创建模型 → 创建版本链 → 引用 → 列表
- 存储层重启后版本数据恢复
- 多模型版本链隔离
- Trace span 完整性
- Explain 输出正确性
- 审计日志与版本创建同步

**验收**：6+ 集成测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 数据结构 | 2 | 0.125w | ⬜ |
| Task 2: 核心实现 | 3 | 0.375w | ⬜ |
| Task 3: 脚手架 | 3 | 0.125w | ⬜ |
| Task 4: 错误和测试 | 3 | 0.25w | ⬜ |
| Task 5: 集成测试 | 1 | 0.125w | ⬜ |
| **合计** | **12** | **1w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 版本链循环检测在深度 >100 时性能退化 | 使用路径压缩：维护 visited set，O(depth) 检测 |
| 并发创建同一父版本的子版本 | 版本 ID 使用 UUID v4 避免冲突，父版本引用用乐观锁 |
| 存储层不支持原子写入 | atomic_write 由存储模型层(Unit T #23)保证，Unit Q 仅做逻辑层校验 |
