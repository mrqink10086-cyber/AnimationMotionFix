# AnimationMotionFix 性能与安全审核

本次在 NG 迁移版上优化执行顺序、碰撞记录缓存及对象生命周期。同一个 DLL 继续支持 Skyrim 1.5.97 / SKSE 2.0.20 与 Skyrim 1.6.1170 / SKSE 2.2.6。优化前源码备份为相邻目录 `AnimationMotionFix-ng-before-optimization-20261108`。

## 执行与逻辑

- 攻击推挤判断先检查攻击状态、动画驱动状态和目标关系，再查询图变量及 AMR。Force / Forbid 图变量的优先级保留，所有模式仍允许图变量覆盖。
- 约束回调中，Proxy 角色解析从每个接触点一次改为每次回调一次；仍保留原回调先执行、约束数量取两个数组有效长度最小值的顺序。
- 两个固定图变量名使用函数局部 `BSFixedString`，复用 interned 名称。角色状态、图变量和 AMR 查询结果仍实时读取；AMR 未安装时在读取图之前返回 false。
- Rigid / Rigid 碰撞的 A→B 与 B→A 判断独立执行，修复只有 B 攻击 A 时遗漏质量响应修改的问题。
- 空字段或被禁用的接触事件继续沿用原插件的跳过规则。两个版本的原回调都会无空检查解引用 properties，正常路径还会解引用 contactPoint，不能将空事件直接交还原函数。
- 俯仰逆变换检查有限数值，排除绝对值达到 π/2 或余弦小于 `1e-4` 的角度，并在结果溢出时保留原位移。零俯仰直接返回，正常角度各计算一次 sin / cos。

## 碰撞记录与内存

`PendingPlanes` 使用互斥锁、`pmr::unordered_map` 和 `unsynchronized_pool_resource`。内存池的所有操作都在同一互斥锁内；池在 map 销毁后销毁。重复控制器保留第一条平面，与原 `emplace` 行为一致。Update 取出并删除记录，DeleteThis 删除记录；读取存档或新游戏时清空记录。

池复用释放后的节点，减少物理回调中的系统分配。测试在预热 512 条记录后重复 100 轮相同工作集，上游分配次数没有增长。记录数没有人为固定上限；内存池会保留已分配的峰值空间至插件退出，实际占用取决于同时待处理的控制器数量及生命周期清理。

首次进入新容量区间仍可能分配内存。分配失败时 `Remember` 返回 false，约束保留原函数给出的速度；不会先清零速度再丢失校正记录。失败注入测试覆盖池已有空闲节点后才需要上游分配的情况。

map 的键是非拥有的控制器地址，只用于匹配和擦除，缓存不解引用键，也不保留游戏对象引用。Update 入口先对引擎传入的有效接收者建立局部 `NiPointer`，执行原 Update、取出记录和速度校正后释放。它不会从缓存中的旧地址重建智能指针。插件锁只覆盖查表、复制和删除，速度 getter / setter 以及引用计数释放均在锁外执行。

多接触法线继续采用原有第一条优先策略。这里没有改成累积或顺序投影；后者会改变碰撞响应，需要场景验证和明确的接触求解策略。

## 线程、图对象与指针

配置字段改为独立 atomic，AMR 接口指针通过 release / acquire 发布，消除 PostLoad 写入与 Hook 读取这些值时的 C++ 数据竞争。配置只在 PostLoad 加载；独立 atomic 不提供多字段配置的事务快照。

AMR 动态查询保留 manager 和 graph 的智能指针。读取 `graphs[0]` 时使用版本化 `updateLock`，退出该锁后调用 AMR API；graph 保留嵌入的 character 对象生命周期。1.5.97 的锁位于 manager +0x98，1.6.1170 位于 +0xA0。二进制证据支持引擎图容器遍历与更新回调受该锁保护，详见 [ABI 说明](ABI.md)。

已核对 AMF 作者的 AMR 分支：提交 [`b5be808a212f2b124f19d5365a18d1158d24b77b`](https://github.com/max-su-2019/AnimationMotionRevolution/commit/b5be808a212f2b124f19d5365a18d1158d24b77b) 提供 V1 导出，接口 ABI 与本地消费者一致。查询在自旋锁内检查角色是否存在于活动动画运动记录中，不解引用传入的 character 指针；同一锁保护相关激活、重置路径。它表示存在已记录的运动数据，不表示当前正在应用位移或 warping。

记录随动画激活、重置和替换变化，API 没有提供失效代数或通知，因此没有跨回调、跨帧缓存 AMR 布尔结果，也没有缓存角色状态、碰撞对象或图裸指针。本地 AMR 源码没有此实现；实际加载的 DLL 若未导出该接口，AMF 会检测到接口不可用并返回 false。提供者来源、锁与生命周期证据见 [AMR API 上游核查](AMR-API-UPSTREAM.md)。

上游查询的容器访问有锁，但上游写入路径在持有该锁时还会调用图变量 setter 和原引擎函数。AMF 在查询前释放自己取得的 manager 锁，未增加这两个锁的嵌套；这不能证明调用者没有其他外层锁，也不能证明整个 AMR 与引擎之间不存在锁顺序问题。

局部 `TESObjectREFRPtr` 保留解析出的角色引用；空指针和约束边界在附加访问前检查。`Is3DLoaded()` 只是 3D 状态检查，不能代替对象所有权或同步。局部智能指针仍依赖引擎在回调入口提供有效对象；原始 Havok 事件、约束和碰撞体依赖其回调生命周期。DeleteThis Hook 及游戏切换清理保留，但静态审核未证明所有派生控制器销毁路径均通过该 Hook，也未证明所有 Havok 工作线程都使用上述图锁。

接触事件的二进制复核位置为 1.5.97 RVA `0x39C690` 和 1.6.1170 RVA `0x3F4B90`：原回调直接解引用 event +0x30 的 properties，并在正常路径解引用 +0x28 的 contactPoint。原回调存在 disabled 分支，但没有证据要求改变 AMF 对 disabled 事件的跳过规则，因此保持原行为。没有建立空 contactMgr 的传递安全性，该空值也继续跳过。

本次未引入递归、大型栈缓冲区或手写汇编。现有 Xbyak 俯仰跳板保持不变，其栈 ABI 证据及限制见 ABI 说明。

## SIMD 与调度

热路径以分支、对象访问、图变量和外部 API 查询为主；现有 `hkVector4` 速度计算继续使用 CommonLib 的向量操作。标量俯仰逆变换只有少量运算，未增加 AVX2 / FMA 编译要求或运行时分发。没有增加后台任务或延迟队列；碰撞速度修改仍在原物理回调阶段完成。

## 验证

独立 C++23 核心测试覆盖：100,000 组随机俯仰逆变换、正负奇点及阈值附近、非有限输入和溢出、原值保留、第一条平面优先、取出一次、删除后地址复用、游戏清理、内存池复用、分配失败后恢复、8 线程各 20,000 轮独立键操作，以及共享键和并发 Clear。

Release 与 AddressSanitizer 配置各通过 1/1 CTest。ASan 只检测独立核心测试，未注入 Skyrim，也不检测数据竞争。并发测试验证插件容器操作，不证明游戏对象的线程访问协议。

在 x64 Native Tools 命令行复现：

```powershell
cmake -S tests -B build/core-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-release
ctest --test-dir build/core-release --output-on-failure
cmake -S tests -B build/core-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_ASAN=ON
cmake --build build/core-asan
ctest --test-dir build/core-asan --output-on-failure
build/core-release/amf_core_tests.exe --benchmark
python tools/verify_hooks.py --output docs/abi-hooks.json
```

2026-11-08 本机 Release 单线程容器微基准，每种批量约 500,000 组写入 / 取出，先预热；比较原 shared_mutex + unordered_map 与新 mutex + pooled map：

| 同时待处理条目 | 原实现 ns / 组 | 新实现 ns / 组 | 原耗时 / 新耗时 |
| --- | ---: | ---: | ---: |
| 1 | 47.21 | 37.15 | 1.27 |
| 16 | 45.82 | 32.83 | 1.40 |
| 64 | 49.10 | 31.71 | 1.55 |
| 512 | 49.18 | 32.44 | 1.52 |

该基准不含引擎调用、Havok 线程争用或完整 Hook 路径，不能换算为 FPS。游戏内还需验证两个运行时的 AMR 动画切换、双向攻击推挤、Proxy / Rigid 组合、存档切换和多角色密集碰撞；本次未运行 Skyrim。

最终 Release 插件编译及安装通过。双版本 ABI 复核通过 14 个调用点和 6 个虚表项；安装 DLL 与构建输出逐字节一致，INI 内容保持不变。发布 DLL 为 652,800 字节，SHA-256 为 `f4c9a38f1c4df6ce0bd2c50ff900ecc486c2b6a2bc0fa7e79b19e59a5c73416c`；导出和运行时元数据记录于 `build/ng-release/plugin-metadata.json`。
