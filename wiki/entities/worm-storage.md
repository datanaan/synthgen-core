# WORM 存储 (Write Once Read Many)

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

WORM（Write Once Read Many）存储是审计日志的底层存储层，保证审计记录写入后不可修改和删除，实现审计不可篡改保证。

## 详情

WORM 存储的核心语义：
- **追加写入**：write() 写入记录并返回 record_id
- **只读查询**：read() 按 record_id 查询，scan() 按时间范围查询
- **不可变保证**：modify() 和 remove() 返回 kWriteOnceViolation 错误
- **存储格式**：带哈希校验的 Parquet 文件（推荐方案）

WORM 存储是审计不可变保证的基础设施层。上层 AuditLog 通过 WORM 存储确保：
- 每条审计记录一旦写入就无法被修改或删除
- 哈希链验证可检测到任何篡改尝试
- 修改/删除操作直接返回错误，而非静默失败

[COORDINATE] C8：WORM 存储选型待决策，三个选项：
- 带哈希校验的 Parquet（推荐）
- Append-only 文件
- 专用 WORM 存储引擎

## v2 范围

v2 Unit O Part A Task A1 实现 WORM 存储：
- `src/storage/audit/worm_storage.h/.cpp`
- 追加写入 Parquet 文件
- modify()/remove() 返回 kWriteOnceViolation
- 时间范围查询
- 至少 5 个测试用例

## 关联实体

- [[audit-log]] — 审计日志基于 WORM 存储
- [[data-grade]] — audit_immutability = "verified" 依赖 WORM 保证

## 来源

- [[source-v2-unit-o-spec]] — 二、2.2 接口定义
- [[source-v2-unit-o-plan]] — Part A Task A1：WORM 存储实现
