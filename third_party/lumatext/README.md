# LumaText 预编译依赖

Windows x64 Debug / Release 共享库，供 LUMEN 本地与 CI 构建复用。

- 来源：Release 已于 2026-09-22 同步同级 lumatext 工作区 out/sdk/Release 的优化编译结果（1,648,128 字节）；Debug 保留 2026-09-20 的既有 SDK。这是本地工作区产物，并非新的远端发布。
- Debug：bin/lumatextd.dll、lib/lumatextd.lib（MDd）；Release：bin/lumatext.dll、lib/lumatext.lib（MT，静态运行库，开启 Release 体积优化）。
- 头文件、DLL、导入库作为同一套更新；SHA256SUMS 记录实际包文件，CI 可继续核验。
- 默认保持 gamma 0.85、Mitchell、透明背景路径；桥接层额外光学补偿归零，避免新版启用旧占位值导致增粗。
- 已知底色线性合成和实验候选参数不自动启用。许可证在 licenses/。

默认使用此包；开发源码时设置 LUMEN_USE_PREBUILT_LUMATEXT=OFF 和 LUMATEXT_SOURCE_DIR。