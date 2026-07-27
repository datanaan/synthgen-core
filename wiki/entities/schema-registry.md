# Schema 注册表 (SchemaRegistry)

> 类型：组件

## 定义

SynthGen Core 的类型管理中心。管理 DEFINE TYPE 语句产生的所有 Schema 实例，提供注册、查询、去重服务。所有后续操作（数据导入、约束定义、数据生成）都通过 SchemaRegistry 查找 Schema。

## 核心接口

```cpp
// src/schema/schema_registry.h
class SchemaRegistry {
public:
    Result<void> register_schema(const std::string& type_name, const Schema& schema);
    Result<SchemaRef> get_schema(const std::string& type_name) const;
    bool has_schema(const std::string& type_name) const;
    std::vector<std::string> list_types() const;
};
```

## 校验规则

- 重复注册返回错误
- 查询不存在的 type 返回错误
- Schema 本身的校验由 Schema 类负责

## 数据流位置

```
DEFINE TYPE AST -> SchemaBuilder -> Schema -> SchemaRegistry.register_schema()
                                                         |
LOAD DATA / DEFINE CONSTRAINT / GENERATE TABLE -> SchemaRegistry.get_schema()
```

## 关联实体

- [[synthlang]] — DEFINE TYPE 语句定义 Schema
- [[storage-engine]] — 存储层需要 Schema 信息进行读写
- [[physics-engine]] — 物理引擎从 Schema 获取默认值域范围

## 来源

- [[source-v1-unit-a-spec]] — §三 SchemaRegistry 接口定义与校验规则
