# 1.5.97 / 1.6.1170 ABI 核验

目标为 Windows x64 Skyrim SE 1.5.97 与 AE 1.6.1170，同一 DLL 使用 CommonLibSSE-NG 的 SE/AE 运行时选择和布局访问器。构建不启用 VR。

## 调用 Hook

| 功能 | SE ID + 偏移 | SE 原目标 RVA | AE ID + 偏移 | AE 原目标 RVA |
| --- | --- | --- | --- | --- |
| 俯仰位移 | 36365 + 0x365 | 0x10FA2F0 | 37356 + 0x3FE | 0x11F19A0 |
| 玩家旋转吸附 | 39379 + 0x42 | 0x6CF2A0 | 40451 + 0x42 | 0x7635E0 |
| 移动吸附 | 36357 + 0x222 | 0x6274A0 | 37348 + 0x1FB | 0x6B8EA0 |
| Proxy → Proxy | 77248 + 0x3F | 0xDC1C60 | 79134 + 0x37 | 0xEA13A0 |
| Proxy → Rigid | 77244 + 0x3CD | 0xDC1C60 | 79130 + 0x3B1 | 0xEA13A0 |
| Rigid → Proxy | 77321 + 0x159 | 0xDF35B0 | 79201 + 0x147 | 0xED5360 |
| Rigid → Rigid | 77306 + 0x29F | 0xDC1C60 | 79186 + 0x2A9 | 0xEA13A0 |

上述位置在两个解包 PE 中均为完整的 5 字节 `E8 rel32` 指令。运行时预检同时核对操作码与解码后的原目标，验证结束前不改写任何 Hook。

## 虚表 Hook

| 类的主虚表 | 槽 / 字节偏移 | SE 原目标 RVA | AE 原目标 RVA |
| --- | --- | --- | --- |
| bhkCharProxyController[0] / ProcessConstraintsCallback | 1 / 0x8 | 0xDEF090 | 0xED0B40 |
| bhkCharRigidBodyController[0] / DeleteThis | 1 / 0x8 | 0xC61A30 | 0xD27520 |
| FOCollisionListener[0] / ContactPointCallback | 0 / 0x0 | 0x39C690 | 0x3F4B90 |

`bhkCharProxyController` 的主基类是 `hkpCharacterProxyListener`，`bhkCharacterController` 子对象偏移为 0x10。ProcessConstraintsCallback 的 `this` 使用完整 Proxy 对象；向接受 `bhkCharacterController*` 的函数传参时由 C++ 执行基类调整。Rigid 控制器的主基类是 `bhkCharacterController`；FOCollisionListener 的主基类是 `bhkBackfaceContactListener`。虚表索引均由本地头文件与对应 PE 的主/次虚表交叉核对。

## 俯仰跳板 ABI

跳板保留 RCX/RDX，将 RDI 复制到第三参数寄存器 R8，调用替换函数后跳转回原调用点 +5。嵌套调用的返回地址成对恢复；跳板依赖原调用点的 Win64 栈对齐及 32 字节 shadow space。报告记录了函数序言和调用点上下文，作为静态 ABI 一致性检查；这些指令本身不能证明 RDI 的 C++ 类型或所有动态栈路径。

## 对象生命周期

动态 AMR 查询保留 manager 和 graph 的智能指针，复制 `graphs[0]` 时使用 `GetRuntimeData().updateLock`，释放该锁后调用 AMR API。它不再返回一个在局部 manager 释放后继续使用的裸 `hkbCharacter*`。这里保留的是图对象生命周期；不宣称该锁覆盖所有 Havok 或第三方写入路径。

额外的二进制追踪确认：1.5.97 的 UpdateAnimationGraphManager（ID 32155，RVA 0x4F1140）调用 0xAE1F60，该函数持有 manager +0x98 锁读取 +0x40 图数组并调用 0xAEF630；1.6.1170 对应 manager 例程 0xBA3C50 使用 +0xA0 锁。这里证明的是图容器遍历与更新回调受锁保护，未证明所有内部 Havok 字段的写入规则或外部 API 的锁顺序。

约束回调仍先调用原函数，再处理 `min(manifold.size(), numConstraints)` 个条目；空 constraints 指针直接跳过附加处理。后续热路径和碰撞逻辑修改见 [性能与安全审核](PERFORMANCE-SAFETY.md)，Hook 位置和调用签名保持一致。

## 证据与限制

`abi-hooks.json` 保存输入 SHA-256、Address Library 映射、指令字节、虚表目标与反汇编上下文。`tools/verify_hooks.py` 可针对本地两个运行时重新生成报告。输入文件路径由命令行传入，不随发布包分发游戏程序。

核验工具需要 Python 3.10+ 和 `capstone`。在项目根目录运行：

```powershell
python tools/verify_hooks.py --output docs/abi-hooks.json
```

默认使用相邻 `CommonLibSSE-NG-ng/tests/REL` 中的两个 Address Library，以及 `../valhallaCombat/build/abi` 中对应版本的解包 EXE。文件位于其他位置时，使用 `--exe-1597`、`--lib-1597`、`--exe-1170`、`--lib-1170` 和 `--commonlib-root` 指定路径；`--help` 列出完整参数。

两个运行时的正向核验均通过。负向对照在临时 1.6.1170 EXE 副本中将 RVA 0x669CCE 的俯仰调用操作码从 E8 改为 E9，工具以退出码 1 报告 `expected E8 rel32 at RVA 0x669cce, found e9`；原 EXE 未改动。

本次 Release 编译与安装通过，最终 PE 为 x64，包含 `SKSEPlugin_Load`、`SKSEPlugin_Query` 和 `SKSEPlugin_Version` 导出。元数据为 v1.3.2、结构依赖、两个显式运行时，最低 SKSE 元数据为 2.0.20；加载函数另对 1.6.1170 要求 2.2.6。安装后的 DLL 与构建输出一致，INI 与原文件逐字节一致。导出、导入和哈希记录在本地 `build/ng-release/plugin-metadata.json`。

尚未运行 Skyrim，因此双版本加载、AMR 交互、俯仰位移、玩家/NPC 吸附及不同控制器组合的碰撞效果仍需游戏内验证。
