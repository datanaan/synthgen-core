SynthGen Core v3 Unit R 设计规范：GC compaction
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v3 阶段设计规范 v1.0
下游文档：Unit R 实施计划
组件：#19 GC compaction
估算：1 周
依赖：#18 模型版本链

---

## 一、本 Unit 交付什么

**Unit R 交付 GC compaction**——3 保护条件 + 自动合并 + 元数据保留。

交付物：
1. **GcCompactor**：3 保护条件 + 自动 compaction
2. **CompactionResult**：合并结果 + 偏差信息
3. **保护条件**：快照引用/锚定/N版本内

---

## 二、#19 GC compaction

### 2.1 核心语义

GC compaction 将旧版本合并为新版本，节省存储空间。

**3 保护条件**（任一满足则不 compact）：
1. **快照引用**：有生成请求引用此版本
2. **锚定**：用户显式保留此版本
3. **N 版本内**：最近 N 个版本不 compact

### 2.2 接口定义

（定义见 v3 阶段设计规范 3.2 节）

### 2.3 错误处理

```cpp
enum class GcErrorCode {
    kCompactionInProgress,     // 正在 compaction
    kProtectedVersion,         // 受保护版本不可 compact
    kCompactionFailed,         // compaction 失败
    kMetadataMergeConflict,    // 元数据合并冲突
    kAutoCompactDisabled,      // 自动 compaction 已禁用
};
```

---

## 三、Unit R 验收标准

### 3.1 功能验收

- [ ] 3 保护条件全部生效
- [ ] compaction 合并版本正确
- [ ] 合并元数据保留
- [ ] 自动 compaction 定时执行

### 3.2 错误测试验收

- [ ] 受保护版本不可 compact
- [ ] compaction 失败恢复
- [ ] 元数据合并冲突处理

### 3.3 边界条件测试

- [ ] 0 个可 compact 版本
- [ ] 所有版本受保护
- [ ] compaction 中断后恢复

### 3.4 测试验收

- [ ] 至少 15 个测试用例
- [ ] 错误测试占比 ≥ 30%

---

## 四、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `GcCompactor::compact()` | Unit S (时间旅行) | compaction 执行 |
| `CompactionResult` | Unit T (偏差报告) | compaction 偏差 |
| `GcCompactor::is_protected()` | Unit S, T | 保护条件查询 |
