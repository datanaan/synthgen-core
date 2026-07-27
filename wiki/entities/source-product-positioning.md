# 产品定位与协作关系 v2.0

> 来源：docs/product/SynthGen Core 产品定位与协作关系 v2.0.md
> 编译日期：2026-05-11

## 摘要

定义 SynthGen Core 的产品定位——以生成为核心的融合型基础设施，与 Polymorphic-Twin 为姊妹系统。五个独立价值全部条件化（不再做无条件承诺）。用户接口为 Python SDK + REST API，SynthLang 为内部 IR。明确"暂不包含"的能力清单。

## 关键要点

- **姊妹系统**：SynthGen Core（证据提供者）+ Polymorphic-Twin（证据审判者），通过 EvidencePackage 标准协议协作
- **独立部署**：SynthGen Core 可独立销售，无 Polymorphic-Twin 时仍有"有比没有好"的工程工具价值
- **用户接口**：Python SDK（pip install synthgen）+ REST API；SynthLang 不对用户暴露
- **必要角色**：数据工程师/ML工程师（核心用户）+ 领域专家（必要前提，否则物理合法性保证失效）
- **五个条件化价值**：替代不可采集数据、替代昂贵标注、覆盖人工无法覆盖的参数空间、可复现、效率提升
- **暂不包含**：反例搜索(v1)、实时流式生成、LLM编译器、多租户/水平扩展

## 提取的实体

- [[synthgen-core]] — 项目整体
- [[polymorphic-twin]] — 姊妹系统
- [[evidence-package]] — 协作协议
- [[synthlang]] — 内部 IR，不对用户暴露
