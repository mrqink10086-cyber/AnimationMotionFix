# AMR `HasAnimMotionData` API：来源与契约

## 核验范围

与 AMF 消费端 API 对应的生产者源码，位于 `max-su-2019/AnimationMotionRevolution` 的 fork 提交 [`b5be808a212f2b124f19d5365a18d1158d24b77b`](https://github.com/max-su-2019/AnimationMotionRevolution/commit/b5be808a212f2b124f19d5365a18d1158d24b77b)，提交时间为 2026-11-05 09:02:08 UTC，提交说明为 “Added HasAnimMotionData API function”。该仓库是 `D7ry/AnimationMotionRevolution` 的 fork。AMF 消费端随后在 [`a26eedad5230b76d4b88949b1e9edd07b40c3f53`](https://github.com/max-su-2019/AnimationMotionFix/commit/a26eedad5230b76d4b88949b1e9edd07b40c3f53) 引入 API 头文件和调用，时间为 2026-11-05 09:05:37 UTC。

原始作者仓库 `alexsylex/AnimationMotionRevolution` 在可核验提交 [`1e296182e8bcd6e33a46b39f9018831d5f60ec33`](https://github.com/alexsylex/AnimationMotionRevolution/commit/1e296182e8bcd6e33a46b39f9018831d5f60ec33)（2022-10-18）中的 CMake 头文件/源文件清单没有 API 头文件或 exporter 源文件；该提交的 `main.cpp` 入口初始化并安装 hooks。此结论限定于已检查的原始仓库提交，不代表所有历史分支或已发布 DLL。证据：[CMake 文件清单](https://github.com/alexsylex/AnimationMotionRevolution/blob/1e296182e8bcd6e33a46b39f9018831d5f60ec33/CMakeLists.txt#L20-L67)、[插件入口](https://github.com/alexsylex/AnimationMotionRevolution/blob/1e296182e8bcd6e33a46b39f9018831d5f60ec33/source/main.cpp#L5-L30)。

## V1 ABI

生产者头文件声明 `InterfaceVersion : std::uint8_t`，其中 `V1` 数值为 0；`IVAMR1` 没有虚析构函数，只有一个纯虚方法：

```cpp
virtual bool HasAnimMotionData(
    const RE::hkbCharacter* a_hkbCharacter) const noexcept = 0;
```

因此该接口声明的对象虚表只有这一个方法槽。`_RequestPluginAPI` 类型是 `void* (*)(const InterfaceVersion)`。生产者导出 `extern "C" DLLEXPORT void* RequestPluginAPI(InterfaceVersion) noexcept`，仅对 V1 返回文件静态单例地址，其他版本返回空指针。消费端头文件中的 helper 通过 `GetModuleHandleA("AnimationMotionRevolution.dll")` 和 `GetProcAddress(..., "RequestPluginAPI")` 查找导出。[V1 头文件](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/include/AnimationMotionRevolutionAPI.h#L17-L45)；[导出实现和单例](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/AnimationMotionRevolutionAPI.cpp#L5-L24)。

本地消费者 [AnimationMotionRevolutionAPI.h](../include/AnimationMotionRevolutionAPI.h#L17) 与上述签名一致；[Settings.cpp](../src/Settings.cpp#L24-L49) 在设置阶段解析接口，并在调用时先将 `graphs[0]` 复制到智能指针，在 manager `updateLock` 作用域结束后再调用 AMR API。这只证明 AMF 自己没有把该 manager 锁跨进 API 调用；不能据此证明调用线程没有持有其他引擎锁。

## 查询语义、同步和失效

生产者用 `std::map<const RE::hkbCharacter*, std::map<std::string, AnimMotionData>>` 按 `hkbCharacter*` 地址和 clip 名称保存条目。`HasAnimMotionData` 对空指针返回 false；非空时获取同一个 `BSSpinLock`，只检查 character 地址是否为 map key，然后返回是否存在。它不遍历 clip 内容，也不解引用传入的 character 指针。因此结果表示“该地址当前有被跟踪的条目”，不是对某个 clip 的查询。[map、锁和查询](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L40-L47)、[查询实现](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L109-L123)。

激活 hook 只在解析出 translation 或 rotation 数据时插入条目；同一 animation 的重复激活增加 `activeCount`。reset hook 减少计数，并在计数小于等于零时删除 clip 条目；character 下最后一个 clip 删除时同时删掉该 character key。若检测到同 key 绑定了不同 animation，激活路径会先移除旧条目。实现还会在 map 插入异常时清空 map。[激活解析与插入](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L145-L166)、[有 motion 数据时 Add](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L280-L301)、[计数与移除](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L304-L323)、[异常清理](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L48-L63)。

map key 是非拥有的 raw `hkbCharacter*`；`AnimMotionData` 也保存 raw `const hkaAnimation*`，而没有在这些字段上保留对象引用。[map 字段](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L119-L123)、[AnimMotionData 字段](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/include/AnimMotionHandler.h#L121-L130)。已检查源码没有提供给 API 使用者的 generation、显式失效通知或 character 生命周期契约；不能假定地址 key 被长期保活。也不能跨帧缓存此 bool：激活/reset 会改变返回值，而 API 没有数据版本号或失效回调。

查询本身只在查表期间持有 map 锁。但 `ComputeStartTimeHook` 持有同一把锁调用原始 `g_computeStartTime`，在替换条目时也会在锁内调用 `SetWarpingGraphVariable`；`ResetIgnoreStartTimeHook` 在锁内调用 setter 和原始 `g_resetIgnoreStartTime`。这证明 AMR 自己的 hook 存在 map-lock 到引擎调用的嵌套；源码不足以证明整体运行时锁顺序安全或一定死锁。[激活路径锁范围](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L145-L166)、[激活路径结尾](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L298-L301)、[reset 路径锁范围](https://github.com/max-su-2019/AnimationMotionRevolution/blob/b5be808a212f2b124f19d5365a18d1158d24b77b/source/Hooks.cpp#L304-L323)。

## 适用范围

`E:\source\repos\AnimationMotionRevolution-main` 是另一个 fork；其本地源码搜索没有发现 `IVAMR1`、`RequestPluginAPI` 或 `HasAnimMotionData`。不能用该 fork 的实现替代上面 max-su fork 的 API 生产者证据。上述结论均为源码核验；没有检查包含该提交的 AMR DLL，因此不能确认任一已安装/发布二进制实际导出 `RequestPluginAPI`。如果运行时 DLL 没有按该提交构建，消费端 `GetProcAddress` 会拿不到接口。
