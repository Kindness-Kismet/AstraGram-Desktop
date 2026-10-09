"""动画与钱包引擎共享同一份 Rust 运行库。"""

from build_support.recipe import Stage


TLOTTIE_REVISION = "92df98dc20"
WALLET_ENGINE_REVISION = "e59e0d89d7ee90c388bf36e3334c5b276f396697"

# 两个源码版本与钱包补丁共同决定最终静态库的缓存键。
RUST_STAGES = [
    Stage(
        name="tlottie",
        location="Libraries",
        version="2",
        commands=r"""git clone https://github.com/dkaraush/tlottie.git
cd tlottie
git checkout """ + TLOTTIE_REVISION + "\n",
    ),
    Stage(
        name="wallet-engine",
        location="Libraries",
        version="2",
        dependencies=["patches/wallet-engine.patch"],
        commands=r"""SET "GIT_LFS_SKIP_SMUDGE=1"
git clone https://github.com/i582/wallet-engine.git
cd wallet-engine
git checkout """ + WALLET_ENGINE_REVISION + r"""
git apply ../patches/wallet-engine.patch
""",
    ),
    Stage(
        name="tdesktop_rust",
        location="Libraries",
        version=f"2.{TLOTTIE_REVISION}.{WALLET_ENGINE_REVISION}",
        dependencies=["patches/wallet-engine.patch"],
        commands=r"""SET "RUSTUP_HOME=%THIRDPARTY_DIR%\rust\rustup"
SET "CARGO_HOME=%THIRDPARTY_DIR%\rust\cargo"
SET RUSTUP_TOOLCHAIN=1.96.1
SET "PATH=%CARGO_HOME%\bin;%PATH%"
SET "RUST_TARGET=x86_64-win7-windows-msvc"
SET "RUST_BUILD_STD=-Z build-std=std,panic_abort"
SET "RUSTC_BOOTSTRAP=1"
if "%SPECIAL_TARGET%"=="winarm" SET "RUST_TARGET=aarch64-pc-windows-msvc"
if "%SPECIAL_TARGET%"=="winarm" SET "RUST_BUILD_STD="
cargo new --lib --vcs none tdesktop_rust
cd tdesktop_rust
echo pub use ::tlottie;> src\lib.rs
echo pub use ::wallet_engine;>> src\lib.rs
cargo add --path ..\tlottie --features c-api
cargo add --path ..\wallet-engine
cargo rustc --lib --release ^
--crate-type staticlib --crate-type cdylib ^
%RUST_BUILD_STD% ^
--target %RUST_TARGET% ^
--config "profile.release.opt-level='z'" ^
--config "profile.release.lto='thin'" ^
--config "profile.release.codegen-units=1" ^
--config "profile.release.panic='unwind'" ^
--config "profile.release.package.tlottie.opt-level=3" ^
--config "target.%RUST_TARGET%.rustflags=['-C','target-feature=+crt-static']" ^
-- --print native-static-libs
mkdir out\lib out\include out\include\tlottie out\include\wallet_engine out\src out\src\wallet_engine
copy target\%RUST_TARGET%\release\tdesktop_rust.lib out\lib\tdesktop_rust.lib
copy ..\tlottie\include\tlottie.h out\include\tlottie\tlottie.h
cargo run --manifest-path ..\wallet-engine\bindgen\cpp\bindgen\Cargo.toml --locked -- ^
--library --out-dir out\include\wallet_engine ^
target\%RUST_TARGET%\release\tdesktop_rust.dll
move out\include\wallet_engine\wallet_engine.cpp out\src\wallet_engine\wallet_engine.cpp
""",
    ),
]
