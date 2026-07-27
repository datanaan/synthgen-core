# Python SDK

> 类型：组件

## 定义

SynthGen Core 的 Python 客户端库。通过 pybind11 绑定 C++ 核心，加上 Python 层封装提供 Pythonic API。支持 Python 3.8+。

## 架构

```
用户代码
    └── python/synthgen/__init__.py  (Pythonic 封装 + 类型提示)
            └── src/sdk/bindings.cpp  (pybind11 C++ 绑定)
                    └── SynthGenService  (C++ 核心服务)
```

## 关键特性

- **Pythonic API**：符合 Python 习惯的接口设计
- **类型提示**：完整的 type hints 支持
- **文档字符串**：所有公开方法包含 docstring
- **错误映射**：C++ Result<T> -> Python 异常（ConnectionError、TimeoutError、ValueError）

## 绑定的核心类

- `SynthGenClient` — Python 客户端
- `Column` — 列定义
- `RangeCheck` — 范围检查
- 结果类型 — GenerationResult 等

## 构建方式

- CMake + pybind11 的 find_package
- 编译为 .so/.pyd 扩展模块

## 文件位置

- C++ 绑定：`src/sdk/bindings.cpp`
- Python 封装：`python/synthgen/__init__.py`
- 测试：`python/tests/test_sdk.py`

## 关联实体

- [[synthgen-service]] — Python SDK 调用的底层 C++ 服务
- [[rest-api-v1]] — SDK 也可以通过 HTTP 连接远程服务
