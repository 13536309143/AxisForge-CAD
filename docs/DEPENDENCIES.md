# 开发依赖与运行库

本机验证版本：Qt 5.15.2、OCCT 7.9.3、MSVC 19.44，全部x64/Release。

## OCCT SDK

官方发布：https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V7_9_3

开发包：opencascade-7.9.3-vc14-64.zip

SHA256：2a4f5e187992427674869e89477882d1ace4d67695d4b27173830d93f8cf11be

下载包已与GitHub发布资源的digest核对。Windows本地工程的SDK位于deps/occt，SDK不提交源码仓库，也不放入便携运行ZIP。其他机器从官方下载安装，设置OCCT_ROOT。运行程序不需要SDK。

OCCT采用LGPL 2.1加OCCT例外，原创应用代码仍为MIT。运行包内保留OCCT许可与例外文件。

## Windows运行包

采用官方OCCT预编译库和对应3rdparty-vc14-64运行库；只按实际DLL依赖收集，未混用其Qt 5.11库。应用仍使用原有Qt 5.15.2。运行库独立放在run目录，可单独替换；不静态嵌入应用可执行文件。

主要新增组件包括OCCT 7.9.3、oneTBB 2021.13、jemalloc、FreeType 2.13.3、FreeImage 3.18、OpenVR 1.14.15，以及OCCT可视化工具包的FFmpeg 3.3.4运行依赖。尽管应用显示仍走自己的OpenGL视图，OCCT XCAF工具包会引用这些官方运行库。

FFmpeg库报告构建参数为：`--enable-shared --disable-static --prefix=/d/build_ffmpeg/ffmpeg-3.3.4-64 --toolchain=msvc --arch=amd64 --disable-asm --disable-yasm --disable-swresample`；其库报告许可为LGPL version 2.1 or later。

第三方许可及上游来源链接保存在运行包run/licenses。上游源代码可从OCCT官方标签、oneapi-src/oneTBB、jemalloc/jemalloc、freetype/freetype、FreeImage官网、ValveSoftware/openvr、FFmpeg/FFmpeg相应版本获取。

## Linux构建

除Qt 6外，需要OCCT建模、OCAF、数据交换、可视化开发包，以及TBB、FreeType、Fontconfig、FreeImage开发包。具体apt命令以.github/workflows/ci.yml为准。Windows与Linux的OCCT工具包名称不同，CMake对STEP库名称作了兼容处理。
