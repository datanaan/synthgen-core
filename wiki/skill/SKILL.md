# SynthGen Core LLM Wiki — Skill 定义

基于 Andrej Karpathy LLM Wiki 方法论，为 SynthGen Core 项目定制的结构化知识库。
核心理念：从"解释器模式"（每次重新检索生成）升级为"编译器模式"（预先编译为结构化 Wiki，查询时直接消费已编译结果）。

## 目录结构

```
wiki/
├── raw/                      # 原始素材（不可变，人写 AI 只读）
│   ├── architecture/         # 架构文档（理论框架、工程框架、路线图、整体设计）
│   ├── decisions/            # 产品决策记录（定位、定价、守则）
│   ├── specs/                # 各版本 Unit spec（v1-v4）
│   ├── plans/                # 各版本 Unit plan（v1-v4）
│   └── reference/            # 参考文档（EvidencePackage Schema、团队信息）
├── wiki/  →  (本目录的 entities/ topics/ index.md log.md)
│   ├── entities/             # 实体页（概念/组件/项目/接口）
│   ├── topics/               # 主题页（跨实体综合分析）
│   ├── index.md              # 知识地图总览
│   └── log.md                # 操作日志
├── skill/
│   └── SKILL.md              # 本文件
└── README.md
```

## 三大核心操作

### 1. INGEST（导入编译）

将 `raw/` 中的原始资料编译为结构化 Wiki 页面。

**触发**：`/llm-wiki ingest raw/specs/v1-unit-a-design.md`

**执行步骤**：
1. 阅读原始文档
2. 创建 source 页（摘要 + 关键要点）
3. 提取实体（概念/组件/项目/接口），创建或更新 entity 页
4. 建立实体间 `[[双向链接]]`
5. 检查与已有知识的矛盾，标注到实体页
6. 更新 `index.md` 知识地图
7. 追加 `log.md` 操作日志

**实体类型标注**：`概念|组件|项目|接口|数据结构`

**命名规则**：
- Source 页：`source-{关键词}.md`（如 `source-v1-unit-a-design.md`）
- Entity 页：`{实体名}.md`（kebab-case，如 `value-range-constraint.md`）
- Topic 页：`topic-{主题}.md`（如 `topic-v1-implementation.md`）

#### Entity 页模板

```markdown
# {实体名}

> 类型：{概念|组件|项目|接口|数据结构}
> 首次编译：{YYYY-MM-DD}
> 最后更新：{YYYY-MM-DD}

## 定义
{100字以内的定义}

## 详情
{详细技术说明}

## 关联实体
- [[{关联实体}]] — {关系说明}

## 来源
- [[source-{key}]] — {引用章节}
```

#### Source 页模板

```markdown
# {文档标题}

> 来源：{raw/路径}
> 编译日期：{YYYY-MM-DD}

## 摘要
{200字以内}

## 关键要点
- {要点}

## 提取的实体
- [[{实体名}]] — {一句话描述}
```

#### 矛盾标注

如果新内容与已有 entity 页冲突：
- 新 entity 页添加 `⚠️ 矛盾` 标记
- 已有 entity 页添加反向引用和冲突说明

### 2. QUERY（查询）

**触发**：`/llm-wiki query "执行路由器的退化路径有哪些"`

**执行步骤**：
1. 读取 `index.md`，定位相关页面
2. 读取相关 entity 页和 source 页
3. 综合回答，引用 `[[实体]]` 和 `[[source-xxx]]`
4. 高质量答案（覆盖 3+ 实体）可回写为新的 `topics/` 页面

### 3. LINT（健康检查）

**触发**：`/llm-wiki lint`

**检查项目**：
- **矛盾检测**：同一实体在不同页面中的描述冲突
- **孤立页面**：无入站 `[[链接]]` 的 entity 页
- **过时信息**：source 页编译日期超过 30 天的内容标记
- **缺失引用**：被 `[[链接]]` 提及但尚未创建的实体页
- **冗余内容**：多个 entity 页包含重复描述，建议合并

## 编译优先级

raw/ 中的文档按以下优先级编译：

| 优先级 | 目录 | 说明 |
|--------|------|------|
| P0 | architecture/ | 核心框架（理论、工程、路线图）— 已编译 |
| P1 | decisions/ | 产品决策与工程守则 — 已编译 |
| P2 | reference/ | Schema 定义与参考 — 已编译 |
| P3 | specs/ | 各版本 Unit spec — 待编译 |
| P4 | plans/ | 各版本 Unit plan — 待编译 |

## 与项目的关系

- `raw/` 中的文档通过符号链接指向 `docs/` 目录，不复制内容
- `docs/` 是人维护的权威来源，`raw/` 是 AI 只读的入口
- wiki 页面（entities/、topics/）由 AI 编写维护，人只读
- 编译后的 Wiki 页面用于快速查询和理解项目，不替代原始文档
