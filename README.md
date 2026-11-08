# Animation Motion Fix — CommonLibSSE-NG

使用相邻目录的 `../CommonLibSSE-NG-ng` 源码构建，同一个 DLL 支持两个运行时：

| Skyrim 运行时 | SKSE64 最低版本 | Address Library |
| --- | --- | --- |
| 1.5.97 | 2.0.20 | SE / 1.5.97 对应版本 |
| 1.6.1170 | 2.2.6 | AE / 1.6.1170 对应版本 |

加载入口会检查游戏和 SKSE 版本。插件直接访问引擎结构，因此元数据声明为 `STRUCT_DEPENDENT`；未列出的运行时不在本次支持范围内。

## 安装

构建产物位于 `package/SKSE/Plugins/AnimationMotionFix.dll`，默认配置位于同目录。把 `package` 作为模组安装，或将其内容复制到游戏的 `Data` 目录。已有自定义 INI 时保留自己的配置。

原有俯仰位移、旋转吸附、移动吸附和角色推挤处理保留。动态移动吸附模式继续通过 Animation Motion Revolution 的 V1 API 查询动画数据；AMR 没有安装或未提供此 API 时查询结果为 false。

当前源码使用 `[Tweak]` 下的 `PlayerMovementMagnetismMode` / `NpcMovementMagnetismMode`：`0` 为保留吸附，`1` 为 AMR 动态判断，`2` 为禁用吸附。旧版说明和 INI 使用的 `DisablePlayerMovementMagnetism` / `DisableNpcMovementMagnetism` 与当前源码不一致；使用模式设置时请使用上述键名。

## 构建

需要 Windows x64、支持 C++23 的 MSVC、CMake、Ninja 和 vcpkg。在 x64 Native Tools 命令行中运行：

```powershell
$env:VCPKG_ROOT = 'E:/BuildTools/VC/vcpkg'
cmake --preset ng-release
cmake --build --preset ng-release
cmake --install build/ng-release --prefix package --config Release
```

其他 CommonLib 源码路径可在配置时通过 `-DCOMMONLIBSSE_NG_PATH=...` 指定。构建同时启用 SE 和 AE，关闭 VR。根清单包含本地 CommonLib 构建所需的 DirectXTK、rapidcsv、spdlog，以及插件所需的 SimpleIni 和 Xbyak。

## 核验

两个版本各有 7 个调用 Hook 和 3 个虚表 Hook；其 ID、偏移和原目标集中在 `include/HookSites.h` / `src/HookSites.cpp`。加载时先核对全部位置，再执行任何写入。若另一插件已改写这些位置，加载会失败并在 `AnimationMotionFix.log` 中记录冲突。

[双版本 ABI 说明](docs/ABI.md) 和 [机器核验报告](docs/abi-hooks.json) 记录二进制证据及复现命令。编译和静态二进制检查不代替游戏内测试；本次未运行 Skyrim。

[性能与安全审核](docs/PERFORMANCE-SAFETY.md) 记录热路径调整、碰撞记录内存池、线程与指针生命周期处理，以及 Release / AddressSanitizer 测试和容器微基准结果。仍使用通用 x64 指令集。

[AMR API 上游核查](docs/AMR-API-UPSTREAM.md) 记录 `HasAnimMotionData` 的实际提供者、V1 ABI、活动记录失效和锁证据；动态模式需要 AMR 提供该导出。
