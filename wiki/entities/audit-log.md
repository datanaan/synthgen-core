# 哈希链审计日志

> 类型：组件
> 首次编译：2026-05-11

## 定义

与 WAL 彻底分离的独立不可变记录系统。每条记录包含前一记录的哈希，形成链式结构，支持篡改检测。

## 详情

**记录格式**：sequence, timestamp, actor, operation, target, parameters, prev_hash, current_hash

**实现细节**：
- 创世记录：prev_hash = SHA256("SYNTHGEN_GENESIS")
- 写入验证：写入前检查 prev_hash 与链尾 current_hash 一致
- 分叉检测：相同 prev_hash 出现多条记录时报错+人工介入
- 每日全链校验：后台任务遍历整条链
- 存储介质：WORM 合规保留的对象存储

**版本对应**：
- v1：审计日志无篡改检测。audit_immutability 标记 not_applicable
- v2 #15：哈希链审计日志就位，audit_immutability: verified

**关键原则**：审计不可变保证从 v2 起生效。v1 诚实声明此保证缺失。

## 关联实体

- [[storage-engine]] — 审计日志是存储引擎的组成部分
- [[evidence-package]] — audit_immutability 字段

## 来源

- [[source-engineering-framework]] — §5.4 审计日志
- [[source-roadmap]] — v2 #15
