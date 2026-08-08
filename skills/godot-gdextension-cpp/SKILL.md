---
name: godot-gdextension-cpp
description: 该项目的 Godot GDExtension C++ 渲染器开发指南。用于修改 C++ 扩展代码、Godot 绑定、构建脚本、场景提取、渲染、film 累积、加速结构或 CPU 路径追踪行为。
---

# Godot GDExtension C++ Renderer Skill

## 中文说明

处理本项目时，优先遵循这些规则。

当修改代码的时候，检查当前skill文件中是否需要做必要的修改。

### 需求登记、任务拆解与测试先行（每次开发必读）

以下流程适用于所有功能开发、缺陷修复、行为变更和重构；只做分析、答疑或纯文档讨论时不创建需求记录。

1. **先检查重复与现状。** 在规划或修改生产代码前，完整阅读 `docs/requirements.md` 与 `docs/completed_requirements.md`，并检查相关现有代码、测试和调用方。
   - 若已完成需求记录了等价功能，先告知用户并引用其 `REQ-XXX`；除非用户明确要求扩展、变更或修复，否则不得重复实现。
   - 若已有待开发或开发中需求高度重叠，更新原记录，不得新建重复编号。
2. **先登记需求，再转换任务。** 对新的开发请求，在 `docs/requirements.md` 中建立唯一、递增且不复用的 `REQ-XXX` 记录，保留原始需求、范围与非目标、验收标准、关联记录、开发任务和测试计划。
   - 开发任务必须是可执行、可验证的最小步骤；不要只写模糊的“实现功能”。
   - 缺少会改变范围或验收标准的信息时，状态设为“待澄清”并先向用户询问；不要猜测。
3. **严格测试先行。** 对每个可测试的行为使用 RED → GREEN → REFACTOR：先写最小测试并实际确认它因缺少目标行为而失败，再写最小实现使它通过，然后运行相关回归测试。
   - 不得先写生产代码再补测试；若项目缺少测试入口，第一项实现任务是建立最小可运行的测试切面，或明确记录阻塞并请求用户决定。
   - 测试计划、RED 失败证据、GREEN 通过证据和完整验证命令必须记录在需求条目中。
4. **完成后归档，未完成则保留。** 只有全部验收标准满足且相关测试、构建或必要集成验证实际成功后，才能将条目从 `docs/requirements.md` 移入 `docs/completed_requirements.md`。
   - 已完成记录必须保留需求编号、实现摘要、关键文件、验收标准、测试先行证据、完整验证结果和已知限制。
   - 代码未验证、部分完成、被阻塞或等待用户验收时，必须留在 `docs/requirements.md` 并更新状态；禁止把它写成已完成。
5. **每次改动同步维护记录。** 工作开始、范围变化、阻塞、验证完成和归档时及时更新对应需求，确保两个文档能作为后续开发的去重依据。

### 硬性约束

- 禁止修改 `godot-cpp/` 下的任何文件，包括生成文件、API dump、binding generator、构建产物和缓存。如果任务看起来必须修改这里，先说明阻塞点并请求用户改变这条规则。
- 禁止为本项目编写、创建或编辑 Python 代码。不要添加 Python 辅助脚本；改动应限制在 C++、GDScript、Godot 项目文件和已有的非 Python 项目配置中。
- 禁止编译项目、运行项目、启动 Godot、运行测试或执行示例，除非用户在当前请求中明确要求。用户明确要求开发、实现、修复或修改功能时，上述需求流程所必需的测试和构建验证视为已获授权；仅做分析、答疑或文档工作时不得执行 SCons、CMake、Godot、测试命令或其他会构建/运行项目的命令。

### 项目结构

- `src/` 存放 GDExtension C++ 源码。
- `src/core/` 存放光线、材质、相机、随机数、数学和颜色等基础渲染类型。
- `src/scene/` 存放 Godot 场景到渲染场景的提取逻辑。
- `src/render/` 存放 `CpuPathTracer`、`Film`、tile/pass 累积等渲染管线代码。
- `src/accel/` 存放加速结构接口和交叉测试抽象。
- `project/` 存放 Godot 工程文件。
- `docs/requirements.md` 是待开发、开发中、待验证和阻塞需求的唯一登记处。
- `docs/completed_requirements.md` 是经实际验证的已完成需求历史索引，开发前必须先查重。
- `skills/godot-gdextension-cpp/SKILL.md` 是本项目 AI 开发的必读工作流。
- `godot-cpp/` 是 Godot C++ 绑定依赖，绝对禁止改动。
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
- 禁止任意形式的重构 `godot-cpp/`。
- 复杂 C++ 渲染、场景提取、导出、异步任务和资源生命周期逻辑应补充中文注释，说明意图、数据流、失败路径和线程/资源边界；简单代码仍避免冗余注释。
