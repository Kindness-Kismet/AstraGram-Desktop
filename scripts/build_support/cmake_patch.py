from pathlib import Path

from build_support.paths import ROOT

# cmake/ 是 desktop-app/cmake_helpers 官方子模块，把库目录硬编码成仓库同级的
# ../Libraries；根 CMakeLists.txt 同样把 venv 写死在 ../ThirdParty。依赖统一放
# build/tmp 后必须改道，官方没有参数入口，故构建时按下表动态 patch（幂等，
# 锚点漂移时直接报错）。fork 已废弃，指针跟随官方提交。
LIBS_LOC_OPTION = "DESKTOP_APP_LIBS_LOC"
PYTHON_OPTION = "Python3_EXECUTABLE"

_VARIABLES = ROOT / "cmake" / "variables.cmake"
_VALIDATE = ROOT / "cmake" / "validate_special_target.cmake"
_ROOT_LISTS = ROOT / "CMakeLists.txt"
_CMARK = ROOT / "cmake" / "external" / "cmark_gfm" / "CMakeLists.txt"
_WINDOWS_OPTIONS = ROOT / "cmake" / "options_win.cmake"

_MARKER = f"# {LIBS_LOC_OPTION} override"
_CMARK_MARKER = "# cmark_gfm warnings off"
_WINDOWS_DEBUG_MARKER = "/DEBUG:FULL"

# VS 2026 已移除 FASTLINK，直接生成完整调试信息。
_WINDOWS_DEBUG_ORIGINAL = "/DEBUG:FASTLINK"
_WINDOWS_DEBUG_PATCHED = _WINDOWS_DEBUG_MARKER

# Debug 增量链接：改少量源文件时只重写变化部分；Release 仍完整链接。
_WINDOWS_INCREMENTAL_MARKER = "$<IF:$<CONFIG:Debug>,/INCREMENTAL,/INCREMENTAL:NO>"
_WINDOWS_INCREMENTAL_ORIGINAL = "        /INCREMENTAL:NO\n"
_WINDOWS_INCREMENTAL_PATCHED = f"        {_WINDOWS_INCREMENTAL_MARKER}\n"

# 断言宏把 __FILE__ 原样编进二进制，绝对路径会随发布包泄露本机目录。
_WINDOWS_TRIM_MARKER = "# strip local build path from __FILE__"
_WINDOWS_TRIM_ORIGINAL = "        /Zc:__cplusplus\n    )\n"
_WINDOWS_TRIM_PATCHED = f"""        /Zc:__cplusplus
        {_WINDOWS_TRIM_MARKER}
        /d1trimfile:${{CMAKE_SOURCE_DIR}}/
    )
"""


_VARIABLES_ORIGINAL = """if (build_win64)
    get_filename_component(libs_loc "../Libraries/win64" REALPATH)
else()
    get_filename_component(libs_loc "../Libraries" REALPATH)
endif()"""

_VARIABLES_PATCHED = f"""{_MARKER}
if ({LIBS_LOC_OPTION})
    get_filename_component(libs_loc "${{{LIBS_LOC_OPTION}}}" REALPATH)
elseif (build_win64)
    get_filename_component(libs_loc "../Libraries/win64" REALPATH)
else()
    get_filename_component(libs_loc "../Libraries" REALPATH)
endif()"""

_VALIDATE_ORIGINAL = 'get_filename_component(libs_loc "../Libraries" REALPATH)'

_VALIDATE_PATCHED = f"""{_MARKER}
if ({LIBS_LOC_OPTION})
    get_filename_component(libs_loc "${{{LIBS_LOC_OPTION}}}" REALPATH)
else()
    get_filename_component(libs_loc "../Libraries" REALPATH)
endif()"""

_ROOT_ORIGINAL = f"""if (NOT DESKTOP_APP_USE_PACKAGED AND WIN32)
    set({PYTHON_OPTION} ${{CMAKE_CURRENT_SOURCE_DIR}}/../ThirdParty/python/Scripts/python)
endif()"""

_ROOT_PATCHED = f"""{_MARKER}
if (NOT DESKTOP_APP_USE_PACKAGED AND WIN32 AND NOT {PYTHON_OPTION})
    set({PYTHON_OPTION} ${{CMAKE_CURRENT_SOURCE_DIR}}/../ThirdParty/python/Scripts/python)
endif()"""

_CMARK_ORIGINAL = """target_link_libraries(external_cmark_gfm
INTERFACE
    libcmark-gfm-extensions_static
    libcmark-gfm_static
)"""

_CMARK_PATCHED = f"""{_CMARK_MARKER}
add_library(external_cmark_gfm_warnings_off INTERFACE)
if (MSVC)
    target_compile_options(external_cmark_gfm_warnings_off
    INTERFACE
        /W0
        /WX-
    )
else()
    target_compile_options(external_cmark_gfm_warnings_off
    INTERFACE
        -w
        -Wno-error
    )
endif()

target_link_libraries(external_cmark_gfm
INTERFACE
    libcmark-gfm-extensions_static
    libcmark-gfm_static
)
target_link_libraries(libcmark-gfm-extensions_static
PRIVATE
    external_cmark_gfm_warnings_off
)
target_link_libraries(libcmark-gfm_static
PRIVATE
    external_cmark_gfm_warnings_off
)"""

# (文件, 标记, 官方原文, patch 后文本)
_PATCHES = (
    (_VARIABLES, _MARKER, _VARIABLES_ORIGINAL, _VARIABLES_PATCHED),
    (_VALIDATE, _MARKER, _VALIDATE_ORIGINAL, _VALIDATE_PATCHED),
    (_ROOT_LISTS, _MARKER, _ROOT_ORIGINAL, _ROOT_PATCHED),
    (_CMARK, _CMARK_MARKER, _CMARK_ORIGINAL, _CMARK_PATCHED),
    (_WINDOWS_OPTIONS, _WINDOWS_DEBUG_MARKER, _WINDOWS_DEBUG_ORIGINAL, _WINDOWS_DEBUG_PATCHED),
    (_WINDOWS_OPTIONS, _WINDOWS_INCREMENTAL_MARKER, _WINDOWS_INCREMENTAL_ORIGINAL, _WINDOWS_INCREMENTAL_PATCHED),
    (_WINDOWS_OPTIONS, _WINDOWS_TRIM_MARKER, _WINDOWS_TRIM_ORIGINAL, _WINDOWS_TRIM_PATCHED),
)


def patch_anchors() -> list[tuple[str, str]]:
    """返回 (相对仓库根的路径, 官方原文)，同步上游时据此检查补丁锚点是否还在。"""
    return [(path.relative_to(ROOT).as_posix(), original) for path, _, original, _ in _PATCHES]


def ensure_libs_loc_override() -> list[str]:
    """幂等地为 cmake 子模块与根 CMakeLists 打补丁。"""
    notes: list[str] = []
    for path, marker, original, patched in _PATCHES:
        if not path.is_file():
            raise SystemExit(f"{path} not found. Run git submodule update --init --recursive.")

        text = path.read_text(encoding="utf-8")
        if marker in text:
            notes.append(f"{path.name:<28} already patched")
            continue
        if original not in text:
            raise SystemExit(f"Cannot patch {path}: expected block not found. The submodule may have changed upstream.")

        path.write_text(text.replace(original, patched, 1), encoding="utf-8", newline="")
        notes.append(f"{path.name:<28} patched")
    return notes


def revert_libs_loc_override() -> list[str]:
    notes: list[str] = []
    for path, marker, original, patched in _PATCHES:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        if marker not in text:
            continue
        path.write_text(text.replace(patched, original, 1), encoding="utf-8", newline="")
        notes.append(f"{path.name} reverted")
    return notes
