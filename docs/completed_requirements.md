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
