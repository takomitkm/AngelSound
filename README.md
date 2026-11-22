# AngelSound

伪声练习录音机。原版是 [MoeDisk/AngelSound](https://github.com/MoeDisk/AngelSound) 的 Delphi VCL 小程序；
这个分支把它重写成 **纯 C + SDL3**，不依赖 Qt / .NET，Windows、macOS、Linux 共用一套源码。

原始 Delphi 文件（`Project1.dpr`、`Unit1.pas`、`Unit1.dfm`…）保留在根目录，方便对照。

## 功能

- 录音：16-bit / 44.1 kHz / 单声道 WAV，保存为工作目录下的 `ASREC_*.wav`
- 文件列表：启动时扫描同目录的 `ASREC_*.wav`
- 播放 / 暂停 / 继续 / 删除
- 输入设备选择（SDL3 枚举，循环切换）
- 练习计时：本次会话累计录音时长、take 数，满 5 分钟提示休息
- 键盘：`R` 录音、`S` 停止、`Space` 播放/暂停/继续、`Del` 删除、`B` 打开目录

界面用 SDL3 自带的 `SDL_RenderDebugText`（8×8 等宽字体）绘制，所以界面文案是英文；
要中文 UI 需要额外引入 SDL3_ttf 或字体渲染，这是当前刻意控制的依赖边界。

## 依赖

- C11 编译器（MSVC / clang / gcc）
- CMake ≥ 3.21
- SDL3 ≥ 3.2
  - 默认优先用系统安装的 SDL3；找不到时由 CMake 通过 FetchContent 拉取
    **SDL3 3.4.20**（带 SHA-256 校验）

## 构建

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

产物：

| 平台 | 路径 |
|---|---|
| Windows | `build/AngelSound.exe`（SDL3.dll 会自动复制到同目录） |
| macOS | `build/AngelSound.app`（Info.plist 已声明麦克风用途） |
| Linux | `build/AngelSound` |

离线构建 / 已有 SDL3 源码时：

```sh
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL3-3.4.20
```

无头冒烟（CI）：

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy ./build/AngelSound --smoke
```

## 目录

```
src/
  main.c      入口、事件循环、文件列表
  audio.c     SDL3 录音/播放/设备管理
  practice.c  练习时长与提醒
  ui.c        SDL3 立即模式界面
  wav.c       WAV 读写（无 SDL 依赖，便于单测）
tests/        test_wav、test_practice（ctest）
```

## 与原版的差异

- 录音质量从 8-bit / 11.025 kHz 提升到 16-bit / 44.1 kHz（旧 8-bit WAV 仍可播放）。
- 列表会扫描已有录音，而不是只列本次会话新录的文件。
- “录好辣”等弹窗改成底部状态提示；“删除”改为真正的确认框。
- Windows 专用按钮（CMD / 录音设置 / 附件目录）没有照搬，改为跨平台的“打开录音目录”。

原版 UI 文案与作者信息见 `Unit1.pas` / `Unit1.dfm`。