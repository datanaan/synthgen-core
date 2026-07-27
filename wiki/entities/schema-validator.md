# Schema 校验器 (Schema Validator)

> 类型：工具
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

Schema 校验器是 v2 工具线的核心工具，实现编译期三方 diff 校验：代码接口注册 vs EvidencePackage Schema vs 理论框架承诺清单。

## 详情

Schema 校验器的目标是防止接口定义、数据 Schema 和理论承诺之间的不一致。

**接口注册机制**：每个组件编译时自动生成接口描述 .json，包含：
- 组件名、命名空间
- 方法签名（参数、返回值）
- 错误码列表
- Trace span 和 Metrics 定义

**校验规则**：

| 校验项 | 规则 | 错误级别 |
|--------|------|---------|
| 字段名拼写 | 代码接口注册 vs Schema 定义 | 高 |
| 必选字段存在性 | Schema 定义 vs 实际填充 | 高 |
| 枚举值匹配 | 适用性标注 vs 实际值 | 中 |
| 适用性标记缺失 | always 字段必须有填充逻辑 | 高 |

**实现语言**：Python（CI 脚本），非产品代码。[COORDINATE] C7 Python 工具链接受度待确认，可回退到 C++ 编译期静态断言。

## v2 范围

v2 工具线 Task 3 实现 Schema 校验器 v1.0：
- `tools/schema_checker/validate.py`
- 接口注册机制（组件编译时生成 .json）
- 三方 diff 校验脚本
- CI 集成（编译后自动运行）
- 至少 3 个校验器测试

## 关联实体

- [[tool-line]] — 工具线组件之一
- [[evidence-package]] — Schema 校验的验证对象之一
- [[constraint-classifier]] — 接口注册的组件之一

## 来源

- [[source-v2-tool-spec]] — 四、Schema 校验器 v1.0
- [[source-v2-tool-plan]] — Task 3：Schema 校验器 v1.0
