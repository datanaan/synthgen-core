# Metrics 注册表 (MetricsRegistry)

> 类型：组件

## 定义

SynthGen Core 的指标管理中心。支持 Counter（计数器）、Histogram（直方图）、Gauge（仪表）三种指标类型，提供 Prometheus 格式输出。所有组件的指标通过 MetricsRegistry 统一注册和暴露。

## 核心接口

```cpp
// src/scaffold/metrics.h
class MetricsRegistry {
public:
    void register_counter(const std::string& name);
    void register_histogram(const std::string& name, const std::vector<double>& buckets);
    void register_gauge(const std::string& name);
    void increment(const std::string& name, double value = 1.0);
    void observe(const std::string& name, double value);
    void set(const std::string& name, double value);
    std::string to_prometheus() const;
};
```

## 指标类型

| 类型 | 操作 | 使用场景 |
|------|------|---------|
| Counter | increment | 请求计数、错误计数 |
| Histogram | observe | 耗时分布、batch 大小分布 |
| Gauge | set | 当前连接数、内存使用量 |

## v1 注册的指标

| 指标名 | 类型 | 来源组件 |
|--------|------|---------|
| parser_parse_total | Counter | Parser |
| parser_parse_errors | Counter | Parser |
| parser_parse_duration_ms | Histogram | Parser |
| generation_total | Counter | Physics Engine |
| generation_rows | Counter | Physics Engine |
| generation_duration_ms | Histogram | Physics Engine |
| validation_total | Counter | Validator |
| validation_passed | Counter | Validator |
| validation_failed | Counter | Validator |
| storage_append_total | Counter | Storage |
| storage_scan_total | Counter | Storage |
| evidence_package_total | Counter | Evidence |
| import_total | Counter | Data Import |

## 暴露方式

- `/v1/metrics` HTTP 端点，Prometheus 格式
- 可被 Prometheus 直接采集

## 关联实体

- [[scaffolding]] — Metrics 是脚手架设施的核心组成部分
- [[rest-api-v1]] — /v1/metrics 端点暴露指标
