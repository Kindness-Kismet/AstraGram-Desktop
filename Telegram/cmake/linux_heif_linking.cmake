# 在父目录修正导入目标，让图片插件和主程序共用完整的静态依赖关系。
find_package(libheif QUIET)
if (NOT TARGET heif)
    return()
endif()

get_target_property(heif_type heif TYPE)
if (NOT heif_type STREQUAL "STATIC_LIBRARY")
    return()
endif()

get_target_property(heif_libraries heif INTERFACE_LINK_LIBRARIES)
if (NOT heif_libraries)
    return()
endif()

# libheif 只导出两个 FFmpeg 文件路径，会让 CMake 把它们排到所需依赖之后。
list(TRANSFORM heif_libraries REPLACE
    "^.*/libav(codec|util)[.]a$" "desktop-app::external_ffmpeg")
set_property(TARGET heif PROPERTY INTERFACE_LINK_LIBRARIES "${heif_libraries}")
