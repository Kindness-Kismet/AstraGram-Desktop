"""winarm64 配方相对 win64 的差异。

由上游 prepare.py 的 winarm 作用域求值得到（win64 自检 21/32 阶段逐字一致）。
"""

from build_support.recipe import Stage
from build_support.qt_native_backdrop_patch import PATCH_SCRIPT, patch_command

# arm64 上 Qt 换 6.11.2：Qt 5.15 没有 Windows arm64 支持，上游同样这么选
QT_VERSION = "6.11.2"

# Qt 换成 6.11.2，ANGLE 只有 x64 一份
SKIP = frozenset({"qt_5.15.19", "tg_angle"})

OVERRIDES: dict[str, str] = {
	"breakpad": r"""git clone https://chromium.googlesource.com/breakpad/breakpad
cd breakpad
git checkout dfcb7b6799
git apply ../patches/breakpad.diff
git clone -b release-1.11.0 https://github.com/google/googletest src/testing
SET "FolderPostfix="
SET ToolsetProp=/property:PlatformToolset=%MSBUILD_TOOLSET%
SET "FolderPostfix=_ARM64"
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
	"libvpx": r"""git clone https://github.com/webmproject/libvpx.git
cd libvpx
git checkout v1.14.1
for /r %%i in (..\patches\libvpx\*) do git apply %%i
SET PATH=%THIRDPARTY_DIR%\msys64\usr\bin;%PATH%
SET CHERE_INVOKING=enabled_from_arguments
SET MSYS2_PATH_TYPE=inherit
SET "TOOLCHAIN=arm64-win64-vs17"
bash --login ../patches/build_libvpx_win.sh
""",
	"dav1d": r"""git clone -b 1.5.4 https://code.videolan.org/videolan/dav1d.git
cd dav1d
SET "TARGET=aarch64"
SET "DAV1D_ASM_DISABLE="
SET "PATH=%LIBS_DIR%\gas-preprocessor;%PATH%"
echo armasm64 fails with 'syntax error in expression: tbnz x14, #4, 8f' as if this instruction is unknown/unsupported.
git revert --no-edit d503bb0ccaf104b2f13da0f092e09cc9411b3297
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
	"ffmpeg": r"""git clone -b n6.1.6 https://github.com/FFmpeg/FFmpeg.git ffmpeg
cd ffmpeg
git apply ../patches/ffmpeg.patch
SET PATH=%THIRDPARTY_DIR%\msys64\usr\bin;%PATH%
SET CHERE_INVOKING=enabled_from_arguments
SET MSYS2_PATH_TYPE=inherit
SET "ARCH_PARAM="
SET "ARCH_PARAM=--arch=aarch64"
bash --login ../patches/build_ffmpeg_win.sh
""",
	"openh264": r"""git clone -b v2.6.0 https://github.com/cisco/openh264.git
cd openh264
SET "TARGET=aarch64"
SET "PATH=%LIBS_DIR%\gas-preprocessor;%PATH%"
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
	"openssl3": r"""git clone -b openssl-3.2.1 https://github.com/openssl/openssl openssl3
cd openssl3
perl Configure no-shared no-tests debug-VC-WIN64-ARM /FS
jom -j%NUMBER_OF_PROCESSORS% build_libs
mkdir out.dbg
move libcrypto.lib out.dbg
move libssl.lib out.dbg
move ossl_static.pdb out.dbg
move out.dbg\ossl_static.pdb out.dbg\ossl_static
jom clean
move out.dbg\ossl_static out.dbg\ossl_static.pdb
perl Configure no-shared no-tests VC-WIN64-ARM /FS /Gs4096
jom -j%NUMBER_OF_PROCESSORS% build_libs
mkdir out
move libcrypto.lib out
move libssl.lib out
move ossl_static.pdb out
""",
}

QT_STAGE = Stage(
	name=f"qt_{QT_VERSION}",
	location="Libraries",
	dependencies=[f"patches/qtbase_{QT_VERSION}/*.patch", str(PATCH_SCRIPT)],
	commands=r"""git clone -b v$QT https://github.com/qt/qt5.git qt_$QT
cd qt_$QT
git submodule update --init --recursive --progress qtbase qtimageformats qtshadertools qtsvg
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
cd ..
SET CONFIGURATIONS=-debug
SET ASSERTS=
SET CONFIGURATIONS=-debug-and-release
if exist "%LIBS_DIR%\Qt-%QT%" rmdir /Q /S "%LIBS_DIR%\Qt-%QT%"
if exist "%LIBS_DIR%\Qt-%QT%" exit /b 1
SET MOZJPEG_DIR=%LIBS_DIR%\mozjpeg
SET OPENSSL_DIR=%LIBS_DIR%\openssl3
SET OPENSSL_LIBS_DIR=%OPENSSL_DIR%\out
SET ZLIB_LIBS_DIR=%LIBS_DIR%\zlib
SET WEBP_DIR=%LIBS_DIR%\libwebp
SET LCMS2_DIR=%LIBS_DIR%\liblcms2
configure -prefix "%LIBS_DIR%\Qt-%QT%" ^
%CONFIGURATIONS% ^
%ASSERTS% ^
-force-debug-info ^
-opensource ^
-confirm-license ^
-static ^
-static-runtime ^
-feature-c++20 ^
-openssl linked ^
-system-webp ^
-system-zlib ^
-system-libjpeg ^
-platform win32-msvc ^
-D ZLIB_WINAPI ^
-- ^
-D OPENSSL_FOUND=1 ^
-D OPENSSL_INCLUDE_DIR="%OPENSSL_DIR%\include" ^
-D LIB_EAY_DEBUG="%OPENSSL_LIBS_DIR%.dbg\libcrypto.lib" ^
-D SSL_EAY_DEBUG="%OPENSSL_LIBS_DIR%.dbg\libssl.lib" ^
-D LIB_EAY_RELEASE="%OPENSSL_LIBS_DIR%\libcrypto.lib" ^
-D SSL_EAY_RELEASE="%OPENSSL_LIBS_DIR%\libssl.lib" ^
-D JPEG_FOUND=1 ^
-D JPEG_INCLUDE_DIR="%MOZJPEG_DIR%" ^
-D JPEG_LIBRARY_DEBUG="%MOZJPEG_DIR%\Debug\jpeg-static.lib" ^
-D JPEG_LIBRARY_RELEASE="%MOZJPEG_DIR%\Release\jpeg-static.lib" ^
-D ZLIB_FOUND=1 ^
-D ZLIB_INCLUDE_DIR="%ZLIB_LIBS_DIR%" ^
-D ZLIB_LIBRARY_DEBUG="%ZLIB_LIBS_DIR%\Debug\libzsd.lib" ^
-D ZLIB_LIBRARY_RELEASE="%ZLIB_LIBS_DIR%\Release\libzs.lib" ^
-D WebP_INCLUDE_DIR="%WEBP_DIR%\src" ^
-D WebP_demux_INCLUDE_DIR="%WEBP_DIR%\src" ^
-D WebP_mux_INCLUDE_DIR="%WEBP_DIR%\src" ^
-D WebP_LIBRARY="%WEBP_DIR%\out\release-static\$X8664\lib\webp.lib" ^
-D WebP_demux_LIBRARY="%WEBP_DIR%\out\release-static\$X8664\lib\webpdemux.lib" ^
-D WebP_mux_LIBRARY="%WEBP_DIR%\out\release-static\$X8664\lib\webpmux.lib" ^
-D LCMS2_FOUND=1 ^
-D LCMS2_INCLUDE_DIR="%LCMS2_DIR%\include" ^
-D LCMS2_LIBRARIES="%LCMS2_DIR%\out\Release\src\liblcms2.a"
cmake --build . --config Debug
cmake --install . --config Debug
cmake --build .
cmake --install .
""".replace("__QT_NATIVE_BACKDROP_PATCH__", patch_command()),
)
