---
name: corforge-build-env
description: How to build/run CorForge on this machine — toolchain, Python gotcha, GPU preference
metadata:
  type: project
---

CorForge builds via `scripts\build.ps1` (VS 2022 BuildTools MSVC + bundled CMake/Ninja). Build is Release with `/O2`. Run with `-Run`, or launch `build\bin\corforge.exe` directly.

**Python gotcha:** the GLAD2 loader generator runs `python -m glad` and needs **jinja2**. CMake's `find_package(Python)` selects the newest interpreter — here `C:\Users\Alex\AppData\Local\Programs\Python\Python314`, into which jinja2 was installed. The Windows Store stub `python.exe` (in `WindowsApps`) sits first on PATH and breaks `build.ps1`'s preamble check. Fix when building: prepend the real Python dir to PATH before invoking, e.g. `$env:PATH = "C:\Users\Alex\AppData\Local\Programs\Python\Python314;" + $env:PATH`.

**GPU:** machine is a laptop with Intel UHD + NVIDIA RTX 5070 Laptop GPU (8GB). A persistent per-app GPU preference (`HKCU\Software\Microsoft\DirectX\UserGpuPreferences`, `GpuPreference=2;`) forces corforge.exe onto the discrete GPU. Verify GPU use with `nvidia-smi`.

Related: optimization work in progress — see [[corforge-optimization]].
