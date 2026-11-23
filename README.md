# AngelSound

伪声练习录音机。原版是 [MoeDisk/AngelSound](https://github.com/MoeDisk/AngelSound) 的 Delphi VCL 小程序；
这个分支把它重写成 **纯 C + SDL3**，Windows、macOS、Linux 共用一套源码。

上游 Delphi/VCL 源码不在本分支，见上面链接。

## 功能

- 录音：直接使用**麦克风的原生格式**（采样率、声道、位深随设备；不重采样、不转格式），
  保存为 WAV（8/16/32-bit PCM 或 32-bit float）
- 录音目录：默认 `~/Music/AngelSound`，可用界面 **Folder** 改；设置保存在 SDL 首选项目录的
  `settings.txt`（`ANGELSOUND_DIR` 环境变量可覆盖）
- 文件列表：启动时扫描录音目录内的 `ASREC_*.wav`
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
  - Android 改由官方 **SDL3 3.4.20 AAR** 提供 Java 类和 Prefab 原生库；
    AAR 由 CI 下载并校验 SHA-256，不提交进仓库

## Android 构建

Android 端使用 SDL3 官方 AAR（`SDL3-devel-3.4.20-android.zip` 内的
`SDL3-3.4.20.aar`），Java 与原生层来自同一个版本。GitHub Actions 的
`android (assembleDebug)` 任务会自动下载、校验并放入 `android/app/libs/`，
然后运行 Gradle；桌面端仍沿用系统 SDL3 / FetchContent 策略。

本地复现 Android 构建时，先手工把该 AAR 放进 `android/app/libs/`，再执行：

```sh
cd android
./gradlew :app:assembleDebug
```

`android/app/libs/*.aar` 已在 `.gitignore` 中排除，不会误提交二进制文件。

## 桌面构建

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

## 上游依赖（供对照）

原版是 Delphi/VCL 程序，不是 Qt 也不是 .NET：

- **Embarcadero Delphi**（Object Pascal + VCL；`Project1.dproj`、`Unit1.dfm`）
- **AlphaControls**：`sSkinManager`、`sStatusBar`、`sListBox`、`sTabControl`、`acPNG` 等
  第三方 VCL 皮肤控件（皮肤数据内嵌在 `Unit1.dfm`）
- **Windows API**：录音/播放走 MCI（`TMediaPlayer` → winmm）、`ShellExecute`（ShellAPI）、
  `RunDLL32 Mmsys.cpl` 打开录音设置

所以原版只能在 Windows 上用 Delphi 编译。

## 与原版的差异

- 录音格式随麦克风设备走（原版固定 8-bit / 11.025 kHz 单声道），不做转换。
- 录音目录默认在 `Music/AngelSound`，可通过 Folder 选择；原版固定在工作目录。
- 列表会扫描已有录音，而不是只列本次会话新录的文件。
- “录好辣”等弹窗改成底部状态提示；“删除”改为真正的确认框。
- Windows 专用按钮（CMD / 录音设置 / 附件目录）没有照搬，改为跨平台的“打开录音目录”。

原版 UI 文案与作者信息见上游仓库。