# SynthGenService

> 类型：组件

## 定义

SynthGen Core 的 C++ 核心服务门面类。封装 Parser、SchemaRegistry、RectangularSampler、ValueRangeValidator、TailReportBuilder、EvidencePackageBuilder 等全部组件，提供统一的生成服务接口。REST API 和 Python SDK 通过调用 SynthGenService 暴露功能。

## 核心接口

```cpp
// src/api/service.h
class SynthGenService {
public:
    explicit SynthGenService(StorageBackend& storage);
    Result<SchemaRef> define_type(const DefineTypeRequest& req);
    Result<ImportResult> load_data(const LoadDataRequest& req);
    Result<ConstraintRef> define_constraint(const DefineConstraintRequest& req);
    Result<ExplainResult> explain(const ExplainRequest& req);
    Result<GenerationResult> generate(const GenerateRequest& req);
};
```

## 内部组合

SynthGenService 组合了以下组件：
- `Parser parser_` — SynthLang 解析器
- `SchemaRegistry registry_` — Schema 注册表
- `RectangularSampler sampler_` — 物理引擎
- `ValueRangeValidator validator_` — 值域验证器
- `TailReportBuilder tail_builder_` — tail_report 构建器
- `EvidencePackageBuilder evidence_builder_` — 证据包构建器

## 设计模式

- **Facade 模式**：为复杂的子系统提供统一接口
- **依赖注入**：通过构造函数注入 StorageBackend

## 关联实体

- [[rest-api-v1]] — REST API 调用 SynthGenService
- [[python-sdk]] — Python SDK 通过 pybind11 调用 SynthGenService
