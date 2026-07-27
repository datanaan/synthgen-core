# Schema Hash

> 类型：机制

## 定义

SynthGen Core 中用于验证 Schema 完整性的 SHA256 哈希值。将 Schema 序列化为字符串后计算 SHA256，生成 64 字符 hex 字符串。作为 EvidencePackage 的必填字段，用于校验数据生成时使用的 Schema 与记录是否一致。

## 计算流程

```
Schema 对象 -> 序列化为字符串 -> SHA256 -> 64 字符 hex 字符串
```

## 属性

- **确定性**：相同 Schema -> 相同 hash
- **唯一性**：不同 Schema -> 不同 hash
- **格式**：64 字符十六进制字符串

## 实现选择

- 可使用 OpenSSL SHA256 实现
- 也可自研简单哈希（非安全场景，仅用于完整性校验）

## 文件位置

`src/common/hash.h`, `src/common/hash.cpp`

## 验证规则

- EvidencePackage 构建时自动计算 schema_hash
- 验证器检查 schema_hash 是否匹配

## 关联实体

- [[evidence-package]] — schema_hash 是 EvidencePackage 的必填字段
- [[evidence-schema-validator]] — 验证器检查 schema_hash 一致性
