# v3 Unit Q Plan — 模型版本链实施计划

> 来源：docs/superpowers/v3/plans/2026-05-10-synthgen-v3-unit-q-plan.md
> 编译日期：2026-05-14

## 摘要

Unit Q 实施计划，5 个 Task、12 个步骤、估算 1 周。Task 1 实现 ModelVersion 数据结构与 Parquet 序列化（0.125w）。Task 2 实现 ModelVersionChain 核心——create_version（UUID v4、父版本校验、循环检测）、get_version/list_versions、不可变保证（0.375w）。Task 3 脚手架集成——Trace span、Explain 接口、Metrics 注册（0.125w）。Task 4 错误处理和测试——7 个错误码全覆盖、15+ 单元测试、5+ 边界条件测试（0.25w）。Task 5 集成测试——6+ 与存储层集成测试（0.125w）。

## 关键要点

- ModelVersion 使用 Arrow SchemaBuilder 序列化，custom_metadata 用 JSON 字符串存储
- create_version 生成 UUID v4，校验父版本存在性和同 model_name，循环检测用 visited set O(depth)
- modify_version 故意不接受有效载荷，仅返回 kImmutableViolation，审计日志记录修改尝试
- 版本链深度 >100 时性能缓解：路径压缩维护 visited set
- 产出文件：src/storage/version/ 下的 model_version.h/cpp, model_version_chain.h/cpp

## 提取的实体

- [[model-version-chain]] — 已有实体，实施计划补充实现细节
- [[model-version]] — 已有实体，数据结构实现细节
