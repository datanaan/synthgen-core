# SynthGen Core LLM Wiki

基于 Karpathy LLM Wiki 方法论的结构化知识库。

## 理念

从"解释器模式"（每次重新检索生成答案）升级为"编译器模式"（预先编译为结构化 Wiki，查询时直接消费已编译结果）。

## 目录结构

| 目录 | 职责 | 读写权限 |
|------|------|----------|
| `raw/` | 原始素材，符号链接指向 `docs/` | 人写，AI 只读 |
| `entities/` | 实体页（概念/组件/项目/接口） | AI 写，人只读 |
| `topics/` | 主题页（跨实体综合分析） | AI 写，人只读 |
| `skill/` | Skill 定义文件 | 人写，AI 只读 |
| `scripts/` | 辅助脚本 | 人写，AI 只读 |

## 操作

| 操作 | 用途 | 示例 |
|------|------|------|
| INGEST | 将 raw/ 文档编译为 Wiki 页面 | `/llm-wiki ingest raw/specs/v1-unit-a-design.md` |
| QUERY | 查询已编译的知识 | `/llm-wiki query "执行路由器退化路径"` |
| LINT | 知识库健康检查 | `/llm-wiki lint` |

## 当前状态

- Source 页：7（核心框架文档已编译）
- Entity 页：22
- Topic 页：3
- 待编译：v1-v4 各 Unit spec（42篇）+ plan（30篇）
