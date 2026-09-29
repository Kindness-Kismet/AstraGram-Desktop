# 静态 Qt 已链接字体库，复用同一份头文件，避免混入另一套字体引擎。
if ((WIN32 OR APPLE) AND NOT DESKTOP_APP_USE_PACKAGED)
    set(extras_qt_font_sources "${libs_loc}/qt_${QT_VERSION}/qtbase/src/3rdparty")
    set_source_files_properties(
        "${src_loc}/extras/features/emoji_packs/emoji_font.cpp"
        PROPERTIES INCLUDE_DIRECTORIES
        "${extras_qt_font_sources}/freetype/include;${extras_qt_font_sources}/harfbuzz-ng/src"
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(EXTRAS_EMOJI_FONTS REQUIRED IMPORTED_TARGET freetype2 harfbuzz)
    target_link_libraries(Telegram PRIVATE PkgConfig::EXTRAS_EMOJI_FONTS)
endif()

if (DESKTOP_APP_TEST_APPS)
    add_executable(test_emoji_font)
    init_target(test_emoji_font "(tests)")
    target_sources(test_emoji_font PRIVATE
        "${src_loc}/extras/features/emoji_packs/emoji_font.cpp"
        "${src_loc}/tests/test_emoji_font.cpp"
    )
    target_include_directories(test_emoji_font PRIVATE "${src_loc}")
    target_link_libraries(test_emoji_font PRIVATE desktop-app::external_qt)
    if (TARGET PkgConfig::EXTRAS_EMOJI_FONTS)
        target_link_libraries(test_emoji_font PRIVATE PkgConfig::EXTRAS_EMOJI_FONTS)
    endif()
    add_dependencies(Telegram test_emoji_font)
endif()
