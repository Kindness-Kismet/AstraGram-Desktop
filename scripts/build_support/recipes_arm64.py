"""winarm64 配方相对 win64 的差异。

由上游 prepare.py 的 winarm 作用域求值得到（win64 自检 21/32 阶段逐字一致）。
"""

from build_support.recipe import Stage

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
SET "TOOLCHAIN=arm64-win64-vs17-v145"
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
	"ffmpeg": r"""git clone -b n8.1.3 https://github.com/FFmpeg/FFmpeg.git ffmpeg
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
	"openssl3": r"""git clone -b openssl-3.5.9 https://github.com/openssl/openssl openssl3
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
