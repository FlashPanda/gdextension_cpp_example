# 已完成需求文档

本文件记录已实现、已验证并已满足验收标准的需求，是防止重复开发的历史索引。每次开始新的开发任务前，AI 必须先检查本文件，确认是否已有相同或高度重叠的功能。

## 记录规则

1. 只有完成全部验收标准，且已实际运行相关测试、构建或必要集成验证的需求，才能记录在这里。
2. 每条记录保留原始 `REQ-XXX` 编号；编号不得复用。
3. 记录必须说明实现范围、关键代码位置、验证命令和实际结果，避免未来仅凭标题误判为已完成。
4. 后续需求若修改、扩展或修复此功能，应在新需求中引用本记录编号，不得覆盖或删除历史记录。
5. 不要将“已设计”“已讨论”“未验证的代码”或“仅部分完成”写入本文件；这些仍属于 [`requirements.md`](requirements.md)。

## 已完成需求

### REQ-001 — 输出曝光、Reinhard 色调映射与 sRGB 导出

- **完成日期：** `2026-08-07`
- **原始需求：**
  - 在线性 HDR radiance 最终导出阶段读取 Godot Environment exposure；无有效 Environment 时使用默认值。
  - 增加 Godot 对齐的 Reinhard tone mapper。
  - 在 `film_to_image()` 最后增加 linear → sRGB 编码。
- **实现摘要：**
  - 新增纯 C++ 后处理模块：非负保护 → exposure → Godot 扩展 Reinhard（white point）→ Godot linear → sRGB。
  - `prepare_render_request()` 在主线程快照纯标量设置：有效 `Camera3D.environment` 优先；否则读取 Camera Viewport 的 `World3D.environment`；均不可用时 exposure/white 均回退为 `1.0`。
  - `RenderRequest` 仅保存 `OutputPostprocessSettings` 值对象；后台渲染不持有或读取 Camera、Viewport、World3D、Environment 等 Godot 场景对象。
  - `film_to_image()` 只对主 PNG 应用后处理；`Film` 继续累积线性 HDR radiance。Tile debug 保留原有线性 clamp 底图，避免诊断图受 exposure/tone mapping/sRGB 改变。
  - 内部 `ToneMapperMode` 为 Linear、Reinhard、Filmic、ACES、AgX 预留接口；当前固定实际使用 Reinhard，未对外暴露未实现模式。
- **关键文件：**
  - `src/render/color_postprocess.h` — 输出设置、纯 RGB 类型、模式预留与纯函数 API。
  - `src/render/color_postprocess.cpp` — Godot 等价 Reinhard、linear → sRGB、参数回退和 RGB 管线。
  - `src/raytrace_exporter.cpp` — 主线程 Environment 快照、主 PNG 接入、Tile debug 旧底图保留。
  - `tests/render/color_postprocess_tests.cpp` — 独立 CTest 数学与 Environment 选择回归测试。
  - `tests/godot/req001_output_postprocess_smoke.gd` — Godot 场景集成、Camera override、World/default fallback 和 tile-debug 回归烟雾测试。
  - `CMakeLists.txt` — `raytrace_postprocess_tests` 原生 CTest 目标（MSVC `/utf-8`）。
- **验收标准：**
  - [x] 有效 Camera Environment 优先、有效 World Environment 次之；两者不可用时 exposure/white 均为 `1.0`。
  - [x] 每像素顺序为：平均线性 radiance → 非负保护 → exposure → Reinhard → linear → sRGB → `RGBA8`。
  - [x] `Film` 累积数据未被显示后处理改写。
  - [x] 扩展 Reinhard、sRGB 阈值、HDR 保留、默认回退、Camera/World Environment 优先级均有回归测试。
  - [x] Tile debug 底图保持线性 clamp，不随输出 exposure 变化。
  - [x] 原生测试、两种目标 DLL 构建、Godot 集成烟雾测试与 `git diff --check` 均实际成功。
- **测试先行证据：**
  - RED：`cmake --build build/req001-tests --target raytrace_postprocess_tests --config Debug` — 依次实际因缺失 `color_postprocess.h`、`apply_exposure`、`tonemap_reinhard`、`linear_to_srgb`、`RgbColor/postprocess_linear_radiance` 而失败。
  - GREEN：`cmake --build build/req001-tests --target raytrace_postprocess_tests --config Debug` 与 `ctest --test-dir build/req001-tests -C Debug --output-on-failure` — `1/1` 测试通过。
  - Tile debug RED：Godot headless smoke 在修复前实际失败：`Tile debug base changed with output exposure: 0.207941 -> 0.210118`。
  - Tile debug GREEN：修复为独立线性 clamp 底图后，同一 Godot smoke 通过。
- **完整验证：**
  - `cmake -S . -B build/req001-tests -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON` — 配置成功。
  - `ctest --test-dir build/req001-tests -C Debug --output-on-failure` — `100% tests passed, 0 tests failed out of 1`。
  - `scons platform=windows target=template_debug -j4` — template_debug DLL 构建成功。
  - `scons platform=windows target=template_debug dev_build=yes -j4` — Godot 实际加载的 template_debug.dev DLL 构建成功。
  - `D:/Godot_Panda/bin/godot.windows.editor.dev.x86_64.console.exe --headless --path D:/gdextension_cpp_example/project --script D:/gdextension_cpp_example/tests/godot/req001_output_postprocess_smoke.gd` — 退出码 `0`；输出：`REQ001_SMOKE PASS world_exposure_1=0.103431 world_exposure_2=0.139338 camera_override_0=0.000000 no_environment=0.107904 default_environment=0.107904`。
  - `git diff --check` — 通过。
- **关联记录：** `无`。
- **已知限制或后续工作：**
  - 本需求不实现自动曝光、Glow、BCS、LUT、Debanding、Filmic、ACES、AgX 或 HDR 输出。
  - 当前异步 worker 不读取 Godot 场景对象，但沿用既有架构创建/保存 `Image` 和日志；若未来需要全面收紧 Godot Object 的跨线程使用范围，应另立需求重构整条异步文件导出路径。

### REQ-002 — 使用本机逻辑线程数减一并行计算光线追踪

- **完成日期：** `2026-08-14`
- **原始需求：**
  - 使用多线程进行光线追踪的计算过程。
  - 线程数量读取本机的线程数，并按本机最大线程数量减一设置。
- **实现摘要：**
  - 新增纯 C++17 有界任务调度器：使用 `std::thread::hardware_concurrency()` 读取逻辑线程数，目标计算线程数为 `max(1, hardware_concurrency - 1)`；当前同步调用线程或异步 job worker 计入总数，实际计算线程数再由 tile 数封顶。
  - 完整图像渲染把二维 tile 网格映射为唯一线性任务索引，辅助线程与调用线程通过原子索引领取互不重叠的 tile；单像素和单 tile 诊断模式保持串行。
  - 辅助线程使用无异常 `JoinableThread` 封装：Windows 通过 `_beginthreadex`、其他平台通过 `pthread_create` 显式报告启动失败；部分创建失败时已启动线程与调用线程继续完成剩余任务，所有句柄在返回前 join。
  - Film 与逐像素命中/未命中计数依靠 tile 像素独占写入；integrator 改为把 `RenderStatistics` 显式写入 tile 本地对象，tile 完成后才持锁归并一次，避免在 per-sample/per-bounce 热路径使用全局锁。
  - 取消检查发生在任务索引领取前和领取后/回调前；进度继续使用原子计数。调度器返回后全部辅助线程已回收，随后才读取统计、转换 Film、保存图像或发布结果。
- **关键文件：**
  - `src/render/parallel_task_scheduler.h/.cpp` — 线程数策略、无异常平台线程句柄、有界任务领取、取消和 join。
  - `src/render/render_statistics.h/.cpp` — 纯数据渲染统计与归并函数。
  - `src/raytrace_exporter.cpp` — 完整图像 tile 并行调度接入；单像素/单 tile 路径保持原语义。
  - `src/render/cpu_path_tracer.h/.cpp` — tile 本地统计和一次同步归并，Film/像素计数维持独占写。
  - `src/render/integrators.h/.cpp` — 每次 trace 显式接收本地统计，不再持有共享统计指针。
  - `tests/render/parallel_task_scheduler_tests.cpp` — 线程数、真实三线程并发、唯一领取、任务封顶、取消、创建失败回收、确定性输出和统计归并测试。
  - `tests/godot/req002_multithreaded_render_smoke.gd` — 真实场景串/并行输出、重复确定性、统计、诊断模式、空场景、尾部 tile、非法尺寸和异步取消回归。
  - `CMakeLists.txt` — 新增 `raytrace_parallel_task_tests` 及跨平台 `Threads::Threads` 链接。
- **验收标准：**
  - [x] 检测值 `0/1` 回退为 `1`，大于 `1` 时使用检测值减一；实际本机检测到 `20` 个逻辑线程，对应目标 `19`。
  - [x] 完整图像按互不重叠 tile 并行执行；原生测试通过同步门实际观察到调用线程加两个 helper 共三个计算线程。
  - [x] 线程数由目标数与任务数共同封顶，每个任务恰好领取一次；线程创建部分失败测试仍完成全部 `32` 个任务并确认已创建 helper 在返回前退出。
  - [x] 真实 32×32 完整渲染输出与四个串行单-tile 输出逐像素一致，两次完整渲染 PNG 字节一致；两条路径均统计 `1024` 条主射线。`intersection_ms` 为并发求交耗时累计值，不参与确定性数值比较。
  - [x] Film/逐像素计数按 tile 独占写，统计按 tile 本地累计并一次加锁归并；取消、进度和任务索引使用原子状态，Film 导出和结果发布发生在全部线程 join 后。
  - [x] 并行阶段只读取请求快照、渲染值对象及 build 后的加速结构，不访问 Camera3D、Viewport、World3D、Environment 或 Image 对象。
  - [x] 单像素、单 tile、单 tile 全图、非整 tile 边界、空场景、零尺寸校验、异步取消和 `REQ-001` 输出行为均通过回归。
  - [x] 两个原生 CTest、两种 GDExtension DLL 构建、两个 Godot headless 测试与 `git diff --check` 均实际成功。
- **测试先行证据：**
  - 调度 RED：`cmake --build build/req002-tests --target raytrace_parallel_task_tests --config Debug` — MSVC `C1083`，缺少目标 API `render/parallel_task_scheduler.h`。
  - 统计 RED：同一目标在添加归并测试后再次得到 MSVC `C1083`，缺少目标模块 `render/render_statistics.h`。
  - GREEN：`cmake --build build/req002-tests --target raytrace_postprocess_tests raytrace_parallel_task_tests --config Debug` 与 `ctest --test-dir build/req002-tests -C Debug --output-on-failure` — `2/2` 测试通过。
- **完整验证：**
  - `cmake -S . -B build/req002-tests -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON` — 从干净目录配置成功，`Threads` 检测成功。
  - `cmake --build build/req002-tests --target raytrace_postprocess_tests raytrace_parallel_task_tests --config Debug` — 两个测试目标构建成功。
  - `ctest --test-dir build/req002-tests -C Debug --output-on-failure` — `100% tests passed, 0 tests failed out of 2`。
  - `scons platform=windows target=template_debug -j4` — template_debug DLL 构建成功，无异常展开警告。
  - `scons platform=windows target=template_debug dev_build=yes -j4` — Godot 实际加载的 template_debug.dev DLL 构建成功。
  - `D:/Godot_Panda/bin/godot.windows.editor.dev.x86_64.console.exe --headless --path D:/gdextension_cpp_example/project --script D:/gdextension_cpp_example/tests/godot/req002_multithreaded_render_smoke.gd` — 退出码 `0`；输出：`REQ002_SMOKE PASS hardware_threads=20 target_render_threads=19 full_primary_rays=1024 serial_primary_rays=1024 cancelled_tiles=0`。
  - `D:/Godot_Panda/bin/godot.windows.editor.dev.x86_64.console.exe --headless --path D:/gdextension_cpp_example/project --script D:/gdextension_cpp_example/tests/godot/req001_output_postprocess_smoke.gd` — 退出码 `0`；输出：`REQ001_SMOKE PASS world_exposure_1=0.103431 world_exposure_2=0.139338 camera_override_0=0.000000 no_environment=0.107904 default_environment=0.107904`。
  - `git diff --check` — 通过。
- **关联记录：** `REQ-001`（输出后处理与 tile debug 回归保持不变）。
- **已知限制或后续工作：**
  - 单像素和单 tile 诊断模式只有一个工作单元，按设计不创建辅助线程。
  - 多个并发渲染 job 会分别按本机逻辑线程数计算各自目标，调用方若允许同时运行大量 job，仍可能产生跨 job 的总体超额订阅；当前编辑器插件一次只维护一个活动 job。
  - Windows 验证环境未运行 ThreadSanitizer；并发安全依据互斥像素所有权、tile 本地统计、原子调度/取消、RAII join、故障注入测试及真实 Godot 重复回归确认。

### REQ-003 — 建立可执行的中文注释生成方案

- **完成日期：** `2026-08-14`
- **原始需求：**
  - 当前 skill 有没有明确注释的生成方案。
  - 帮我生成一个方案。
- **实现摘要：**
  - 在项目主技能中新增强制注释生成方案，明确必须注释、按需注释和禁止注释的场景。
  - 将注释生成安排在 GREEN 后、最终验证前的 REFACTOR 阶段，形成“检查 diff 与调用方、判定复杂度、选择最窄位置、按统一语言生成、同步清理旧注释、完成检查”的闭环。
  - 提供算法、数据、并发、资源生命周期和 `TODO(REQ-XXX)` 五类紧凑模板，并补充路径追踪、场景提取和异步导出的项目热点。
  - 把注释准确性、Godot 主线程边界、过期/冗余注释和待办可追踪性纳入需求完成门槛。
- **关键文件：**
  - `skills/godot-gdextension-cpp/SKILL.md` — 注释判定、生成流程、模板、项目热点和完成检查。
- **验收标准：**
  - [x] 明确必须注释、按需注释和禁止注释的场景。
  - [x] 将注释生成安排在 TDD 的 REFACTOR 阶段，并给出六步执行流程。
  - [x] 默认使用中文，保留英文标识符/类型/公式，并覆盖意图、不变量、单位/坐标空间、数据流、失败、线程、所有权和生命周期边界。
  - [x] 提供算法、数据、并发、资源生命周期和 `TODO` 紧凑模板。
  - [x] 禁止冗余逐行解释、注释掉的代码、无活动需求编号的 `TODO` 和过期注释。
  - [x] 主技能 YAML frontmatter 有效，共 `129` 行，技能快速校验和 `git diff --check` 通过。
- **测试先行证据：**
  - RED：PowerShell 内容断言退出码为 `1`，确认当前缺少“注释生成方案、注释判定、生成流程、紧凑模板、完成检查”五个目标标题。
  - GREEN：同一内容断言检查 `8` 个关键项，输出 `CONTENT_CHECK=PASS items=8`。
- **完整验证：**
  - `python -X utf8 C:/Users/xuelangyun/.codex/skills/.system/skill-creator/scripts/quick_validate.py D:/gdextension_cpp_example/skills/godot-gdextension-cpp` — 输出 `Skill is valid!`。
  - PowerShell 行数检查 — 输出 `SKILL_LINES=129`，满足少于 500 行的约束。
  - `git diff --check` — 通过；仅报告工作树既有的 LF/CRLF 转换提示。
  - 本需求只修改 Skill 与需求文档，按项目规则未运行 GDExtension 构建或 Godot 测试。
- **关联记录：** `无`。
- **已知限制或后续工作：**
  - 方案对后续代码改动生效，本需求不批量改写现有源码注释；需要系统性清理旧注释时应另立需求。

### REQ-004 — 为 REQ-002 多线程光线追踪补充中文注释

- **完成日期：** `2026-08-14`
- **原始需求：**
  - 根据注释生成方案，生成 REQ-002 相关代码的注释。
  - 注释必须是中文；若方案里没有该硬性要求，则补充到方案。
- **实现摘要：**
  - 将注释方案从“默认使用中文”收紧为“生成的代码注释必须使用中文”，并把该要求加入生成流程和完成检查；仅允许标识符、类型、公式、坐标空间名称和必要代码术语保留英文。
  - 为调度器补充线程数策略、调用线程计数、平台线程入口所有权、原子唯一领取、取消窗口、部分创建失败恢复和返回前 join 的中文说明。
  - 为 `CpuPathTracer`、integrator 和渲染统计补充互不重叠 tile 的 Film/计数独占写、tile 局部统计、低频加锁归并及 `intersection_ms` 累计工作量语义。
  - 为完整图像渲染补充线性任务索引到二维 tile 的一一映射，以及所有 helper 回收后才读取统计、Film 和创建 Godot `Image` 的线程边界。
- **关键文件：**
  - `skills/godot-gdextension-cpp/SKILL.md` — 中文注释强制规则、生成步骤和完成检查。
  - `src/render/parallel_task_scheduler.h/.cpp` — 线程策略、所有权、任务领取、故障恢复和 join 生命周期注释。
  - `src/render/render_statistics.h/.cpp` — 局部累计、耗时语义和外部同步责任注释。
  - `src/render/cpu_path_tracer.h/.cpp` — tile 并行前置条件、像素独占写和统计归并注释。
  - `src/render/integrators.h/.cpp` — 调用线程局部统计对象和主射线计数不变量注释。
  - `src/raytrace_exporter.cpp` — tile 映射、计算线程回收和 Godot Object 交接注释。
- **验收标准：**
  - [x] Skill 明确规定生成和修改的代码注释必须使用中文，仅允许必要的英文代码元素。
  - [x] 调度器已说明线程数策略、调用线程参与、唯一领取、取消窗口、部分创建失败和 join 生命周期。
  - [x] `CpuPathTracer`、`RenderStatistics` 和 integrator 已说明 tile 独占写、局部统计、低频归并及耗时累计语义。
  - [x] `raytrace_exporter` 已说明唯一 tile 映射、并行数据边界和调度返回后的线程回收保证。
  - [x] 本需求新增的代码注释均使用中文，未新增逐行复述、注释掉的代码或 `TODO`。
  - [x] 未修改可执行语句；Skill、内容、原生测试、完整扩展编译和 diff 验证均通过。
- **测试先行证据：**
  - RED：PowerShell 内容断言退出码为 `1`，确认中文强制措辞及七类 REQ-002 关键边界说明共 `8` 个目标均缺失。
  - GREEN：同一内容断言输出 `COMMENT_CONTENT_CHECK=PASS items=8`。
- **完整验证：**
  - `python -X utf8 C:/Users/xuelangyun/.codex/skills/.system/skill-creator/scripts/quick_validate.py D:/gdextension_cpp_example/skills/godot-gdextension-cpp` — 输出 `Skill is valid!`；Skill 共 `131` 行。
  - `cmake --build build/req002-tests --target raytrace_parallel_task_tests --config Debug` — 未运行测试，因原构建目录已不存在而退出；随后改用新目录重新配置。
  - `cmake -S . -B build/req004-comments -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON` — 配置成功，`Threads` 检测成功。
  - `cmake --build build/req004-comments --target raytrace_parallel_task_tests --config Debug` — 构建成功。
  - `ctest --test-dir build/req004-comments -C Debug --output-on-failure -R raytrace_parallel_task_tests` — `1/1` 测试通过。
  - `scons platform=windows target=template_debug -j4` — 所有受影响源文件编译成功，GDExtension DLL 链接成功。
  - `git diff --check` — 通过；仅报告工作树既有的 LF/CRLF 转换提示。
- **关联记录：** `REQ-002`、`REQ-003`。
- **已知限制或后续工作：**
  - 本需求聚焦 REQ-002 生产代码的复杂边界；名称和断言已能表达意图的测试代码未增加冗余注释。



### REQ-005 — 修复多线程日志崩溃并将异步导出收尾移至主线程

- **完成日期：** `2026-08-14`
- **原始需求：**
  - 单线程的时候允许调用日志，多线程状态下禁止调用日志。
  - 收紧异步导出路径，确保 `Image`、`FileAccess` 等 Godot Object 只在主线程操作。
- **根因与实现摘要：**
  - Windows 转储确认崩溃调用链为 `RandomWalkIntegrator::trace -> Logger::info -> UtilityFunctions::print -> EditorNode::_print_handler -> _purecall`；多个 helper 同时停在 Godot 日志全局锁，排除 Film/tile 越界。
  - 新增日志投递策略和可注入 trace log sink：0 个或两个及以上计算线程禁用日志且不构造 per-ray 文本；同步单线程直接输出；异步单线程只收集文本并在原提交线程输出。
  - 将异步任务拆成 `compute_done` 与最终 `done` 两阶段。worker 只执行 tracer reset、加速结构 build、渲染与纯数据统计；原提交线程在 `poll/cancel` 中 join 后创建 `Image`、保存 PNG/遮罩/tile debug、通过 `FileAccess` 写 timing log，全部结束后才发布 `done=true`。
  - 请求快照记录提交线程 ID，错误线程只能读取运行状态，不能进入 Godot Object 收尾；计算完成后再取消仍不会发布图像产物。
  - 保留 REQ-002 的“本机逻辑线程数减一”策略、tile 划分、确定性、统计、公开 Dictionary 和插件轮询协议。
- **关键文件：**
  - `src/render/render_execution_policy.h/.cpp` — 直接、延迟、禁用三态日志策略。
  - `src/render/integrators.h/.cpp` — 可注入日志 sink；禁用时跳过消息构造。
  - `src/render/cpu_path_tracer.h/.cpp` — 日志 sink 在 integrator 重建时保持生效。
  - `src/raytrace_exporter.cpp` — 提交线程快照、异步纯计算结果、主线程收尾和双阶段任务状态。
  - `tests/render/parallel_task_scheduler_tests.cpp` — 0/1/多线程及提交线程日志策略原生测试。
  - `tests/godot/req005_async_main_thread_finalize_smoke.gd` — 首次 poll 前零产物、poll 收尾、计算后取消零图像产物测试。
- **验收标准：**
  - [x] 日志策略对 0 个计算线程禁用；同步单线程直接输出；异步单线程延迟输出；两个及以上计算线程完全禁用。
  - [x] 多线程积分路径没有 `Logger`/`UtilityFunctions` 调用，且 sink 为空时不构造 per-ray 日志文本。
  - [x] 异步 worker 不创建或访问 `Image`、`FileAccess`、`DirAccess`，不保存 PNG，不写 timing log。
  - [x] 原提交线程 join worker 后完成全部 Godot Object 与文件收尾，随后才设置最终 `done=true`。
  - [x] 计算完成与收尾之间取消时不发布主 PNG、命中遮罩或 tile debug；同步入口行为保持兼容。
  - [x] Full、单 tile、单 pixel、异步成功/取消、REQ-001 后处理和 REQ-002 确定性/统计回归通过。
  - [x] 新增复杂边界注释均为中文；两种调试 DLL、原生测试、Godot 回归和 diff 检查通过。
- **测试先行证据：**
  - 原生 RED：`cmake --build build/req005-tests --target raytrace_parallel_task_tests --config Debug` — MSVC `C1083`，缺少目标 API `render/render_execution_policy.h`。
  - Godot RED：`req005_async_main_thread_finalize_smoke.gd` — 退出码 `1`，首次 poll 前已出现 `full.png`，同时可见 64 条 worker 日志。
  - GREEN：同一原生策略测试与异步 smoke 均通过；异步 smoke 输出 `REQ005_SMOKE PASS artifacts_before_poll=0 finalized_on_poll=1 cancelled_artifacts=0`。
- **完整验证：**
  - `cmake --build build/req005-tests --config Debug` — 两个原生测试目标构建成功。
  - `ctest --test-dir build/req005-tests -C Debug --output-on-failure` — `2/2` 测试通过。
  - `scons platform=windows target=template_debug -j4` — template_debug DLL 编译、链接成功。
  - `scons platform=windows target=template_debug dev_build=yes -j4` — 编辑器实际加载的 template_debug.dev DLL 编译、链接成功。
  - `req005_async_main_thread_finalize_smoke.gd`（独立工作区 `--log-file`）— 退出码 `0`，成功收尾且计算后取消不产生图像文件。
  - 同一 REQ-005 smoke 使用 `--headless --editor` — 退出码 `0`，输出 PASS，无 `_purecall`、FAST_FAIL 或崩溃；项目资源自身的编辑器退出警告不影响目标结果。
  - `req002_multithreaded_render_smoke.gd` — 退出码 `0`，`full_primary_rays=1024`、`serial_primary_rays=1024`、`cancelled_tiles=0`。
  - `req001_output_postprocess_smoke.gd` — 退出码 `0`，World/Camera/default Environment 后处理回归通过。
  - `git diff --check` — 通过，仅有工作树既有的 LF/CRLF 提示。
- **关联记录：** `REQ-001`、`REQ-002`、`REQ-003`、`REQ-004`。
- **已知限制或后续工作：**
  - 异步最终收尾要求由创建任务的同一提交线程调用 `poll_render_job` 或 `cancel_render_job`；其他线程可以查询进度，但不会代替提交线程操作 Godot Object。

---

## 完成记录模板

### REQ-XXX — 简短标题

- **完成日期：** `YYYY-MM-DD`
- **原始需求：**
  -
- **实现摘要：**
  -
- **关键文件：**
  - `path/to/file:line` —
- **验收标准：**
  - [x]
- **测试先行证据：**
  - RED：`<命令>` — <预期失败原因>
  - GREEN：`<命令>` — <通过结果>
- **完整验证：**
  - `<命令>` — <实际结果>
- **关联记录：** `无`，或前序 / 后续 `REQ-XXX`。
- **已知限制或后续工作：**
  - 无
