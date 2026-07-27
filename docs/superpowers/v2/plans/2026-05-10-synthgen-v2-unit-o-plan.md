SynthGen Core v2 Unit O 实施计划：哈希链审计 + 数据引擎 KDE
文档性质：Unit 级实施计划 [COORDINATE]
版本：v1.0
日期：2026-05-10
上游文档：Unit O 设计规范 v1.0
估算：4 周（#15: 1w + #15b: 3w）
依赖：v1 #4 存储
协调项：C1, C8

---

## 概述

Unit O 包含两个独立组件，可并行开发：
- #15 哈希链审计日志（1 周）
- #15b 数据引擎 v1(KDE)（3 周）

---

## Part A：#15 哈希链审计日志

### Task A1：WORM 存储实现

**目标**：实现 WORM（Write Once Read Many）存储

#### Step A1.1：WORMStorage 实现

**做什么**：实现追加写入、不可修改的存储层

**产出**：`src/storage/audit/worm_storage.h`, `src/storage/audit/worm_storage.cpp`

**关键逻辑**：
- 追加写入 Parquet 文件
- write() → 写入并返回 record_id
- modify() / remove() → 返回 kWriteOnceViolation
- scan() → 按时间范围查询

**验收**：
- [ ] 追加写入成功
- [ ] 修改/删除被拒绝
- [ ] 时间范围查询正确

#### Step A1.2：WORM 存储测试

**产出**：`tests/unit/worm_storage_test.cpp`

**验收**：5+ 测试通过

---

### Task A2：哈希链实现

**目标**：实现哈希链计算和验证

#### Step A2.1：哈希链计算

**做什么**：实现 SHA-256 哈希链

**产出**：`src/storage/audit/hash_chain.h`, `src/storage/audit/hash_chain.cpp`

**关键逻辑**：
- content_hash = SHA-256(record 内容)
- chain_hash = SHA-256(prev_hash + content_hash)
- 创世记录：prev_hash = "0"

**验收**：哈希链计算正确

#### Step A2.2：哈希链验证

**做什么**：实现全链验证

**关键逻辑**：
- 逐条验证 prev_hash 与前一条 chain_hash 一致
- 记录断裂位置
- 记录分叉点

**验收**：
- [ ] 完整链验证通过
- [ ] 断裂检测正确
- [ ] 分叉检测正确

---

### Task A3：AuditLog 实现

**目标**：实现完整审计日志

#### Step A3.1：AuditLog 核心方法

**产出**：`src/storage/audit/audit_log.h`, `src/storage/audit/audit_log.cpp`

**验收**：
- [ ] create_genesis() 正确
- [ ] append() 哈希链正确
- [ ] verify_chain() 正确
- [ ] daily_verification() 正确
- [ ] detect_forks() 正确

#### Step A3.2：审计日志测试

**产出**：`tests/unit/audit_log_test.cpp`

**测试用例**（至少 15 个）：
- 创世记录创建
- 追加记录
- 哈希链完整性
- 修改被拒绝
- 删除被拒绝
- 哈希链断裂检测
- 分叉检测
- 时间范围查询
- 重复创世拒绝
- 空操作名
- 大量记录性能
- daily_verification
- metadata 传递
- actor_identity 传递
- scan 分页

**验收**：15+ 测试通过

---

## Part B：#15b 数据引擎 v1(KDE)

### Task B1：KDE 核心数学

**目标**：实现 KDE 核心数学函数

#### Step B1.1：核函数实现

**做什么**：实现高斯核、Epanechnikov 核、Tophat 核

**产出**：`src/engine/data/kernel.h`, `src/engine/data/kernel.cpp`

**验收**：三种核函数计算正确

#### Step B1.2：带宽选择

**做什么**：实现 Silverman 带宽规则

**产出**：`src/engine/data/bandwidth.h`, `src/engine/data/bandwidth.cpp`

**关键逻辑**：
- Silverman 规则：h = (4/(d+2))^(1/(d+4)) * n^(-1/(d+4)) * σ
- d = 维度，n = 样本数，σ = 标准差

**验收**：带宽选择合理

---

### Task B2：DataEngineV1 实现

**目标**：实现数据引擎核心

#### Step B2.1：fit 实现

**做什么**：实现 KDE 学习

**产出**：`src/engine/data/data_engine.h`, `src/engine/data/data_engine.cpp`

**关键逻辑**：
- 加载训练数据到内存
- 计算每个维度的带宽（Silverman）
- 验证维度 ≤ max_dimensions
- 验证行数 ≤ max_training_rows

**验收**：
- [ ] fit 正确学习
- [ ] 维度 >20 警告
- [ ] 行数 >max 拒绝

#### Step B2.2：sample 实现

**做什么**：实现 KDE 采样（拒绝采样法）

**关键逻辑**：
- 在约束空间内均匀采样候选点
- 计算候选点的 KDE 密度
- 按密度概率接受/拒绝
- 使用种子控制确定性

**验收**：
- [ ] 采样分布与训练数据统计特征接近
- [ ] 种子固定输出一致

#### Step B2.3：volume_ratio 实现

**做什么**：实现体积比计算（蒙特卡洛法）

**关键逻辑**：
- 在约束空间内均匀采样 n 个点
- 计算每个点的 KDE 密度
- 体积比 ≈ 在约束空间内的密度积分 / 总密度积分

**验收**：
- [ ] 体积比计算合理
- [ ] 约束空间越大体积比越大

#### Step B2.4：estimate_density 实现

**做什么**：实现密度估计

**验收**：训练数据范围内密度值更高

---

### Task B3：错误处理和边界条件

#### Step B3.1：错误路径实现

**验收**：全部 DataEngineErrorCode 有处理

#### Step B3.2：边界条件测试

**产出**：`tests/unit/data_engine_boundary_test.cpp`

**验收**：9+ 边界条件测试通过

---

### Task B4：数据引擎测试

#### Step B4.1：核心功能测试

**产出**：`tests/unit/data_engine_test.cpp`

**测试用例**（至少 25 个）：
- fit 基础
- fit 多维
- 采样基础
- 采样分布验证
- 体积比计算
- 密度估计
- Silverman 带宽
- 未 fit 调用
- 维度 >20 警告
- 空训练数据
- 训练数据过大
- 负带宽
- 无效参数
- 种子确定性
- 核函数切换
- 1 维数据
- 19 维数据
- 21 维数据
- 1 行训练数据
- 极小带宽
- 极大带宽
- bandwidth_factor 调整
- metadata 正确
- Explain 输出
- 集成：fit→sample→volume_ratio

**验收**：25+ 测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| A1: WORM 存储 | 2 | 0.25w | ⬜ |
| A2: 哈希链 | 2 | 0.25w | ⬜ |
| A3: AuditLog | 2 | 0.5w | ⬜ |
| **#15 小计** | **6** | **1w** | — |
| B1: KDE 数学 | 2 | 0.5w | ⬜ |
| B2: 引擎实现 | 4 | 1.5w | ⬜ |
| B3: 错误/边界 | 2 | 0.5w | ⬜ |
| B4: 测试 | 1 | 0.5w | ⬜ |
| **#15b 小计** | **9** | **3w** | — |
| **合计** | **15** | **4w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| [COORDINATE] KDE 技术选型未定 | 本计划按自研 C++ KDE 编写，待 C1 决策后调整 |
| [COORDINATE] WORM 存储选型未定 | 本计划按带哈希校验 Parquet 编写，待 C8 决策后调整 |
| KDE 高维精度差 | 明确声明维度限制（<20维），高维用简化分布 |
| Silverman 带宽不适用所有数据分布 | 提供手动带宽覆盖 + bandwidth_factor 调整 |
| 蒙特卡洛体积比计算精度不足 | 可增加采样数（默认 10000，可配置） |
| 3 周估算偏紧 | 优先实现核心功能（fit/sample/volume_ratio），estimate_density 可后补 |
