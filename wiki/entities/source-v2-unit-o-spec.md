# v2 Unit O Spec — 哈希链审计 + KDE 数据引擎

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-unit-o-design.md
> 编译日期：2026-05-14

## 摘要

Unit O 包含两个独立可并行组件：#15 哈希链审计日志（1 周）和 #15b 数据引擎 v1 KDE（3 周）。哈希链审计实现所有生成操作的不可篡改记录，含创世记录、SHA-256 链式哈希、分叉检测、每日校验。数据引擎基于核密度估计（KDE）学习训练数据分布，提供密度采样、体积比计算、密度估计三大能力，用于后筛选排除率预估。含 [COORDINATE] KDE 技术选型（推荐自研 C++）和 WORM 存储选型（推荐带哈希校验 Parquet）。

## 关键要点

- 审计保证：创世记录(prev_hash="0")、写入验证、分叉检测、每日全链校验
- WORM 存储：Write Once Read Many，modify/remove 返回 kWriteOnceViolation
- KDE 三大能力：密度采样、体积比计算（蒙特卡洛法）、密度估计
- 维度限制：中低维 <20 有效，>20 返回 kDimensionTooHigh 警告
- 带宽选择：默认 Silverman 规则，可手动指定或 bandwidth_factor 调整
- 训练数据量限制：默认最大 100 万行
- 3 种核函数：gaussian、epanechnikov、tophat
- 测试要求：#15 至少 15 个、#15b 至少 25 个

## 提取的实体

- [[audit-log]] — 哈希链审计日志
- [[worm-storage]] — WORM 存储层
- [[kde]] — 核密度估计数据引擎
- [[data-engine]] — 数据引擎 v1 接口
- [[exclusion-rate]] — 体积比用于排除率预估
