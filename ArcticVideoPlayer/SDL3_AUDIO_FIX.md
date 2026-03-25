# 🎵 音频修复 - SDL3 版本

## ✅ 已修改

由于 Qt Multimedia 模块未安装，改用 **SDL3** 进行音频输出。

### 修改的文件

1. **FFmpegArc.h**
   - 移除 `QAudioOutput` 和 `QMediaDevices`
   - 添加 `SDL3/SDL.h`
   - 使用 SDL3 音频 API

2. **FFmpegArc.cpp**
   - 使用 SDL3 音频流 API
   - `SDL_OpenAudioDevice` + `SDL_CreateAudioStream` + `SDL_PutAudioStreamData`

3. **FFmpegArcQt.vcxproj**
   - 移除 `multimedia;multimediawidgets` 模块

---

## 🔧 项目配置

需要在项目中添加 SDL3 的包含路径和库：

### 在 Visual Studio 中配置

1. **右键项目** → **属性**
2. **配置** → **所有配置**
3. **平台** → **x64**

#### 包含目录
```
D:\ArcticFFmpeg\SDL3-3.4.2\include
$(SolutionDir)..\ffmpeg\include
```

#### 库目录
```
D:\ArcticFFmpeg\SDL3-3.4.2\lib\x64
$(SolutionDir)..\ffmpeg\lib
```

#### 附加依赖项
```
SDL3.lib
avformat.lib
avcodec.lib
avutil.lib
avfilter.lib
swscale.lib
swresample.lib
```

---

## 📋 SDL3 DLL 复制

编译后需要将 `SDL3.dll` 复制到输出目录：

### 方法 1: 后期生成事件
在项目属性中添加：
```
copy "D:\ArcticFFmpeg\SDL3-3.4.2\lib\x64\SDL3.dll" "$(OutDir)"
```

### 方法 2: 手动复制
```
D:\ArcticFFmpeg\SDL3-3.4.2\lib\x64\SDL3.dll
→ 复制到
D:\ArcticFFmpeg\ArcticLight\ArcticVideoPlayer\x64\Debug\
```

---

## 🎮 SDL3 音频 API 变化

| SDL2 | SDL3 |
|------|------|
| `SDL_OpenAudioDevice(NULL, 0, ...)` | `SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, ...)` |
| `SDL_PauseAudioDevice(id, 0)` | 自动播放 |
| 回调函数模式 | `SDL_PutAudioStreamData` 推送模式 |
| `SDL_AudioCallback` | 不需要 |

---

## 🚀 编译步骤

1. 打开 `FFmpegArcQt.sln`
2. 配置 SDL3 包含路径和库
3. 按 **Ctrl+Shift+B** 编译
4. 复制 `SDL3.dll` 到输出目录
5. 按 **F5** 运行

---

## ⚠️ 可能的错误

### 错误 1: C1083: Cannot open include file: 'SDL3/SDL.h'
**解决**: 添加 SDL3 包含目录到项目属性

### 错误 2: LNK2019: unresolved external symbol SDL_*
**解决**: 添加 `SDL3.lib` 到附加依赖项

### 错误 3: 运行时找不到 SDL3.dll
**解决**: 复制 `SDL3.dll` 到 exe 所在目录

---

**现在配置项目并重新编译！** 🎵