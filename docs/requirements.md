# 需求文档

本文件是当前项目**待开发、开发中、待验证或受阻塞需求**的唯一登记处。每次提出新的开发需求时，AI 必须先阅读本文件和 [`completed_requirements.md`](completed_requirements.md)，确认该需求没有已经完成或正在进行的等价实现，再开始设计或修改代码。

## 使用规则

1. 每个需求使用唯一编号：`REQ-001`、`REQ-002`，依次递增且永不复用。
2. 先记录用户原始需求，再拆分为可执行、可验证的开发任务；不要直接开始改生产代码。
3. 每项开发任务都要写明验收标准和测试计划。实现必须遵循测试先行：先编写并实际运行失败的测试，再实现最小代码使测试通过。
4. 只有在全部验收标准满足、相关测试和构建验证成功后，才能将需求移入 [`completed_requirements.md`](completed_requirements.md)。移入后从本文件删除该需求，避免双重登记。
5. 若需求已完成、部分重叠或需要修改既有功能，先引用已完成需求编号；除非用户明确要求扩展、变更或修复，否则不要重复实现。
6. 需求未完成、验证失败或被阻塞时必须保留在本文件，并准确更新状态、阻塞原因和下一步。

## 状态定义

| 状态 | 含义 |
| --- | --- |
| 待澄清 | 缺少会影响实现范围或验收的关键信息。 |
| 已规划 | 已拆分开发任务和测试计划，尚未开始实现。 |
| 开发中 | 正在按测试先行流程实现。 |
| 待验证 | 代码完成，等待完整测试、构建或用户验收。 |
| 已阻塞 | 因依赖、环境或外部决策无法继续。 |

## 待开发需求

### REQ-007 — 新增独立 SAH BVH 加速类型

- **状态：** 待验证
- **提出日期：** `2026-10-14`
- **原始需求：**
  - 在 `AccelerationType` 中新增 SAH 类型，按 pbrt-v4 的 SAH 构建算法实现 BVH，并统一继承 `AccelInterface`。
  - 使用 SCons 编译扩展；最终编译后不运行测试或 Godot。
- **范围与非目标：**
  - 范围：独立的 `SahBvhAccel`、共享 BVH 存储与遍历、12 桶 SAH 构建、`sah` 选项和编辑器选择。
  - 非目标：替换现有中位数 BVH、移植 pbrt 的 HLBVH、线性节点布局或并行构建。
- **关联记录：** `REQ-006`。
- **验收标准：**
  - [x] `Sah` 枚举、工厂、大小写不敏感的 `sah` 参数和编辑器选项完整接入，旧枚举值与默认值保持不变。
  - [x] SAH 使用 pbrt-v4 的最大质心轴、12 桶面积代价、常规叶容量 1 和退化停止规则；中位数 BVH 行为保持不变。
  - [x] 两种 BVH 共用节点与求交遍历，SAH 对暴力求交保持最近命中、`Hit` 字段及 `t_max` 语义。
  - [ ] 原生 RED/GREEN、SCons 扩展编译和必要的 Godot 集成验证完成。
- **开发任务：**
  - [x] 1. 补充原生测试和集成测试用例，确认原生 RED。
  - [x] 2. 提取共享 BVH 核心并实现 SAH 分裂和退化处理。
  - [x] 3. 接入工厂、导出参数和编辑器，更新当前使用文档。
  - [x] 4. 运行原生 GREEN 与回归，最终用 SCons 编译扩展。
  - [x] 5. 记录验证结果；未运行集成测试前保留为“待验证”，不归档。
- **测试计划与命令：**
  - 原生测试：`cmake --build build/req007-tests --target raytrace_accel_tests --config Debug`；`ctest --test-dir build/req007-tests -C Debug --output-on-failure -R raytrace_accel_tests`。
  - 最终扩展编译：`scons platform=windows target=template_debug dev_build=yes -j4`；之后不运行测试或 Godot。
- **验证结果：**
  - 实现前（RED）：`cmake --build build/req007-tests --target raytrace_accel_tests --config Debug` 退出码 `1`，MSVC `C1083` 缺少 `accel/bvh_tree.h`，确认共享 BVH 核心尚不存在。
  - 实现后（GREEN）：`cmake --build build/req007-tests --target raytrace_accel_tests --config Debug` 成功；`ctest --test-dir build/req007-tests -C Debug --output-on-failure -R raytrace_accel_tests` 为 `1/1` 通过。
  - 构建 / 集成验证：`scons platform=windows target=template_debug dev_build=yes -j4` 退出码 `0`，生成 `project/bin/libgdexample.windows.template_debug.dev.x86_64.dll`（4,122,112 字节）；`git diff --check` 通过。Godot 集成测试按本次要求未运行。
- **阻塞项或决策记录：**
  - SAH 使用 pbrt-v4 的递归分裂规则，沿用项目的节点与遍历；常规叶容量为 1，退化时允许多三角形叶。
  - 最终编译后不运行测试，因此若 Godot 集成未实际验证，本需求保留“待验证”。

## 需求模板

### REQ-XXX — 简短标题

- **状态：** 待澄清 / 已规划 / 开发中 / 待验证 / 已阻塞
- **提出日期：** `YYYY-MM-DD`
- **原始需求：**
  -
- **范围与非目标：**
  - 范围：
  - 非目标：
- **关联记录：** `无`，或已完成需求编号 / 相关 issue / 相关代码位置。
- **验收标准：**
  - [ ]
- **开发任务：**
  - [ ] 1. 定位现有实现、调用方和测试入口。
  - [ ] 2. 编写最小失败测试，并实际确认失败原因符合需求。
  - [ ] 3. 实现最小代码使该测试通过。
  - [ ] 4. 补充边界或回归测试，并运行相关测试与构建。
  - [ ] 5. 更新文档并完成移交记录。
- **测试计划与命令：**
  -
- **验证结果：**
  - 实现前（RED）：
  - 实现后（GREEN）：
  - 构建 / 集成验证：
- **阻塞项或决策记录：**
  -
