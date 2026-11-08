# Animation Motion Fix 1.3.2 — CommonLibSSE-NG（1.5.97 / 1.6.1170）

修复第三人称攻击时角色被动画位移带着乱走、转身时朝向被吸回去的问题。同一个 DLL 支持两个运行时：Skyrim **1.5.97** 配 SKSE 2.0.20，Skyrim **1.6.1170** 配 SKSE 2.2.6，各自用对应版本的 Address Library。

## 安装

1. 需要 SKSE64（1.5.97 用 2.0.20 及以上，1.6.1170 用 2.2.6 及以上）、对应运行时的 Address Library for SKSE Plugins、Microsoft Visual C++ x64 运行库。
2. 把 ZIP 交给 MO2 / Vortex，或把包内的 `SKSE` 目录合并进游戏 `Data`：
   ```
   Data/SKSE/Plugins/AnimationMotionFix.dll
   Data/SKSE/Plugins/AnimationMotionFix.ini
   ```
3. 已有自定义 INI 时保留自己的配置，不要覆盖。
4. 想用动态移动吸附模式，需要装 Animation Motion Revolution 并让它导出 `HasAnimMotionData`；没装或没这个导出时该模式按“不吸附”处理。

## 下载

- **DLL**：`SKSE/Plugins/AnimationMotionFix.dll`，652,800 字节，FileVersion `1.3.2.0`，SHA256 `f4c9a38f1c4df6ce0bd2c50ff900ecc486c2b6a2bc0fa7e79b19e59a5c73416c`
- **压缩包**：`AnimationMotionFix-NG-1.5.97-1.6.1170.zip`，289,294 字节，2 个文件，SHA256 `4b862242910150c4d81ea73d0c11f92887a805abcd51cb819f9fcb1e08969c1c`

包里只有插件本体和默认 INI，不含行为、动画和语音资源。

## 功能

- **俯仰位移**：第三人称攻击时按角色俯仰角反向抵消动画带来的前冲位移，零俯仰直接跳过，接近 ±π/2 的奇点角度排除在外。
- **旋转吸附**：转身动画结束后把朝向固定回攻击方向，避免模型带着角色继续旋转。
- **移动吸附**：`[Tweak]` 下 `PlayerMovementMagnetismMode` / `NpcMovementMagnetismMode` 三档——`0` 保留吸附，`1` 由 AMR 动态判断这一帧是否有动画位移，`2` 完全禁用。旧版 INI 的 `DisablePlayerMovementMagnetism` / `DisableNpcMovementMagnetism` 与当前源码不一致，请改用上面的模式键。
- **角色推挤**：修正攻击时双方被弹开的方向与力度，Rigid 对 Rigid 的 A→B 与 B→A 两个方向独立判定。

## 1.3.2 改了什么

**执行耗时**

- 碰撞平面记录改用 `pmr::unordered_map` 加 `unsynchronized_pool_resource`：同时待处理 1 / 16 / 64 / 512 条时，每组写入取出从 47.2 / 45.8 / 49.1 / 49.2 ns 降到 37.2 / 32.8 / 31.7 / 32.4 ns（最多 1.55 倍）。基准只测容器，不含引擎调用与 Havok 争用，不能换算成帧率。
- 约束回调里 Proxy 角色解析从每个接触点一次改成每次回调一次。
- 两个固定图变量名改用函数局部 `BSFixedString`，复用已 intern 的名称。
- 俯仰逆变换先做有限数检查与零角度短路，正常角度只算一次 sin / cos。
- 攻击推挤判断把攻击状态、动画驱动状态、目标关系的检查提到图变量查询和 AMR 调用之前。

**指针与并发**

- 配置字段改成独立 atomic；AMR 接口指针用 release / acquire 发布，消除 PostLoad 写入与 Hook 读取之间的数据竞争。
- AMR 查询保留 manager 与 graph 的智能指针，读 `graphs[0]` 时走版本化 `updateLock`，退出锁后再调 AMR API；1.5.97 的锁在 manager +0x98，1.6.1170 在 +0xA0。
- `PendingPlanes` 的内存池操作全部在同一把互斥锁内，池在 map 销毁后销毁；`Update` 入口先给接收者建局部 `NiPointer`，缓存里的键只用于匹配和擦除，不解引用、不保留游戏对象引用。
- 分配失败时保留原函数给出的速度，不会先清零再丢失校正记录。

**逻辑修复**

- Rigid / Rigid 碰撞的 A→B 与 B→A 判断此前没有独立执行，只有 B 攻击 A 时会漏掉质量响应修改，现已分开处理。

## 验证状态

- Release 构建与安装通过，安装 DLL 与构建输出逐字节一致，INI 内容不变。
- 双版本 ABI 复核通过 **14 个调用点与 6 个虚表项**（1.5.97 与 1.6.1170 各一套），加载时先核对全部位置再写入，冲突会拒绝加载并写日志。
- 独立 C++23 核心测试通过：10 万组随机俯仰逆变换、奇点与阈值、非有限输入、第一条平面优先、取出一次、删除后地址复用、内存池复用、分配失败恢复、8 线程各 2 万轮并发键操作。
- AddressSanitizer 构建的同一套测试通过。

**尚未游戏内实测**。待实机确认：两个运行时的 AMR 动画切换、双向攻击推挤、Proxy / Rigid 组合、存读档切换场景、多角色密集碰撞。

## 已知限制

- 只支持 1.5.97 与 1.6.1170；其他 AE 版本、VR、GOG 不在支持范围。
- 动态吸附模式依赖 AMR 导出的 `HasAnimMotionData`，上游未提供时该模式恒为 false。
- 静态二进制核验不能替代游戏内测试；Havok 工作线程与第三方插件的锁顺序未完全证明。
