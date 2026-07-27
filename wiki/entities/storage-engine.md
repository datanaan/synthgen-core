# 存储引擎

> 类型：组件
> 首次编译：2026-05-11

## 定义

四层存储架构，通过 StorageBackend 抽象接口对上层提供统一访问。存储层是 WORM 合规的不可变系统。

## 详情

**四层结构**：
1. **基表层**：原始输入（INSERT ONLY）+ 约束卡片 DDL
2. **快照层**：生成数据 + provenance + tail_report（写入后不可变）
3. **模型层**：参数/检查点 + 版本链 + GC 自动 compaction
4. **审计日志层**：哈希链 + WORM 存储

**StorageBackend 接口**：
- 写入：append(table_id, batch) / append_model(model_id, checkpoint)
- 读取：scan(table_id, snapshot_id) / load_model(model_id, version_id)
- 版本：list_versions / get_snapshot
- 生命周期：compact / verify_audit_chain
- 事务：atomic_write(operations)

**v1 默认后端**：对象存储 + Parquet + 自研元数据层。Parquet 提供列式存储和高效压缩。

**事务语义**（atomic_write）：
1. 先写数据到对象存储（幂等）
2. 写元数据层（原子操作）
3. 提交审计日志
4. 中断恢复以元数据层状态为准

**版本对应**：
- v1 #4：基础存储（对象存储+Parquet+元数据层v1）
- v2 #15：哈希链审计日志
- v3 #23：存储模型层（检查点+流式加载+版本索引+两阶段提交）

## 关联实体

- [[audit-log]] — 审计日志是存储引擎的组成部分
- [[model-version-chain]] — 模型层版本链
- [[synthgen-core]] — 整体架构中的存储层

## 来源

- [[source-engineering-framework]] — §5 存储引擎
- [[source-overall-design]] — §3 目录结构（storage/）
- [[source-v1-unit-b-spec]] — §二 StorageBackend 接口、§三 ObjectStoreBackend 实现与元数据层
