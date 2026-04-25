# Agent 行为规范

## Commit 规范

### Commit 格式
```
<type>(<scope>): <subject>

<body>

Model: <model-name> <model-provider>
Agent: <agent-name> <agent-email>
```

### Type 类型
- `feat`: 新功能
- `fix`: 错误修复
- `refactor`: 重构（非功能性修改）
- `docs`: 文档变更
- `test`: 测试相关
- `chore`: 构建/工具变更
- **自定义类型**: 可根据实际场景添加，如 `perf`, `ci`, `build` 等

### Subject 规范

- 使用祈使语气（如 "add" 而非 "added"）
- 不要大写开头
- 不使用句号结尾
- 控制在 50 字符以内

### Body 规范

- 每行不超过 72 字符
- 说明变更原因和方式，而非做了什么
- 可用 "-" 列表形式详细说明

### 示例
```
fix(usb): correct clock macros and port configuration

- Fix USB clock macros: USB1_OTG_HS -> USB_OTG_HS
- Change BOARD_TUD_RHPORT from 0 to 1 for correct port mapping
- Update ULPI, high-speed and full-speed pin initialization macros

Model: MiniMax-M2.7 MiniMax
Agent: opencode opencode@anomaly.co
```

## 查询优先级

在执行任何操作前，必须按以下顺序查询相关文件：

1. **AGENT.md** - 本文件，包含行为规范
2. **README.md** - 项目概述和构建说明
3. **项目目录结构** - 了解代码组织
4. **相关头文件** - 理解接口和类型定义
5. **相近功能的实现** - 参考现有代码模式
