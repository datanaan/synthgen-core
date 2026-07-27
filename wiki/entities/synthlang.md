# SynthLang

> 类型：组件
> 首次编译：2026-05-11

## 定义

SynthGen Core 的自研领域特定语言（DSL），不模仿任何现有 SQL 方言，关键字和语义完全为数据生成场景定制。

## 详情

**设计原则**：
- 自研 parser，不模仿 SQL 方言
- Schema 强制：类型系统是 DDL 一等公民，排序列在 Schema 级别 ORDER 关键字声明
- 物理约束以命名卡片形式定义，可组合、可版本化
- v1 作为内部编译目标（IR），用户不直接书写，通过 Python SDK/REST API 间接使用

**核心语法**：
```synthlang
DEFINE TYPE sensor_log {
    timestamp: DATETIME NOT NULL ORDER,
    wind_speed: FLOAT [0.0, 50.0],
    status: ENUM('normal', 'warning', 'fault')
};

LOAD DATA INTO sensor_log FROM '/data/sensors/*.parquet';

DEFINE CONSTRAINT wind_safety ON sensor_log {
    wind_speed BETWEEN 0 AND 25 DURING status = 'normal_operation',
    vibration[t] - vibration[t-1] < 5.0,
    AVG(temperature) OVER (INTERVAL 1 HOUR) <= 40.0
};

GENERATE TABLE gen_samples FROM sensor_log
WITH CONSTRAINTS wind_safety MODE = constrained LIMIT 1000;
```

**窗口类型扩展**：v1 仅 INTERVAL（时间窗口）；v2 增加 ROWS、PARTITION BY；v3 增加 SESSION。语法解析器预留关键字，标记 unsupported_in_v1。

**版本对应**：
- v1 #1：核心语法（DEFINE TYPE / LOAD DATA / DEFINE CONSTRAINT 仅值域 / GENERATE TABLE）
- v2 #12：Parser 扩展（行间约束 / 聚合约束 / DURING/WHEN 语义）

## 关联实体

- [[constraint-layering]] — 约束类型决定 SynthLang 语法
- [[execution-router]] — SynthLang 编译后的 AST 驱动路由

## 来源

- [[source-engineering-framework]] — §2 接口层 SynthLang
- [[source-overall-design]] — §3 目录结构（parser/）
