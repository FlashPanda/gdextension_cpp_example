# REQ-008：HLBVH 验证记录

验证日期：2026-10-17。参考算法来自本地 pbrt-v4，具体接入与线程边界见
[加速结构架构说明](req006_acceleration_structures.md)。

## 工具链与隔离约束

- MSVC 19.51 / Visual Studio 2026、随 VS 安装的 CMake 4.3 与 Ninja。
- 原生测试使用 C++17，不链接 godot-cpp 库。
- 初次构建使用 Codex 附带 Python 3.12 和安装在 `build/req008-tools` 的 SCons 4.11.1。
- 用户随后要求安装日常工具，已通过 winget 安装 Python 3.13.15，再用其 pip 安装 SCons 4.11.1；新用户 PATH 已包含 Python、Scripts、Launcher。
- 用户明确允许在 `build/` 生成隔离的绑定产物。使用现有生成入口，设置 `PYTHONDONTWRITEBYTECODE=1`，不编写或修改 Python 代码。
- 原依赖目录未产生源码、头文件、库、对象文件、PDB 或 Python 缓存改动。CMake 配置附带创建的空 `godot-cpp/bin` 已清理。

## 测试先行证据

1. 在实现前加入 HLBVH 参数解析断言，运行原生加速测试：失败于 `parse HLBVH case-insensitively` 与 `HLBVH canonical name`。
2. 加入算法、结构和并发测试后构建：MSVC C1083，缺少 `accel/hlbvh_accel.h`，实际 RED。
3. 同距三角形测试暴露共享包围盒裁剪精度问题：期望原始图元索引 8，实际为 0；新增 `nextafter(5, 0)` slab 测试同样失败。
4. 实现 HLBVH，并对 slab 区间增加保守舍入误差余量；最终三角形 `t_max` 不变。原生测试 GREEN。
5. 编辑器选项/映射静态断言先失败、后通过；最终真实编辑器脚本验证选项、ID、选择映射与运行期锁定。

## 原生验证命令

以下路径对应实际验证机器；在仓库根目录运行。VS 生成器因本机环境变量及本地化问题未成功，实际采用 Ninja。

```powershell
& 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$cmakeTool = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctestTool = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$ninjaTool = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$env:PYTHONDONTWRITEBYTECODE = '1'
& $cmakeTool -S godot-cpp -B build/req008-bindings -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaTool" '-DPython3_EXECUTABLE=C:/Users/panda/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -DCMAKE_BUILD_TYPE=Debug
& $cmakeTool --build build/req008-bindings --target generate_bindings
& $cmakeTool -S . -B build/req008-native -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninjaTool" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON '-DGODOT_CPP_GENERATED_INCLUDE_DIR=D:/OtherEngines/gdextension_cpp_example/build/req008-bindings/gen/include'
& $cmakeTool --build build/req008-native --target raytrace_accel_tests --clean-first
& $cmakeTool --build build/req008-native
& $ctestTool --test-dir build/req008-native --output-on-failure
```

实际结果：3/3 通过，约 6.8 秒。覆盖 Morton 边界和稳定排序、碰撞叶、低位 LBVH 分裂、上层 SAH、重合中心及零面积回退、
空场景/单图元/rebuild、固定种子射线、完整 Hit、有限 t_max、单/多线程拓扑一致、真实辅助线程执行、部分/全部线程启动失败和并发只读求交。

## SCons 构建

由于原依赖没有预编译产物，使用 SCons repository 模式把所有目标输出放入隔离目录。
预先建立 `build/req008-scons/godot-cpp/tools` 与 `include` 到原依赖同名目录的联接，仅用于读取工具和头文件；不对源目录建立联接。
已通过 verbose dry-run 确认 `/Fo`、库、生成绑定和 DLL 目标都在隔离构建目录。

```powershell
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONPATH = 'D:/OtherEngines/gdextension_cpp_example/build/req008-tools;D:/OtherEngines/gdextension_cpp_example/godot-cpp'
& 'C:/Users/panda/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -B -m SCons -C build/req008-scons -Y D:/OtherEngines/gdextension_cpp_example platform=windows target=template_debug dev_build=yes gdextension_dir=D:/OtherEngines/gdextension_cpp_example/godot-cpp/gdextension -j4
```

实际结果：退出码 0，DLL 为 `build/req008-scons/project/bin/libgdexample.windows.template_debug.dev.x86_64.dll`，4,626,944 字节。
DLL、PDB 和匹配的本机验证用 `.gdextension` 也复制到工作区 `project/bin/`（忽略的本地产物）。
未改动现有 SConstruct；在隔离环境也实际编译了项目完整 C++ 源码。

安装用户工具链后，又用新安装的 SCons 完成构建确认（退出码 0）：

```powershell
$env:PYTHONPATH = 'D:/OtherEngines/gdextension_cpp_example/godot-cpp'
$env:PYTHONDONTWRITEBYTECODE = '1'
& C:/Users/panda/AppData/Local/Programs/Python/Python313/Scripts/scons.exe -C build/req008-scons -Y D:/OtherEngines/gdextension_cpp_example platform=windows target=template_debug dev_build=yes gdextension_dir=D:/OtherEngines/gdextension_cpp_example/godot-cpp/gdextension -j4
```

新 Python 触发依赖重新构建，全部完成后再次复制 DLL/PDB 到 `project/bin/`。重新读取机器/用户 PATH 后，
`python --version` 为 3.13.15、`scons --version` 为 4.11.1，`py` 与 `python -m pip --version` 也可正常使用。

## Godot 验证

使用 Godot `4.4.2.rc.custom_build.9891e5a95`。隔离的最小工程位于 `build/req008-scons/project`，
包含刚构建的 DLL、入口 `example_library_init` 的扩展描述，以及原编辑器插件的同内容副本。
测试脚本直接执行仓库版本，并自行创建真实场景；没有依赖原示例场景的美术资源。

```powershell
$godotTool = 'D:/OtherEngines/PandaGodot/bin/godot.windows.editor.dev.x86_64.console.exe'
$smokeProject = 'D:/OtherEngines/gdextension_cpp_example/build/req008-scons/project'
& $godotTool --headless --editor --path $smokeProject --import --quit --log-file D:/OtherEngines/gdextension_cpp_example/build/req008-import.log
& $godotTool --headless --path $smokeProject --script D:/OtherEngines/gdextension_cpp_example/tests/godot/req006_acceleration_selection_smoke.gd --log-file D:/OtherEngines/gdextension_cpp_example/build/req008-selection.log
& $godotTool --headless --editor --path $smokeProject --script D:/OtherEngines/gdextension_cpp_example/tests/godot/req008_hlbvh_editor_smoke.gd --log-file D:/OtherEngines/gdextension_cpp_example/build/req008-editor.log
& $godotTool --headless --path $smokeProject --script D:/OtherEngines/gdextension_cpp_example/tests/godot/req001_output_postprocess_smoke.gd --log-file D:/OtherEngines/gdextension_cpp_example/build/req001_output_postprocess_smoke.log
& $godotTool --headless --path $smokeProject --script D:/OtherEngines/gdextension_cpp_example/tests/godot/req002_multithreaded_render_smoke.gd --log-file D:/OtherEngines/gdextension_cpp_example/build/req002_multithreaded_render_smoke.log
& $godotTool --headless --path $smokeProject --script D:/OtherEngines/gdextension_cpp_example/tests/godot/req005_async_main_thread_finalize_smoke.gd --log-file D:/OtherEngines/gdextension_cpp_example/build/req005_async_main_thread_finalize_smoke.log
```

全部目标测试退出码 0：

- `REQ006_SMOKE PASS acceleration=brute_force,bvh,octree,sah,hlbvh pixels=1024`；五种 Full 图像字节一致，HLBVH Pixel/Tile、异步和 timing log 回显通过。
- `REQ008_EDITOR PASS option=hlbvh id=4 lock=ok`。
- `REQ001_SMOKE PASS`：World/Camera/default Environment 输出后处理一致。
- `REQ002_SMOKE PASS hardware_threads=8 target_render_threads=7 full_primary_rays=1024 serial_primary_rays=1024 cancelled_tiles=0`。
- `REQ005_SMOKE PASS artifacts_before_poll=0 finalized_on_poll=1 cancelled_artifacts=0`。

首次沙箱内编辑器启动因无法创建用户缓存目录产生错误；后续测试获准在正常用户环境运行。
定制 Godot 仍输出已有的 `ClassDB::bind_integer_constant`、`currentColor` 诊断，编辑器测试退出时有 RID/ObjectDB 清理警告；
以上目标断言均实际通过，不把这些引擎诊断描述为无警告运行。未验证整套飞船美术场景的交互预览，也未进行性能基准或 ThreadSanitizer 测试。

## 完成检查

- `git diff --check` 通过；算法、量化边界、索引重映射、线程独占与 join、故障回退、保守裁剪均有中文注释。
- 检查项目技能后，无需修改技能：现有接口、线程边界、测试先行、中文注释及归档规则已覆盖本次改动。
- 原依赖目录内没有新增构建产物，未编写或编辑 Python 代码；工具安装属于用户另行明确请求。
