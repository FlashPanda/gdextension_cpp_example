#!/usr/bin/env python
import os
import sys

# --- Force MSVC to English + UTF-8 output for all child processes --- #
os.environ.setdefault("VSLANG", "1033")        # 1033 = English
os.environ.setdefault("PYTHONIOENCODING", "utf-8")
os.environ.setdefault("LANG", "en_US.UTF-8")
os.environ.setdefault("LC_ALL", "C")
# -------------------------------------------------------------------- #

from SCons import __version__ as scons_raw_version

env = SConscript("godot-cpp/SConstruct")

# 双保险：确保 SCons spawn 子进程用的 env 也被覆盖
env["ENV"]["VSLANG"] = "1033"
env["ENV"]["PYTHONIOENCODING"] = "utf-8"
env["ENV"]["PYTHONUTF8"] = "1"
env["ENV"]["LANG"] = "en_US.UTF-8"
env["ENV"]["LC_ALL"] = "C"

# For reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "demo/bin/libgdexample.{}.{}.framework/libgdexample.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "demo/bin/libgdexample.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "demo/bin/libgdexample.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    library = env.SharedLibrary(
        "demo/bin/libgdexample{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
