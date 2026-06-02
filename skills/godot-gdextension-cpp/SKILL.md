---
name: godot-gdextension-cpp
description: Bilingual project guidance for this Godot GDExtension C++ renderer. Use when modifying C++ extension code, Godot bindings, build scripts, scene extraction, rendering, film accumulation, acceleration structures, or CPU path tracing behavior.
---

# Godot GDExtension C++ Renderer Skill

## 中文说明

处理本项目时，优先遵循这些规则。

当修改代码的时候，检查当前skill文件中是否需要做必要的修改。

### 项目结构

- `src/` 存放 GDExtension C++ 源码。
- `src/core/` 存放光线、材质、相机、随机数、数学和颜色等基础渲染类型。
- `src/scene/` 存放 Godot 场景到渲染场景的提取逻辑。
- `src/render/` 存放 `CpuPathTracer`、`Film`、tile/pass 累积等渲染管线代码。
- `src/accel/` 存放加速结构接口和交叉测试抽象。
- `project/` 存放 Godot 工程文件。
- `godot-cpp/` 是 Godot C++ 绑定依赖，避免随意改动。
- `SConstruct` 是主要构建入口；`CMakeLists.txt` 主要用于 IDE/索引辅助。

### GDExtension 开发

- Godot 暴露的类使用 `GDCLASS`，并在 `_bind_methods()` 中绑定方法、属性和信号。
- 添加新的 Godot 可见类后，检查 `src/register_types.cpp` 是否需要注册。
- 修改 Godot 可见 API 后，检查 `.gdextension`、场景和脚本引用是否匹配。
- 优先把引擎集成和性能敏感逻辑放进 C++；普通场景控制逻辑可保留在 GDScript。

### 渲染和路径追踪

- 保持 `Scene` 简单：它当前只拥有 triangles 和 materials；新增数据时同步考虑提取、加速结构和 integrator 的使用路径。
- 修改 `Triangle`、`Material`、`Camera`、`Hit` 等核心类型时，检查 `SceneExtractor`、`CpuPathTracer` 和 `AccelInterface` 的影响。
- `CpuPathTracer::reset()` 负责规范化 settings、重置 film/tile accumulator，并在存在加速结构时 rebuild。
- `render_next_tile()` 应保持渐进式渲染语义：一次取一个 tile，渲染后可把 tile 返回给调用方。
- `render_tile()` 中的随机采样应保持可复现：seed 由 pixel、pass、sample 和 base seed 混合得到。
- `trace_path()` 中要避免自相交，继续使用或明确替换 ray epsilon 策略。
- 添加 BRDF、直接光照、Russian roulette、MIS、镜面或折射材质时，明确更新 throughput、PDF、法线朝向和终止条件。
- `Film` 存储 radiance sum 和 sample count；新增输出、tone mapping 或 denoise 逻辑时，不要破坏原始累积数据。
- 加速结构实现必须满足 `AccelInterface::build()`、`intersect()` 和 `intersect_p()` 语义；`intersect()` 应填充有效的 `Hit`、`t`、normal、materialId 和 triangleIndex。

### 场景提取

- `SceneExtractor` 只接受可转成三角形的 mesh surface；新增 primitive 支持时要定义跳过或转换规则。
- 提取 mesh 时保持 world-space position、normal inverse-transpose、UV 和 materialId 的一致性。
- 材质从 `BaseMaterial3D` 提取 albedo、roughness、emission；新增材质属性时同步更新 `Material` 和路径追踪逻辑。
- 相机优先使用 current perspective camera；修改相机提取时保持 FOV 轴向和 Godot keep-aspect 行为一致。

### 构建与验证

- 优先使用 SCons 构建扩展，输出应进入 `project/bin/`。
- 只改渲染、数学或场景提取逻辑时，优先做最小范围编译检查。
- 改动路径追踪行为时，至少检查：空场景、无加速结构、零尺寸/非法 settings、发光材质、无效 materialId、缺失 normal/UV、tile 边界。
- 性能相关改动要避免在 per-sample/per-bounce 热路径中引入不必要分配。

### 风格

- 使用 C++17，保持现有命名、目录和 include 风格。
- 不随意重构 `godot-cpp/`。
- 只在必要处添加简短注释，优先让类型和函数边界说明意图。

## English Notes

Follow these rules when working on this project.

When modifying code, check whether the current skill file needs any necessary updates.

### Project Layout

- `src/` contains the GDExtension C++ source code.
- `src/core/` contains core rendering types for rays, materials, cameras, RNG, math, and color.
- `src/scene/` contains Godot-to-render-scene extraction logic.
- `src/render/` contains `CpuPathTracer`, `Film`, and tile/pass accumulation code.
- `src/accel/` contains acceleration interface and intersection abstractions.
- `project/` contains the Godot project files.
- `godot-cpp/` is the Godot C++ binding dependency; avoid editing it unless explicitly required.
- `SConstruct` is the main build entrypoint; `CMakeLists.txt` is mainly for IDE/indexing support.

### GDExtension Development

- Use `GDCLASS` for Godot-exposed classes and bind methods, properties, and signals in `_bind_methods()`.
- After adding a new Godot-visible class, check whether `src/register_types.cpp` needs registration updates.
- After changing Godot-visible APIs, verify `.gdextension`, scenes, and scripts still reference the correct classes and methods.
- Keep engine integration and performance-sensitive behavior in C++; ordinary scene-control behavior can stay in GDScript.

### Rendering And Path Tracing

- Keep `Scene` simple: it currently owns triangles and materials only; when adding data, update extraction, acceleration, and integrator usage together.
- When changing `Triangle`, `Material`, `Camera`, or `Hit`, check the effects on `SceneExtractor`, `CpuPathTracer`, and `AccelInterface`.
- `CpuPathTracer::reset()` normalizes settings, resets film/tile accumulation, and rebuilds the acceleration structure when one exists.
- Preserve progressive rendering semantics in `render_next_tile()`: take one tile, render it, then optionally return the tile to the caller.
- Keep random sampling in `render_tile()` reproducible: derive seeds from pixel, pass, sample, and base seed.
- Avoid self-intersection in `trace_path()`; keep the ray epsilon strategy or replace it deliberately.
- When adding BRDFs, direct lighting, Russian roulette, MIS, mirror, or dielectric materials, update throughput, PDFs, normal orientation, and termination rules explicitly.
- `Film` stores radiance sums and sample counts; do not destroy raw accumulation data when adding output, tone mapping, or denoising logic.
- Acceleration structures must satisfy `AccelInterface::build()`, `intersect()`, and `intersect_p()` semantics; `intersect()` should fill a valid `Hit`, `t`, normal, materialId, and triangleIndex.

### Scene Extraction

- `SceneExtractor` only accepts mesh surfaces that can become triangles; define skip or conversion rules when adding primitive support.
- Keep world-space positions, inverse-transpose normals, UVs, and materialId consistent during mesh extraction.
- Materials are extracted from `BaseMaterial3D` as albedo, roughness, and emission; when adding material properties, update both `Material` and path tracing logic.
- Prefer the current perspective camera; when changing camera extraction, preserve FOV-axis and Godot keep-aspect behavior.

### Build And Validation

- Prefer SCons for building the extension, with outputs under `project/bin/`.
- For rendering, math, or scene extraction changes, run the smallest useful compile check first.
- For path tracing behavior changes, check at least: empty scenes, missing acceleration structures, zero-size/invalid settings, emissive materials, invalid materialId, missing normal/UV data, and tile boundaries.
- Avoid unnecessary allocation in per-sample or per-bounce hot paths for performance-sensitive changes.

### Style

- Use C++17 and match the existing naming, folder, and include style.
- Do not refactor `godot-cpp/` casually.
- Add short comments only where they clarify non-obvious logic; prefer clear types and function boundaries.
