function(extras_override_version file)
    file(STRINGS ${file} lines)
    foreach(line ${lines})
        if (line MATCHES "^AppVersion[ ]+([0-9]+)")
            set(app_version "${CMAKE_MATCH_1}")
        elseif (line MATCHES "^AppUpdateVersion[ ]+([0-9]+)")
            set(update_version "${CMAKE_MATCH_1}")
        elseif (line MATCHES "^AppStorageReadVersion[ ]+([0-9]+)")
            set(storage_read_version "${CMAKE_MATCH_1}")
        elseif (line MATCHES "^AppVersionStrOfficial[ ]+([^ ]+)")
            set(official "${CMAKE_MATCH_1}")
        elseif (line MATCHES "^AppVersionStrSmall[ ]+([^ ]+)")
            set(display "${CMAKE_MATCH_1}")
        elseif (line MATCHES "^AppVersionStrFile[ ]+([^ ]+)")
            set(file_version "${CMAKE_MATCH_1}")
        elseif (line MATCHES "^BetaChannel[ ]+([01])")
            set(beta_channel "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if (NOT DEFINED app_version
        OR NOT DEFINED update_version
        OR NOT DEFINED storage_read_version
        OR NOT DEFINED official
        OR NOT DEFINED display
        OR NOT DEFINED file_version
        OR NOT DEFINED beta_channel)
        message(FATAL_ERROR "AyuGram version fields are incomplete in ${file}")
    endif()

    string(REPLACE "." "," comma_version "${file_version}")
    if (beta_channel)
        set(beta true)
    else()
        set(beta false)
    endif()

    # 版本文件是唯一来源：改动后自动重新配置，并重新生成供代码和资源共用的头文件。
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
    set(generated_dir "${CMAKE_BINARY_DIR}/extras_version")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/extras_app_version.h.in"
        "${generated_dir}/extras_app_version.h"
        @ONLY)
    include_directories("${generated_dir}")

    set(desktop_app_version_string "${official}" PARENT_SCOPE)
    set(desktop_app_version_string_small "${display}" PARENT_SCOPE)
    set(desktop_app_version_dot "${file_version}" PARENT_SCOPE)
    set(desktop_app_version_comma "${comma_version}" PARENT_SCOPE)
    set(desktop_app_version_cmake "${file_version}" PARENT_SCOPE)
    set(desktop_app_version_int_alpha 0 PARENT_SCOPE)
    set(desktop_app_update_version "${update_version}" PARENT_SCOPE)
endfunction()
