# Build Guide (Windows)

This guide explains how to build and run the engine from source on Windows.

## 1. Prerequisites

Install the following:

1. Visual Studio 2022 (Desktop development with C++)
2. CMake 3.20+
3. Vulkan SDK
4. Git

## 2. Clone and bootstrap dependencies

From the repository root:

```powershell
git submodule update --init --recursive
.\vcpkg\bootstrap-vcpkg.bat
```

## 3. Configure CMake

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

## 4. Build

### Release

```powershell
cmake --build build --config Release
```

### Debug

```powershell
cmake --build build --config Debug
```

## 5. Run

### Release executable

```powershell
.\build\release\AIGameEngine.exe
```

### Debug executable

```powershell
.\build\debug\AIGameEngine.exe
```

## 6. What to expect at startup

A successful recent build includes:

1. Base renderer output
2. Development/debug UI path (ImGui overlays)
3. Interactive demo/debug window support

If the app starts but you do not see UI, verify you are running the latest binary from `build\release`.

## 7. Incremental rebuild workflow

After code changes:

```powershell
cmake --build build --config Release -- /m /nologo /verbosity:minimal
```

## 8. Common issues

### Missing Vulkan runtime or validation issues

Ensure Vulkan SDK/runtime is installed and available on `PATH`.

### Missing runtime DLLs

Run from the generated output folder (`build\release` or `build\debug`) so adjacent DLLs can be found.

### Dependency/config drift

If builds become inconsistent, regenerate:

```powershell
Remove-Item -Recurse -Force .\build
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
```

## 9. Stage 27 UI asset cook/load quickstart

Stage 27 UI assets are now first-class cook/load inputs:

- Widget blueprint: `.widgetbp`
- Widget layout: `.widgetlayout`
- Widget style: `.widgetstyle`
- Localization table: `.uiloc`

Example cook pipeline usage (C++):

```cpp
Core::Asset::AssetPipeline pipeline;
Core::Asset::CookOptions options;
options.OutputDirectory = "build/cooked";
pipeline.SetOptions(options);
pipeline.ScanSourceDirectory("Assets/UI");
pipeline.CookAll();
```

Example runtime load usage (C++):

```cpp
auto blueprint = Core::Asset::AssetLoader::LoadWidgetBlueprintAsset("build/cooked/hud.widgetbp.cooked");
auto layout = Core::Asset::AssetLoader::LoadWidgetLayoutAsset("build/cooked/hud.widgetlayout.cooked");
auto locale = Core::Asset::AssetLoader::LoadLocalizationTableAsset("build/cooked/ui_en.uiloc.cooked");
```

