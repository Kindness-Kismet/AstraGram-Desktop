"""win64 第三方库编译配方。

配方文本按上游 Telegram/build/prepare/prepare.py 的 win64 作用域求值后固化，
其中 $VAR 由 dependency_env 提供，^ 为 cmd 续行符，均须原样保留。
"""

from build_support import recipes_arm64
from build_support.recipe import Stage
from build_support.qt_native_backdrop_patch import PATCH_SCRIPT, patch_command

# Qt 版本决定补丁目录与产物前缀，随上游 qt_version 同步
QT_VERSION = "5.15.19"

# venv 绑定创建它的解释器，Python 版本变动必须重建，故版本号在运行时填入
PYTHON_VERSION_PLACEHOLDER = "__PYTHON_VERSION__"


STAGES: list[Stage] = [
    Stage(
        name="patches",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/desktop-app/patches.git
cd patches
git checkout 519aaa084608fa6f9a2bfbd1959d133c44d94227
""",
    ),
    Stage(
        name="msys64",
        location="ThirdParty",
        version="0",
        dependencies=[],
        commands=r"""SET PATH=%THIRDPARTY_DIR%\msys64\usr\bin;%PATH%
SET CHERE_INVOKING=enabled_from_arguments
SET MSYS2_PATH_TYPE=inherit
$FETCH download https://github.com/msys2/msys2-installer/releases/download/2025-08-30/msys2-base-x86_64-20250830.sfx.exe msys64.exe
msys64.exe
del msys64.exe
bash -c "pacman-key --init; pacman-key --populate; pacman -Syu --noconfirm"
pacman -Syu --noconfirm ^
make ^
mingw-w64-x86_64-diffutils ^
mingw-w64-x86_64-gperf ^
mingw-w64-x86_64-nasm ^
mingw-w64-x86_64-perl ^
mingw-w64-x86_64-pkgconf
""",
    ),
    Stage(
        name="python",
        location="ThirdParty",
        version=PYTHON_VERSION_PLACEHOLDER,
        dependencies=[],
        commands=r"""python -m venv python
python\Scripts\activate.bat
pip install pywin32 six meson
deactivate
""",
    ),
    Stage(
        name="NuGet",
        location="ThirdParty",
        version="0",
        dependencies=[],
        commands=r"""mkdir NuGet
$FETCH download https://dist.nuget.org/win-x86-commandline/latest/nuget.exe NuGet/nuget.exe
""",
    ),
    Stage(
        name="jom",
        location="ThirdParty",
        version="0",
        dependencies=[],
        commands=r"""$FETCH download https://master.qt.io/official_releases/jom/jom_1_1_3.zip jom.zip
$FETCH unzip jom.zip jom
del jom.zip
""",
    ),
    Stage(
        name="gyp",
        location="ThirdParty",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/desktop-app/gyp.git
cd gyp
git checkout 5e2425c47b
""",
    ),
    Stage(
        name="rust",
        location="ThirdParty",
        commands=r"""$FETCH download https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe rustup-init.exe
SET "RUSTUP_HOME=%THIRDPARTY_DIR%\rust\rustup"
SET "CARGO_HOME=%THIRDPARTY_DIR%\rust\cargo"
rustup-init.exe -y --no-modify-path --profile minimal ^
--default-toolchain 1.96.1 ^
--component rust-src ^
--target aarch64-pc-windows-msvc
del rustup-init.exe
""",
    ),
    Stage(
        name="lzma",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/desktop-app/lzma.git
cd lzma\C\Util\LzmaLib
SET ToolsetProp=/property:PlatformToolset=%MSBUILD_TOOLSET%
msbuild -m LzmaLib.sln /property:Configuration=Debug /property:Platform="$X8664" %ToolsetProp%
msbuild -m LzmaLib.sln /property:Configuration=Release /property:Platform="$X8664" %ToolsetProp%
""",
    ),
    Stage(
        name="zlib",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/madler/zlib.git
cd zlib
git checkout e3dc0a85b7032e98380dec011bc8f2c2ee0d8fca
cmake . ^
-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
-DCMAKE_C_FLAGS="/DZLIB_WINAPI" ^
-DZLIB_BUILD_SHARED=OFF ^
-DZLIB_BUILD_TESTING=OFF ^
-DZLIB_BUILD_MINIZIP=ON ^
-DZLIB_MINIZIP_BUILD_SHARED=OFF ^
-DZLIB_MINIZIP_BUILD_TESTING=OFF
cmake --build . --config Debug
cmake --build . --config Release
""",
    ),
    Stage(
        name="mozjpeg",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b v4.1.5 https://github.com/mozilla/mozjpeg.git
cd mozjpeg
cmake . ^
-DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
-DWITH_JPEG8=ON ^
-DPNG_SUPPORTED=OFF
cmake --build . --config Debug
cmake --build . --config Release
""",
    ),
    Stage(
        name="openssl3",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b openssl-3.2.1 https://github.com/openssl/openssl openssl3
cd openssl3
perl Configure no-shared no-tests debug-VC-WIN64A /FS
jom -j%NUMBER_OF_PROCESSORS% build_libs
mkdir out.dbg
move libcrypto.lib out.dbg
move libssl.lib out.dbg
move ossl_static.pdb out.dbg
move out.dbg\ossl_static.pdb out.dbg\ossl_static
jom clean
move out.dbg\ossl_static out.dbg\ossl_static.pdb
perl Configure no-shared no-tests VC-WIN64A /FS
jom -j%NUMBER_OF_PROCESSORS% build_libs
mkdir out
move libcrypto.lib out
move libssl.lib out
move ossl_static.pdb out
""",
    ),
    Stage(
        name="opus",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b v1.5.2 https://github.com/xiph/opus.git
cd opus
cmake -B out . ^
-DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
-DOPUS_STATIC_RUNTIME=ON
cmake --build out --config Debug
cmake --build out --config Release
cmake --install out --config Release
""",
    ),
    Stage(
        name="rnnoise",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/desktop-app/rnnoise.git
cd rnnoise
git checkout d8ea2b0
mkdir out
cd out
cmake .. -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"
cmake --build . --config Debug
cmake --build . --config Release
""",
    ),
    Stage(
        name="gas-preprocessor",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/FFmpeg/gas-preprocessor
cd gas-preprocessor
echo @echo off > cpp.bat
echo cl %%%%%%** >> cpp.bat
""",
    ),
    Stage(
        name="dav1d",
        location="Libraries",
        version="0",
        dependencies=["python/Scripts/activate.bat"],
        commands=r"""git clone -b 1.5.4 https://code.videolan.org/videolan/dav1d.git
cd dav1d
SET "TARGET=x86_64"
SET "DAV1D_ASM_DISABLE="
set FILE=cross-file.txt
echo [binaries] > %FILE%
echo c = 'cl' >> %FILE%
echo cpp = 'cl' >> %FILE%
echo ar = 'lib' >> %FILE%
echo windres = 'rc' >> %FILE%
echo [host_machine] >> %FILE%
echo system = 'windows' >> %FILE%
echo cpu_family = '%TARGET%' >> %FILE%
echo cpu = '%TARGET%' >> %FILE%
echo endian = 'little' >> %FILE%
%THIRDPARTY_DIR%\python\Scripts\activate.bat
meson setup --cross-file %FILE% --prefix %LIBS_DIR%/local --default-library=static --buildtype=debug -Denable_tools=false -Denable_tests=false %DAV1D_ASM_DISABLE% -Db_vscrt=mtd builddir-debug
meson compile -C builddir-debug
meson install -C builddir-debug
meson setup --cross-file %FILE% --prefix %LIBS_DIR%/local --default-library=static --buildtype=release -Denable_tools=false -Denable_tests=false -Db_vscrt=mt builddir-release
meson compile -C builddir-release
meson install -C builddir-release
copy %LIBS_DIR%\local\lib\libdav1d.a %LIBS_DIR%\local\lib\dav1d.lib
deactivate
""",
    ),
    Stage(
        name="openh264",
        location="Libraries",
        version="0",
        dependencies=["python/Scripts/activate.bat"],
        commands=r"""git clone -b v2.6.0 https://github.com/cisco/openh264.git
cd openh264
SET "TARGET=x86_64"
set FILE=cross-file.txt
echo [binaries] > %FILE%
echo c = 'cl' >> %FILE%
echo cpp = 'cl' >> %FILE%
echo ar = 'lib' >> %FILE%
echo windres = 'rc' >> %FILE%
echo [host_machine] >> %FILE%
echo system = 'windows' >> %FILE%
echo cpu_family = '%TARGET%' >> %FILE%
echo cpu = '%TARGET%' >> %FILE%
echo endian = 'little' >> %FILE%
%THIRDPARTY_DIR%\python\Scripts\activate.bat
meson setup --cross-file %FILE% --prefix %LIBS_DIR%/local --default-library=static --buildtype=debug -Db_vscrt=mtd builddir-debug
meson compile -C builddir-debug
meson install -C builddir-debug
meson setup --cross-file %FILE% --prefix %LIBS_DIR%/local --default-library=static --buildtype=release -Db_vscrt=mt builddir-release
meson compile -C builddir-release
meson install -C builddir-release
copy %LIBS_DIR%\local\lib\libopenh264.a %LIBS_DIR%\local\lib\openh264.lib
deactivate
""",
    ),
    Stage(
        name="libavif",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b v1.4.2 https://github.com/AOMediaCodec/libavif.git
cd libavif
cmake . ^
-DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
-DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
-DBUILD_SHARED_LIBS=OFF ^
-DAVIF_ENABLE_WERROR=OFF ^
-DAVIF_CODEC_DAV1D=SYSTEM ^
-DAVIF_LIBYUV=OFF
cmake --build . --config Debug
cmake --install . --config Debug
cmake --build . --config Release
cmake --install . --config Release
""",
    ),
    Stage(
        name="libde265",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b v1.1.1 https://github.com/strukturag/libde265.git
cd libde265
cmake . ^
-DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
-DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
-DCMAKE_C_FLAGS="/DLIBDE265_STATIC_BUILD" ^
-DCMAKE_CXX_FLAGS="/DLIBDE265_STATIC_BUILD" ^
-DENABLE_SDL=OFF ^
-DBUILD_SHARED_LIBS=OFF ^
-DENABLE_DECODER=OFF ^
-DENABLE_ENCODER=OFF
cmake --build . --config Debug
cmake --install . --config Debug
cmake --build . --config Release
cmake --install . --config Release
""",
    ),
    Stage(
        name="libwebp",
        location="Libraries",
        version="0",
        dependencies=[],
        # Makefile.vc 靠 cl 横幅探测架构，本地化横幅与 GNU find 都会让它失败，故显式给 ARCH
        commands=r"""git clone -b v1.6.0 https://github.com/webmproject/libwebp.git
cd libwebp
nmake /f Makefile.vc CFG=debug-static OBJDIR=out RTLIBCFG=static ARCH=$X8664 all
nmake /f Makefile.vc CFG=release-static OBJDIR=out RTLIBCFG=static ARCH=$X8664 all
copy out\release-static\$X8664\lib\libwebp.lib out\release-static\$X8664\lib\webp.lib
copy out\release-static\$X8664\lib\libwebpdemux.lib out\release-static\$X8664\lib\webpdemux.lib
copy out\release-static\$X8664\lib\libwebpmux.lib out\release-static\$X8664\lib\webpmux.lib
""",
    ),
    Stage(
        name="libheif",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b v1.23.1 https://github.com/strukturag/libheif.git
cd libheif
%THIRDPARTY_DIR%\msys64\usr\bin\sed.exe -i 's/LIBHEIF_EXPORTS/LIBDE265_STATIC_BUILD/g' libheif/CMakeLists.txt
%THIRDPARTY_DIR%\msys64\usr\bin\sed.exe -i 's/HAVE_VISIBILITY/LIBHEIF_STATIC_BUILD/g' libheif/CMakeLists.txt
%THIRDPARTY_DIR%\msys64\usr\bin\sed.exe -i 's/LIBHEIF_EXPORTS/LIBDE265_STATIC_BUILD/g' heifio/CMakeLists.txt
%THIRDPARTY_DIR%\msys64\usr\bin\sed.exe -i 's/HAVE_VISIBILITY/LIBHEIF_STATIC_BUILD/g' heifio/CMakeLists.txt
cmake . ^
-DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
-DBUILD_SHARED_LIBS=OFF ^
-DBUILD_DOCUMENTATION=OFF ^
-DBUILD_TESTING=OFF ^
-DENABLE_PLUGIN_LOADING=OFF ^
-DWITH_LIBDE265=ON ^
-DWITH_X264=OFF ^
-DWITH_OpenH264_DECODER=OFF ^
-DWITH_SvtEnc=OFF ^
-DWITH_SvtEnc_PLUGIN=OFF ^
-DWITH_RAV1E=OFF ^
-DWITH_RAV1E_PLUGIN=OFF ^
-DWITH_LIBSHARPYUV=OFF ^
-DCMAKE_DISABLE_FIND_PACKAGE_TIFF=TRUE ^
-DCMAKE_DISABLE_FIND_PACKAGE_JPEG=TRUE ^
-DCMAKE_DISABLE_FIND_PACKAGE_PNG=TRUE ^
-DWITH_EXAMPLES=OFF
cmake --build . --config Debug
cmake --install . --config Debug
cmake --build . --config Release
cmake --install . --config Release
""",
    ),
    Stage(
        name="libjxl",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b v0.12.0 --recursive --shallow-submodules https://github.com/libjxl/libjxl.git
cd libjxl
SET "cmake_defines=-DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DJPEGXL_ENABLE_FUZZERS=OFF -DJPEGXL_ENABLE_DEVTOOLS=OFF -DJPEGXL_ENABLE_TOOLS=OFF -DJPEGXL_ENABLE_DOXYGEN=OFF -DJPEGXL_ENABLE_MANPAGES=OFF -DJPEGXL_ENABLE_EXAMPLES=OFF -DJPEGXL_ENABLE_JNI=OFF -DJPEGXL_ENABLE_SJPEG=OFF -DJPEGXL_ENABLE_OPENEXR=OFF -DJPEGXL_ENABLE_SKCMS=ON -DJPEGXL_ENABLE_VIEWERS=OFF -DJPEGXL_ENABLE_TCMALLOC=OFF -DJPEGXL_ENABLE_PLUGINS=OFF -DJPEGXL_ENABLE_COVERAGE=OFF -DJPEGXL_WARNINGS_AS_ERRORS=OFF"
cmake . ^
-DCMAKE_INSTALL_PREFIX=%LIBS_DIR%/local ^
-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
-DCMAKE_C_FLAGS="/DJXL_STATIC_DEFINE /DJXL_THREADS_STATIC_DEFINE /DJXL_CMS_STATIC_DEFINE" ^
-DCMAKE_CXX_FLAGS="/DJXL_STATIC_DEFINE /DJXL_THREADS_STATIC_DEFINE /DJXL_CMS_STATIC_DEFINE" ^
%cmake_defines%
cmake --build . --config Debug
cmake --install . --config Debug
cmake --build . --config Release
cmake --install . --config Release
""",
    ),
    Stage(
        name="libvpx",
        location="Libraries",
        version="0",
        dependencies=["patches/libvpx/*.patch", "patches/build_libvpx_win.sh"],
        commands=r"""git clone https://github.com/webmproject/libvpx.git
cd libvpx
git checkout v1.14.1
for /r %%i in (..\patches\libvpx\*) do git apply %%i
rem x86_64 缺 v145 target，照 0005 补丁的 arm64 先例补一行；\x22 是引号，避开 cmd 引号嵌套
%THIRDPARTY_DIR%\msys64\usr\bin\sed.exe -i "s/x86_64-win64-vs17\x22/x86_64-win64-vs17 x86_64-win64-vs17-v145\x22/" configure
SET PATH=%THIRDPARTY_DIR%\msys64\usr\bin;%PATH%
SET CHERE_INVOKING=enabled_from_arguments
SET MSYS2_PATH_TYPE=inherit
SET "TOOLCHAIN=x86_64-win64-vs17-v145"
bash --login ../patches/build_libvpx_win.sh
""",
    ),
    Stage(
        name="liblcms2",
        location="Libraries",
        version="0",
        dependencies=["python/Scripts/activate.bat"],
        commands=r"""git clone -b lcms2.16 https://github.com/mm2/Little-CMS.git liblcms2
cd liblcms2
%THIRDPARTY_DIR%\python\Scripts\activate.bat
meson setup --default-library=static --buildtype=debug -Db_vscrt=mtd out/Debug
meson compile -C out/Debug
meson setup --default-library=static --buildtype=release -Db_vscrt=mt out/Release
meson compile -C out/Release
deactivate
""",
    ),
    Stage(
        name="nv-codec-headers",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b n12.1.14.0 https://github.com/FFmpeg/nv-codec-headers.git
""",
    ),
    Stage(
        name="regex",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b boost-1.83.0 https://github.com/boostorg/regex.git
""",
    ),
    Stage(
        name="ffmpeg",
        location="Libraries",
        version="0",
        dependencies=["patches/ffmpeg.patch", "patches/build_ffmpeg_win.sh"],
        commands=r"""git clone -b n6.1.6 https://github.com/FFmpeg/FFmpeg.git ffmpeg
cd ffmpeg
git apply ../patches/ffmpeg.patch
SET PATH=%THIRDPARTY_DIR%\msys64\usr\bin;%PATH%
SET CHERE_INVOKING=enabled_from_arguments
SET MSYS2_PATH_TYPE=inherit
SET "ARCH_PARAM="
bash --login ../patches/build_ffmpeg_win.sh
""",
    ),
    Stage(
        name="openal-soft",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/telegramdesktop/openal-soft.git
cd openal-soft
git checkout 291c0fdbbd
cmake -B build . ^
-D LIBTYPE:STRING=STATIC ^
-D FORCE_STATIC_VCRT=ON ^
-D ALSOFT_UTILS=OFF ^
-D ALSOFT_EXAMPLES=OFF ^
-D ALSOFT_TESTS=OFF
cmake --build build --config Debug
cmake --build build --config RelWithDebInfo
""",
    ),
    Stage(
        name="breakpad",
        location="Libraries",
        version="0",
        dependencies=["patches/breakpad.diff", "python/Scripts/activate.bat"],
        # 上游此处设 PYTHONUTF8=1，但中文 Windows 下 gyp 会按 UTF-8 解码 reg.exe
        # 查询 DirectX SDK 时返回的 GBK 报错而崩溃，故不再设置，交给系统默认编码
        commands=r"""git clone https://chromium.googlesource.com/breakpad/breakpad
cd breakpad
git checkout dfcb7b6799
git apply ../patches/breakpad.diff
git clone -b release-1.11.0 https://github.com/google/googletest src/testing
SET "FolderPostfix="
SET ToolsetProp=/property:PlatformToolset=%MSBUILD_TOOLSET%
SET "FolderPostfix=_x64"
%THIRDPARTY_DIR%\python\Scripts\activate.bat
cd src\client\windows
gyp --no-circular-check breakpad_client.gyp --format=ninja
cd ..\..
ninja -C out/Debug%FolderPostfix% common crash_generation_client exception_handler
ninja -C out/Release%FolderPostfix% common crash_generation_client exception_handler
cd tools\windows\dump_syms
gyp dump_syms.gyp --format=msvs
msbuild -m dump_syms.vcxproj /property:Configuration=Release /property:Platform="x64" %ToolsetProp%
deactivate
""",
    ),
    Stage(
        name="tg_angle",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/desktop-app/tg_angle.git
cd tg_angle
git checkout d4c3606e47
cmake -B out ^
-DTG_ANGLE_SPECIAL_TARGET=%SPECIAL_TARGET% ^
-DTG_ANGLE_ZLIB_INCLUDE_PATH=%LIBS_DIR%/zlib
cmake --build out --config Debug
cmake --build out --config Release
""",
    ),
    Stage(
        name=f"qt_{QT_VERSION}",
        location="Libraries",
        version="0",
        dependencies=[f"patches/qtbase_{QT_VERSION}/*.patch", str(PATCH_SCRIPT)],
        commands=r"""git clone -b v$QT-lts-lgpl https://github.com/qt/qt5.git qt_$QT
cd qt_$QT
git submodule update --init --recursive --progress qtbase qtimageformats qtsvg
cd qtbase
setlocal enabledelayedexpansion
for /r %%i in (..\..\patches\qtbase_%QT%\*) do (
git apply %%i -v
if errorlevel 1 (
echo ERROR: Applying patch %%~nxi failed!
exit /b 1
)
)
__QT_NATIVE_BACKDROP_PATCH__
rem jom 1.1.3 在命令行超过 1000 字符时崩溃（0xC0000409），bootstrap 换 nmake；
rem 主构建的 qmake Makefile 本身用响应文件链接，不受影响
%THIRDPARTY_DIR%\msys64\usr\bin\sed.exe -i "s/set MAKE=jom/set MAKE=nmake/" configure.bat
cd ..
SET CONFIGURATIONS=-debug
SET CONFIGURATIONS=-debug-and-release
if exist "%LIBS_DIR%\Qt-5.15.19" rmdir /Q /S "%LIBS_DIR%\Qt-5.15.19"
if exist "%LIBS_DIR%\Qt-5.15.19" exit /b 1
SET ANGLE_DIR=%LIBS_DIR%\tg_angle
SET ANGLE_LIBS_DIR=%ANGLE_DIR%\out
SET MOZJPEG_DIR=%LIBS_DIR%\mozjpeg
SET OPENSSL_DIR=%LIBS_DIR%\openssl3
SET OPENSSL_LIBS_DIR=%OPENSSL_DIR%\out
SET ZLIB_LIBS_DIR=%LIBS_DIR%\zlib
SET WEBP_DIR=%LIBS_DIR%\libwebp
configure -prefix "%LIBS_DIR%\Qt-%QT%" ^
%CONFIGURATIONS% ^
-force-debug-info ^
-opensource ^
-confirm-license ^
-static ^
-static-runtime ^
-opengl es2 -no-angle ^
-I "%ANGLE_DIR%\include" ^
-D "KHRONOS_STATIC=" ^
-D "DESKTOP_APP_QT_STATIC_ANGLE=" ^
QMAKE_LIBS_OPENGL_ES2_DEBUG="%ANGLE_LIBS_DIR%\Debug\tg_angle.lib %ZLIB_LIBS_DIR%\Debug\libzsd.lib d3d9.lib dxgi.lib dxguid.lib" ^
QMAKE_LIBS_OPENGL_ES2_RELEASE="%ANGLE_LIBS_DIR%\Release\tg_angle.lib %ZLIB_LIBS_DIR%\Release\libzs.lib d3d9.lib dxgi.lib dxguid.lib" ^
-egl ^
QMAKE_LIBS_EGL_DEBUG="%ANGLE_LIBS_DIR%\Debug\tg_angle.lib %ZLIB_LIBS_DIR%\Debug\libzsd.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
QMAKE_LIBS_EGL_RELEASE="%ANGLE_LIBS_DIR%\Release\tg_angle.lib %ZLIB_LIBS_DIR%\Release\libzs.lib d3d9.lib dxgi.lib dxguid.lib Gdi32.lib User32.lib" ^
-openssl-linked ^
-I "%OPENSSL_DIR%\include" ^
OPENSSL_LIBS_DEBUG="%OPENSSL_LIBS_DIR%.dbg\libssl.lib %OPENSSL_LIBS_DIR%.dbg\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
OPENSSL_LIBS_RELEASE="%OPENSSL_LIBS_DIR%\libssl.lib %OPENSSL_LIBS_DIR%\libcrypto.lib Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib" ^
-I "%MOZJPEG_DIR%" ^
LIBJPEG_LIBS_DEBUG="%MOZJPEG_DIR%\Debug\jpeg-static.lib" ^
LIBJPEG_LIBS_RELEASE="%MOZJPEG_DIR%\Release\jpeg-static.lib" ^
-system-webp ^
-I "%WEBP_DIR%\src" ^
-L "%WEBP_DIR%\out\release-static\$X8664\lib" ^
-mp ^
-no-feature-netlistmgr ^
-nomake examples ^
-nomake tests ^
-platform win32-msvc
cd qtbase\qmake
rem jom 1.1.3 在命令行超过 1000 字符时崩溃（0xC0000409），nmake 自动转响应文件；
rem 预链出 qmake.exe，主构建递归到 binary 时判依赖已满足而跳过
nmake binary
cd ..\..
rem jom -jN occasionally fails to create the shared mkspecs\modules-inst
rem directory due to a race in qmake's mkpath under parallel builds; the
rem build is incremental, so simply retrying picks up where it stopped.
jom -j%NUMBER_OF_PROCESSORS% || jom -j%NUMBER_OF_PROCESSORS%
jom -j%NUMBER_OF_PROCESSORS% install
""".replace("__QT_NATIVE_BACKDROP_PATCH__", patch_command()),
    ),
    Stage(
        name="tg_owt",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/desktop-app/tg_owt.git
cd tg_owt
git checkout 89df288dd6ba5b2ec95b3c5eaf1e7e0c3a870fc4
git submodule update --init --recursive
SET MOZJPEG_PATH=$LIBS_DIR/mozjpeg
SET OPUS_PATH=$USED_PREFIX/include/opus
SET OPENSSL_PATH=$LIBS_DIR/openssl3/include
SET LIBVPX_PATH=$USED_PREFIX/include
SET OPENH264_PATH=$USED_PREFIX/include
SET FFMPEG_PATH=$LIBS_DIR/ffmpeg
cmake -B out         -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"         -DTG_OWT_BUILD_AUDIO_BACKENDS=OFF         -DTG_OWT_SPECIAL_TARGET=$SPECIAL_TARGET         -DTG_OWT_LIBJPEG_INCLUDE_PATH=$MOZJPEG_PATH         -DTG_OWT_OPENSSL_INCLUDE_PATH=$OPENSSL_PATH         -DTG_OWT_OPUS_INCLUDE_PATH=$OPUS_PATH         -DTG_OWT_LIBVPX_INCLUDE_PATH=$LIBVPX_PATH         -DTG_OWT_OPENH264_INCLUDE_PATH=$OPENH264_PATH         -DTG_OWT_FFMPEG_INCLUDE_PATH=$FFMPEG_PATH
cmake --build out --config Debug
cmake --build out --config Release
""",
    ),
    Stage(
        name="ada",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone -b v3.2.4 https://github.com/ada-url/ada.git
cd ada
cmake -B out . ^
-D ADA_TESTING=OFF ^
-D ADA_TOOLS=OFF ^
-D ADA_INCLUDE_URL_PATTERN=OFF ^
-D CMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"
cmake --build out --config Debug
cmake --build out --config Release
""",
    ),
    Stage(
        name="tde2e",
        location="Libraries",
        version="0",
        dependencies=[],
        commands=r"""git clone https://github.com/tdlib/td.git tde2e
cd tde2e
git checkout 51743df
SET OPENSSL_DIR=%LIBS_DIR%\openssl3
SET OPENSSL_LIBS_DIR=%OPENSSL_DIR%\out
SET ZLIB_LIBS_DIR=%LIBS_DIR%\zlib
%THIRDPARTY_DIR%\msys64\usr\bin\sed -i "s/STREQUAL/MATCHES/" td/generate/CMakeLists.txt
mkdir out
cd out
mkdir Debug
cd Debug
cmake ^
-DOPENSSL_FOUND=1 ^
-DOPENSSL_INCLUDE_DIR=%OPENSSL_DIR%\include ^
-DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_LIBS_DIR%.dbg\libcrypto.lib" ^
-DZLIB_FOUND=1 ^
-DZLIB_INCLUDE_DIR=%ZLIB_LIBS_DIR% ^
-DZLIB_LIBRARIES="%ZLIB_LIBS_DIR%\Debug\libzsd.lib" ^
-DCMAKE_CONFIGURATION_TYPES=Debug ^
-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
-DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
-DCMAKE_C_FLAGS="/DZLIB_WINAPI" ^
-DCMAKE_CXX_FLAGS="/DZLIB_WINAPI" ^
-DCMAKE_EXE_LINKER_FLAGS="/SAFESEH:NO Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib %OPENSSL_LIBS_DIR%.dbg\libssl.lib" ^
-DCMAKE_SHARED_LINKER_FLAGS="/SAFESEH:NO Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib %OPENSSL_LIBS_DIR%.dbg\libssl.lib" ^
-DTD_ENABLE_MULTI_PROCESSOR_COMPILATION=ON ^
-DTD_E2E_ONLY=ON ^
../..
cmake --build . --config Debug
cd ..
mkdir Release
cd Release
cmake ^
-DOPENSSL_FOUND=1 ^
-DOPENSSL_INCLUDE_DIR=%OPENSSL_DIR%\include ^
-DOPENSSL_CRYPTO_LIBRARY="%OPENSSL_LIBS_DIR%\libcrypto.lib" ^
-DZLIB_FOUND=1 ^
-DZLIB_INCLUDE_DIR=%ZLIB_LIBS_DIR% ^
-DZLIB_LIBRARIES="%ZLIB_LIBS_DIR%\Release\libzs.lib" ^
-DCMAKE_CONFIGURATION_TYPES=Release ^
-DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ^
-DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
-DCMAKE_C_FLAGS="/DZLIB_WINAPI" ^
-DCMAKE_CXX_FLAGS="/DZLIB_WINAPI" ^
-DCMAKE_EXE_LINKER_FLAGS="/SAFESEH:NO Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib %OPENSSL_LIBS_DIR%\libssl.lib" ^
-DCMAKE_SHARED_LINKER_FLAGS="/SAFESEH:NO Ws2_32.lib Gdi32.lib Advapi32.lib Crypt32.lib User32.lib %OPENSSL_LIBS_DIR%\libssl.lib" ^
-DTD_ENABLE_MULTI_PROCESSOR_COMPILATION=ON ^
-DTD_E2E_ONLY=ON ^
../..
cmake --build . --config Release
""",
    ),
    Stage(
        name="tlottie",
        location="Libraries",
        dependencies=["patches/tlottie.patch"],
        commands=r"""git clone https://github.com/dkaraush/tlottie.git
cd tlottie
git checkout 31f1b542f8
git apply ../patches/tlottie.patch
SET "RUSTUP_HOME=%THIRDPARTY_DIR%\rust\rustup"
SET "CARGO_HOME=%THIRDPARTY_DIR%\rust\cargo"
SET RUSTUP_TOOLCHAIN=1.96.1
SET "PATH=%CARGO_HOME%\bin;%PATH%"
SET "RUST_TARGET=x86_64-win7-windows-msvc"
SET "RUST_BUILD_STD=-Z build-std=std,panic_abort"
SET "RUSTC_BOOTSTRAP=1"
if "%SPECIAL_TARGET%"=="winarm" SET "RUST_TARGET=aarch64-pc-windows-msvc"
if "%SPECIAL_TARGET%"=="winarm" SET "RUST_BUILD_STD="
cargo rustc --lib --release --locked ^
--features c-api --crate-type staticlib ^
%RUST_BUILD_STD% ^
--target %RUST_TARGET% ^
--config "target.%RUST_TARGET%.rustflags=['-C','target-feature=+crt-static']" ^
-- --print native-static-libs
mkdir out\lib out\include
copy target\%RUST_TARGET%\release\tlottie.lib out\lib\tlottie.lib
copy include\tlottie.h out\include\tlottie.h
""",
    ),
]


STAGE_NAMES = [stage.name for stage in STAGES]


def qt_version(target: str) -> str:
    """arm64 换 Qt 6：Qt 5.15 没有 Windows arm64 支持，上游同样这么选。"""
    return recipes_arm64.QT_VERSION if target == "winarm64" else QT_VERSION


def stages_for(target: str) -> list[Stage]:
    """按目标平台取阶段清单，arm64 的差异见 recipes_arm64。"""
    from dataclasses import replace

    if target == "win64":
        return list(STAGES)
    if target != "winarm64":
        raise SystemExit(f"unknown target: {target}")
    qt_stage = f"qt_{QT_VERSION}"
    result: list[Stage] = []
    for stage in STAGES:
        if stage.name == qt_stage:
            result.append(recipes_arm64.QT_STAGE)
        elif stage.name in recipes_arm64.SKIP:
            continue
        elif stage.name in recipes_arm64.OVERRIDES:
            result.append(replace(stage, commands=recipes_arm64.OVERRIDES[stage.name]))
        else:
            result.append(stage)
    return result


def resolved_stages(target: str) -> list[Stage]:
    """填入运行时版本后的阶段清单。"""
    import sys
    from dataclasses import replace

    version = "0." + ".".join(str(part) for part in sys.version_info[:3])
    return [
        replace(stage, version=version) if stage.version == PYTHON_VERSION_PLACEHOLDER else stage
        for stage in stages_for(target)
    ]
