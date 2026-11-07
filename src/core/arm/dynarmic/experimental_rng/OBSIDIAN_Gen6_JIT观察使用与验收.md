---
tags: [Azahar, Gen6, RNG, ARM, Dynarmic, JIT, Obsidian]
date: 2026-11-07
status: jit-source-integrated-not-runtime-verified
---
# Gen6 X 1.5 — Dynarmic JIT 原生 MT 取数观察（研究版）

## 为什么不再用 GDB 高频轮询
用户实际测得 CT 每次刷新跨越大约 14 个 RNG advances，尽管已使用批量 Socket、8ms 定时器和单请求 Index 读取。轮询每次读取仍跨越模拟器的 CPU 时片，无法证明每次原生取数都被观测。

该补丁使用锁定于 fork commit `278405bd71999ed3f3c77c5f78344a06fef798b9` 的 Dynarmic `A32::UserCallbacks::PreCodeTranslationHook` + IR `CallHostFunction`，在 JIT 的指定 guest PC 编译原生观测回调。**不是高频 GDB 断点，也不修改任何 guest ARM 指令**。

## 范围与 Gate
- **仅 X 1.5 主 MT**，所有其它游戏/引擎暂不支持；环境变量不设置时完全关闭。
- 地址：函数 body `0x00119F88`、返回 `BX LR @0x0011A010`，主对象候选 `0x08C52848`。
- 运行时读取 `0x0010869C...` 版本标记 16B、入口 NOP/Push、返回 BX LR、三项 temper/twist 常量，十个 32-bit 字须全匹配。**这不是对完整 .bin 的 SHA 校验。**
- 只有 entry 的 R0 等于主 MT 对象地址、旧 Index 在 0..623、LR 合法，且 return 的 LR 一致、Index 恰好 +1（含 623→0），才记下 `kind=draw` 的 R0 结果。
- 记录器本身是独立的 8192 项 SPSC 环。JIT HostCall 无锁、无堆分配、无 I/O。只有执行片段退出 JIT 后的 `ARM_Dynarmic::Run()` 才写本地 trace 文件。
- 插桩工作仅收集观测数据，**不提供写 SEED、修改出蛋、改变存档或远程网络接口**。

## Windows PowerShell 启动（需要自己编译此研究分支）
```powershell
$env:AZAHAR_RNG_TRACE_PROFILE = "X15"
$env:AZAHAR_RNG_TRACE_FILE = "C:\\RNG\\x15_mt"
& "C:\\Path\\To\\Azahar\\azahar.exe"
```
输出文件名为 `C:\RNG\x15_mt.core0.rngtrace` 等（按运行的 CPU 核心分别生成）。建议提前建立目录；只会在代码验证且有输出路径时创建文件。每份 trace 采用 `RNGTRACE` v1 格式，包含 24B 头、36B 事件，`emu_frame=0` 表示帧号尚未采集，不要冒充逐帧时间戳。

解码工具来自配套私有仓库：
```powershell
python native_event_observer/tools/decode_trace.py C:\RNG\x15_mt.core0.rngtrace --csv C:\RNG\x15_mt.csv
```

## 证据等级
| 验收 | 状态 |
|---|---|
| X.bin 原始十字节签名和 156B MT 函数 | 静态验证 |
| 8192 SPSC / 800 模拟事件格式及恢复 | 独立 CI 通过 |
| Azahar fork 的 JIT 插桩源码 | 研究分支已提交 |
| 完整 Azahar CMake/Clang 编译 | 单独 GitHub Actions 工作流验证 |
| 实际游戏内捕获 500 次以上 `kind=draw` | **未验证** |
| Index、R0 输出和 PokeReader 的同步对账 | **未验证** |
| 解决“14 advances 才刷新一次” | **未验证** |

## 真实运行验收
1. 自己的备份存档，X 1.5，首次只读测试，不并行其它注入型作弊。
2. 通过游戏内动作触发至少 500 次 RNG 输出，保存所有 core trace；检查每条 `kind=draw` 的 `sequence`、`rng_index`、`R0` 和 `context`，`dropped=0`。
3. 独立读取主 MT 当前 RAM 状态，用同一帧事件序列的最后 Index 与真实游戏对账。
4. 对照 v0.4.36 CT 的 Index 采样日志，量化每次显示之间遗失了多少输出，而非仅比较十六进制随机数。
5. 只有无丢失且游戏行为不受影响后，才考虑把事件交给 CT UI。七代 SFMT/TinyMT 需要逐引擎独立研究，不移植 Gen6 地址。

**注意：测试用的环境变量必须在 Azahar 启动前设置。已存在的 JIT 缓存和切换游戏时的旧 PageTable 可能导致实验没有事件；失败或零条日志不能解读为游戏没有使用 RNG。**
