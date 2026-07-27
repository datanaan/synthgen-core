# REST API v1

> 类型：组件

## 定义

SynthGen Core v1 的 HTTP 接口层。基于 cpp-httplib 实现，提供 7 个端点，覆盖类型定义、数据导入、约束定义、执行计划查询、数据生成、指标暴露、健康检查等全部功能。

## 端点列表

| 方法 | 路径 | 功能 | HTTP 状态码 |
|------|------|------|------------|
| POST | /v1/types | 定义类型 | 200/400 |
| POST | /v1/types/{name}/data | 导入数据 | 200/400/404 |
| POST | /v1/constraints | 定义约束 | 200/400/404 |
| POST | /v1/explain | 执行计划 | 200/400 |
| POST | /v1/generate | 生成数据 | 200/400/404 |
| GET | /v1/metrics | Prometheus 指标 | 200 |
| GET | /v1/health | 健康检查 | 200 |

## 技术选择

- **cpp-httplib**：轻量 C++ HTTP 库，足够 v1 使用，v2+ 可替换
- **JSON 请求/响应**：所有 POST 端点使用 JSON 格式
- **Prometheus 格式**：/v1/metrics 输出 Prometheus 可采集的指标

## 错误处理

- 非法 JSON -> 400
- 不存在的 type/constraint -> 404
- v1 不支持的语法 -> 400
- 请求体过大 -> 413

## 并发模型

- 使用线程池处理并发请求
- 限制并发数

## 文件位置

- HTTP 服务器：`src/api/server.h/.cpp`
- 请求处理器：`src/api/handlers.h/.cpp`

## 关联实体

- [[synthgen-service]] — REST API 调用 SynthGenService
- [[scaffolding]] — Health/Metrics/Trace 端点属于脚手架设施
