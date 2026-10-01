# ijkplayer

## `ffmpeg-5.0` 分支：FFmpeg 5.1.10 Android 迁移候选

本分支的实际版本是 **FFmpeg 5.1.10**，`ffmpeg-5.0` 是迁移系列的分支名。
请从下面的新入口开始；旧的 `init-android.sh` 仍指向 **FFmpeg 4.3**，
不能用于构建本分支的 FFmpeg 5 候选。

- [完整构建说明、固定源码版本与验证边界](doc/FFMPEG5.md)
- [配套 FFmpeg 源码分支](https://github.com/CarGuo/FFmpeg/tree/ffmpeg-5.0)
- [GSYVideoPlayer 配套分支、九库校验与验证说明](https://github.com/CarGuo/GSYVideoPlayer/blob/ffmpeg-5.0/doc/ffmpeg-5.0.md)

### FFmpeg 5 快速开始

已验证的构建环境为 Linux x86_64 + Android NDK r22b（`22.1.7171670`）。
另需 Bash、Git、Perl、GNU Make、`sha256sum`，以及用于 x86_64 汇编的 NASM 或 YASM。
使用新的工作目录，避免与旧 FFmpeg 4 构建产物混用：

```sh
git clone --branch ffmpeg-5.0 https://github.com/CarGuo/ijkplayer.git ijkplayer-ffmpeg5
cd ijkplayer-ffmpeg5
./init-android-ffmpeg5.sh
export ANDROID_NDK=/absolute/path/to/android-ndk-r22b
export FFMPEG5_SOURCE="$PWD/extra/ffmpeg5"
export OPENSSL_SOURCE="$PWD/extra/openssl-3.5.9"
(
  set -e
  for abi in arm64 armv7a x86_64; do
    android/contrib/compile-ffmpeg5.sh "$abi"
    (cd android && ./compile-ijk.sh "$abi")
  done
)
```

当前修复源码加入 HEVC 参数集更新缓存、TLS 域名／IP 身份校验，并升级到
OpenSSL 3.5.9。初始化脚本已固定到已发布的四补丁提交
[`5d01026835e0fc18be967461df180432e85db0c7`](https://github.com/CarGuo/FFmpeg/commit/5d01026835e0fc18be967461df180432e85db0c7)，
四补丁反向校验通过；新 pin 的完整全新初始化尚未重跑。

三个 ABI 的新九库已构建完成，独立静态检查 73/73 通过。
Android 11/API 30、x86_64、4 KB 模拟器上，核心 21/21、TLS 7/7、
网络语义 4/4、H.264 与普通 HEVC 的同步／异步 MediaCodec 检查通过；
RTSP／RTMP／MJPEG 八项协议用例的普通与严格中断两轮均为 8/8。
严格中断验证包含回调及重新创建播放器，不代表自动重连。

三帧 HEVC 参数更新样本的同步检查通过，但首次渲染回调晚于完成回调；
异步检查因未观察到首次渲染而失败，尚不能确认 Android 画面更新正确。
扩展编码矩阵也未整体通过。新 16 KB 运行仅完成加载／注册清单，
核心播放被 Surface 前置条件超时阻塞。新版长期稳定性、真实 ARM 设备及最终
GSY AAR／APK 尚无完整验收通过结论；旧九库运行记录仍是历史证据，不能套用。
新二进制发布及完整发布验收仍未完成，详见完整说明。

新初始化脚本会为全新目录检出固定的 FFmpeg 提交；即使远端分支新增文档提交，
也不会自动换用其他源码版本。已有目录若版本不符，会保留原目录并报错。
OpenSSL 使用独立的 `extra/openssl-3.5.9` 源码目录及按内容寻址的缓存；
可用 `OPENSSL_ONLY=1 android/contrib/compile-ffmpeg5.sh arm64` 单独验证依赖构建。
构建明确使用 `config/module-lite-more.sh`；三个 ABI 分别对应 arm64-v8a、
armeabi-v7a、x86_64。替换时请保持每个 ABI 的 FFmpeg／player／SDL 三库成套。
原九库哈希及
[可选版本标记复现步骤](doc/FFMPEG5.md#optional-reproduction-of-the-tested-candidate)
属于历史候选；新版 HEVC／TLS／OpenSSL 输入不能仅靠版本标记复现旧哈希。
请同时保留真实源码提交、依赖及构建清单。

这是有明确验证范围的迁移候选：新 16 KB 核心播放及参数更新异步渲染仍未验收通过，
旧候选的硬解旋转缺口也不能由新 4 KB 结果替代；未完成真实 ARM 设备兼容认证。修复后的 `tls_verify=1` 校验证书链及域名／IP，
Android 需显式提供可信 CA 文件；默认及显式 `tls_verify=0` 仍不认证服务器。
OpenSSL 3.5 默认拒绝旧 TLS 1.0／1.1 和弱密钥，旧设备兼容要求仍待确认。
不要把构建成功或旧 4 KB 环境通过等同于全部设备、16 KB 场景或 TLS 安全验收。
详见[验证结果与限制](doc/FFMPEG5.md#checks-and-limits)。

## 旧 FFmpeg 4.3 链路说明（保留参考，不是 FFmpeg 5 构建入口）

以下为原有修改说明（64 位 16 KB + 32 位可构建），其中环境、标签、命令和验证结论
属于旧 FFmpeg 4.3 链路。构建 FFmpeg 5 请使用本页上方的新入口。

> 当前链路以 **arm64-v8a 生产** 为主，保持 **arm64-v8a/x86_64 为 16K page size**；同时补齐 **armeabi-v7a（FFmpeg 4.3，4K page size）** 构建用于 32 位设备兼容。

### 1) 当前支持环境与系统版本（已验证）
- 主机系统：macOS 26.2（Build 25C56），Apple Silicon（Darwin arm64）
- Android NDK：`22.1.7171670`（r22）
- FFmpeg 基线：`n4.3`，并使用远端 tag：`ijk-n4.3-20260301-007`
- OpenSSL 基线：`OpenSSL_1_1_1w`
- 目标 ABI：`arm64-v8a`（生产，16K）+ `x86_64`（16K）+ `armeabi-v7a`（可构建，4K）

### 2) 本次关键修复与增强
- 已修复 `compile-openssl.sh arm64` 在 Darwin arm64 + NDK r22 的 `aarch64-linux-android-ar` 崩溃问题。
- 已修复 `compile-ffmpeg.sh arm64` 同类 `ar` 崩溃问题，并显式导出 `RANLIB/NM`。
- 已为 `arm64-v8a/libijkffmpeg.so` 开启 Stack Canary（`-fstack-protector-strong`）。
- 已修复运行时崩溃：`ijkav_register_async_protocol+24`（`SIGSEGV/SEGV_ACCERR`），n4.3 下改为兼容 no-op 注册逻辑。
- 已补齐 `x86_64` 构建链路（NDK r22），并将 `APP_STL` 迁移为 `c++_static`。
- 已修复 `x86_64` 下 HTTPS 不生效问题：
  - `compile-openssl.sh x86_64` 现可稳定产出 `libssl.a/libcrypto.a`；
  - `compile-ffmpeg.sh x86_64` 检测到 OpenSSL 已就绪但旧 `config.h` 未启用 `--enable-openssl` 时，会自动清理旧配置并重新 `configure`；
  - 避免出现“OpenSSL 已编出，但 FFmpeg 仍复用旧配置，最终 `CONFIG_HTTPS_PROTOCOL=0`”的问题。
- 已补齐 `armeabi-v7a`（FFmpeg 4.3）在 NDK r22 的构建兼容：
  - `compile-ijk.sh` 增加 `armv7a` 直接入口；
  - `ijkplayer-armv7a` 的 `APP_STL` 调整为 `c++_static`；
  - OpenSSL/FFmpeg 脚本增加 armv7a 在 r22 下的工具链兼容桥接。
- `init-android.sh` 固定引用：
  - `IJK_FFMPEG_UPSTREAM=https://github.com/CarGuo/FFmpeg.git`
  - `IJK_FFMPEG_FORK=https://github.com/CarGuo/FFmpeg.git`
  - `IJK_FFMPEG_COMMIT=ijk-n4.3-20260301-007`

### 3) 16K / 4K ABI 策略（已验证）
- `arm64-v8a`：16K（`PT_LOAD Align = 0x4000`）
- `x86_64`：16K（`PT_LOAD Align = 0x4000`）
- `armeabi-v7a`：4K（`PT_LOAD Align = 0x1000`）
- `x86_64` HTTPS：已验证 `CONFIG_OPENSSL=1`、`CONFIG_HTTPS_PROTOCOL=1`、`CONFIG_TLS_PROTOCOL=1`

### 4) 对比官方 ijkplayer 的 git diff 文件范围
- `android/compile-ijk.sh`
- `android/contrib/tools/do-compile-ffmpeg.sh`
- `android/contrib/tools/do-compile-openssl.sh`
- `android/contrib/tools/do-detect-env.sh`
- `android/ijkplayer/ijkplayer-arm64/build.gradle`
- `android/ijkplayer/ijkplayer-arm64/src/main/jni/Application.mk`
- `android/ijkplayer/ijkplayer-armv7a/src/main/jni/Application.mk`
- `android/ijkplayer/ijkplayer-x86_64/src/main/jni/Application.mk`
- `config/module-lite.sh`
- `config/module.sh`
- `config/module-lite-more.sh`
- `ijkmedia/ijkj4a/Android.mk`
- `ijkmedia/ijkplayer/Android.mk`
- `ijkmedia/ijksdl/Android.mk`
- `ijkprof/android-ndk-profiler-dummy/jni/Android.mk`
- `init-android-openssl.sh`
- `init-android.sh`
- `commit.patch`
- `README.md`

### 5) 相关补丁（对比官方 ijkplayer）
- 补丁目录：[GSYVideoPlayer/16kpatch](https://github.com/CarGuo/GSYVideoPlayer/tree/master/16kpatch)
  - `ndk_r22_16k_commit.patch`（ijkplayer 主仓全量补丁）
  - `ndk_r22_ffmpeg_n4.3_ijk.patch`（FFmpeg: `n4.3 -> ijk-n4.3-20260301-007`）
  - `ndk_r22_soundtouch.patch`（`ijkmedia/ijksoundtouch`）
  - `ndk_r22_ijkyuv.patch`（`ijkmedia/ijkyuv`）

# 官方原文档（历史参考）

以下保留上游旧版本文档；其中下载坐标、环境、`init-android.sh` 和
`compile-ffmpeg.sh` 命令不适用于本分支的 FFmpeg 5 快速构建。

 Platform | Build Status
 -------- | ------------
 Android | [![Build Status](https://travis-ci.org/Bilibili/ci-ijk-ffmpeg-android.svg?branch=master)](https://travis-ci.org/Bilibili/ci-ijk-ffmpeg-android)
 iOS | [![Build Status](https://travis-ci.org/Bilibili/ci-ijk-ffmpeg-ios.svg?branch=master)](https://travis-ci.org/Bilibili/ci-ijk-ffmpeg-ios)

Video player based on [ffplay](http://ffmpeg.org)

### Download

- Android:
 - Gradle
```
# required
allprojects {
    repositories {
        jcenter()
    }
}

dependencies {
    # required, enough for most devices.
    compile 'tv.danmaku.ijk.media:ijkplayer-java:0.8.8'
    compile 'tv.danmaku.ijk.media:ijkplayer-armv7a:0.8.8'

    # Other ABIs: optional
    compile 'tv.danmaku.ijk.media:ijkplayer-armv5:0.8.8'
    compile 'tv.danmaku.ijk.media:ijkplayer-arm64:0.8.8'
    compile 'tv.danmaku.ijk.media:ijkplayer-x86:0.8.8'
    compile 'tv.danmaku.ijk.media:ijkplayer-x86_64:0.8.8'

    # ExoPlayer as IMediaPlayer: optional, experimental
    compile 'tv.danmaku.ijk.media:ijkplayer-exo:0.8.8'
}
```
- iOS
 - in coming...

### My Build Environment
- Common
 - Mac OS X 10.11.5
- Android
 - [NDK r10e](http://developer.android.com/tools/sdk/ndk/index.html)
 - Android Studio 2.1.3
 - Gradle 2.14.1
- iOS
 - Xcode 7.3 (7D175)
- [HomeBrew](http://brew.sh)
 - ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
 - brew install git

### Latest Changes
- [NEWS.md](NEWS.md)

### Features
- Common
 - remove rarely used ffmpeg components to reduce binary size [config/module-lite.sh](config/module-lite.sh)
 - workaround for some buggy online video.
- Android
 - platform: API 9~23
 - cpu: ARMv7a, ARM64v8a, x86 (ARMv5 is not tested on real devices)
 - api: [MediaPlayer-like](android/ijkplayer/ijkplayer-java/src/main/java/tv/danmaku/ijk/media/player/IMediaPlayer.java)
 - video-output: NativeWindow, OpenGL ES 2.0
 - audio-output: AudioTrack, OpenSL ES
 - hw-decoder: MediaCodec (API 16+, Android 4.1+)
 - alternative-backend: android.media.MediaPlayer, ExoPlayer
- iOS
 - platform: iOS 7.0~10.2.x
 - cpu: armv7, arm64, i386, x86_64, (armv7s is obselete)
 - api: [MediaPlayer.framework-like](ios/IJKMediaPlayer/IJKMediaPlayer/IJKMediaPlayback.h)
 - video-output: OpenGL ES 2.0
 - audio-output: AudioQueue, AudioUnit
 - hw-decoder: VideoToolbox (iOS 8+)
 - alternative-backend: AVFoundation.Framework.AVPlayer, MediaPlayer.Framework.MPMoviePlayerControlelr (obselete since iOS 8)

### NOT-ON-PLAN
- obsolete platforms (Android: API-8 and below; iOS: pre-6.0)
- obsolete cpu: ARMv5, ARMv6, MIPS (I don't even have these types of devices…)
- native subtitle render
- avfilter support

### Before Build（旧上游流程）
```
# install homebrew, git, yasm
ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
brew install git
brew install yasm

# add these lines to your ~/.bash_profile or ~/.profile
# export ANDROID_SDK=<your sdk path>
# export ANDROID_NDK=<your ndk path>

# on Cygwin (unmaintained)
# install git, make, yasm
```

- If you prefer more codec/format
```
cd config
rm module.sh
ln -s module-default.sh module.sh
cd android/contrib
# cd ios
sh compile-ffmpeg.sh clean
```

- If you prefer less codec/format for smaller binary size (include hevc function)
```
cd config
rm module.sh
ln -s module-lite-hevc.sh module.sh
cd android/contrib
# cd ios
sh compile-ffmpeg.sh clean
```

- If you prefer less codec/format for smaller binary size (by default)
```
cd config
rm module.sh
ln -s module-lite.sh module.sh
cd android/contrib
# cd ios
sh compile-ffmpeg.sh clean
```

- For Ubuntu/Debian users.
```
# choose [No] to use bash
sudo dpkg-reconfigure dash
```

- If you'd like to share your config, pull request is welcome.

### Build Android（旧上游流程，非 FFmpeg 5）
```
git clone https://github.com/Bilibili/ijkplayer.git ijkplayer-android
cd ijkplayer-android
git checkout -B latest k0.8.8

./init-android.sh

cd android/contrib
./compile-ffmpeg.sh clean
./compile-ffmpeg.sh all

cd ..
./compile-ijk.sh all

# Android Studio:
#     Open an existing Android Studio project
#     Select android/ijkplayer/ and import
#
#     define ext block in your root build.gradle
#     ext {
#       compileSdkVersion = 23       // depending on your sdk version
#       buildToolsVersion = "23.0.0" // depending on your build tools version
#
#       targetSdkVersion = 23        // depending on your sdk version
#     }
#
# If you want to enable debugging ijkplayer(native modules) on Android Studio 2.2+: (experimental)
#     sh android/patch-debugging-with-lldb.sh armv7a
#     Install Android Studio 2.2(+)
#     Preference -> Android SDK -> SDK Tools
#     Select (LLDB, NDK, Android SDK Build-tools,Cmake) and install
#     Open an existing Android Studio project
#     Select android/ijkplayer
#     Sync Project with Gradle Files
#     Run -> Edit Configurations -> Debugger -> Symbol Directories
#     Add "ijkplayer-armv7a/.externalNativeBuild/ndkBuild/release/obj/local/armeabi-v7a" to Symbol Directories
#     Run -> Debug 'ijkplayer-example'
#     if you want to reverse patches:
#     sh patch-debugging-with-lldb.sh reverse armv7a
#
# Eclipse: (obselete)
#     File -> New -> Project -> Android Project from Existing Code
#     Select android/ and import all project
#     Import appcompat-v7
#     Import preference-v7
#
# Gradle
#     cd ijkplayer
#     gradle

```


### Build iOS
```
git clone https://github.com/Bilibili/ijkplayer.git ijkplayer-ios
cd ijkplayer-ios
git checkout -B latest k0.8.8

./init-ios.sh

cd ios
./compile-ffmpeg.sh clean
./compile-ffmpeg.sh all

# Demo
#     open ios/IJKMediaDemo/IJKMediaDemo.xcodeproj with Xcode
# 
# Import into Your own Application
#     Select your project in Xcode.
#     File -> Add Files to ... -> Select ios/IJKMediaPlayer/IJKMediaPlayer.xcodeproj
#     Select your Application's target.
#     Build Phases -> Target Dependencies -> Select IJKMediaFramework
#     Build Phases -> Link Binary with Libraries -> Add:
#         IJKMediaFramework.framework
#
#         AudioToolbox.framework
#         AVFoundation.framework
#         CoreGraphics.framework
#         CoreMedia.framework
#         CoreVideo.framework
#         libbz2.tbd
#         libz.tbd
#         MediaPlayer.framework
#         MobileCoreServices.framework
#         OpenGLES.framework
#         QuartzCore.framework
#         UIKit.framework
#         VideoToolbox.framework
#
#         ... (Maybe something else, if you get any link error)
# 
```


### Support (支持) ###
- Please do not send e-mail to me. Public technical discussion on github is preferred.
- 请尽量在 github 上公开讨论[技术问题](https://github.com/bilibili/ijkplayer/issues)，不要以邮件方式私下询问，恕不一一回复。


### License

```
Copyright (c) 2017 Bilibili
Licensed under LGPLv2.1 or later
```

ijkplayer required features are based on or derives from projects below:
- LGPL
  - [FFmpeg](http://git.videolan.org/?p=ffmpeg.git)
  - [libVLC](http://git.videolan.org/?p=vlc.git)
  - [kxmovie](https://github.com/kolyvan/kxmovie)
  - [soundtouch](http://www.surina.net/soundtouch/sourcecode.html)
- zlib license
  - [SDL](http://www.libsdl.org)
- BSD-style license
  - [libyuv](https://code.google.com/p/libyuv/)
- ISC license
  - [libyuv/source/x86inc.asm](https://code.google.com/p/libyuv/source/browse/trunk/source/x86inc.asm)

android/ijkplayer-exo is based on or derives from projects below:
- Apache License 2.0
  - [ExoPlayer](https://github.com/google/ExoPlayer)

android/example is based on or derives from projects below:
- GPL
  - [android-ndk-profiler](https://github.com/richq/android-ndk-profiler) (not included by default)

ios/IJKMediaDemo is based on or derives from projects below:
- Unknown license
  - [iOS7-BarcodeScanner](https://github.com/jpwiddy/iOS7-BarcodeScanner)

ijkplayer's build scripts are based on or derives from projects below:
- [gas-preprocessor](http://git.libav.org/?p=gas-preprocessor.git)
- [VideoLAN](http://git.videolan.org)
- [yixia/FFmpeg-Android](https://github.com/yixia/FFmpeg-Android)
- [kewlbear/FFmpeg-iOS-build-script](https://github.com/kewlbear/FFmpeg-iOS-build-script) 

### Commercial Use
ijkplayer is licensed under LGPLv2.1 or later, so itself is free for commercial use under LGPLv2.1 or later

But ijkplayer is also based on other different projects under various licenses, which I have no idea whether they are compatible to each other or to your product.

[IANAL](https://en.wikipedia.org/wiki/IANAL), you should always ask your lawyer for these stuffs before use it in your product.
